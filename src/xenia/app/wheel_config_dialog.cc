#include "xenia/app/wheel_config_dialog.h"

#include <algorithm>
#include <map>
#include <sstream>
#include "third_party/imgui/imgui.h"
#include "xenia/app/emulator_window.h"
#include "xenia/base/cvar.h"
#include "xenia/config.h"
#include "xenia/helper/sdl/sdl_helper.h"
#include "xenia/hid/input_system.h"

DECLARE_string(hid);
DECLARE_bool(wheel_enabled);
DECLARE_string(wheel_name);
DECLARE_string(wheel_buttons);
DECLARE_bool(wheel_calibrated);
DECLARE_bool(wheel_ffb);
DECLARE_bool(wheel_ffb_invert);
DECLARE_int32(wheel_ffb_gain);
DECLARE_int32(wheel_steering_axis);
DECLARE_int32(wheel_throttle_axis);
DECLARE_int32(wheel_brake_axis);
DECLARE_int32(wheel_left);
DECLARE_int32(wheel_center);
DECLARE_int32(wheel_right);
DECLARE_int32(wheel_throttle_released);
DECLARE_int32(wheel_throttle_pressed);
DECLARE_int32(wheel_brake_released);
DECLARE_int32(wheel_brake_pressed);

namespace xe::app {
namespace {
constexpr uint16_t kMasks[] = {4096, 8192, 16384, 32768, 256, 512, 16,  32,
                               1,    2,    4,     8,     64,  128, 1024};
constexpr const char* kActions[] = {
    "A / Confirm",      "B / Back",          "X",          "Y",
    "LB / Left paddle", "RB / Right paddle", "Start",      "Back",
    "D-pad up",         "D-pad down",        "D-pad left", "D-pad right",
    "Left stick (L3)",  "Right stick (R3)",  "Guide"};
constexpr const char* kPrompts[] = {
    "Center the wheel and release all pedals.",
    "Fully press ONLY the throttle pedal.",
    "Fully release the throttle pedal.",
    "Fully press ONLY the brake pedal.",
    "Fully release the brake pedal.",
    "Turn the wheel all the way LEFT.",
    "Turn the wheel all the way RIGHT.",
    "Center the wheel again and keep all pedals released."};
template <typename T>
void Store(const char* name, T value) {
  auto* variable =
      dynamic_cast<cvar::ConfigVar<T>*>(cvar::ConfigVars->at(name));
  variable->OverrideConfigValue(std::move(value));
}
float Pedal(int raw, int rest, int full) {
  return rest == full
             ? 0.0f
             : std::clamp(float(raw - rest) / float(full - rest), 0.0f, 1.0f);
}
}  // namespace

WheelConfigDialog::WheelConfigDialog(ui::ImGuiDrawer* drawer,
                                     EmulatorWindow& window,
                                     std::function<void()> closed)
    : ui::ImGuiDialog(drawer), window_(window), closed_(std::move(closed)) {
  // Physical buttons being assigned must not also activate ImGui widgets.
  restore_gamepad_navigation_ =
      (GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) != 0;
  GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
  use_wheel_ = cvars::wheel_enabled &&
               (cvars::hid == "wheel" ||
                (cvars::hid != "xinput" && !cvars::wheel_name.empty()));
  if (cvars::hid == "sdl" || cvars::hid == "xinput") {
    controller_backend_ = cvars::hid;
  }
  selected_ = cvars::wheel_name;
  calibration_ = {cvars::wheel_steering_axis,
                  cvars::wheel_throttle_axis,
                  cvars::wheel_brake_axis,
                  cvars::wheel_left,
                  cvars::wheel_center,
                  cvars::wheel_right,
                  cvars::wheel_throttle_released,
                  cvars::wheel_throttle_pressed,
                  cvars::wheel_brake_released,
                  cvars::wheel_brake_pressed,
                  cvars::wheel_calibrated};
  ffb_ = cvars::wheel_ffb;
  gain_ = cvars::wheel_ffb_gain;
  inverted_ = cvars::wheel_ffb_invert;
  bindings_.fill(-1);
  std::istringstream list(cvars::wheel_buttons);
  std::string item;
  while (std::getline(list, item, ',')) {
    std::istringstream pair(item);
    int button, mask;
    char colon;
    if (pair >> button >> colon >> mask && colon == ':' && button >= 0) {
      for (size_t i = 0; i < bindings_.size(); ++i) {
        if (mask & kMasks[i]) {
          bindings_[i] = button;
        }
      }
    }
  }
  window_.app_context().CallInUIThreadSynchronous([this] {
    initialized_ = helper::sdl::SDLHelper::Prepare() &&
                   SDL_InitSubSystem(SDL_INIT_JOYSTICK) == 0;
    if (!initialized_) {
      status_ = "Could not initialize wheel detection.";
    }
  });
  Refresh();
}

WheelConfigDialog::~WheelConfigDialog() {
  if (restore_gamepad_navigation_) {
    GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  }
  window_.app_context().CallInUIThreadSynchronous([this] {
    if (joystick_) {
      SDL_JoystickClose(joystick_);
    }
    if (initialized_) {
      SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
    }
  });
  closed_();
}

void WheelConfigDialog::Refresh() {
  if (!initialized_) {
    return;
  }
  window_.app_context().CallInUIThreadSynchronous([this] {
    SDL_PumpEvents();
    devices_.clear();
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
      const char* name = SDL_JoystickNameForIndex(i);
      if (name) {
        devices_.push_back({i, name});
      }
    }
    if (joystick_ && !SDL_JoystickGetAttached(joystick_)) {
      SDL_JoystickClose(joystick_);
      joystick_ = nullptr;
      axes_.clear();
      buttons_.clear();
      wizard_.Cancel();
      binding_ = -1;
    }
    if (!joystick_) {
      for (auto& device : devices_) {
        if (device.name == selected_ ||
            (selected_.empty() &&
             device.name == "Gudsen R3 Racing Wheel and Pedals")) {
          Select(device.index);
          break;
        }
      }
    }
  });
}

