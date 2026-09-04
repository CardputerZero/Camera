#include "input/linux_keypad.h"

#include <cstdlib>

#if defined(CAMERA_APP_SCONS_BUILD)
#include "camera_app_config.h"
#endif
#include "utils/logger.h"

#ifndef APP_KEY_INPUT_DEVICE
#define APP_KEY_INPUT_DEVICE ""
#endif

#if defined(__linux__)
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#endif

namespace input {

#if defined(__linux__)
namespace {

struct SymbolKeyMapping {
  uint16_t code;
  uint32_t symbol;
};

constexpr std::array<SymbolKeyMapping, 32> kSymbolKeyMappings{{
    {26, '!'},  {27, '@'},  {39, '#'},  {40, '$'},  {41, '%'},  {43, '^'},
    {51, '&'},  {52, '*'},  {53, '('},  {94, ')'},  {55, '~'},  {69, '`'},
    {70, '_'},  {71, '-'},  {72, '+'},  {73, '='},  {74, '['},  {75, ']'},
    {76, '{'},  {77, '}'},  {79, ';'},  {80, ':'},  {81, '\''}, {82, '"'},
    {83, '<'},  {85, '>'},  {86, '\\'}, {89, '|'},  {90, ','},  {91, '.'},
    {92, '/'},  {93, '?'},
}};

template <size_t N>
bool test_bit_(const std::array<unsigned long, N>& bits, unsigned int bit) {
  constexpr unsigned int kBitsPerWord = sizeof(unsigned long) * 8;
  const unsigned int index            = bit / kBitsPerWord;
  const unsigned int offset           = bit % kBitsPerWord;
  return index < bits.size() && ((bits[index] >> offset) & 1UL) != 0;
}

bool has_camera_keys_(int fd) {
  constexpr size_t kKeyBitsSize =
      (KEY_MAX + sizeof(unsigned long) * 8) / (sizeof(unsigned long) * 8);
  std::array<unsigned long, kKeyBitsSize> key_bits{};
  if (::ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits.data()) < 0) {
    return false;
  }

  return test_bit_(key_bits, KEY_ESC) || test_bit_(key_bits, KEY_ENTER) ||
         test_bit_(key_bits, KEY_KPENTER) || test_bit_(key_bits, KEY_F) ||
         test_bit_(key_bits, KEY_X) || test_bit_(key_bits, KEY_Z) || test_bit_(key_bits, KEY_C) ||
         test_bit_(key_bits, KEY_U) || test_bit_(key_bits, KEY_HELP) ||
         test_bit_(key_bits, KEY_1) || test_bit_(key_bits, KEY_4) || test_bit_(key_bits, KEY_5) ||
         test_bit_(key_bits, KEY_6) || test_bit_(key_bits, KEY_7) || test_bit_(key_bits, KEY_8);
}

bool env_enabled_(const char* name, bool fallback) {
  const char* value = std::getenv(name);
  if (!value || !value[0]) {
    return fallback;
  }

  return std::strcmp(value, "0") != 0 && std::strcmp(value, "false") != 0 &&
         std::strcmp(value, "False") != 0 && std::strcmp(value, "off") != 0 &&
         std::strcmp(value, "OFF") != 0;
}

}  // namespace
#endif

LinuxKeypad::~LinuxKeypad() { close(); }

bool LinuxKeypad::open_default() {
#if defined(__linux__)
  if (const char* device_path = std::getenv("CAMERA_APP_KEYBOARD_DEVICE")) {
    return open_device(device_path);
  }
  if (APP_KEY_INPUT_DEVICE[0]) {
    return open_device(APP_KEY_INPUT_DEVICE);
  }
  if (const char* device_path = std::getenv("APPLAUNCH_LINUX_KEYBOARD_DEVICE")) {
    return open_device(device_path);
  }
  if (const char* device_path = std::getenv("LV_LINUX_KEYBOARD_DEVICE")) {
    return open_device(device_path);
  }

  bool opened = false;
  for (int index = 0; index < 32; ++index) {
    const std::string path = "/dev/input/event" + std::to_string(index);
    opened                 = open_device(path) || opened;
  }

  if (!opened) {
    LOG_WARN(
        "No Linux keyboard input device opened. Set CAMERA_APP_KEYBOARD_DEVICE=/dev/input/eventX "
        "if needed.");
  }

  return opened;
#else
  LOG_INFO("Linux keypad input is unavailable on this platform");
  return false;
#endif
}

bool LinuxKeypad::open_device(const std::string& path) {
#if defined(__linux__)
  if (!ensure_indev_()) {
    return false;
  }

  const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) {
    return false;
  }

  if (!has_camera_keys_(fd)) {
    ::close(fd);
    return false;
  }

  const bool grab_input = env_enabled_("CAMERA_APP_KEYBOARD_GRAB", true);
  if (grab_input && ::ioctl(fd, EVIOCGRAB, 1) < 0) {
    LOG_WARN("Failed to grab Linux input device {} exclusively: {}", path, std::strerror(errno));
    ::close(fd);
    return false;
  }

  event_fds_.push_back(fd);
  LOG_DEBUG("{} Linux input device: {}", grab_input ? "Opened and grabbed" : "Opened shared", path);
  return true;
#else
  (void)path;
  return false;
#endif
}

void LinuxKeypad::close() {
#if defined(__linux__)
  for (int fd : event_fds_) {
    if (fd >= 0) {
      (void)::ioctl(fd, EVIOCGRAB, 0);
      ::close(fd);
    }
  }
#endif
  event_fds_.clear();

  if (indev_) {
    lv_indev_delete(indev_);
    indev_ = nullptr;
  }

  pending_keys_.clear();
}

