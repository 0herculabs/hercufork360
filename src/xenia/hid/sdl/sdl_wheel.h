#ifndef XENIA_HID_SDL_SDL_WHEEL_H_
#define XENIA_HID_SDL_SDL_WHEEL_H_

#include <array>
#include <chrono>
#include <mutex>
#include "SDL.h"
#include "xenia/hid/input.h"
#include "xenia/hid/wheel_ffb.h"

namespace xe::hid::sdl {
bool BuildWheelEffect(const WheelEffect& source, bool inverted,
                      SDL_HapticEffect& output);
// Device access belongs exclusively to the SDL UI thread. Guest threads only
// exchange snapshots/commands under mutex_. No motor work in event callbacks.
class SDLWheel {
 public:
  void Update(bool focused);
  void Close();
  bool Enabled() const;
  bool GetState(X_INPUT_STATE* state);
  bool GetCapabilities(X_INPUT_CAPABILITIES* caps);
  bool Command(WheelCommand command, const WheelEffect& effect);

 private:
  void Stop();
  bool Calibrated() const;
  std::mutex mutex_;
  SDL_Joystick* joystick_ = nullptr;
  SDL_Haptic* haptic_ = nullptr;
  std::array<int, 5> handles_{{-1, -1, -1, -1, -1}};
  std::array<WheelEffect, 5> effects_{};
  std::array<bool, 5> active_{};
  X_INPUT_STATE state_{};
  bool connected_ = false, available_ = false, driving_ = false;
  bool reset_ = false, output_ = false, initialized_ = false;
  bool bridge_ = true;
  int guest_gain_ = 255;
  std::chrono::steady_clock::time_point heartbeat_{}, renewed_{}, scan_{},
      logged_{};
};
}  // namespace xe::hid::sdl
#endif  // XENIA_HID_SDL_SDL_WHEEL_H_
