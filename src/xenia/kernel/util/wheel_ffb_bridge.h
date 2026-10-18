#ifndef XENIA_KERNEL_UTIL_WHEEL_FFB_BRIDGE_H_
#define XENIA_KERNEL_UTIL_WHEEL_FFB_BRIDGE_H_
#include <cstdint>
#include <functional>
#include "xenia/hid/wheel_ffb.h"
namespace xe {
class Memory;
}
namespace xe::kernel {
class KernelState;
class UserModule;
enum class WheelFFBRequest {
  kInfo,
  kSet,
  kUpdate,
  kOperation,
  kControl,
  kGain
};
using WheelFFBSink =
    std::function<bool(uint32_t, hid::WheelCommand, const hid::WheelEffect&)>;
uint32_t ProcessWheelFFB(Memory*, const WheelFFBSink&, WheelFFBRequest,
                         uint32_t user, uint32_t buffer, uint32_t count,
                         uint32_t completion);
uint32_t DispatchWheelFFB(KernelState*, WheelFFBRequest, uint32_t user,
                          uint32_t buffer, uint32_t count, uint32_t completion);
void ResetWheelFFB(KernelState*);
void InitializeWheelFFB(KernelState* kernel, UserModule* module);
}  // namespace xe::kernel
#endif  // XENIA_KERNEL_UTIL_WHEEL_FFB_BRIDGE_H_
