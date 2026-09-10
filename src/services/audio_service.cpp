/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "services/audio_service.h"

#include <atomic>
#include <memory>
#include <string>

#define MINIAUDIO_IMPLEMENTATION
#ifdef APP_MINIAUDIO_HEADER
#include APP_MINIAUDIO_HEADER
#else
#include <miniaudio.h>
#endif

#include "utils/asset_manager.h"
#include "utils/logger.h"

namespace service {
namespace {

constexpr const char* kShutterAsset    = "audio/shutter.wav";
constexpr const char* kPressAsset      = "audio/click.wav";
constexpr unsigned int kRecordRate     = 48000;
constexpr unsigned int kRecordChannels = 1;
constexpr float kDefaultPlaybackVolume = 0.5F;

struct SelectedDevice {
  ma_device_id id{};
  std::string name;
  bool found{false};
};

SelectedDevice select_device(ma_device_info* devices,
                             ma_uint32 device_count,
                             const char* direction) {
  SelectedDevice selected;
  if (!devices || device_count == 0) {
    LOG_WARN("No miniaudio {} device found", direction);
    return selected;
  }

  ma_uint32 selected_index = 0;
  for (ma_uint32 index = 0; index < device_count; ++index) {
    if (devices[index].isDefault) {
      selected_index = index;
      break;
    }
  }

  selected.id    = devices[selected_index].id;
  selected.name  = devices[selected_index].name;
  selected.found = true;
  LOG_INFO("Selected miniaudio {} device: name='{}' default={}",
           direction,
           selected.name,
           devices[selected_index].isDefault == MA_TRUE);
  return selected;
}

}  // namespace

struct AudioService::Impl {
  ma_context context{};
  ma_engine engine{};
  ma_device capture_device{};
  ma_encoder capture_encoder{};
  SelectedDevice playback_device;
  SelectedDevice capture_device_info;
  bool context_initialized{false};
  bool engine_initialized{false};
  bool capture_device_initialized{false};
  bool capture_encoder_initialized{false};
  std::atomic<bool> recording{false};
  std::atomic<bool> record_ok{true};

  ~Impl() { shutdown(); }

  bool initialize_context() {
    if (context_initialized) {
      return playback_device.found;
    }

    ma_result result;
#if defined(__linux__)
    const ma_backend backends[] = {ma_backend_pulseaudio};
    result                      = ma_context_init(backends, 1, nullptr, &context);
#else
    result = ma_context_init(nullptr, 0, nullptr, &context);
#endif
    if (result != MA_SUCCESS) {
      LOG_ERROR("Miniaudio context initialization failed: {}", ma_result_description(result));
      return false;
    }
    context_initialized = true;

    ma_device_info* playback_devices = nullptr;
    ma_device_info* capture_devices  = nullptr;
    ma_uint32 playback_count         = 0;
    ma_uint32 capture_count          = 0;
    result                           = ma_context_get_devices(&context,
                                                              &playback_devices,
                                                              &playback_count,
                                                              &capture_devices,
                                                              &capture_count);
    if (result != MA_SUCCESS) {
      LOG_ERROR("Miniaudio device enumeration failed: {}", ma_result_description(result));
      ma_context_uninit(&context);
      context_initialized = false;
      return false;
    }

    playback_device     = select_device(playback_devices, playback_count, "playback");
    capture_device_info = select_device(capture_devices, capture_count, "capture");
    LOG_INFO("Using miniaudio backend: {} playback={} capture={}",
             ma_get_backend_name(context.backend),
             playback_device.found ? playback_device.name : "unavailable",
             capture_device_info.found ? capture_device_info.name : "unavailable");
    return playback_device.found;
  }

