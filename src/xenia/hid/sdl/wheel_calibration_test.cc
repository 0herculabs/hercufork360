#include "xenia/hid/sdl/wheel_calibration.h"
#include <cstdio>
#include <cstdlib>

namespace {
void Check(bool ok, const char* text) {
  if (!ok) {
    std::fprintf(stderr, "FAILED: %s\n", text);
    std::exit(1);
  }
}
}  // namespace
int main() {
  using namespace xe::hid::sdl;
  for (bool reversed : {false, true}) {
    WheelCalibrationWizard w;
    std::vector<int> axes(8);
    int rest = reversed ? -32768 : 32767, full = reversed ? 32767 : -32768;
    axes[1] = axes[2] = axes[5] = rest;
    w.Start();
    Check(w.Capture(axes) && w.step == 1, "baseline capture");
    Check(!w.Capture(axes) && w.step == 1, "unchanged accelerator rejected");
    axes[2] = full;
    axes[1] = full;
    Check(!w.Capture(axes) && w.step == 1, "ambiguous moving axes rejected");
    axes[1] = rest;
    Check(w.Capture(axes) && w.result.throttle == 2,
          "accelerator axis detected");
    Check(!w.Capture(axes) && w.step == 2,
          "held accelerator cannot be captured as released");
    axes[2] = rest;
    Check(w.Capture(axes), "accelerator release");
    axes[5] = full;
    Check(w.Capture(axes) && w.result.brake == 5, "brake axis detected");
    axes[5] = rest;
    Check(w.Capture(axes), "brake release");
    axes[0] = reversed ? 32767 : -32768;
    Check(w.Capture(axes) && w.result.steering == 0, "steering axis detected");
    Check(!w.Capture(axes) && w.step == 6, "same steering endpoint rejected");
    axes[0] = reversed ? -32768 : 32767;
    Check(w.Capture(axes), "opposite steering endpoint");
    Check(!w.Capture(axes) && w.step == 7, "uncentered final capture rejected");
    axes[0] = 0;
    Check(w.Capture(axes) && w.step == 8 && !w.Active(), "wizard completed");
    Check(ValidWheelCalibration(w.result, axes.size()),
          "runtime-compatible calibration");
    Check(w.result.throttle_rest == rest && w.result.throttle_full == full,
          "pedal direction preserved");
    w.Start();
    Check(w.Capture(axes), "restart capture");
    Check(!w.Capture(std::vector<int>(2)), "changed device rejected");
    w.Cancel();
    Check(!w.Active() && !w.result.valid,
          "cancel discards unfinished calibration");
  }
  std::puts("Guided wheel calibration tests passed.");
}
