// SDL2 port of FM4-recomp's native wheel effect translation.
#include "xenia/hid/sdl/sdl_wheel.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <sstream>
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"

DEFINE_bool(wheel_enabled, true,
            "Enable raw wheel input when a wheel is selected.", "HID");
DEFINE_string(wheel_name, "",
              "Exact SDL joystick name. Empty disables raw wheel input.",
              "HID");
DEFINE_bool(wheel_calibrated, false,
            "Enable driving axes after setting calibration endpoints.", "HID");
DEFINE_int32(wheel_steering_axis, 0, "Raw steering axis (zero based).", "HID");
DEFINE_int32(wheel_throttle_axis, 2, "Raw throttle axis (zero based).", "HID");
DEFINE_int32(wheel_brake_axis, 5, "Raw brake axis (zero based).", "HID");
DEFINE_int32(wheel_left, -32768, "Raw steering at full left.", "HID");
DEFINE_int32(wheel_center, 0, "Raw steering at center.", "HID");
DEFINE_int32(wheel_right, 32767, "Raw steering at full right.", "HID");
DEFINE_int32(wheel_throttle_released, 32767, "Raw released accelerator.",
             "HID");
DEFINE_int32(wheel_throttle_pressed, -32768, "Raw fully pressed accelerator.",
             "HID");
DEFINE_int32(wheel_brake_released, 32767, "Raw released brake.", "HID");
DEFINE_int32(wheel_brake_pressed, -32768, "Raw fully pressed brake.", "HID");
DEFINE_string(wheel_buttons,
              "0:4096,1:8192,2:16384,3:32768,4:256,5:512,6:32,7:16",
              "Comma separated raw button:decimal Xbox mask. First POV also "
              "supplies D-pad.",
              "HID");
DEFINE_bool(
    wheel_ffb, false,
    "Experimental native Xbox 360 wheel FFB (requires calibrated wheel).",
    "HID");
DEFINE_int32(wheel_ffb_gain, 10, "Hardware gain limit, 0 to 100 percent.",
             "HID");
DEFINE_bool(wheel_ffb_invert, false, "Invert constant steering torque.", "HID");
DEFINE_bool(wheel_log_axes, false,
            "Log raw wheel axes once per second for manual calibration.",
            "HID");

