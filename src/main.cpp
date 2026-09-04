
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#if defined(CAMERA_APP_SCONS_BUILD)
#include "camera_app_config.h"
#endif
#include "app/app_state_machine.h"
#include "input/linux_keypad.h"
#include "input/sdl_keypad.h"
#include "screens/screen.h"
#include "screens/screen_manager.h"
#include "services/app_services.h"
#include "services/preview_frame_limiter.h"
#include "utils/device_config.h"
#include "utils/logger.h"
#include "viewmodels/camera_viewmodel.h"
#include "viewmodels/gallery_viewmodel.h"
#include "viewmodels/splash_viewmodel.h"
#include "views/camera_view.h"
#include "views/gallery_view.h"
#include "views/help_popup.h"
#include "views/splash_view.h"

#if USE_DESKTOP
#if __has_include(<SDL.h>)
#include <SDL.h>
#elif __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#else
#error "SDL2 header not found"
#endif
#include <src/drivers/sdl/lv_sdl_mouse.h>
#include <src/drivers/sdl/lv_sdl_mousewheel.h>
#include <src/drivers/sdl/lv_sdl_window.h>
#else
#include <src/drivers/display/fb/lv_linux_fbdev.h>
#if APP_USE_DRM
#include <src/drivers/display/drm/lv_linux_drm.h>
#endif
#endif

namespace {

volatile sig_atomic_t g_exit_requested = 0;

void request_program_exit() { g_exit_requested = 1; }

void handle_program_signal(int) { request_program_exit(); }

uint32_t app_tick_ms() {
#if USE_DESKTOP
  return SDL_GetTicks();
#else
  static const auto start_time = std::chrono::steady_clock::now();
  const auto elapsed           = std::chrono::steady_clock::now() - start_time;
  return static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
#endif
}

void app_delay_ms(uint32_t ms) {
#if USE_DESKTOP
  SDL_Delay(ms);
#else
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#endif
}

#if !USE_DESKTOP
lv_display_t* setup_fb_display(const char* fb_device) {
  lv_display_t* display = lv_linux_fbdev_create();
  if (display == nullptr) {
    LOG_ERROR("Failed to create Linux framebuffer display");
    return nullptr;
  }

  LOG_DEBUG("Create Linux framebuffer display with {}", fb_device);
  if (lv_linux_fbdev_set_file(display, fb_device) != LV_RESULT_OK) {
    LOG_ERROR("Failed to set Linux framebuffer device: {}", fb_device);
    lv_display_delete(display);
    return nullptr;
  }

  return display;
}

#if APP_USE_DRM
lv_display_t* setup_drm_display(const char* drm_device) {
  lv_display_t* display = lv_linux_drm_create();
  if (display == nullptr) {
    LOG_ERROR("Failed to create Linux DRM display");
    return nullptr;
  }

  LOG_DEBUG("Create Linux DRM display with {}", drm_device);
  if (lv_linux_drm_set_file(display, drm_device, -1) != LV_RESULT_OK) {
    LOG_ERROR("Failed to set Linux DRM device: {}", drm_device);
    lv_display_delete(display);
    return nullptr;
  }

  return display;
}
#endif
#endif

}  // namespace

void stop_app_services(const std::shared_ptr<service::AppServices>& services) {
  if (!services) {
    return;
  }

  if (services->camera) {
    services->camera->stop();
  }
  if (services->gallery) {
    services->gallery->stop();
  }
  if (services->audio) {
    services->audio->stop();
  }
}

void teardown_lvgl(lv_display_t* display) {
  if (display) {
    lv_display_delete(display);
  }
  lv_deinit();
}

void setup_logger() {
  auto logger = std::make_shared<util::Logger>();
  logger->set_tag("Camera");
  logger->set_color_mode(util::ColorMode::AUTO);
  logger->set_level(util::LogLevel::DEBUG);
  logger->set_timestamp_enabled(true);
}

