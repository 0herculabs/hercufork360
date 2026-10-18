// Native Xbox wheel effects, translated using the FM4-recomp effect model.
#ifndef XENIA_HID_WHEEL_FFB_H_
#define XENIA_HID_WHEEL_FFB_H_

#include <cstdint>

namespace xe::hid {
struct WheelEffect {
  int32_t id = 0, type = 0, gain = 0, magnitude = 0;
  int32_t positive_coefficient = 0, negative_coefficient = 0;
  int32_t positive_saturation = 0, negative_saturation = 0;
  int32_t offset = 0, deadband = 0;
};
enum class WheelCommand {
  kAvailable,
  kSet,
  kStart,
  kStop,
  kReset,
  kDriving,
  kEnableBridge,
  kCreate,
  kUpdate,
  kGain,
  kHeartbeat
};
}  // namespace xe::hid

#endif  // XENIA_HID_WHEEL_FFB_H_