namespace xe::hid::sdl {
bool BuildWheelEffect(const WheelEffect& s, bool inverted,
                      SDL_HapticEffect& o) {
  o = {};
  if (s.id < 1 || s.id > 5) {
    return false;
  }
  switch (s.type) {
    case 1:
      o.type = SDL_HAPTIC_CONSTANT;
      break;
    case 8:
      o.type = SDL_HAPTIC_SPRING;
      break;
    case 9:
      o.type = SDL_HAPTIC_DAMPER;
      break;
    case 10:
      o.type = SDL_HAPTIC_INERTIA;
      break;
    case 11:
      o.type = SDL_HAPTIC_FRICTION;
      break;
    default:
      return false;
  }
  float scale = std::clamp(s.gain, 0, 255) / 255.0f;
  auto force = [&](int v) {
    return Sint16(
        std::lround(std::clamp(v, -127, 127) / 127.0f * 32767 * scale));
  };
  auto saturation = [&](int v) {
    return Uint16(std::lround(std::clamp(v, 0, 255) / 255.0f * 65535 * scale));
  };
  if (s.type == 1) {
    o.constant.direction.type = SDL_HAPTIC_STEERING_AXIS;
    o.constant.length = 100;
    o.constant.level = force(s.magnitude) * (inverted ? -1 : 1);
  } else {
    o.condition.direction.type = SDL_HAPTIC_STEERING_AXIS;
    o.condition.length = 100;
    o.condition.right_coeff[0] = force(s.positive_coefficient);
    o.condition.left_coeff[0] = force(s.negative_coefficient);
    o.condition.right_sat[0] = saturation(s.positive_saturation);
    o.condition.left_sat[0] = saturation(s.negative_saturation);
    o.condition.center[0] =
        Sint16(std::clamp(s.offset, -127, 127) * 32767 / 127);
    o.condition.deadband[0] =
        Uint16(std::clamp(s.deadband, 0, 255) * 32767 / 255);
  }
  return true;
}

bool SDLWheel::Enabled() const {
  return cvars::wheel_enabled && !cvars::wheel_name.empty();
}

bool SDLWheel::Calibrated() const {
  auto raw = [](int v) { return v >= -32768 && v <= 32767; };
  const int l = cvars::wheel_left, c = cvars::wheel_center,
            r = cvars::wheel_right;
  const int t = cvars::wheel_throttle_axis, b = cvars::wheel_brake_axis,
            s = cvars::wheel_steering_axis;
  return joystick_ && cvars::wheel_calibrated && s >= 0 && t >= 0 && b >= 0 &&
         s != t && s != b && t != b &&
         std::max({s, t, b}) < SDL_JoystickNumAxes(joystick_) && raw(l) &&
         raw(c) && raw(r) &&
         ((c - l > 1024 && r - c > 1024) || (l - c > 1024 && c - r > 1024)) &&
         raw(cvars::wheel_throttle_released) &&
         raw(cvars::wheel_throttle_pressed) &&
         raw(cvars::wheel_brake_released) && raw(cvars::wheel_brake_pressed) &&
         std::abs(cvars::wheel_throttle_released -
                  cvars::wheel_throttle_pressed) > 1024 &&
         std::abs(cvars::wheel_brake_released - cvars::wheel_brake_pressed) >
             1024;
}

void SDLWheel::Stop() {
  if (haptic_ && output_) {
    SDL_HapticStopAll(haptic_);
  }
  output_ = false;
}

void SDLWheel::Close() {
  std::lock_guard<std::mutex> lock(mutex_);
  Stop();
  if (haptic_) {
    SDL_HapticClose(haptic_);
  }
  if (joystick_) {
    SDL_JoystickClose(joystick_);
  }
  haptic_ = nullptr;
  joystick_ = nullptr;
  handles_.fill(-1);
  effects_ = {};
  active_ = {};
  connected_ = available_ = driving_ = reset_ = false;
  state_ = {};
  if (initialized_) {
    SDL_QuitSubSystem(SDL_INIT_HAPTIC);
  }
  initialized_ = false;
}

bool SDLWheel::GetState(X_INPUT_STATE* state) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!connected_) {
    return false;
  }
  if (state) {
    *state = state_;
  }
  return true;
}

bool SDLWheel::GetCapabilities(X_INPUT_CAPABILITIES* caps) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!connected_) {
    return false;
  }
  *caps = {};
  caps->type = XINPUT_DEVTYPE_GAMEPAD;
  caps->sub_type = XINPUT_DEVSUBTYPE_WHEEL;
  caps->flags = available_ ? X_INPUT_CAPS_FFB_SUPPORTED : 0;
  caps->gamepad.buttons = 0xF3FF;
  caps->gamepad.thumb_lx = 32767;
  caps->gamepad.left_trigger = caps->gamepad.right_trigger = 255;
  return true;
}

bool SDLWheel::Command(WheelCommand command, const WheelEffect& effect) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (command == WheelCommand::kEnableBridge) {
    bridge_ = effect.magnitude != 0;
    available_ = false;
    active_.fill(false);
    effects_ = {};
    driving_ = false;
    reset_ = true;
    return true;
  }
  if (command == WheelCommand::kReset) {
    guest_gain_ = 255;
    reset_ = true;
    active_.fill(false);
    effects_ = {};
    driving_ = false;
    return true;
  }
  if (!available_ || !cvars::wheel_ffb) {
    return false;
  }
  if (command == WheelCommand::kGain) {
    guest_gain_ = std::clamp(effect.gain, 0, 255);
  } else if (command == WheelCommand::kHeartbeat) {
    heartbeat_ = std::chrono::steady_clock::now();
  } else if (command == WheelCommand::kDriving) {
    driving_ = effect.magnitude != 0;
    heartbeat_ = std::chrono::steady_clock::now();
  } else if (command != WheelCommand::kAvailable) {
    if (effect.id < 1 || effect.id > 5) {
      return false;
    }
    auto i = size_t(effect.id - 1);
    if (command == WheelCommand::kSet || command == WheelCommand::kCreate ||
        command == WheelCommand::kUpdate) {
      effects_[i] = effect;
      if (command != WheelCommand::kUpdate) {
        active_[i] = command == WheelCommand::kSet;
      }
    } else {
      active_[i] = command == WheelCommand::kStart;
    }
    if (active_[i]) {
      driving_ = true;
      heartbeat_ = std::chrono::steady_clock::now();
    }
  }
  return true;
}