void WheelConfigDialog::Select(int index) {
  window_.app_context().CallInUIThreadSynchronous([this, index] {
    if (joystick_) {
      SDL_JoystickClose(joystick_);
    }
    joystick_ = SDL_JoystickOpen(index);
    axes_.clear();
    buttons_.clear();
    wizard_.Cancel();
    binding_ = -1;
    if (!joystick_) {
      status_ = "Could not open the device. Click Refresh.";
      return;
    }
    const std::string name = SDL_JoystickName(joystick_);
    if (name != selected_) {
      calibration_.valid = false;
      ffb_ = false;
    }
    selected_ = name;
    status_.clear();
  });
}

void WheelConfigDialog::Poll() {
  window_.app_context().CallInUIThreadSynchronous([this] {
    if (!joystick_) {
      return;
    }
    SDL_PumpEvents();
    SDL_JoystickUpdate();
    if (!SDL_JoystickGetAttached(joystick_)) {
      SDL_JoystickClose(joystick_);
      joystick_ = nullptr;
      axes_.clear();
      buttons_.clear();
      wizard_.Cancel();
      binding_ = -1;
      status_ = "Wheel disconnected. Reconnect it and click Refresh.";
      return;
    }
    axes_.resize(SDL_JoystickNumAxes(joystick_));
    for (size_t i = 0; i < axes_.size(); ++i) {
      axes_[i] = SDL_JoystickGetAxis(joystick_, int(i));
    }
    if (calibration_.valid &&
        !hid::sdl::ValidWheelCalibration(calibration_, axes_.size())) {
      calibration_.valid = false;
    }
    int pressed = -1, count = 0;
    for (int i = 0; i < SDL_JoystickNumButtons(joystick_); ++i) {
      bool down = SDL_JoystickGetButton(joystick_, i) != 0;
      if (down && (size_t(i) >= buttons_.size() || !buttons_[i])) {
        pressed = i;
        ++count;
      }
    }
    if (binding_ >= 0 && count == 1) {
      for (int& button : bindings_) {
        if (button == pressed) {
          button = -1;
        }
      }
      bindings_[binding_] = pressed;
      binding_ = -1;
    }
    buttons_.resize(SDL_JoystickNumButtons(joystick_));
    for (size_t i = 0; i < buttons_.size(); ++i) {
      buttons_[i] = SDL_JoystickGetButton(joystick_, int(i)) != 0;
    }
    SDL_FlushEvents(SDL_JOYAXISMOTION, SDL_JOYDEVICEREMOVED);
  });
}

