// Runs exclusively against a virtual joystick; never enables physical haptics.
#include "xenia/hid/sdl/sdl_wheel.h"
#include <cstdio>
#include <cstdlib>
#include "xenia/base/cvar.h"

DECLARE_string(wheel_name);
DECLARE_string(wheel_buttons);
DECLARE_bool(wheel_calibrated);
DECLARE_bool(wheel_ffb);
DECLARE_int32(wheel_brake_axis);
DECLARE_int32(wheel_throttle_released);
DECLARE_int32(wheel_throttle_pressed);

namespace {
void Check(bool ok, const char* message) {
  if (!ok) {
    std::fprintf(stderr, "FAILED: %s (%s)\n", message, SDL_GetError());
    std::exit(1);
  }
}
}  // namespace
int main() {
  using namespace xe::hid;
  using namespace xe::hid::sdl;
  WheelEffect s;
  s.id = 1;
  s.type = 1;
  s.gain = 255;
  s.magnitude = 127;
  SDL_HapticEffect e{};
  Check(BuildWheelEffect(s, false, e) && e.constant.level == 32767 &&
            e.constant.length == 100,
        "constant force");
  Check(BuildWheelEffect(s, true, e) && e.constant.level == -32767,
        "inverted torque");
  s.gain = 0;
  Check(BuildWheelEffect(s, false, e) && e.constant.level == 0, "zero gain");
  s.gain = 999;
  s.magnitude = -999;
  Check(BuildWheelEffect(s, false, e) && e.constant.level == -32767,
        "clamped force");
  for (int type = 8; type <= 11; ++type) {
    s.type = type;
    s.positive_coefficient = 127;
    s.negative_coefficient = -127;
    s.positive_saturation = 255;
    s.negative_saturation = 0;
    s.deadband = 255;
    Check(BuildWheelEffect(s, false, e) && e.condition.length == 100 &&
              e.condition.right_coeff[0] == 32767 &&
              e.condition.left_coeff[0] == -32767 &&
              e.condition.right_sat[0] == 65535 &&
              e.condition.left_sat[0] == 0 && e.condition.deadband[0] == 32767,
          "condition effect scaling");
  }
  s.type = 99;
  Check(!BuildWheelEffect(s, false, e), "unknown effect rejected");
  s.type = 1;
  s.id = 6;
  Check(!BuildWheelEffect(s, false, e), "invalid slot rejected");

  cvars::wheel_ffb = false;
  SDL_SetMainReady();
  Check(SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) == 0,
        "SDL initialization");
  SDL_VirtualJoystickDesc desc{};
  desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
  desc.type = SDL_JOYSTICK_TYPE_WHEEL;
  desc.naxes = 8;
  desc.nbuttons = 128;
  desc.nhats = 1;
  desc.name = "HercuFork virtual test wheel";
  int index = SDL_JoystickAttachVirtualEx(&desc);
  Check(index >= 0, "virtual wheel creation");
  auto* joystick = SDL_JoystickOpen(index);
  Check(joystick != nullptr, "virtual wheel open");
  cvars::wheel_name = desc.name;
  cvars::wheel_buttons = "0:4096,127:512";
  SDLWheel wheel;
  wheel.Update(true);
  SDL_JoystickSetVirtualAxis(joystick, 0, -32768);
  SDL_JoystickSetVirtualAxis(joystick, 2, -32768);
  SDL_JoystickSetVirtualAxis(joystick, 5, 32767);
  SDL_JoystickSetVirtualButton(joystick, 0, 1);
  SDL_JoystickSetVirtualButton(joystick, 127, 1);
  SDL_JoystickSetVirtualHat(joystick, 0, SDL_HAT_LEFTUP);
  SDL_JoystickUpdate();
  wheel.Update(true);
  X_INPUT_STATE state{};
  Check(wheel.GetState(&state) &&
            state.gamepad.buttons == (0x1000 | 0x200 | 1 | 4),
        "menu buttons, paddles and POV without calibration");
  Check(state.gamepad.thumb_lx == 0 && state.gamepad.right_trigger == 0,
        "uncalibrated axes neutral");
  cvars::wheel_calibrated = true;
  wheel.Update(true);
  wheel.GetState(&state);
  Check(state.gamepad.thumb_lx == -32767 &&
            state.gamepad.right_trigger == 255 &&
            state.gamepad.left_trigger == 0,
        "left steering and independent accelerator");
  SDL_JoystickSetVirtualAxis(joystick, 0, 32767);
  SDL_JoystickSetVirtualAxis(joystick, 2, 32767);
  SDL_JoystickSetVirtualAxis(joystick, 5, -32768);
  SDL_JoystickUpdate();
  wheel.Update(true);
  wheel.GetState(&state);
  Check(state.gamepad.thumb_lx == 32767 && state.gamepad.right_trigger == 0 &&
            state.gamepad.left_trigger == 255,
        "right steering and independent brake");
  cvars::wheel_throttle_released = -32768;
  cvars::wheel_throttle_pressed = 32767;
  wheel.Update(true);
  wheel.GetState(&state);
  Check(state.gamepad.right_trigger == 255, "reversed pedal calibration");
  cvars::wheel_brake_axis = 2;
  wheel.Update(true);
  wheel.GetState(&state);
  Check(state.gamepad.right_trigger == 0 && state.gamepad.left_trigger == 0,
        "duplicate axes rejected");
  X_INPUT_CAPABILITIES caps{};
  Check(wheel.GetCapabilities(&caps) &&
            caps.sub_type == XINPUT_DEVSUBTYPE_WHEEL && caps.flags == 0,
        "wheel subtype without false FFB capability");
  Check(!wheel.Command(WheelCommand::kAvailable, {}), "FFB disabled");
  SDL_JoystickDetachVirtual(index);
  wheel.Update(true);
  Check(!wheel.GetState(&state), "disconnect clears state");
  wheel.Close();
  SDL_JoystickClose(joystick);
  SDL_Quit();
  std::puts("Wheel effect translation and virtual input tests passed.");
  return 0;
}