void SDLWheel::Update(bool focused) {
  const auto now = std::chrono::steady_clock::now();
  if (joystick_ && (!Enabled() || !SDL_JoystickGetAttached(joystick_))) {
    Close();
  }
  if (!Enabled()) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (!joystick_ && now - scan_ > std::chrono::seconds(1)) {
    scan_ = now;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
      const char* name = SDL_JoystickNameForIndex(i);
      if (name) {
        XELOGI("Wheel candidate {}: {}", i, name);
      }
      if (name && cvars::wheel_name == name) {
        joystick_ = SDL_JoystickOpen(i);
        break;
      }
    }
    if (joystick_) {
      state_.packet_number = 1;
      XELOGI("Raw wheel opened: {} ({} axes, {} buttons)",
             SDL_JoystickName(joystick_), SDL_JoystickNumAxes(joystick_),
             SDL_JoystickNumButtons(joystick_));
    }
  }
  connected_ = joystick_ != nullptr;
  if (!connected_) {
    return;
  }
  const bool calibrated = Calibrated();
  if (bridge_ && calibrated && cvars::wheel_ffb && !haptic_ && !initialized_) {
    initialized_ = SDL_InitSubSystem(SDL_INIT_HAPTIC) == 0;
    if (initialized_) {
      haptic_ = SDL_HapticOpenFromJoystick(joystick_);
    }
    if (haptic_) {
      auto supported = SDL_HapticQuery(haptic_);
      if (!(supported & SDL_HAPTIC_CONSTANT) ||
          !(supported & SDL_HAPTIC_GAIN) ||
          SDL_HapticSetGain(haptic_,
                            std::clamp(cvars::wheel_ffb_gain, 0, 100)) < 0) {
        SDL_HapticClose(haptic_);
        haptic_ = nullptr;
      } else {
        SDL_HapticSetAutocenter(haptic_, 0);
      }
    }
    XELOGI("Wheel native FFB: {} ({})", haptic_ ? "available" : "unavailable",
           SDL_GetError());
  }
  available_ = bridge_ && calibrated && cvars::wheel_ffb && haptic_;
  X_INPUT_STATE next{};
  next.packet_number = state_.packet_number;
  auto& pad = next.gamepad;
  if (calibrated) {
    const int raw = SDL_JoystickGetAxis(joystick_, cvars::wheel_steering_axis);
    const int l = cvars::wheel_left, c = cvars::wheel_center,
              r = cvars::wheel_right;
    const bool left = l < r ? raw < c : raw > c;
    const float position =
        left ? -float(raw - c) / float(l - c) : float(raw - c) / float(r - c);
    pad.thumb_lx =
        int16_t(std::lround(std::clamp(position, -1.0f, 1.0f) * 32767));
    auto pedal = [&](int axis, int released, int pressed) {
      return uint8_t(std::lround(
          std::clamp(float(SDL_JoystickGetAxis(joystick_, axis) - released) /
                         float(pressed - released),
                     0.0f, 1.0f) *
          255));
    };
    pad.right_trigger =
        pedal(cvars::wheel_throttle_axis, cvars::wheel_throttle_released,
              cvars::wheel_throttle_pressed);
    pad.left_trigger =
        pedal(cvars::wheel_brake_axis, cvars::wheel_brake_released,
              cvars::wheel_brake_pressed);
  }
  std::istringstream bindings(cvars::wheel_buttons);
  std::string item;
  while (std::getline(bindings, item, ',')) {
    std::istringstream binding(item);
    int button = -1, mask = 0;
    char colon = 0;
    if (binding >> button >> colon >> mask && colon == ':' && button >= 0 &&
        button < SDL_JoystickNumButtons(joystick_) && mask > 0 &&
        mask <= 65535 && SDL_JoystickGetButton(joystick_, button)) {
      pad.buttons = uint16_t(pad.buttons | mask);
    }
  }
  if (SDL_JoystickNumHats(joystick_) > 0) {
    const auto hat = SDL_JoystickGetHat(joystick_, 0);
    if (hat & SDL_HAT_UP) {
      pad.buttons = uint16_t(pad.buttons | X_INPUT_GAMEPAD_DPAD_UP);
    }
    if (hat & SDL_HAT_DOWN) {
      pad.buttons = uint16_t(pad.buttons | X_INPUT_GAMEPAD_DPAD_DOWN);
    }
    if (hat & SDL_HAT_LEFT) {
      pad.buttons = uint16_t(pad.buttons | X_INPUT_GAMEPAD_DPAD_LEFT);
    }
    if (hat & SDL_HAT_RIGHT) {
      pad.buttons = uint16_t(pad.buttons | X_INPUT_GAMEPAD_DPAD_RIGHT);
    }
  }
  if (std::memcmp(&next.gamepad, &state_.gamepad, sizeof(pad))) {
    ++next.packet_number;
  }
  state_ = next;
  if (cvars::wheel_log_axes && now - logged_ > std::chrono::seconds(1)) {
    logged_ = now;
    std::string axes;
    for (int i = 0; i < SDL_JoystickNumAxes(joystick_); ++i) {
      axes += fmt::format(" {}:{}", i, SDL_JoystickGetAxis(joystick_, i));
    }
    XELOGI("Wheel raw axes:{}", axes);
  }
  if (reset_) {
    Stop();
    for (auto& handle : handles_) {
      if (haptic_ && handle >= 0) {
        SDL_HapticDestroyEffect(haptic_, handle);
      }
      handle = -1;
    }
    reset_ = false;
  }
  const bool allowed = available_ && focused && driving_ && now >= heartbeat_ &&
                       now - heartbeat_ < std::chrono::milliseconds(200);
  if (!allowed) {
    Stop();
    return;
  }
  if (now - renewed_ < std::chrono::milliseconds(32)) {
    return;
  }
  renewed_ = now;
  if (SDL_HapticSetGain(haptic_, std::clamp(cvars::wheel_ffb_gain, 0, 100) *
                                     guest_gain_ / 255) < 0) {
    Stop();
    return;
  }
  static std::atomic<uint32_t> failures{0}, successes{0};
  for (size_t i = 0; i < effects_.size(); ++i) {
    SDL_HapticEffect effect{};
    if (!active_[i] ||
        !BuildWheelEffect(effects_[i], cvars::wheel_ffb_invert, effect) ||
        !SDL_HapticEffectSupported(haptic_, &effect)) {
      if (handles_[i] >= 0) {
        SDL_HapticStopEffect(haptic_, handles_[i]);
      }
      continue;
    }
    if (handles_[i] < 0) {
      handles_[i] = SDL_HapticNewEffect(haptic_, &effect);
    } else if (SDL_HapticUpdateEffect(haptic_, handles_[i], &effect) < 0) {
      SDL_HapticDestroyEffect(haptic_, handles_[i]);
      handles_[i] = SDL_HapticNewEffect(haptic_, &effect);
    }
    if (handles_[i] >= 0 && SDL_HapticRunEffect(haptic_, handles_[i], 1) == 0) {
      output_ = true;
      if (successes.fetch_add(1) < 5) {
        XELOGI(
            "Wheel native FFB: SDL effect running, slot {}, type {}, gain {}, "
            "magnitude {}",
            i + 1, effects_[i].type, effects_[i].gain, effects_[i].magnitude);
      }
    } else if (failures.fetch_add(1) < 8) {
      XELOGW("Wheel native FFB: SDL effect failed, slot {}: {}", i + 1,
             SDL_GetError());
    }
  }
}
}  // namespace xe::hid::sdl