void WheelConfigDialog::Save() {
  if (window_.emulator()->is_title_open() ||
      (use_wheel_ && (!joystick_ || selected_.empty() || wizard_.Active()))) {
    return;
  }
  binding_ = -1;
  window_.app_context().CallInUIThreadSynchronous([this] {
    auto lock = window_.emulator()->input_system()->lock();
    Store("wheel_enabled", use_wheel_);
    if (!use_wheel_) {
      Store("hid", controller_backend_);
      config::SaveConfig();
      return;
    }
    Store("hid", std::string("wheel"));
    Store("wheel_name", selected_);
    Store("wheel_calibrated", calibration_.valid);
    Store("wheel_steering_axis", int32_t(calibration_.steering));
    Store("wheel_throttle_axis", int32_t(calibration_.throttle));
    Store("wheel_brake_axis", int32_t(calibration_.brake));
    Store("wheel_left", int32_t(calibration_.left));
    Store("wheel_center", int32_t(calibration_.center));
    Store("wheel_right", int32_t(calibration_.right));
    Store("wheel_throttle_released", int32_t(calibration_.throttle_rest));
    Store("wheel_throttle_pressed", int32_t(calibration_.throttle_full));
    Store("wheel_brake_released", int32_t(calibration_.brake_rest));
    Store("wheel_brake_pressed", int32_t(calibration_.brake_full));
    Store("wheel_ffb", ffb_ && calibration_.valid);
    Store("wheel_ffb_gain", int32_t(std::clamp(gain_, 0, 100)));
    Store("wheel_ffb_invert", inverted_);
    std::map<int, uint16_t> buttons;
    for (size_t i = 0; i < bindings_.size(); ++i) {
      if (bindings_[i] >= 0) {
        buttons[bindings_[i]] |= kMasks[i];
      }
    }
    std::string serialized;
    for (auto [button, mask] : buttons) {
      if (!serialized.empty()) {
        serialized += ',';
      }
      serialized += std::to_string(button) + ':' + std::to_string(mask);
    }
    Store("wheel_buttons", serialized);
    config::SaveConfig();
  });
  status_ = "Saved. Restart the emulator to apply the selected input method.";
}

void WheelConfigDialog::DrawControls() {
  if (!calibration_.valid) {
    ImGui::TextWrapped(
        "Calibrate the steering and both pedals to enable driving controls.");
    return;
  }
  auto value = [&](int axis) {
    return axis >= 0 && size_t(axis) < axes_.size() ? axes_[axis] : 0;
  };
  int raw = value(calibration_.steering), c = calibration_.center;
  bool left = calibration_.left < calibration_.right ? raw < c : raw > c;
  float steering = left ? -float(raw - c) / float(calibration_.left - c)
                        : float(raw - c) / float(calibration_.right - c);
  float values[] = {
      (std::clamp(steering, -1.0f, 1.0f) + 1) / 2,
      Pedal(value(calibration_.throttle), calibration_.throttle_rest,
            calibration_.throttle_full),
      Pedal(value(calibration_.brake), calibration_.brake_rest,
            calibration_.brake_full)};
  const char* labels[] = {"Steering", "Throttle", "Brake"};
  for (int i = 0; i < 3; ++i) {
    ImGui::TextUnformatted(labels[i]);
    std::string text =
        std::to_string(
            int(std::lround((i == 0 ? steering : values[i]) * 100))) +
        "%";
    ImGui::ProgressBar(values[i], ImVec2(-1, 0), text.c_str());
  }
}

