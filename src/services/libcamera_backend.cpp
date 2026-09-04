#include "services/libcamera_backend.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

#if defined(CAMERA_APP_SCONS_BUILD)
#include "camera_app_config.h"
#endif
#include "services/camera_backend_utils.h"
#include "services/camera_frame_pool.h"
#include "services/jpeg_metadata.h"
#include "services/video_recorder.h"
#include "utils/logger.h"

#if !USE_DESKTOP
#include <fcntl.h>
#include <libcamera/base/shared_fd.h>
#include <libcamera/base/unique_fd.h>
#include <libcamera/camera.h>
#include <libcamera/camera_manager.h>
#include <libcamera/control_ids.h>
#include <libcamera/formats.h>
#include <libcamera/framebuffer.h>
#include <libcamera/orientation.h>
#include <libcamera/property_ids.h>
#include <libcamera/request.h>
#include <libcamera/stream.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <map>
#include <vector>
#endif

namespace service {
namespace {

using namespace camera_backend;
constexpr unsigned int kPreviewBufferCount   = 1;
constexpr unsigned int kCaptureBufferCount   = 1;
constexpr int64_t kPreviewMinFrameDurationUs = 16667;
constexpr int64_t kPreviewMaxFrameDurationUs = 33333;
constexpr int64_t kStillMinFrameDurationUs   = 100;
constexpr int64_t kStillMaxFrameDurationUs   = 1000000000;
constexpr unsigned int kSensorRawBitDepth    = 10;
constexpr int kStillJpegQuality              = 95;
constexpr auto kStillCaptureTimeout          = std::chrono::seconds(30);

#if !USE_DESKTOP
constexpr const char* kCameraDmaHeapPath = "/dev/dma_heap/default_cma_region";

class CameraDmaHeap {
 public:
  CameraDmaHeap() {
    const int fd = ::open(kCameraDmaHeapPath, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
      LOG_ERROR("Failed to open camera dma-heap {}: {}", kCameraDmaHeapPath, std::strerror(errno));
      return;
    }
    handle_ = libcamera::UniqueFD(fd);
  }

  bool is_valid() const { return handle_.isValid(); }

  libcamera::UniqueFD allocate(const std::string& name, size_t size) const {
    if (!handle_.isValid() || name.empty() || size == 0) {
      return {};
    }

    struct dma_heap_allocation_data allocation{};
    allocation.len      = size;
    allocation.fd_flags = O_CLOEXEC | O_RDWR;
    if (::ioctl(handle_.get(), DMA_HEAP_IOCTL_ALLOC, &allocation) < 0) {
      LOG_ERROR("dma-heap allocation failed: heap={} name={} size={} error={}",
                kCameraDmaHeapPath,
                name,
                size,
                std::strerror(errno));
      return {};
    }

    libcamera::UniqueFD fd(static_cast<int>(allocation.fd));
    if (::ioctl(fd.get(), DMA_BUF_SET_NAME, name.c_str()) < 0) {
      LOG_ERROR("Failed to name dma-buf {}: {}", name, std::strerror(errno));
      return {};
    }
    return fd;
  }

 private:
  libcamera::UniqueFD handle_;
};

bool configure_rpi_apps_pipeline() {
  const char* existing = std::getenv("LIBCAMERA_RPI_CONFIG_FILE");
  if (existing && existing[0]) {
    LOG_INFO("Using configured Raspberry Pi pipeline file: {}", existing);
    return true;
  }

  constexpr const char* config_paths[] = {
      "/usr/local/share/libcamera/pipeline/rpi/vc4/rpi_apps.yaml",
      "/usr/share/libcamera/pipeline/rpi/vc4/rpi_apps.yaml",
  };
  for (const char* path : config_paths) {
    struct stat info{};
    if (::stat(path, &info) == 0 && S_ISREG(info.st_mode)) {
      if (::setenv("LIBCAMERA_RPI_CONFIG_FILE", path, 1) == 0) {
        LOG_INFO("Using rpicam-apps pipeline configuration: {}", path);
        return true;
      }
      LOG_ERROR("Failed to set Raspberry Pi pipeline configuration {}: {}",
                path,
                std::strerror(errno));
      return false;
    }
  }

  LOG_ERROR("Raspberry Pi rpi_apps.yaml pipeline configuration was not found");
  return false;
}

bool sync_dma_buf(int fd, uint64_t flags) {
  if (fd < 0) {
    return false;
  }

  struct dma_buf_sync sync{flags};
  if (::ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync) < 0) {
    LOG_ERROR("dma-buf sync failed: fd={} flags={} error={}", fd, flags, std::strerror(errno));
    return false;
  }
  return true;
}

PixelFormat map_libcamera_format(const libcamera::PixelFormat& format) {
  if (format == libcamera::formats::YUV420) return PixelFormat::YUV420;
  if (format == libcamera::formats::YUYV) return PixelFormat::YUYV;
  if (format == libcamera::formats::UYVY) return PixelFormat::UYVY;
  if (format == libcamera::formats::RGB565) return PixelFormat::RGB565;
  if (format == libcamera::formats::RGB888) return PixelFormat::RGB888;
  if (format == libcamera::formats::BGR888) return PixelFormat::BGR888;
  if (format == libcamera::formats::XRGB8888) return PixelFormat::XRGB8888;
  return PixelFormat::XBGR8888;
}

std::string escape_json_string(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size() + 8);
  for (char c : value) {
    switch (c) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\b':
        escaped += "\\b";
        break;
      case '\f':
        escaped += "\\f";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped += c;
        break;
    }
  }
  return escaped;
}

template <typename T>
void append_json_value(std::ostringstream& json, const char* key, const std::optional<T>& value) {
  if (!value) {
    return;
  }
  if (json.tellp() > 1) {
    json << ',';
  }
  json << '"' << key << "\":" << *value;
}

template <typename T>
void append_json_value(std::ostringstream& json, const char* key, T value) {
  if (json.tellp() > 1) {
    json << ',';
  }
  json << '"' << key << "\":" << value;
}

void append_json_bool(std::ostringstream& json, const char* key, const std::optional<bool>& value) {
  if (!value) {
    return;
  }
  if (json.tellp() > 1) {
    json << ',';
  }
  json << '"' << key << "\":" << (*value ? "true" : "false");
}

void append_json_bool(std::ostringstream& json, const char* key, bool value) {
  if (json.tellp() > 1) {
    json << ',';
  }
  json << '"' << key << "\":" << (value ? "true" : "false");
}

void append_json_string(std::ostringstream& json, const char* key, const std::string& value) {
  if (value.empty()) {
    return;
  }
  if (json.tellp() > 1) {
    json << ',';
  }
  json << '"' << key << "\":\"" << escape_json_string(value) << '"';
}

void append_json_size(std::ostringstream& json, const char* key, const libcamera::Size& value) {
  if (json.tellp() > 1) {
    json << ',';
  }
  json << '"' << key << "\":{\"width\":" << value.width << ",\"height\":" << value.height << '}';
}

void append_json_rectangle(std::ostringstream& json,
                           const char* key,
                           const libcamera::Rectangle& value) {
  if (json.tellp() > 1) {
    json << ',';
  }
  json << '"' << key << "\":{\"x\":" << value.x << ",\"y\":" << value.y
       << ",\"width\":" << value.width << ",\"height\":" << value.height << '}';
}

template <typename Span>
void append_json_array(std::ostringstream& json, const char* key, const Span& value) {
  if (json.tellp() > 1) {
    json << ',';
  }
  json << '"' << key << "\":[";
  for (size_t i = 0; i < value.size(); ++i) {
    if (i > 0) {
      json << ',';
    }
    json << value[i];
  }
  json << ']';
}

template <typename Span>
void append_json_rectangles(std::ostringstream& json, const char* key, const Span& value) {
  if (json.tellp() > 1) {
    json << ',';
  }
  json << '"' << key << "\":[";
  for (size_t i = 0; i < value.size(); ++i) {
    if (i > 0) {
      json << ',';
    }
    json << "{\"x\":" << value[i].x << ",\"y\":" << value[i].y << ",\"width\":" << value[i].width
         << ",\"height\":" << value[i].height << '}';
  }
  json << ']';
}

uint16_t exif_metering_mode(int32_t mode) {
  switch (mode) {
    case libcamera::controls::MeteringCentreWeighted:
      return 2;
    case libcamera::controls::MeteringSpot:
      return 3;
    case libcamera::controls::MeteringMatrix:
      return 5;
    default:
      return 255;
  }
}

uint16_t exif_light_source(int32_t awb_mode) {
  switch (awb_mode) {
    case libcamera::controls::AwbDaylight:
      return 1;
    case libcamera::controls::AwbFluorescent:
      return 2;
    case libcamera::controls::AwbIncandescent:
    case libcamera::controls::AwbTungsten:
      return 3;
    case libcamera::controls::AwbCloudy:
      return 10;
    case libcamera::controls::AwbAuto:
      return 0;
    default:
      return 255;
  }
}

std::string control_info_value_string(const libcamera::ControlValue& value) {
  return value.isNone() ? std::string{} : value.toString();
}
#endif

}  // namespace

