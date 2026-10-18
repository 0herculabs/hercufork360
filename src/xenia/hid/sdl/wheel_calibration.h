#ifndef XENIA_HID_SDL_WHEEL_CALIBRATION_H_
#define XENIA_HID_SDL_WHEEL_CALIBRATION_H_

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace xe::hid::sdl {
struct WheelCalibration {
  int steering = 0, throttle = 2, brake = 5;
  int left = -32768, center = 0, right = 32767;
  int throttle_rest = 32767, throttle_full = -32768;
  int brake_rest = 32767, brake_full = -32768;
  bool valid = false;
};
inline bool ValidWheelCalibration(const WheelCalibration& c, size_t count) {
  auto raw = [](int v) { return v >= -32768 && v <= 32767; };
  return c.valid && c.steering >= 0 && c.throttle >= 0 && c.brake >= 0 &&
         size_t(std::max({c.steering, c.throttle, c.brake})) < count &&
         c.steering != c.throttle && c.steering != c.brake &&
         c.throttle != c.brake && raw(c.left) && raw(c.center) &&
         raw(c.right) && raw(c.throttle_rest) && raw(c.throttle_full) &&
         raw(c.brake_rest) && raw(c.brake_full) &&
         ((c.center - c.left > 1024 && c.right - c.center > 1024) ||
          (c.left - c.center > 1024 && c.center - c.right > 1024)) &&
         std::abs(c.throttle_full - c.throttle_rest) > 1024 &&
         std::abs(c.brake_full - c.brake_rest) > 1024;
}

// A draft-only wizard: a failed or cancelled capture never changes the active
// calibration. Detect the moving axis, excluding controls already identified.
class WheelCalibrationWizard {
 public:
  int step = -1;
  WheelCalibration result;
  std::string error;
  void Start() {
    step = 0;
    result = {};
    baseline_.clear();
    error.clear();
  }
  void Cancel() {
    step = -1;
    error.clear();
  }
  bool Active() const { return step >= 0 && step < 8; }
  bool Capture(const std::vector<int>& axes) {
    error.clear();
    auto fail = [&](const char* text) {
      error = text;
      return false;
    };
    if (!Active() || axes.empty()) {
      return fail("No axes available.");
    }
    for (int v : axes) {
      if (v < -32768 || v > 32767) {
        return fail("Invalid axis reading.");
      }
    }
    if (step == 0) {
      baseline_ = axes;
      ++step;
      return true;
    }
    if (axes.size() != baseline_.size()) {
      return fail("The device has changed. Restart calibration.");
    }
    if (step == 1 || step == 3 || step == 5) {
      int best = -1, movement = 0, second = 0;
      for (size_t i = 0; i < axes.size(); ++i) {
        if ((step >= 3 && int(i) == result.throttle) ||
            (step == 5 && int(i) == result.brake)) {
          continue;
        }
        int delta = std::abs(axes[i] - baseline_[i]);
        if (delta > movement) {
          second = movement;
          movement = delta;
          best = int(i);
        } else {
          second = std::max(second, delta);
        }
      }
      if (movement < 8192 || second > movement / 2) {
        return fail(
            "Could not identify a single axis. Move only the requested control "
            "and try again.");
      }
      if (step == 1) {
        result.throttle = best;
        result.throttle_full = axes[best];
      }
      if (step == 3) {
        result.brake = best;
        result.brake_full = axes[best];
      }
      if (step == 5) {
        result.steering = best;
        result.left = axes[best];
      }
    } else if (step == 2 || step == 4) {
      int axis = step == 2 ? result.throttle : result.brake;
      int full = step == 2 ? result.throttle_full : result.brake_full;
      if (std::abs(axes[axis] - baseline_[axis]) > 4096 ||
          std::abs(axes[axis] - full) < 8192) {
        return fail("Fully release the pedal before continuing.");
      }
      if (step == 2) {
        result.throttle_rest = axes[axis];
      } else {
        result.brake_rest = axes[axis];
      }
    } else if (step == 6) {
      int initial = baseline_[result.steering];
      int value = axes[result.steering];
      if ((result.left < initial && value <= initial + 1024) ||
          (result.left > initial && value >= initial - 1024)) {
        return fail(
            "Turn the wheel to the opposite end from the previous capture.");
      }
      result.right = value;
    } else if (step == 7) {
      int value = axes[result.steering];
      if (std::abs(value - baseline_[result.steering]) > 4096 ||
          value <= std::min(result.left, result.right) + 1024 ||
          value >= std::max(result.left, result.right) - 1024) {
        return fail("Center the wheel and keep the pedals released.");
      }
      result.center = value;
      result.valid = true;
    }
    ++step;
    return true;
  }

 private:
  std::vector<int> baseline_;
};
}  // namespace xe::hid::sdl
#endif  // XENIA_HID_SDL_WHEEL_CALIBRATION_H_