void WheelConfigDialog::OnDraw(ImGuiIO& io) {
  Poll();
  bool open = true;
  ImGui::SetNextWindowSize(ImVec2(std::min(700.0f, io.DisplaySize.x - 32),
                                  std::min(700.0f, io.DisplaySize.y - 50)),
                           ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Input setup", &open)) {
    int method = use_wheel_ ? 1 : 0;
    if (ImGui::Combo("Input method", &method, "Controller\0Steering wheel\0")) {
      use_wheel_ = method == 1;
      wizard_.Cancel();
      binding_ = -1;
      status_.clear();
    }
    ImGui::TextWrapped(
        "Changes are applied after saving and restarting the emulator.");
    if (!use_wheel_) {
      const char* preview = controller_backend_ == "xinput"
                                ? "Xbox controller (XInput)"
                            : controller_backend_ == "sdl" ? "Controller (SDL)"
                                                           : "Automatic";
      if (ImGui::BeginCombo("Controller backend", preview)) {
        if (ImGui::Selectable("Automatic", controller_backend_ == "any")) {
          controller_backend_ = "any";
        }
#if XE_PLATFORM_WIN32
        if (ImGui::Selectable("Xbox controller (XInput)",
                              controller_backend_ == "xinput")) {
          controller_backend_ = "xinput";
        }
#endif
        if (ImGui::Selectable("Controller (SDL)",
                              controller_backend_ == "sdl")) {
          controller_backend_ = "sdl";
        }
        ImGui::EndCombo();
      }
      ImGui::TextWrapped(
          "Wheel input and FFB will be disabled. Your wheel device, "
          "calibration, buttons and FFB settings are kept for next time.");
    }
    if (use_wheel_) {
      ImGui::TextWrapped(
          "Select your wheel. Changes are only applied when saved.");
      ImGui::SetNextItemWidth(-140);
      if (ImGui::BeginCombo("##wheel", selected_.empty() ? "Select a device..."
                                                         : selected_.c_str())) {
        for (auto& device : devices_) {
          ImGui::PushID(device.index);
          if (ImGui::Selectable(device.name.c_str(),
                                device.name == selected_)) {
            Select(device.index);
          }
          ImGui::PopID();
        }
        ImGui::EndCombo();
      }
      ImGui::SameLine();
      if (ImGui::Button("Refresh")) {
        Refresh();
      }
      if (devices_.empty()) {
        ImGui::TextWrapped(
            "No devices detected. Connect your wheel in PC mode and click "
            "Refresh.");
      }
      if (joystick_) {
        ImGui::Text("Connected: %d axes, %d buttons", int(axes_.size()),
                    int(buttons_.size()));
      }
      ImGui::Separator();
      ImGui::BeginDisabled(!joystick_);
      if (ImGui::BeginTabBar("wheel-tabs")) {
        if (ImGui::BeginTabItem("Calibration")) {
          if (!wizard_.Active()) {
            if (ImGui::Button(calibration_.valid
                                  ? "Recalibrate"
                                  : "Start guided calibration")) {
              wizard_.Start();
              binding_ = -1;
            }
            if (calibration_.valid) {
              ImGui::Text(
                  "Calibrated: steering axis %d, throttle axis %d, brake axis "
                  "%d",
                  calibration_.steering, calibration_.throttle,
                  calibration_.brake);
            }
            DrawControls();
          } else {
            ImGui::Spacing();
            ImGui::Text("Step %d of 8", wizard_.step + 1);
            ImGui::ProgressBar(float(wizard_.step) / 8, ImVec2(-1, 0));
            ImGui::TextWrapped("%s", kPrompts[wizard_.step]);
            ImGui::TextWrapped(
                "Hold that position and click Capture and continue.");
            if (ImGui::Button(wizard_.step == 7 ? "Finish calibration"
                                                : "Capture and continue")) {
              if (wizard_.Capture(axes_) && wizard_.step == 8) {
                calibration_ = wizard_.result;
                status_ =
                    "Calibration complete. Check the live indicators and save "
                    "your changes.";
              }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel calibration")) {
              wizard_.Cancel();
            }
            if (!wizard_.error.empty()) {
              ImGui::TextWrapped("%s", wizard_.error.c_str());
            }
            if (wizard_.step >= 2) {
              ImGui::Text("Throttle detected: axis %d",
                          wizard_.result.throttle);
            }
            if (wizard_.step >= 4) {
              ImGui::Text("Brake detected: axis %d", wizard_.result.brake);
            }
            if (wizard_.step >= 6) {
              ImGui::Text("Steering detected: axis %d",
                          wizard_.result.steering);
            }
          }
          ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Buttons and paddles")) {
          ImGui::TextWrapped(
              "Click Assign, then press a button or paddle on your wheel. The "
              "first POV hat automatically controls the D-pad.");
          for (size_t i = 0; i < bindings_.size(); ++i) {
            ImGui::PushID(int(i));
            ImGui::Text("%s: %s", kActions[i],
                        bindings_[i] < 0
                            ? "Unassigned"
                            : std::to_string(bindings_[i]).c_str());
            ImGui::SameLine(300);
            if (ImGui::Button(binding_ == int(i) ? "Press a button..."
                                                 : "Assign")) {
              binding_ = int(i);
              wizard_.Cancel();
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear")) {
              bindings_[i] = -1;
              binding_ = -1;
            }
            ImGui::PopID();
          }
          if (binding_ >= 0 && ImGui::Button("Cancel assignment")) {
            binding_ = -1;
          }
          ImGui::TextWrapped(
              "Paddles use LB/RB by default. Select the matching control "
              "layout "
              "in the game.");
          ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("FFB")) {
          ImGui::TextWrapped(
              "Experimental native Xbox 360 wheel FFB. Supported effects "
              "depend "
              "on "
              "the game and wheel driver.");
          ImGui::BeginDisabled(!calibration_.valid);
          ImGui::Checkbox("Enable native FFB", &ffb_);
          ImGui::SliderInt("Gain (%)", &gain_, 0, 100);
          ImGui::Checkbox("Invert steering force", &inverted_);
          ImGui::EndDisabled();
          ImGui::TextWrapped(
              "Recommended starting gain: 10%%. This window does not send test "
              "pulses or forces.");
          ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Live axes")) {
          ImGui::TextWrapped(
              "Move a control to see which axis changes. Guided calibration "
              "detects the axes automatically.");
          for (size_t i = 0; i < axes_.size(); ++i) {
            ImGui::Text("Axis %d: %d", int(i), axes_[i]);
            ImGui::ProgressBar((axes_[i] + 32768) / 65535.0f, ImVec2(-1, 0));
          }
          ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
      }
      ImGui::EndDisabled();
    }
    ImGui::Separator();
    bool running = window_.emulator()->is_title_open();
    if (running) {
      ImGui::TextWrapped("Close the game before saving input settings.");
    }
    ImGui::BeginDisabled(running ||
                         (use_wheel_ && (!joystick_ || wizard_.Active())));
    if (ImGui::Button(use_wheel_ ? "Save and enable wheel"
                                 : "Save and use controller")) {
      Save();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
      open = false;
    }
    if (!status_.empty()) {
      ImGui::TextWrapped("%s", status_.c_str());
    }
  }
  ImGui::End();
  if (!open) {
    Close();
  }
}
}  // namespace xe::app