struct LibcameraBackend::Impl {
#if !USE_DESKTOP
  struct FocusCapability {
    bool af_mode{false};
    bool af_trigger{false};
    bool af_state{false};
    bool focus_fom{false};
    bool lens_position{false};
    std::string lens_position_min;
    std::string lens_position_max;
    std::string lens_position_default;
  };

  struct MappedBuffer {
    struct Plane {
      void* addr{nullptr};
      size_t size{0};
      int fd{-1};
      size_t data_offset{0};
      size_t data_size{0};
    };

    std::vector<Plane> planes;
  };

  class DmaBufReadGuard {
   public:
    explicit DmaBufReadGuard(const MappedBuffer& buffer) {
      for (const auto& plane : buffer.planes) {
        if (std::find(fds_.begin(), fds_.end(), plane.fd) != fds_.end()) {
          continue;
        }
        if (!sync_dma_buf(plane.fd, DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ)) {
          valid_ = false;
          continue;
        }
        fds_.push_back(plane.fd);
      }
      valid_ = valid_ && !fds_.empty();
    }

    ~DmaBufReadGuard() {
      for (int fd : fds_) {
        sync_dma_buf(fd, DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ);
      }
    }

    bool is_valid() const { return valid_; }

   private:
    std::vector<int> fds_;
    bool valid_{true};
  };

  struct StillEncodeJob {
    std::vector<uint8_t> yuv420;
    int width{0};
    int height{0};
    int stride{0};
    std::string path;
    CameraResolution requested_resolution{};
    ExifMetadata metadata;
  };

  enum class StreamMode { Stopped, Preview, Still };

  std::unique_ptr<libcamera::CameraManager> manager;
  std::shared_ptr<libcamera::Camera> camera;
  std::unique_ptr<libcamera::CameraConfiguration> config;
  CameraDmaHeap dma_heap;
  std::map<libcamera::Stream*, std::vector<std::unique_ptr<libcamera::FrameBuffer>>> frame_buffers;
  libcamera::Stream* preview_stream{nullptr};
  libcamera::Stream* still_stream{nullptr};
  libcamera::Stream* raw_stream{nullptr};
  std::vector<std::unique_ptr<libcamera::Request>> requests;
  std::map<const libcamera::FrameBuffer*, MappedBuffer> mapped_buffers;
  std::mutex mutex;
  std::mutex video_mutex;
  std::mutex still_mutex;
  std::mutex capture_mutex;
  std::condition_variable still_cv;
  std::condition_variable capture_cv;
  std::deque<StillEncodeJob> still_jobs;
  std::thread still_worker;
  std::thread capture_worker;
  bool still_worker_stop{false};
  bool still_worker_running{false};
  bool capture_worker_stop{false};
  bool capture_worker_running{false};
  bool capture_job_pending{false};
  bool capture_frame_done{false};
  bool capture_frame_ok{false};
  StillFrameStabilityTracker still_stability;
  CameraResolution capture_output_resolution{kSensorMaxWidth, kSensorMaxHeight};
  CameraResolution capture_requested_resolution{kSensorMaxWidth, kSensorMaxHeight};
  CameraFrame pending_frame;
  CameraFramePool preview_pool{3};
  bool new_frame{false};
  std::atomic<bool> opened{false};
  std::atomic<bool> streaming{false};
  std::atomic<bool> capture_in_progress{false};
  std::atomic<StreamMode> stream_mode{StreamMode::Stopped};
  CaptureState capture_state{CaptureState::Idle};
  CameraResolution capture_saved_resolution{};
  VideoState video_state{VideoState::Idle};
  std::string last_capture_path;
  std::string last_video_path_value;
  std::string last_error;
  MjpegAviWriter video_writer;
  int video_quality{80};
  CameraResolution capture_resolution{kSensorMaxWidth, kSensorMaxHeight};
  CameraZoomState zoom_state{};
  libcamera::Rectangle scaler_crop_max{};
  int preview_w{kPreviewWidth};
  int preview_h{kPreviewHeight};
  int preview_stride{kPreviewWidth * 2};
  libcamera::PixelFormat preview_format{libcamera::formats::RGB565};
  bool pipeline_rotation{false};
  std::atomic<uint64_t> preview_input_frames{0};
  std::atomic<uint64_t> preview_published_frames{0};
  std::atomic<uint64_t> preview_dropped_frames{0};
  std::atomic<uint64_t> preview_convert_us{0};
  int still_w{kDefaultCaptureWidth};
  int still_h{kDefaultCaptureHeight};
  int still_stride{kDefaultCaptureWidth};
  libcamera::PixelFormat still_format{libcamera::formats::YUV420};
  std::string camera_model{"CardputerZero IMX219"};
  float sensor_sensitivity{100.0f};
  bool has_sensor_sensitivity{false};
  FocusCapability focus_capability{};

  void start_still_worker() {
    std::lock_guard<std::mutex> lock(still_mutex);
    if (still_worker_running) return;
    still_worker_stop    = false;
    still_worker_running = true;
    still_worker         = std::thread([this] { still_encode_loop(); });
  }

  void stop_still_worker() {
    {
      std::lock_guard<std::mutex> lock(still_mutex);
      if (!still_worker_running) return;
      still_worker_stop = true;
    }
    still_cv.notify_one();
    if (still_worker.joinable()) still_worker.join();
    std::lock_guard<std::mutex> lock(still_mutex);
    still_jobs.clear();
    still_worker_running = false;
  }

  void still_encode_loop() {
    for (;;) {
      StillEncodeJob job;
      {
        std::unique_lock<std::mutex> lock(still_mutex);
        still_cv.wait(lock, [this] { return still_worker_stop || !still_jobs.empty(); });
        if (still_jobs.empty() && still_worker_stop) return;
        job = std::move(still_jobs.front());
        still_jobs.pop_front();
      }

      const bool saved = save_jpeg_yuv420(job.path,
                                          job.yuv420,
                                          job.width,
                                          job.height,
                                          job.stride,
                                          kStillJpegQuality,
                                          &job.metadata);
      {
        std::lock_guard<std::mutex> lock(mutex);
        capture_state = saved ? CaptureState::Saved : CaptureState::Failed;
        capture_saved_resolution =
            saved ? CameraResolution{job.width, job.height} : CameraResolution{};
      }
      capture_in_progress.store(false);
      LOG_INFO(
          "Still JPEG encode: requested={}x{} captured={}x{} encoded={}x{} fallback={} path={}",
          job.requested_resolution.width,
          job.requested_resolution.height,
          job.width,
          job.height,
          job.width,
          job.height,
          job.width != job.requested_resolution.width ||
              job.height != job.requested_resolution.height,
          job.path);
      if (!saved) LOG_WARN("Failed to encode still image: {}", job.path);
    }
  }

  bool enqueue_still_job(StillEncodeJob job) {
    std::lock_guard<std::mutex> lock(still_mutex);
    constexpr size_t kMaxStillJobs = 2;
    if (still_jobs.size() >= kMaxStillJobs || !still_worker_running || still_worker_stop) {
      return false;
    }
    still_jobs.push_back(std::move(job));
    still_cv.notify_one();
    return true;
  }

  bool capture_stop_requested() {
    std::lock_guard<std::mutex> lock(capture_mutex);
    return capture_worker_stop;
  }

  void mark_capture_failed() {
    std::lock_guard<std::mutex> lock(mutex);
    if (capture_state == CaptureState::Requested) {
      capture_state            = CaptureState::Failed;
      capture_saved_resolution = {};
    }
  }

  void perform_capture_cycle() {
    CameraResolution requested_resolution;
    {
      std::lock_guard<std::mutex> lock(capture_mutex);
      requested_resolution = capture_output_resolution;
    }

    bool capture_started = false;
    for (CameraResolution candidate : capture_resolution_candidates(requested_resolution)) {
      if (capture_stop_requested()) break;
      LOG_INFO("Switching camera to still mode: requested={}x{}",
               candidate.width,
               candidate.height);
      if (start_still_stream(candidate)) {
        capture_started = true;
        break;
      }
    }

    bool frame_ok = false;
    if (capture_started) {
      std::unique_lock<std::mutex> lock(capture_mutex);
      const bool completed = capture_cv.wait_for(lock, kStillCaptureTimeout, [this] {
        return capture_worker_stop || capture_frame_done;
      });
      frame_ok             = completed && capture_frame_done && capture_frame_ok;
      if (!completed) {
        LOG_ERROR("Timed out waiting for full-resolution still frame");
      }
    }

    release_stream_resources();

    bool preview_restored = false;
    if (!capture_stop_requested()) {
      preview_restored = start_preview_stream();
      if (!preview_restored) {
        LOG_ERROR("Failed to restore camera preview after still capture");
      }
    }

    if (!frame_ok) {
      mark_capture_failed();
    }
    if (!preview_restored && !capture_stop_requested()) {
      opened.store(false);
    }
    if (!frame_ok) {
      capture_in_progress.store(false);
    }
  }

  void capture_loop() {
    for (;;) {
      {
        std::unique_lock<std::mutex> lock(capture_mutex);
        capture_cv.wait(lock, [this] { return capture_worker_stop || capture_job_pending; });
        if (capture_worker_stop) return;
        capture_job_pending = false;
        capture_frame_done  = false;
        capture_frame_ok    = false;
        still_stability.reset();
      }
      perform_capture_cycle();
    }
  }