  bool start() {
    if (engine_initialized) {
      return true;
    }
    if (!initialize_context()) {
      return false;
    }

    ma_engine_config config  = ma_engine_config_init();
    config.pContext          = &context;
    config.pPlaybackDeviceID = &playback_device.id;
    ma_result result         = ma_engine_init(&config, &engine);
    if (result != MA_SUCCESS) {
      LOG_ERROR("Miniaudio engine initialization failed: {}", ma_result_description(result));
      return false;
    }
    engine_initialized = true;

    result = ma_engine_set_volume(&engine, kDefaultPlaybackVolume);
    if (result != MA_SUCCESS) {
      LOG_ERROR("Miniaudio playback volume setup failed: {}", ma_result_description(result));
      ma_engine_uninit(&engine);
      engine_initialized = false;
      return false;
    }

    LOG_INFO("Miniaudio engine initialized: backend={} device='{}' volume={:.2f}",
             ma_get_backend_name(context.backend),
             playback_device.name,
             kDefaultPlaybackVolume);
    return true;
  }

  void shutdown() {
    stop_recording();
    if (engine_initialized) {
      ma_engine_uninit(&engine);
      engine_initialized = false;
    }
    if (context_initialized) {
      ma_context_uninit(&context);
      context_initialized = false;
    }
    playback_device     = {};
    capture_device_info = {};
  }

  bool play_asset(const char* asset_name) {
    if (!start()) {
      return false;
    }

    std::string path;
    if (!asset::AssetManager::resolve_path(asset_name, path)) {
      LOG_WARN("Audio asset not found: {}", asset_name);
      return false;
    }

    const ma_result result = ma_engine_play_sound(&engine, path.c_str(), nullptr);
    if (result != MA_SUCCESS) {
      LOG_WARN("Miniaudio playback failed for {}: {}", path, ma_result_description(result));
      return false;
    }

    LOG_VERBOSE("Miniaudio playback started: {}", path);
    return true;
  }

  bool play_shutter() { return play_asset(kShutterAsset); }

  bool play_click() { return play_asset(kPressAsset); }

  static void capture_callback(ma_device* device,
                               void* output,
                               const void* input,
                               ma_uint32 frame_count) {
    auto* impl = static_cast<Impl*>(device->pUserData);
    if (!impl || !input || !impl->capture_encoder_initialized) {
      return;
    }

    ma_uint64 frames_written = 0;
    const ma_result result =
        ma_encoder_write_pcm_frames(&impl->capture_encoder, input, frame_count, &frames_written);
    if (result != MA_SUCCESS || frames_written != frame_count) {
      impl->record_ok = false;
    }
    (void)output;
  }

  bool start_recording(const std::string& path) {
    if (path.empty()) {
      return false;
    }
    if (recording.load()) {
      return true;
    }
    if (!start()) {
      return false;
    }
    if (!capture_device_info.found) {
      LOG_ERROR("Miniaudio capture device is unavailable");
      record_ok = false;
      return false;
    }

    record_ok = true;
    const ma_encoder_config encoder_config =
        ma_encoder_config_init(ma_encoding_format_wav, ma_format_s16, kRecordChannels, kRecordRate);
    ma_result result = ma_encoder_init_file(path.c_str(), &encoder_config, &capture_encoder);
    if (result != MA_SUCCESS) {
      LOG_ERROR("Miniaudio encoder initialization failed for {}: {}",
                path,
                ma_result_description(result));
      record_ok = false;
      return false;
    }
    capture_encoder_initialized = true;

    ma_device_config device_config  = ma_device_config_init(ma_device_type_capture);
    device_config.capture.pDeviceID = &capture_device_info.id;
    device_config.capture.format    = ma_format_s16;
    device_config.capture.channels  = kRecordChannels;
    device_config.sampleRate        = kRecordRate;
    device_config.dataCallback      = capture_callback;
    device_config.pUserData         = this;

    LOG_INFO("Initializing miniaudio capture: backend={} device='{}' rate={} channels={}",
             ma_get_backend_name(context.backend),
             capture_device_info.name,
             kRecordRate,
             kRecordChannels);
    result = ma_device_init(&context, &device_config, &capture_device);
    if (result != MA_SUCCESS) {
      LOG_ERROR("Miniaudio capture initialization failed: {}", ma_result_description(result));
      ma_encoder_uninit(&capture_encoder);
      capture_encoder_initialized = false;
      record_ok                   = false;
      return false;
    }
    capture_device_initialized = true;

    result = ma_device_start(&capture_device);
    if (result != MA_SUCCESS) {
      LOG_ERROR("Miniaudio capture start failed: {}", ma_result_description(result));
      ma_device_uninit(&capture_device);
      capture_device_initialized = false;
      ma_encoder_uninit(&capture_encoder);
      capture_encoder_initialized = false;
      record_ok                   = false;
      return false;
    }

    recording = true;
    LOG_INFO("Miniaudio recording started: {}", path);
    return true;
  }