lv_display_t* setup_display() {
#if USE_DESKTOP
  constexpr int w_width  = 320;
  constexpr int w_height = 170;

  LOG_DEBUG("Create desktop window, size {} x {}", w_width, w_height);
  lv_display_t* display = lv_sdl_window_create(w_width, w_height);

  if (display == nullptr) {
    LOG_FATAL("Failed to create SDL window");
    return nullptr;
  }

  (void)lv_sdl_mouse_create();
  (void)lv_sdl_mousewheel_create();

  return display;
#else
  lv_tick_set_cb(app_tick_ms);
  lv_delay_set_cb(app_delay_ms);

  const char* env_fb_device          = std::getenv("CAMERA_APP_FB_DEVICE");
  const char* env_launcher_fb_device = std::getenv("APPLAUNCH_LINUX_FBDEV_DEVICE");
  const char* env_lv_fb_device       = std::getenv("LV_LINUX_FBDEV_DEVICE");
  const char* fb_device = env_fb_device && env_fb_device[0] ? env_fb_device
                          : env_launcher_fb_device && env_launcher_fb_device[0]
                              ? env_launcher_fb_device
                          : env_lv_fb_device && env_lv_fb_device[0] ? env_lv_fb_device
                                                                    : APP_FRAMEBUFFER_DEVICE;
  if (env_fb_device && env_fb_device[0]) {
    lv_display_t* display = setup_fb_display(fb_device);
    if (!display) {
      LOG_FATAL("Failed to initialize requested framebuffer display: {}", fb_device);
    }
    return display;
  }

#if APP_USE_DRM
  const char* env_device = std::getenv("CAMERA_APP_DRM_DEVICE");
  const char* drm_device = env_device && env_device[0] ? env_device : APP_DRM_DEVICE;
  if (lv_display_t* display = setup_drm_display(drm_device)) {
    return display;
  }

  LOG_WARN("DRM display unavailable, falling back to framebuffer: {}", fb_device);
#else
  LOG_DEBUG("DRM display support disabled, using framebuffer: {}", fb_device);
#endif
  if (lv_display_t* display = setup_fb_display(fb_device)) {
    return display;
  }

  LOG_FATAL("Failed to initialize Linux display via DRM or framebuffer");
  return nullptr;
#endif
}

void setup_screen_manager(screen::ScreenManager& manager,
                          const std::shared_ptr<service::AppServices>& services) {
  manager.register_screen(app::screen_id(app::AppState::Splash), [services](lv_obj_t* parent) {
    auto viewmodel = std::make_shared<viewmodel::SplashViewModel>(services);
    auto view      = std::make_unique<view::SplashView>(parent);
    view->bind(viewmodel->status_text_subject(), viewmodel->status_visible_subject());
    return std::make_shared<screen::Screen>(parent, std::move(view), viewmodel);
  });

  manager.register_screen(app::screen_id(app::AppState::Camera), [services](lv_obj_t* parent) {
    auto viewmodel = std::make_shared<viewmodel::CameraViewModel>(services);
    auto view      = std::make_unique<view::CameraView>(parent);
    return std::make_shared<screen::Screen>(parent, std::move(view), viewmodel);
  });

  manager.register_screen(app::screen_id(app::AppState::Gallery), [services](lv_obj_t* parent) {
    auto viewmodel = std::make_shared<viewmodel::GalleryViewModel>(services);
    auto view      = std::make_unique<view::GalleryView>(parent);
    view->bind(viewmodel->image_path_subject(),
               viewmodel->counter_subject(),
               viewmodel->title_subject(),
               viewmodel->status_subject(),
               viewmodel->empty_visible_subject(),
               viewmodel->confirm_delete_subject(),
               viewmodel->delete_choice_subject(),
               viewmodel->info_visible_subject(),
               viewmodel->info_text_subject(),
               viewmodel->info_scroll_subject());
    return std::make_shared<screen::Screen>(parent, std::move(view), viewmodel);
  });
}