void LinuxKeypad::poll() {
#if defined(__linux__)
  for (int fd : event_fds_) {
    input_event event{};
    while (true) {
      const ssize_t bytes_read = ::read(fd, &event, sizeof(event));
      if (bytes_read == sizeof(event)) {
        if (event.type == EV_KEY) {
          push_key_event_(event.code, event.value);
        }
        continue;
      }

      if (bytes_read < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        break;
      }

      if (bytes_read < 0) {
        LOG_WARN("Failed to read Linux input event: {}", std::strerror(errno));
      }
      break;
    }
  }
#endif
}

void LinuxKeypad::set_action_callback(ActionCallback callback) {
  action_callback_ = std::move(callback);
}

void LinuxKeypad::read_cb_(lv_indev_t* indev, lv_indev_data_t* data) {
  auto* keypad = static_cast<LinuxKeypad*>(lv_indev_get_user_data(indev));
  if (!keypad || !data) {
    return;
  }

  if (!keypad->pending_keys_.empty()) {
    const KeyEvent event = keypad->pending_keys_.front();
    keypad->pending_keys_.pop_front();
    keypad->last_key_      = event.key;
    data->key              = event.key;
    data->state            = event.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = !keypad->pending_keys_.empty();
    return;
  }

  data->key   = keypad->last_key_;
  data->state = LV_INDEV_STATE_RELEASED;
}

bool LinuxKeypad::ensure_indev_() {
  if (indev_) {
    return true;
  }

  indev_ = lv_indev_create();
  if (!indev_) {
    LOG_ERROR("Failed to create LVGL keypad input device");
    return false;
  }

  lv_indev_set_type(indev_, LV_INDEV_TYPE_KEYPAD);
  lv_indev_set_read_cb(indev_, read_cb_);
  lv_indev_set_user_data(indev_, this);
  return true;
}

void LinuxKeypad::push_key_event_(uint16_t code, int32_t value) {
  if (value != 0 && value != 1) {
    return;
  }

  const uint32_t key = translate_key_(code);
  if (key == 0) {
    return;
  }

  const bool pressed = value == 1;
  pending_keys_.push_back({key, pressed});

  if (!pressed || !action_callback_) {
    return;
  }

  if (key == LV_KEY_ESC) {
    action_callback_(app::AppAction::Exit);
  } else if (key == 'h' || key == 'H' || key == '1') {
    action_callback_(app::AppAction::ToggleHint);
  } else if (key == 'u' || key == 'U') {
    action_callback_(app::AppAction::ToggleCameraBackend);
  } else if (key == '4') {
    action_callback_(app::AppAction::ZoomOut);
  } else if (key == '5') {
    action_callback_(app::AppAction::ZoomIn);
  } else if (key == LV_KEY_DEL || key == LV_KEY_BACKSPACE) {
    action_callback_(app::AppAction::Delete);
  } else if (key == LV_KEY_ENTER) {
    action_callback_(app::AppAction::Confirm);
  } else if (key == '6') {
    action_callback_(app::AppAction::Capture);
  } else if (key == '7') {
    action_callback_(app::AppAction::OpenGallery);
  } else if (key == '8') {
    action_callback_(app::AppAction::ToggleCaptureMode);
  } else if (key == 'f' || key == 'F' || key == LV_KEY_UP) {
    action_callback_(app::AppAction::PanUp);
  } else if (key == 'x' || key == 'X' || key == LV_KEY_DOWN) {
    action_callback_(app::AppAction::PanDown);
  } else if (key == 'z' || key == 'Z' || key == LV_KEY_LEFT) {
    action_callback_(app::AppAction::PanLeft);
  } else if (key == 'c' || key == 'C' || key == LV_KEY_RIGHT) {
    action_callback_(app::AppAction::PanRight);
  }
}

uint32_t LinuxKeypad::translate_key_(uint16_t code) const {
#if defined(__linux__)
  for (const auto& mapping : kSymbolKeyMappings) {
    if (mapping.code == code) {
      return mapping.symbol;
    }
  }

  switch (code) {
    case KEY_ESC:
      return LV_KEY_ESC;
    case KEY_UP:
      return LV_KEY_UP;
    case KEY_DOWN:
      return LV_KEY_DOWN;
    case KEY_LEFT:
      return LV_KEY_LEFT;
    case KEY_RIGHT:
      return LV_KEY_RIGHT;
    case KEY_ENTER:
      return LV_KEY_ENTER;
    case KEY_KPENTER:
      return LV_KEY_ENTER;
    case KEY_BACKSPACE:
      return LV_KEY_BACKSPACE;
    case KEY_DELETE:
      return LV_KEY_DEL;
    case KEY_TAB:
      return LV_KEY_NEXT;
    case KEY_H:
      return 'h';
    case KEY_HELP:
      return 'h';
    case KEY_F:
      return 'f';
    case KEY_X:
      return 'x';
    case KEY_Z:
      return 'z';
    case KEY_C:
      return 'c';
    case KEY_U:
      return 'u';
    case KEY_1:
      return '1';
    case KEY_4:
      return '4';
    case KEY_5:
      return '5';
    case KEY_6:
      return '6';
    case KEY_7:
      return '7';
    case KEY_8:
      return '8';
    case KEY_F1:
      return '1';
    default:
      return 0;
  }
#else
  (void)code;
  return 0;
#endif
}

}  // namespace input