  bool stop_recording() {
    if (capture_device_initialized) {
      ma_device_stop(&capture_device);
      ma_device_uninit(&capture_device);
      capture_device_initialized = false;
    }
    if (capture_encoder_initialized) {
      ma_encoder_uninit(&capture_encoder);
      capture_encoder_initialized = false;
    }

    const bool was_recording = recording.exchange(false);
    if (was_recording) {
      LOG_INFO("Miniaudio recording stopped");
    }
    return record_ok.load();
  }
};

AudioService::AudioService() = default;

AudioService::~AudioService() { stop(); }

void AudioService::ensure_impl_() {
  if (!impl_) {
    impl_ = std::make_unique<Impl>();
  }
}

void AudioService::start() {
  ensure_impl_();
  if (state_ == AudioServiceState::Recording) {
    return;
  }

  if (!impl_->start()) {
    state_          = AudioServiceState::Error;
    status_message_ = "Miniaudio initialization failed";
    return;
  }

  state_          = AudioServiceState::Ready;
  status_message_ = "Audio ready";
  LOG_INFO("Audio service ready");
}

void AudioService::stop() {
  if (impl_) {
    impl_->shutdown();
  }

  state_          = AudioServiceState::Idle;
  status_message_ = "Audio idle";
  recording_path_.clear();
  LOG_INFO("Audio service stopped");
}

void AudioService::update(uint32_t /*delta_ms*/) {}

bool AudioService::play_shutter() {
  ensure_impl_();
  if (state_ == AudioServiceState::Idle) {
    start();
  }
  if (state_ != AudioServiceState::Ready && state_ != AudioServiceState::Recording) {
    return false;
  }

  const bool ok   = impl_->play_shutter();
  status_message_ = ok ? "Shutter sound played" : "Shutter sound unavailable";
  return ok;
}

bool AudioService::play_click() {
  ensure_impl_();
  if (state_ == AudioServiceState::Idle) {
    start();
  }
  if (state_ != AudioServiceState::Ready && state_ != AudioServiceState::Recording) {
    return false;
  }

  const bool ok   = impl_->play_click();
  status_message_ = ok ? "Click sound played" : "Click sound unavailable";
  return ok;
}

bool AudioService::start_recording(const std::string& path) {
  ensure_impl_();
  if (state_ == AudioServiceState::Idle) {
    start();
  }
  if (state_ != AudioServiceState::Ready && state_ != AudioServiceState::Recording) {
    return false;
  }

  if (state_ == AudioServiceState::Recording) {
    return true;
  }

  if (!impl_->start_recording(path)) {
    state_          = AudioServiceState::Error;
    status_message_ = "Audio recording failed";
    return false;
  }

  recording_path_ = path;
  state_          = AudioServiceState::Recording;
  status_message_ = "Audio recording";
  return true;
}

bool AudioService::stop_recording() {
  if (state_ != AudioServiceState::Recording) {
    return true;
  }

  ensure_impl_();
  const bool ok   = impl_->stop_recording();
  state_          = ok ? AudioServiceState::Ready : AudioServiceState::Error;
  status_message_ = ok ? "Audio ready" : "Audio stop failed";
  return ok;
}

}  // namespace service