void handle_navigation(screen::ScreenManager& manager, app::AppStateMachine& state_machine) {
  static view::CameraView* preview_owner = nullptr;
  static service::PreviewFrameLimiter preview_limiter;
  static uint32_t preview_stats_started_ms = 0;
  auto current                             = manager.current_screen();
  if (!current) {
    LOG_ERROR("Failed to get current screen...");
    return;
  }

  auto camera_vm   = std::dynamic_pointer_cast<viewmodel::CameraViewModel>(current->viewmodel());
  auto camera_view = dynamic_cast<view::CameraView*>(current->view());
  if (camera_vm && camera_view) {
    if (preview_owner != camera_view) {
      preview_owner = camera_view;
      preview_limiter.reset();
      preview_stats_started_ms = app_tick_ms();
    }
    service::CameraFrame frame;
    if (camera_vm->consume_frame(frame)) {
      preview_limiter.push(std::move(frame));
    }
    const uint32_t now = app_tick_ms();
    if (preview_limiter.take(now, frame)) {
      camera_view->set_preview_frame(frame);
      const uint64_t presented = preview_limiter.presented_frames();
      if (presented == 1 || presented % 300 == 0) {
        const uint32_t elapsed = now - preview_stats_started_ms;
        LOG_INFO("Preview display stats: presented={} coalesced={} fps={}",
                 presented,
                 preview_limiter.coalesced_frames(),
                 elapsed ? presented * 1000 / elapsed : 0);
      }
    }
    camera_view->set_zoom_state(camera_vm->zoom_state());

    if (camera_vm->consume_capture_feedback()) {
      camera_view->play_capture_feedback();
    }

    const service::CaptureResult capture_result = camera_vm->consume_capture_result();
    if (capture_result.state == service::CaptureState::Saved ||
        capture_result.state == service::CaptureState::Failed) {
      camera_view->set_capture_status(capture_result);
    }
  }

  auto vm = current->viewmodel();
  if (!vm) {
    LOG_ERROR("Failed to get current viewmodel");
    return;
  }

  if (!camera_vm || !camera_view) {
    preview_owner = nullptr;
    preview_limiter.reset();
    preview_stats_started_ms = 0;
  }

  app::AppState requested_state = vm->consume_transition_request();
  if (requested_state == app::AppState::None) {
    return;
  }

  if (state_machine.transition_to(requested_state)) {
    LOG_DEBUG("Switching to {}", app::screen_id(requested_state));
    manager.replace_screen(app::screen_id(requested_state));
  }
}

bool dispatch_action(screen::ScreenManager& manager,
                     const std::shared_ptr<service::AppServices>& services,
                     app::AppAction action) {
  if (action == app::AppAction::None) {
    return false;
  }

  auto current = manager.current_screen();
  if (!current || !current->viewmodel()) {
    return false;
  }

  const bool camera_capture =
      (action == app::AppAction::Capture || action == app::AppAction::Confirm) &&
      std::dynamic_pointer_cast<viewmodel::CameraViewModel>(current->viewmodel()) != nullptr;

  const bool handled = current->viewmodel()->handle_action(action);
  if (handled && services && services->audio) {
    if (camera_capture) {
      services->audio->play_shutter();
    } else {
      services->audio->play_click();
    }
  }

  return handled;
}