  void start_capture_worker() {
    std::lock_guard<std::mutex> lock(capture_mutex);
    if (capture_worker_running) return;
    capture_worker_stop = false;
    capture_job_pending = false;
    capture_frame_done  = false;
    capture_frame_ok    = false;
    still_stability.reset();
    capture_worker_running = true;
    capture_worker         = std::thread([this] { capture_loop(); });
  }

  void stop_capture_worker() {
    {
      std::lock_guard<std::mutex> lock(capture_mutex);
      if (!capture_worker_running) return;
      capture_worker_stop = true;
    }
    capture_cv.notify_all();
    if (capture_worker.joinable()) capture_worker.join();
    std::lock_guard<std::mutex> lock(capture_mutex);
    capture_job_pending = false;
    capture_frame_done  = false;
    capture_frame_ok    = false;
    still_stability.reset();
    capture_worker_running = false;
    capture_in_progress.store(false);
  }

  static bool is_supported(const libcamera::PixelFormat& format) {
    return format == libcamera::formats::YUV420 || format == libcamera::formats::YUYV ||
           format == libcamera::formats::UYVY || format == libcamera::formats::RGB565 ||
           format == libcamera::formats::RGB888 || format == libcamera::formats::BGR888 ||
           format == libcamera::formats::XRGB8888 || format == libcamera::formats::XBGR8888;
  }

  void update_scaler_crop_max() {
    const auto crop_max = camera->properties().get(libcamera::properties::ScalerCropMaximum);
    scaler_crop_max =
        crop_max ? *crop_max : libcamera::Rectangle(0, 0, kSensorMaxWidth, kSensorMaxHeight);
  }

  bool configure_preview_stream() {
    config = camera->generateConfiguration({libcamera::StreamRole::Viewfinder});
    if (!config || config->size() != 1) {
      last_error = "Camera preview configuration generation failed";
      LOG_ERROR("{}", last_error);
      return false;
    }

    config->orientation                         = libcamera::Orientation::Rotate180;
    libcamera::StreamConfiguration& preview_cfg = config->at(0);
    preview_cfg.size.width                      = kPreviewWidth;
    preview_cfg.size.height                     = kPreviewHeight;
    preview_cfg.pixelFormat                     = libcamera::formats::RGB565;
    preview_cfg.bufferCount                     = kPreviewBufferCount;

    if (config->validate() == libcamera::CameraConfiguration::Invalid) {
      last_error = "Invalid camera preview configuration";
      LOG_WARN("{}", last_error);
      return false;
    }

    pipeline_rotation = config->orientation == libcamera::Orientation::Rotate180;
    if (camera->configure(config.get())) {
      last_error = "Camera preview configure failed";
      LOG_WARN("{}", last_error);
      return false;
    }

    libcamera::StreamConfiguration& active = config->at(0);
    if (!is_supported(active.pixelFormat)) {
      last_error = "Unsupported camera preview format: " + active.pixelFormat.toString();
      LOG_WARN("{}", last_error);
      return false;
    }

    preview_stream = active.stream();
    preview_w      = static_cast<int>(active.size.width);
    preview_h      = static_cast<int>(active.size.height);
    preview_stride = static_cast<int>(active.stride);
    preview_format = active.pixelFormat;
    update_scaler_crop_max();

    if (!allocate_capture_buffers(active, "camera-preview")) {
      last_error = "Camera preview dma-heap framebuffer allocation failed";
      frame_buffers.clear();
      return false;
    }

    const uint64_t bytes = static_cast<uint64_t>(active.frameSize) * active.bufferCount;
    LOG_INFO("Camera CMA buffers: mode=preview heap={} frame_size={} buffers={} total_bytes={}",
             kCameraDmaHeapPath,
             active.frameSize,
             active.bufferCount,
             bytes);
    return true;
  }

  bool configure_still_stream(CameraResolution resolution) {
    config = camera->generateConfiguration(
        {libcamera::StreamRole::StillCapture, libcamera::StreamRole::Raw});
    if (!config || config->size() != 2) {
      last_error = "Camera still/raw configuration generation failed";
      LOG_ERROR("{}", last_error);
      return false;
    }

    config->orientation                       = libcamera::Orientation::Rotate180;
    libcamera::StreamConfiguration& still_cfg = config->at(0);
    still_cfg.size.width                      = static_cast<unsigned int>(resolution.width);
    still_cfg.size.height                     = static_cast<unsigned int>(resolution.height);
    still_cfg.pixelFormat                     = libcamera::formats::YUV420;
    still_cfg.bufferCount                     = kCaptureBufferCount;

    // As in rpicam-still, an external mandatory RAW stream both selects the sensor mode and
    // removes the need for the VC4 pipeline to allocate its four internal Unicam RAW buffers.
    libcamera::StreamConfiguration& raw_cfg = config->at(1);
    raw_cfg.size.width                      = static_cast<unsigned int>(resolution.width);
    raw_cfg.size.height                     = static_cast<unsigned int>(resolution.height);
    raw_cfg.pixelFormat                     = libcamera::formats::SBGGR10_CSI2P;
    raw_cfg.bufferCount                     = kCaptureBufferCount;
    config->sensorConfig                    = libcamera::SensorConfiguration();
    config->sensorConfig->outputSize        = raw_cfg.size;
    config->sensorConfig->bitDepth          = kSensorRawBitDepth;

    if (config->validate() == libcamera::CameraConfiguration::Invalid) {
      last_error = "Invalid camera still/raw configuration";
      LOG_WARN("{}: requested={}x{}", last_error, resolution.width, resolution.height);
      return false;
    }

    pipeline_rotation = config->orientation == libcamera::Orientation::Rotate180;
    if (camera->configure(config.get())) {
      last_error = "Camera still/raw configure failed";
      LOG_WARN("{}: requested={}x{}", last_error, resolution.width, resolution.height);
      return false;
    }

    libcamera::StreamConfiguration& active_still = config->at(0);
    libcamera::StreamConfiguration& active_raw   = config->at(1);
    if (!is_supported(active_still.pixelFormat)) {
      last_error = "Unsupported camera still format: " + active_still.pixelFormat.toString();
      LOG_WARN("{}", last_error);
      return false;
    }

    still_stream = active_still.stream();
    raw_stream   = active_raw.stream();
    still_w      = static_cast<int>(active_still.size.width);
    still_h      = static_cast<int>(active_still.size.height);
    still_stride = static_cast<int>(active_still.stride);
    still_format = active_still.pixelFormat;
    update_scaler_crop_max();

    if (!allocate_capture_buffers(active_still, "camera-still") ||
        !allocate_capture_buffers(active_raw, "camera-raw")) {
      last_error = "Camera still/raw dma-heap framebuffer allocation failed";
      frame_buffers.clear();
      return false;
    }

    const uint64_t still_bytes =
        static_cast<uint64_t>(active_still.frameSize) * active_still.bufferCount;
    const uint64_t raw_bytes = static_cast<uint64_t>(active_raw.frameSize) * active_raw.bufferCount;
    LOG_INFO(
        "Camera CMA buffers: mode=still heap={} still={}x{} format={} frame_size={} buffers={} "
        "raw={}x{} format={} frame_size={} buffers={} sensor={}x{} bit_depth={} total_bytes={}",
        kCameraDmaHeapPath,
        active_still.size.width,
        active_still.size.height,
        active_still.pixelFormat.toString(),
        active_still.frameSize,
        active_still.bufferCount,
        active_raw.size.width,
        active_raw.size.height,
        active_raw.pixelFormat.toString(),
        active_raw.frameSize,
        active_raw.bufferCount,
        config->sensorConfig ? config->sensorConfig->outputSize.width : 0,
        config->sensorConfig ? config->sensorConfig->outputSize.height : 0,
        config->sensorConfig ? config->sensorConfig->bitDepth : 0,
        still_bytes + raw_bytes);
    return true;
  }

  bool allocate_capture_buffers(const libcamera::StreamConfiguration& stream_config,
                                const char* name_prefix) {
    libcamera::Stream* stream = stream_config.stream();
    if (!dma_heap.is_valid() || !stream || stream_config.frameSize == 0 ||
        stream_config.bufferCount == 0) {
      LOG_ERROR(
          "Invalid dma-heap stream allocation: heap_valid={} stream={} frame_size={} buffers={}",
          dma_heap.is_valid(),
          static_cast<const void*>(stream),
          stream_config.frameSize,
          stream_config.bufferCount);
      return false;
    }

    auto& buffers = frame_buffers[stream];
    buffers.reserve(stream_config.bufferCount);
    for (unsigned int i = 0; i < stream_config.bufferCount; ++i) {
      const std::string name = std::string(name_prefix) + "-" + std::to_string(i);
      libcamera::UniqueFD fd = dma_heap.allocate(name, stream_config.frameSize);
      if (!fd.isValid()) {
        return false;
      }

      std::vector<libcamera::FrameBuffer::Plane> planes(1);
      planes[0].fd     = libcamera::SharedFD(std::move(fd));
      planes[0].offset = 0;
      planes[0].length = stream_config.frameSize;
      buffers.push_back(std::make_unique<libcamera::FrameBuffer>(planes));
    }
    return true;
  }