int main() {
  setup_logger();
  std::signal(SIGINT, handle_program_signal);
  std::signal(SIGTERM, handle_program_signal);
  std::signal(SIGPIPE, SIG_IGN);

  lv_init();
  lv_display_t* display = setup_display();
  if (!display) {
    return 1;
  }

  auto services                                 = service::AppServices::create();
  const util::CameraResolutionConfig resolution = util::load_camera_resolution_config();
  LOG_INFO("Configured still capture resolution: {}x{}", resolution.width, resolution.height);
  services->camera->set_capture_resolution({resolution.width, resolution.height});
  app::AppStateMachine state_machine(app::AppState::Splash);
  screen::ScreenManager manager(lv_scr_act());
  setup_screen_manager(manager, services);
  manager.push_screen(app::screen_id(app::AppState::Splash));

  auto help_popup = std::make_unique<view::HelpPopup>(lv_layer_top());

  LOG_INFO("Camera application started");

  bool running = true;
  auto dispatch_app_action =
      [&running, &manager, &services, &state_machine, &help_popup](app::AppAction action) {
        if (action == app::AppAction::ToggleHint) {
          if (help_popup->visible()) {
            help_popup->hide();
          } else {
            help_popup->show(state_machine.current_state());
          }
          if (services && services->audio) {
            services->audio->play_click();
          }
          return;
        }

        if (help_popup->visible()) {
          if (action == app::AppAction::Exit) {
            help_popup->hide();
            if (services && services->audio) {
              services->audio->play_click();
            }
          } else if (action == app::AppAction::PanUp || action == app::AppAction::PanDown) {
            help_popup->scroll(action == app::AppAction::PanUp ? -1 : 1);
          }
          return;
        }

        if (action == app::AppAction::ToggleCameraBackend) {
          if (services && services->camera) {
            const service::CameraBackendPreference preference =
                services->camera->toggle_backend_preference();
            LOG_INFO("Camera backend toggle requested: {}",
                     preference == service::CameraBackendPreference::Usb ? "usb" : "csi");
          }
          (void)state_machine.transition_to(app::AppState::Splash);
          manager.replace_screen(app::screen_id(app::AppState::Splash));
          if (services && services->audio) {
            services->audio->play_click();
          }
          return;
        }

        auto current = manager.current_screen();
        const bool is_camera_screen =
            current &&
            std::dynamic_pointer_cast<viewmodel::CameraViewModel>(current->viewmodel()) != nullptr;
        if (is_camera_screen) {
          if (action == app::AppAction::ZoomOut) {
            action = app::AppAction::Exit;
          } else if (action == app::AppAction::ZoomIn) {
            action = app::AppAction::ZoomOut;
          } else if (action == app::AppAction::OpenGallery) {
            action = app::AppAction::ZoomIn;
          } else if (action == app::AppAction::ToggleCaptureMode) {
            action = app::AppAction::OpenGallery;
          }
        }

        const bool is_gallery_screen =
            current &&
            std::dynamic_pointer_cast<viewmodel::GalleryViewModel>(current->viewmodel()) != nullptr;
        if (is_gallery_screen) {
          if (action == app::AppAction::ZoomOut) {
            action = app::AppAction::Exit;
          } else if (action == app::AppAction::ZoomIn) {
            action = app::AppAction::PanLeft;
          } else if (action == app::AppAction::Capture) {
            action = app::AppAction::ShowInfo;
          } else if (action == app::AppAction::OpenGallery) {
            action = app::AppAction::PanRight;
          } else if (action == app::AppAction::ToggleCaptureMode) {
            action = app::AppAction::Delete;
          }
        }

        if (action == app::AppAction::Exit) {
          if (dispatch_action(manager, services, action)) {
            return;
          }
          LOG_INFO("Exit requested by keyboard");
          running = false;
          request_program_exit();
          return;
        }

        dispatch_action(manager, services, action);
      };

  input::LinuxKeypad keypad;
  keypad.set_action_callback(dispatch_app_action);
  keypad.open_default();

#if USE_DESKTOP
  input::SdlKeypad desktop_keypad;
  desktop_keypad.set_action_callback(dispatch_app_action);
#endif

  uint32_t last_update_ms = app_tick_ms();
  while (running && !g_exit_requested) {
    const uint32_t now = app_tick_ms();
    manager.update(now - last_update_ms);
    last_update_ms = now;
    handle_navigation(manager, state_machine);

    keypad.poll();
#if USE_DESKTOP
    desktop_keypad.poll();
#endif
    lv_timer_handler();
    app_delay_ms(5);
  }

  keypad.close();
  help_popup.reset();
  manager.clear();
  stop_app_services(services);
  teardown_lvgl(display);

  LOG_INFO("Camera program exited");
  return 0;
}