  void release_stream_resources() {
    const bool was_streaming = streaming.exchange(false);
    stream_mode.store(StreamMode::Stopped);
    if (camera) {
      camera->requestCompleted.disconnect(this);
      if (was_streaming) {
        camera->stop();
      }
    }

    requests.clear();

    for (auto& item : mapped_buffers) {
      for (auto& plane : item.second.planes) {
        if (plane.addr && plane.addr != MAP_FAILED) {
          ::munmap(plane.addr, plane.size);
        }
      }
    }
    mapped_buffers.clear();
    frame_buffers.clear();
    preview_stream = nullptr;
    still_stream   = nullptr;
    raw_stream     = nullptr;
  }

  bool map_buffer(const libcamera::FrameBuffer* buffer) {
    const auto planes = buffer->planes();
    if (planes.empty()) {
      return false;
    }

    MappedBuffer mapped;
    for (const auto& plane : planes) {
      const long page_size_value = ::sysconf(_SC_PAGE_SIZE);
      const size_t page_size    = page_size_value > 0 ? static_cast<size_t>(page_size_value) : 4096;
      const size_t plane_offset = static_cast<size_t>(plane.offset);
      const size_t map_offset   = plane_offset & ~(page_size - 1);
      const size_t data_offset  = plane_offset - map_offset;
      const size_t map_length   = data_offset + static_cast<size_t>(plane.length);

      void* memory = ::mmap(nullptr,
                            map_length,
                            PROT_READ | PROT_WRITE,
                            MAP_SHARED,
                            plane.fd.get(),
                            map_offset);
      if (memory == MAP_FAILED) {
        LOG_WARN("Camera framebuffer mmap failed");
        break;
      }

      mapped.planes.push_back(
          {memory, map_length, plane.fd.get(), data_offset, static_cast<size_t>(plane.length)});
    }

    if (mapped.planes.size() != planes.size()) {
      for (auto& plane : mapped.planes) {
        if (plane.addr && plane.addr != MAP_FAILED) {
          ::munmap(plane.addr, plane.size);
        }
      }
      return false;
    }

    mapped_buffers[buffer] = std::move(mapped);
    return true;
  }

  bool create_preview_requests() {
    const auto preview_it = frame_buffers.find(preview_stream);
    if (preview_it == frame_buffers.end()) {
      last_error = "Camera preview dma-heap buffers are missing";
      return false;
    }

    for (const auto& buffer : preview_it->second) {
      if (!map_buffer(buffer.get())) {
        last_error = "Camera preview dma-buf mmap failed";
        return false;
      }
      auto request = camera->createRequest();
      if (!request || request->addBuffer(preview_stream, buffer.get()) < 0) {
        last_error = "Camera preview request creation failed";
        return false;
      }
      apply_preview_request_controls(request.get());
      requests.push_back(std::move(request));
    }
    return true;
  }

  bool create_still_request() {
    const auto still_it = frame_buffers.find(still_stream);
    const auto raw_it   = frame_buffers.find(raw_stream);
    if (still_it == frame_buffers.end() || raw_it == frame_buffers.end() ||
        still_it->second.size() != 1 || raw_it->second.size() != 1) {
      last_error = "Camera still/raw dma-heap buffers are missing";
      return false;
    }

    if (!map_buffer(still_it->second.front().get())) {
      last_error = "Camera still dma-buf mmap failed";
      return false;
    }

    auto request = camera->createRequest();
    if (!request || request->addBuffer(still_stream, still_it->second.front().get()) < 0 ||
        request->addBuffer(raw_stream, raw_it->second.front().get()) < 0) {
      last_error = "Camera still/raw request creation failed";
      return false;
    }
    requests.push_back(std::move(request));
    return true;
  }

  bool start_configured_stream(StreamMode mode) {
    camera->requestCompleted.connect(this, &Impl::request_complete);

    libcamera::ControlList still_controls;
    const libcamera::ControlList* start_controls = nullptr;
    if (mode == StreamMode::Still) {
      still_controls = still_start_controls();
      start_controls = &still_controls;
    }
    if (camera->start(start_controls)) {
      last_error = "Camera start failed";
      LOG_WARN("{}: mode={}", last_error, mode == StreamMode::Preview ? "preview" : "still");
      camera->requestCompleted.disconnect(this);
      return false;
    }

    stream_mode.store(mode);
    streaming.store(true);
    for (auto& request : requests) {
      if (camera->queueRequest(request.get()) < 0) {
        last_error = "Camera initial request queue failed";
        LOG_ERROR("{}", last_error);
        release_stream_resources();
        return false;
      }
    }
    return true;
  }

  bool start_preview_stream() {
    release_stream_resources();
    if (!configure_preview_stream() || !create_preview_requests() ||
        !start_configured_stream(StreamMode::Preview)) {
      release_stream_resources();
      return false;
    }
    LOG_INFO("Camera preview started: {}x{} stride={} format={} buffers={}",
             preview_w,
             preview_h,
             preview_stride,
             preview_format.toString(),
             kPreviewBufferCount);
    return true;
  }

  bool start_still_stream(CameraResolution resolution) {
    release_stream_resources();
    if (!configure_still_stream(resolution) || !create_still_request() ||
        !start_configured_stream(StreamMode::Still)) {
      release_stream_resources();
      return false;
    }
    LOG_INFO("Camera still capture started: {}x{} stride={} format={} raw_buffer=true",
             still_w,
             still_h,
             still_stride,
             still_format.toString());
    return true;
  }

  void detect_focus_capability() {
    focus_capability = {};
    if (!camera) {
      return;
    }

    const libcamera::ControlInfoMap& controls = camera->controls();
    focus_capability.af_mode = controls.find(libcamera::controls::AfMode.id()) != controls.end();
    focus_capability.af_trigger =
        controls.find(libcamera::controls::AfTrigger.id()) != controls.end();
    focus_capability.af_state = controls.find(libcamera::controls::AfState.id()) != controls.end();
    focus_capability.focus_fom =
        controls.find(libcamera::controls::FocusFoM.id()) != controls.end();

    const auto lens_position       = controls.find(libcamera::controls::LensPosition.id());
    focus_capability.lens_position = lens_position != controls.end();
    if (focus_capability.lens_position) {
      focus_capability.lens_position_min = control_info_value_string(lens_position->second.min());
      focus_capability.lens_position_max = control_info_value_string(lens_position->second.max());
      focus_capability.lens_position_default =
          control_info_value_string(lens_position->second.def());
    }

    LOG_INFO(
        "Focus controls: LensPosition={} range=[{},{}] default={} AfMode={} AfTrigger={} "
        "AfState={} FocusFoM={}",
        focus_capability.lens_position,
        focus_capability.lens_position_min,
        focus_capability.lens_position_max,
        focus_capability.lens_position_default,
        focus_capability.af_mode,
        focus_capability.af_trigger,
        focus_capability.af_state,
        focus_capability.focus_fom);
  }

  bool open() {
    if (!dma_heap.is_valid()) {
      last_error = std::string("Required camera dma-heap is unavailable: ") + kCameraDmaHeapPath;
      LOG_ERROR("{}", last_error);
      return false;
    }

    if (!configure_rpi_apps_pipeline()) {
      last_error = "Required rpicam-apps pipeline configuration is unavailable";
      return false;
    }

    start_still_worker();
    manager = std::make_unique<libcamera::CameraManager>();
    if (manager->start()) {
      last_error = "CameraManager start failed";
      LOG_ERROR("{}", last_error);
      return false;
    }

    std::shared_ptr<libcamera::Camera> selected;
    for (const auto& cam : manager->cameras()) {
      std::string model_text = cam->id();
      if (auto model = cam->properties().get(libcamera::properties::Model)) {
        model_text = *model;
      }
      LOG_INFO("Found camera: {}", model_text);

      const std::string lower = lower_string(model_text);
      if (!selected || lower.find("imx219") != std::string::npos) {
        selected = cam;
        if (lower.find("imx219") != std::string::npos) {
          break;
        }
      }
    }

    if (!selected) {
      last_error = "No libcamera camera found. Check libcamera IPA modules and ABI version.";
      LOG_ERROR("{}", last_error);
      return false;
    }

    camera       = selected;
    camera_model = "CardputerZero IMX219";
    if (auto model = camera->properties().get(libcamera::properties::Model)) {
      const std::string model_value(model->begin(), model->end());
      if (!model_value.empty()) {
        camera_model = model_value;
      }
    }
    if (auto sensitivity = camera->properties().get(libcamera::properties::SensorSensitivity)) {
      sensor_sensitivity     = *sensitivity;
      has_sensor_sensitivity = true;
    }
    detect_focus_capability();
    if (camera->acquire()) {
      last_error = "Camera acquire failed";
      LOG_ERROR("{}", last_error);
      camera.reset();
      return false;
    }

    {
      std::lock_guard<std::mutex> lock(mutex);
      pending_frame.width  = kPreviewWidth;
      pending_frame.height = kPreviewHeight;
      pending_frame.rgb565 =
          std::make_shared<std::vector<uint16_t>>(kPreviewWidth * kPreviewHeight, 0);
    }

    if (!start_preview_stream()) {
      if (last_error.empty()) {
        last_error = "Camera preview stream configuration failed";
      }
      LOG_ERROR("{}", last_error);
      close();
      return false;
    }
    opened.store(true);
    start_capture_worker();
    return true;
  }

  void close() {
    opened.store(false);
    (void)stop_video_recording();
    stop_capture_worker();
    if (camera) {
      release_stream_resources();
      camera->release();
      camera.reset();
    }

    stop_still_worker();

    if (manager) {
      manager->stop();
      manager.reset();
    }
  }

  bool consume_frame(CameraFrame& frame) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!new_frame) {
      return false;
    }
    frame     = std::move(pending_frame);
    new_frame = false;
    return true;
  }

  bool request_capture() {
    if (!opened.load() || !streaming.load() || stream_mode.load() != StreamMode::Preview) {
      return false;
    }

    std::lock_guard<std::mutex> video_lock(video_mutex);
    if (video_writer.is_open()) {
      last_error = "Still capture is unavailable while video recording is active";
      LOG_WARN("{}", last_error);
      return false;
    }

    bool expected = false;
    if (!capture_in_progress.compare_exchange_strong(expected, true)) {
      return false;
    }

    CameraResolution requested_resolution;
    {
      std::lock_guard<std::mutex> lock(mutex);
      last_capture_path            = make_photo_path();
      capture_state                = CaptureState::Requested;
      capture_saved_resolution     = {};
      requested_resolution         = capture_resolution;
      capture_requested_resolution = requested_resolution;
    }

    {
      std::lock_guard<std::mutex> lock(capture_mutex);
      if (!capture_worker_running || capture_worker_stop) {
        capture_in_progress.store(false);
        mark_capture_failed();
        return false;
      }
      capture_output_resolution = requested_resolution;
      capture_job_pending       = true;
    }
    capture_cv.notify_one();
    return true;
  }

  bool start_video_recording(int fps, int quality) {
    std::lock_guard<std::mutex> video_lock(video_mutex);
    if (!opened.load() || !streaming.load() || stream_mode.load() != StreamMode::Preview ||
        capture_in_progress.load() || preview_w <= 0 || preview_h <= 0) {
      video_state = VideoState::Failed;
      last_error  = "Camera preview stream is not ready for video recording";
      return false;
    }
    if (video_writer.is_open()) {
      return true;
    }

    last_video_path_value = make_video_path();
    video_quality         = clamp_int(quality, 1, 100);
    if (!video_writer.open(last_video_path_value, preview_w, preview_h, std::max(1, fps))) {
      video_state = VideoState::Failed;
      return false;
    }
    video_state = VideoState::Recording;
    LOG_INFO("Video recording started: {}", last_video_path_value);
    return true;
  }

  bool stop_video_recording() {
    std::lock_guard<std::mutex> video_lock(video_mutex);
    if (!video_writer.is_open()) {
      return video_state != VideoState::Failed;
    }

    const std::string path = video_writer.path();
    const uint32_t frames  = video_writer.frame_count();
    const bool saved       = video_writer.close() && frames > 0;
    last_video_path_value  = path;
    video_state            = saved ? VideoState::Saved : VideoState::Failed;
    LOG_INFO("Video recording stopped: {} frames={} saved={}", path, frames, saved);
    return saved;
  }

  VideoState consume_video_state(std::string* path) {
    const VideoState state = video_state;
    if (path) {
      *path = last_video_path_value;
    }
    if (video_state == VideoState::Saved || video_state == VideoState::Failed) {
      video_state = VideoState::Idle;
    }
    return state;
  }

  void set_capture_resolution(CameraResolution resolution) {
    std::lock_guard<std::mutex> lock(mutex);
    capture_resolution.width  = clamp_int(resolution.width, 1, kSensorMaxWidth);
    capture_resolution.height = clamp_int(resolution.height, 1, kSensorMaxHeight);
  }

  void set_zoom_state(CameraZoomState state) {
    std::lock_guard<std::mutex> lock(mutex);
    zoom_state.zoom_percent   = normalize_zoom_percent(state.zoom_percent);
    zoom_state.view_x_percent = clamp_int(state.view_x_percent, 0, 100);
    zoom_state.view_y_percent = clamp_int(state.view_y_percent, 0, 100);
    if (zoom_state.zoom_percent == kMinZoomPercent) {
      zoom_state.view_x_percent = 50;
      zoom_state.view_y_percent = 50;
    }
  }

  CameraZoomState current_zoom_state() {
    std::lock_guard<std::mutex> lock(mutex);
    return zoom_state;
  }

  libcamera::Rectangle scaler_crop_for_zoom_state(const CameraZoomState& state) const {
    const libcamera::Rectangle full =
        scaler_crop_max.isNull() ? libcamera::Rectangle(0, 0, kSensorMaxWidth, kSensorMaxHeight)
                                 : scaler_crop_max;
    const int zoom      = clamp_int(state.zoom_percent, kMinZoomPercent, kMaxZoomPercent);
    unsigned int crop_w = std::max(1u, full.width * 100u / static_cast<unsigned int>(zoom));
    unsigned int crop_h = std::max(1u, full.height * 100u / static_cast<unsigned int>(zoom));

    if (crop_w * 3 > crop_h * 4) {
      crop_w = std::max(1u, crop_h * 4 / 3);
    } else {
      crop_h = std::max(1u, crop_w * 3 / 4);
    }

    crop_w = std::min(crop_w, full.width);
    crop_h = std::min(crop_h, full.height);

    // ScalerCrop uses the unrotated sensor axes; the navigator follows the Rotate180 preview.
    const int max_x         = static_cast<int>(full.width - crop_w);
    const int max_y         = static_cast<int>(full.height - crop_h);
    const int sensor_view_x = 100 - clamp_int(state.view_x_percent, 0, 100);
    const int sensor_view_y = 100 - clamp_int(state.view_y_percent, 0, 100);
    const int crop_x        = full.x + max_x * sensor_view_x / 100;
    const int crop_y        = full.y + max_y * sensor_view_y / 100;
    return {crop_x, crop_y, crop_w, crop_h};
  }

  libcamera::Rectangle full_scaler_crop() const {
    return scaler_crop_max.isNull() ? libcamera::Rectangle(0, 0, kSensorMaxWidth, kSensorMaxHeight)
                                    : scaler_crop_max;
  }

  void apply_preview_request_controls(libcamera::Request* request) {
    if (!request) {
      return;
    }
    const CameraZoomState state = current_zoom_state();
    request->controls().set(libcamera::controls::ScalerCrop, scaler_crop_for_zoom_state(state));
    request->controls().set(libcamera::controls::FrameDurationLimits,
                            {kPreviewMinFrameDurationUs, kPreviewMaxFrameDurationUs});
  }

  libcamera::ControlList still_start_controls() const {
    libcamera::ControlList controls(camera->controls());
    controls.set(libcamera::controls::ScalerCrop, full_scaler_crop());
    controls.set(libcamera::controls::FrameDurationLimits,
                 {kStillMinFrameDurationUs, kStillMaxFrameDurationUs});
    controls.set(libcamera::controls::AeMeteringMode, libcamera::controls::MeteringCentreWeighted);
    controls.set(libcamera::controls::AwbMode, libcamera::controls::AwbAuto);
    controls.set(libcamera::controls::Brightness, 0.0f);
    controls.set(libcamera::controls::Contrast, 1.0f);
    controls.set(libcamera::controls::Saturation, 1.0f);
    controls.set(libcamera::controls::Sharpness, 1.0f);
    controls.set(libcamera::controls::draft::NoiseReductionMode,
                 libcamera::controls::draft::NoiseReductionModeHighQuality);
    return controls;
  }

  ExifMetadata build_still_exif_metadata(const libcamera::Request* request, int width, int height) {
    ExifMetadata metadata         = make_default_exif_metadata(width, height);
    metadata.model                = "CardputerZero IMX219";
    metadata.software             = "Camera 0.3.7";
    metadata.f_number_x100        = 200;
    metadata.focal_length_mm_x100 = 285;
    metadata.lens_make            = "M5Stack";
    metadata.lens_model           = "IMX219_PLCC 1/4 inch 2.85mm F2.0";

    const libcamera::ControlList* request_metadata = request ? &request->metadata() : nullptr;
    const auto exposure_time_us =
        request_metadata ? request_metadata->get(libcamera::controls::ExposureTime) : std::nullopt;
    const auto exposure_time_mode =
        request_metadata ? request_metadata->get(libcamera::controls::ExposureTimeMode)
                         : std::nullopt;
    const auto exposure_value =
        request_metadata ? request_metadata->get(libcamera::controls::ExposureValue) : std::nullopt;
    const auto analogue_gain =
        request_metadata ? request_metadata->get(libcamera::controls::AnalogueGain) : std::nullopt;
    const auto analogue_gain_mode =
        request_metadata ? request_metadata->get(libcamera::controls::AnalogueGainMode)
                         : std::nullopt;
    const auto digital_gain =
        request_metadata ? request_metadata->get(libcamera::controls::DigitalGain) : std::nullopt;
    const auto ae_enable =
        request_metadata ? request_metadata->get(libcamera::controls::AeEnable) : std::nullopt;
    const auto ae_state =
        request_metadata ? request_metadata->get(libcamera::controls::AeState) : std::nullopt;
    const auto ae_metering_mode = request_metadata
                                      ? request_metadata->get(libcamera::controls::AeMeteringMode)
                                      : std::nullopt;
    const auto ae_constraint_mode =
        request_metadata ? request_metadata->get(libcamera::controls::AeConstraintMode)
                         : std::nullopt;
    const auto ae_exposure_mode = request_metadata
                                      ? request_metadata->get(libcamera::controls::AeExposureMode)
                                      : std::nullopt;
    const auto ae_flicker_mode =
        request_metadata ? request_metadata->get(libcamera::controls::AeFlickerMode) : std::nullopt;
    const auto ae_flicker_period = request_metadata
                                       ? request_metadata->get(libcamera::controls::AeFlickerPeriod)
                                       : std::nullopt;
    const auto ae_flicker_detected =
        request_metadata ? request_metadata->get(libcamera::controls::AeFlickerDetected)
                         : std::nullopt;
    const auto lux =
        request_metadata ? request_metadata->get(libcamera::controls::Lux) : std::nullopt;
    const auto brightness =
        request_metadata ? request_metadata->get(libcamera::controls::Brightness) : std::nullopt;
    const auto contrast =
        request_metadata ? request_metadata->get(libcamera::controls::Contrast) : std::nullopt;
    const auto saturation =
        request_metadata ? request_metadata->get(libcamera::controls::Saturation) : std::nullopt;
    const auto sharpness =
        request_metadata ? request_metadata->get(libcamera::controls::Sharpness) : std::nullopt;
    const auto awb_enable =
        request_metadata ? request_metadata->get(libcamera::controls::AwbEnable) : std::nullopt;
    const auto awb_mode =
        request_metadata ? request_metadata->get(libcamera::controls::AwbMode) : std::nullopt;
    const auto awb_locked =
        request_metadata ? request_metadata->get(libcamera::controls::AwbLocked) : std::nullopt;
    const auto colour_gains =
        request_metadata ? request_metadata->get(libcamera::controls::ColourGains) : std::nullopt;
    const auto colour_temperature =
        request_metadata ? request_metadata->get(libcamera::controls::ColourTemperature)
                         : std::nullopt;
    const auto sensor_black_levels =
        request_metadata ? request_metadata->get(libcamera::controls::SensorBlackLevels)
                         : std::nullopt;
    const auto focus_fom =
        request_metadata ? request_metadata->get(libcamera::controls::FocusFoM) : std::nullopt;
    const auto frame_duration_us =
        request_metadata ? request_metadata->get(libcamera::controls::FrameDuration) : std::nullopt;
    const auto frame_duration_limits =
        request_metadata ? request_metadata->get(libcamera::controls::FrameDurationLimits)
                         : std::nullopt;
    const auto sensor_timestamp_ns =
        request_metadata ? request_metadata->get(libcamera::controls::SensorTimestamp)
                         : std::nullopt;
    const auto sensor_temperature =
        request_metadata ? request_metadata->get(libcamera::controls::SensorTemperature)
                         : std::nullopt;
    const auto af_mode =
        request_metadata ? request_metadata->get(libcamera::controls::AfMode) : std::nullopt;
    const auto af_range =
        request_metadata ? request_metadata->get(libcamera::controls::AfRange) : std::nullopt;
    const auto af_speed =
        request_metadata ? request_metadata->get(libcamera::controls::AfSpeed) : std::nullopt;
    const auto af_state =
        request_metadata ? request_metadata->get(libcamera::controls::AfState) : std::nullopt;
    const auto af_pause_state =
        request_metadata ? request_metadata->get(libcamera::controls::AfPauseState) : std::nullopt;
    const auto lens_position =
        request_metadata ? request_metadata->get(libcamera::controls::LensPosition) : std::nullopt;
    const auto hdr_mode =
        request_metadata ? request_metadata->get(libcamera::controls::HdrMode) : std::nullopt;
    const auto hdr_channel =
        request_metadata ? request_metadata->get(libcamera::controls::HdrChannel) : std::nullopt;
    const auto gamma =
        request_metadata ? request_metadata->get(libcamera::controls::Gamma) : std::nullopt;
    const auto frame_wall_clock = request_metadata
                                      ? request_metadata->get(libcamera::controls::FrameWallClock)
                                      : std::nullopt;
    const auto wdr_mode =
        request_metadata ? request_metadata->get(libcamera::controls::WdrMode) : std::nullopt;
    const auto wdr_strength =
        request_metadata ? request_metadata->get(libcamera::controls::WdrStrength) : std::nullopt;
    const auto wdr_max_bright_pixels =
        request_metadata ? request_metadata->get(libcamera::controls::WdrMaxBrightPixels)
                         : std::nullopt;
    const auto lens_dewarp_enable =
        request_metadata ? request_metadata->get(libcamera::controls::LensDewarpEnable)
                         : std::nullopt;
    const auto lens_shading_correction_enable =
        request_metadata ? request_metadata->get(libcamera::controls::LensShadingCorrectionEnable)
                         : std::nullopt;
    const auto awb_state = request_metadata
                               ? request_metadata->get(libcamera::controls::draft::AwbState)
                               : std::nullopt;
    const auto sensor_rolling_shutter_skew =
        request_metadata
            ? request_metadata->get(libcamera::controls::draft::SensorRollingShutterSkew)
            : std::nullopt;
    const auto lens_shading_map_mode =
        request_metadata ? request_metadata->get(libcamera::controls::draft::LensShadingMapMode)
                         : std::nullopt;
    const auto pipeline_depth =
        request_metadata ? request_metadata->get(libcamera::controls::draft::PipelineDepth)
                         : std::nullopt;
    const auto max_latency = request_metadata
                                 ? request_metadata->get(libcamera::controls::draft::MaxLatency)
                                 : std::nullopt;
    const auto noise_reduction_mode =
        request_metadata ? request_metadata->get(libcamera::controls::draft::NoiseReductionMode)
                         : std::nullopt;
    const auto color_correction_aberration_mode =
        request_metadata
            ? request_metadata->get(libcamera::controls::draft::ColorCorrectionAberrationMode)
            : std::nullopt;
    const auto scaler_crop =
        request_metadata ? request_metadata->get(libcamera::controls::ScalerCrop) : std::nullopt;

    if (exposure_time_us) {
      metadata.exposure_time_us = *exposure_time_us;
    }
    if (exposure_value) {
      metadata.exposure_bias_value = static_cast<int32_t>(std::lround(*exposure_value * 100.0f));
    }
    if (ae_metering_mode) {
      metadata.metering_mode = exif_metering_mode(*ae_metering_mode);
    }
    if (awb_mode) {
      metadata.light_source = exif_light_source(*awb_mode);
    }

    if (analogue_gain) {
      const float base_iso     = has_sensor_sensitivity ? sensor_sensitivity : 100.0f;
      const float gain         = *analogue_gain * digital_gain.value_or(1.0f);
      const auto estimated_iso = static_cast<int>(std::lround(std::max(1.0f, base_iso * gain)));
      metadata.iso_speed       = static_cast<uint16_t>(clamp_int(estimated_iso, 1, 65535));
    }

    std::ostringstream json;
    json << '{';
    append_json_string(json, "backend", "libcamera");
    append_json_string(json, "sensor_model", camera_model);
    append_json_string(json, "module_sensor", "IMX219_PLCC");
    append_json_string(json, "module_pixels", "8M");
    append_json_string(json, "lens_type", "1/4 inch");
    append_json_value(json, "lens_focal_length_mm", 2.85f);
    append_json_value(json, "lens_focal_length_tolerance_percent", 5.0f);
    append_json_value(json, "lens_f_number", 2.0f);
    append_json_value(json, "lens_f_number_tolerance_percent", 5.0f);
    append_json_value(json, "lens_field_of_view_degrees", 76.9f);
    append_json_value(json, "lens_distortion_max_percent", 1.5f);
    append_json_value(json, "depth_of_field_near_cm", 10);
    append_json_string(json, "depth_of_field_far", "infinity");
    append_json_value(json, "calibration_distance_cm", 80);
    append_json_bool(json, "focus_lens_position_supported", focus_capability.lens_position);
    append_json_bool(json, "focus_af_mode_supported", focus_capability.af_mode);
    append_json_bool(json, "focus_af_trigger_supported", focus_capability.af_trigger);
    append_json_bool(json, "focus_af_state_supported", focus_capability.af_state);
    append_json_bool(json, "focus_fom_supported", focus_capability.focus_fom);
    append_json_string(json, "focus_lens_position_min", focus_capability.lens_position_min);
    append_json_string(json, "focus_lens_position_max", focus_capability.lens_position_max);
    append_json_string(json, "focus_lens_position_default", focus_capability.lens_position_default);
    append_json_value(json, "exposure_time_us", exposure_time_us);
    append_json_value(json, "exposure_time_mode", exposure_time_mode);
    append_json_value(json, "exposure_value", exposure_value);
    append_json_value(json, "analogue_gain", analogue_gain);
    append_json_value(json, "analogue_gain_mode", analogue_gain_mode);
    append_json_value(json, "digital_gain", digital_gain);
    append_json_value(json, "estimated_iso", metadata.iso_speed);
    append_json_bool(json, "ae_enable", ae_enable);
    append_json_value(json, "ae_state", ae_state);
    append_json_value(json, "ae_metering_mode", ae_metering_mode);
    append_json_value(json, "ae_constraint_mode", ae_constraint_mode);
    append_json_value(json, "ae_exposure_mode", ae_exposure_mode);
    append_json_value(json, "ae_flicker_mode", ae_flicker_mode);
    append_json_value(json, "ae_flicker_period_us", ae_flicker_period);
    append_json_value(json, "ae_flicker_detected", ae_flicker_detected);
    append_json_value(json, "lux", lux);
    append_json_value(json, "brightness", brightness);
    append_json_value(json, "contrast", contrast);
    append_json_value(json, "saturation", saturation);
    append_json_value(json, "sharpness", sharpness);
    append_json_bool(json, "awb_enable", awb_enable);
    append_json_value(json, "awb_mode", awb_mode);
    append_json_bool(json, "awb_locked", awb_locked);
    if (colour_gains) {
      append_json_array(json, "colour_gains", *colour_gains);
    }
    append_json_value(json, "colour_temperature", colour_temperature);
    if (sensor_black_levels) {
      append_json_array(json, "sensor_black_levels", *sensor_black_levels);
    }
    append_json_value(json, "focus_fom", focus_fom);
    append_json_value(json, "frame_duration_us", frame_duration_us);
    if (frame_duration_limits) {
      append_json_array(json, "frame_duration_limits_us", *frame_duration_limits);
    }
    append_json_value(json, "sensor_timestamp_ns", sensor_timestamp_ns);
    append_json_value(json, "sensor_temperature_c", sensor_temperature);
    append_json_value(json, "af_mode", af_mode);
    append_json_value(json, "af_range", af_range);
    append_json_value(json, "af_speed", af_speed);
    append_json_value(json, "af_state", af_state);
    append_json_value(json, "af_pause_state", af_pause_state);
    append_json_value(json, "lens_position", lens_position);
    append_json_value(json, "hdr_mode", hdr_mode);
    append_json_value(json, "hdr_channel", hdr_channel);
    append_json_value(json, "gamma", gamma);
    append_json_value(json, "frame_wall_clock", frame_wall_clock);
    append_json_value(json, "wdr_mode", wdr_mode);
    append_json_value(json, "wdr_strength", wdr_strength);
    append_json_value(json, "wdr_max_bright_pixels", wdr_max_bright_pixels);
    append_json_bool(json, "lens_dewarp_enable", lens_dewarp_enable);
    append_json_bool(json, "lens_shading_correction_enable", lens_shading_correction_enable);
    append_json_value(json, "awb_state", awb_state);
    append_json_value(json, "sensor_rolling_shutter_skew_ns", sensor_rolling_shutter_skew);
    append_json_value(json, "lens_shading_map_mode", lens_shading_map_mode);
    append_json_value(json, "pipeline_depth", pipeline_depth);
    append_json_value(json, "max_latency", max_latency);
    append_json_value(json, "noise_reduction_mode", noise_reduction_mode);
    append_json_value(json, "color_correction_aberration_mode", color_correction_aberration_mode);
    if (auto pixel_array_size = camera->properties().get(libcamera::properties::PixelArraySize)) {
      append_json_size(json, "pixel_array_size", *pixel_array_size);
    }
    if (auto unit_cell_size = camera->properties().get(libcamera::properties::UnitCellSize)) {
      append_json_size(json, "unit_cell_size", *unit_cell_size);
    }
    if (auto sensor_sensitivity_value =
            camera->properties().get(libcamera::properties::SensorSensitivity)) {
      append_json_value(json, "sensor_sensitivity", sensor_sensitivity_value);
    }
    if (auto location = camera->properties().get(libcamera::properties::Location)) {
      append_json_value(json, "camera_location", location);
    }
    if (auto rotation = camera->properties().get(libcamera::properties::Rotation)) {
      append_json_value(json, "camera_rotation", rotation);
    }
    if (auto crop_max = camera->properties().get(libcamera::properties::ScalerCropMaximum)) {
      append_json_rectangle(json, "scaler_crop_maximum", *crop_max);
    }
    if (auto active_areas =
            camera->properties().get(libcamera::properties::PixelArrayActiveAreas)) {
      append_json_rectangles(json, "pixel_array_active_areas", *active_areas);
    }
    if (auto optical_black =
            camera->properties().get(libcamera::properties::PixelArrayOpticalBlackRectangles)) {
      append_json_rectangles(json, "pixel_array_optical_black_rectangles", *optical_black);
    }
    if (scaler_crop) {
      append_json_rectangle(json, "scaler_crop", *scaler_crop);
    }
    json << '}';
    metadata.user_comment = json.str();
    return metadata;
  }

  CaptureResult consume_capture_result() {
    std::lock_guard<std::mutex> lock(mutex);
    CaptureResult result;
    result.state                = capture_state;
    result.path                 = last_capture_path;
    result.requested_resolution = capture_requested_resolution;
    result.saved_resolution     = capture_saved_resolution;
    if (capture_state == CaptureState::Saved || capture_state == CaptureState::Failed) {
      capture_state = CaptureState::Idle;
    }
    return result;
  }

  struct StillFrameDecision : StillFrameStabilityDecision {
    int ae_state{-1};
    int awb_state{-1};
    float red_gain{-1.0f};
    float blue_gain{-1.0f};
  };

  StillFrameDecision evaluate_still_frame(const libcamera::ControlList& metadata) {
    StillFrameDecision decision;
    const auto ae_state     = metadata.get(libcamera::controls::AeState);
    const auto awb_state    = metadata.get(libcamera::controls::draft::AwbState);
    const auto colour_gains = metadata.get(libcamera::controls::ColourGains);

    StillFrameStabilitySample sample;
    decision.ae_state          = ae_state.value_or(-1);
    decision.awb_state         = awb_state.value_or(-1);
    sample.ae_state_available  = ae_state.has_value();
    sample.ae_converged        = decision.ae_state == libcamera::controls::AeStateConverged;
    sample.awb_state_available = awb_state.has_value();
    sample.awb_converged       = decision.awb_state == libcamera::controls::draft::AwbConverged ||
                                 decision.awb_state == libcamera::controls::draft::AwbLocked;
    if (colour_gains) {
      decision.red_gain             = (*colour_gains)[0];
      decision.blue_gain            = (*colour_gains)[1];
      sample.colour_gains_available = true;
      sample.red_gain               = decision.red_gain;
      sample.blue_gain              = decision.blue_gain;
    }

    std::lock_guard<std::mutex> lock(capture_mutex);
    static_cast<StillFrameStabilityDecision&>(decision) = still_stability.evaluate(sample);
    return decision;
  }

  void finish_still_frame(bool frame_ok) {
    {
      std::lock_guard<std::mutex> lock(capture_mutex);
      capture_frame_ok   = frame_ok;
      capture_frame_done = true;
    }
    capture_cv.notify_one();
  }

  bool requeue_still_request(libcamera::Request* request,
                             libcamera::FrameBuffer* still_buffer,
                             libcamera::FrameBuffer* raw_buffer) {
    if (!request || !still_buffer || !raw_buffer) {
      return false;
    }
    request->reuse();
    if (request->addBuffer(still_stream, still_buffer) < 0 ||
        request->addBuffer(raw_stream, raw_buffer) < 0) {
      LOG_ERROR("Failed to re-add still/raw buffers while waiting for 3A");
      return false;
    }
    if (!camera || !streaming.load() || stream_mode.load() != StreamMode::Still ||
        camera->queueRequest(request) < 0) {
      LOG_ERROR("Failed to requeue still request while waiting for 3A");
      return false;
    }
    return true;
  }

  void request_complete(libcamera::Request* request) {
    if (!request || request->status() == libcamera::Request::RequestCancelled) {
      return;
    }

    if (!streaming.load()) {
      return;
    }

    const StreamMode mode = stream_mode.load();
    if (mode == StreamMode::Still) {
      libcamera::FrameBuffer* still_buffer =
          still_stream ? request->findBuffer(still_stream) : nullptr;
      libcamera::FrameBuffer* raw_buffer = raw_stream ? request->findBuffer(raw_stream) : nullptr;
      if (!still_buffer || !raw_buffer) {
        finish_still_frame(false);
        return;
      }
      if (still_buffer->metadata().status != libcamera::FrameMetadata::FrameSuccess ||
          raw_buffer->metadata().status != libcamera::FrameMetadata::FrameSuccess) {
        LOG_INFO("Ignoring non-success still frame: still_status={} raw_status={}",
                 static_cast<int>(still_buffer->metadata().status),
                 static_cast<int>(raw_buffer->metadata().status));
        if (!requeue_still_request(request, still_buffer, raw_buffer)) {
          finish_still_frame(false);
        }
        return;
      }

      const StillFrameDecision decision = evaluate_still_frame(request->metadata());
      LOG_INFO(
          "Still 3A settle: frame={} ae_state={} awb_state={} gains=[{:.4f},{:.4f}] "
          "gain_delta={:.4f} stable_gain_frames={} capture={} forced={}",
          decision.frame,
          decision.ae_state,
          decision.awb_state,
          decision.red_gain,
          decision.blue_gain,
          decision.gain_delta,
          decision.stable_gain_frames,
          decision.capture,
          decision.forced);
      if (!decision.capture) {
        if (!requeue_still_request(request, still_buffer, raw_buffer)) {
          finish_still_frame(false);
        }
        return;
      }

      const bool frame_ok = still_buffer && process_completed_stream_buffer(request,
                                                                            still_stream,
                                                                            still_w,
                                                                            still_h,
                                                                            still_stride,
                                                                            still_format,
                                                                            true);
      finish_still_frame(frame_ok);
      return;
    }

    if (mode != StreamMode::Preview || !preview_stream) {
      return;
    }

    libcamera::FrameBuffer* preview_buffer = request->findBuffer(preview_stream);
    if (preview_buffer) {
      process_completed_stream_buffer(request,
                                      preview_stream,
                                      preview_w,
                                      preview_h,
                                      preview_stride,
                                      preview_format,
                                      false);
    }

    request->reuse();
    if (preview_buffer && request->addBuffer(preview_stream, preview_buffer) < 0) {
      LOG_WARN("Failed to re-add preview buffer");
      return;
    }
    apply_preview_request_controls(request);

    if (camera && streaming.load() && stream_mode.load() == StreamMode::Preview &&
        camera->queueRequest(request) < 0) {
      LOG_ERROR("Failed to requeue camera request");
    }
  }

  bool process_completed_stream_buffer(libcamera::Request* request,
                                       libcamera::Stream* completed_stream,
                                       int width,
                                       int height,
                                       int stride,
                                       const libcamera::PixelFormat& format,
                                       bool is_still) {
    if (!request || !completed_stream) {
      return false;
    }

    auto buffer_it = request->buffers().find(completed_stream);
    if (buffer_it == request->buffers().end()) {
      return false;
    }
    libcamera::FrameBuffer* buffer = buffer_it->second;
    auto map_it                    = mapped_buffers.find(buffer);
    if (map_it == mapped_buffers.end()) {
      return false;
    }

    const auto& mapped = map_it->second;
    std::vector<const uint8_t*> plane_data;
    std::vector<size_t> bytes_used;
    plane_data.reserve(mapped.planes.size());
    bytes_used.reserve(mapped.planes.size());
    for (const auto& plane : mapped.planes) {
      plane_data.push_back(static_cast<const uint8_t*>(plane.addr) + plane.data_offset);
      bytes_used.push_back(plane.data_size);
    }

    const auto& metadata = buffer->metadata();
    if (!metadata.planes().empty()) {
      const size_t plane_count = std::min(metadata.planes().size(), bytes_used.size());
      for (size_t i = 0; i < plane_count; ++i) {
        if (metadata.planes()[i].bytesused > 0) {
          bytes_used[i] =
              std::min(bytes_used[i], static_cast<size_t>(metadata.planes()[i].bytesused));
        }
      }
    }

    DmaBufReadGuard read_guard(mapped);
    if (!read_guard.is_valid()) {
      return false;
    }

    if (is_still) {
      if (format != libcamera::formats::YUV420 || !pipeline_rotation || plane_data.size() != 1) {
        LOG_ERROR("Still frame is not contiguous oriented YUV420: format={} planes={} rotated={}",
                  format.toString(),
                  plane_data.size(),
                  pipeline_rotation);
        return false;
      }
      const size_t yuv_size = static_cast<size_t>(stride) * height * 3 / 2;
      if (stride < width || bytes_used[0] < yuv_size) {
        LOG_ERROR("Still YUV420 buffer is too small: bytes={} required={} stride={}",
                  bytes_used[0],
                  yuv_size,
                  stride);
        return false;
      }

      std::string capture_path;
      CameraResolution requested_resolution;
      {
        std::lock_guard<std::mutex> lock(mutex);
        capture_path = last_capture_path;
      }
      {
        std::lock_guard<std::mutex> lock(capture_mutex);
        requested_resolution = capture_output_resolution;
      }
      StillEncodeJob job;
      job.yuv420.assign(plane_data[0], plane_data[0] + yuv_size);
      job.width                = width;
      job.height               = height;
      job.stride               = stride;
      job.path                 = std::move(capture_path);
      job.requested_resolution = requested_resolution;
      job.metadata             = build_still_exif_metadata(request, width, height);
      if (!enqueue_still_job(std::move(job))) {
        std::lock_guard<std::mutex> lock(mutex);
        capture_state = CaptureState::Failed;
        LOG_WARN("Still encode queue is full or worker is stopped");
        return false;
      }
      return true;
    }

    CameraFrame converted_frame;
    {
      ++preview_input_frames;
      converted_frame.width  = width;
      converted_frame.height = height;
      converted_frame.rgb565 = preview_pool.acquire(static_cast<size_t>(width) * height);
      if (!converted_frame.rgb565) {
        ++preview_dropped_frames;
        return false;
      }
    }
    const auto convert_started = std::chrono::steady_clock::now();
    const bool converted       = convert_frame_to_outputs(plane_data,
                                                          bytes_used,
                                                          width,
                                                          height,
                                                          stride,
                                                          map_libcamera_format(format),
                                                          false,
                                                          &converted_frame,
                                                          nullptr,
                                                          !pipeline_rotation);
    preview_convert_us +=
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                  std::chrono::steady_clock::now() - convert_started)
                                  .count());
    if (converted) {
      bool video_failed = false;
      {
        std::lock_guard<std::mutex> video_lock(video_mutex);
        if (video_writer.is_open() &&
            !video_writer.write_rgb565_frame(converted_frame, video_quality)) {
          video_failed = true;
          (void)video_writer.close();
        }
      }
      std::lock_guard<std::mutex> lock(mutex);
      pending_frame            = std::move(converted_frame);
      new_frame                = true;
      const uint64_t published = ++preview_published_frames;
      if (video_failed) {
        video_state = VideoState::Failed;
      }
      if (published % 300 == 0) {
        const uint64_t input = preview_input_frames.load();
        LOG_INFO("Camera preview stats: input={} published={} dropped={} avg_convert_us={}",
                 input,
                 published,
                 preview_dropped_frames.load(),
                 input ? preview_convert_us.load() / input : 0);
      }
    }
    return converted;
  }
#endif
};

LibcameraBackend::LibcameraBackend()
    : impl_(std::make_unique<Impl>()) {}

LibcameraBackend::~LibcameraBackend() { close(); }

bool LibcameraBackend::open() {
#if USE_DESKTOP
  return false;
#else
  return impl_->open();
#endif
}

void LibcameraBackend::close() {
#if !USE_DESKTOP
  if (impl_) {
    impl_->close();
  }
#endif
}

bool LibcameraBackend::consume_frame(CameraFrame& frame) {
#if USE_DESKTOP
  (void)frame;
  return false;
#else
  return impl_->consume_frame(frame);
#endif
}

bool LibcameraBackend::request_capture() {
#if USE_DESKTOP
  return false;
#else
  return impl_->request_capture();
#endif
}

bool LibcameraBackend::start_video_recording(int fps, int quality) {
#if USE_DESKTOP
  (void)fps;
  (void)quality;
  return false;
#else
  return impl_->start_video_recording(fps, quality);
#endif
}

bool LibcameraBackend::stop_video_recording() {
#if USE_DESKTOP
  return false;
#else
  return impl_->stop_video_recording();
#endif
}

void LibcameraBackend::set_capture_resolution(CameraResolution resolution) {
#if !USE_DESKTOP
  impl_->set_capture_resolution(resolution);
#else
  (void)resolution;
#endif
}

void LibcameraBackend::set_zoom_state(CameraZoomState state) {
#if !USE_DESKTOP
  impl_->set_zoom_state(state);
#else
  (void)state;
#endif
}

CaptureResult LibcameraBackend::consume_capture_result() {
#if USE_DESKTOP
  return {};
#else
  return impl_->consume_capture_result();
#endif
}

VideoState LibcameraBackend::consume_video_state(std::string* path) {
#if USE_DESKTOP
  if (path) {
    path->clear();
  }
  return VideoState::Idle;
#else
  return impl_->consume_video_state(path);
#endif
}

std::string LibcameraBackend::last_capture_path() const {
#if USE_DESKTOP
  return {};
#else
  return impl_->last_capture_path;
#endif
}

std::string LibcameraBackend::last_video_path() const {
#if USE_DESKTOP
  return {};
#else
  return impl_->last_video_path_value;
#endif
}

std::string LibcameraBackend::last_error() const {
#if USE_DESKTOP
  return "libcamera backend unavailable in desktop build";
#else
  return impl_->last_error;
#endif
}

}  // namespace service
