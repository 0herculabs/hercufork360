/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/util/wheel_ffb_bridge.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"

namespace xe {
namespace kernel {
namespace xboxkrnl {

dword_result_t HidReadKeys_entry(dword_t unk1, unknown_t unk2, unknown_t unk3) {
  /* TODO(gibbed):
   * Games check for the following errors:
   *   0xC000009D - translated to 0x48F  - ERROR_DEVICE_NOT_CONNECTED
   *   0x103      - translated to 0x10D2 - ERROR_EMPTY
   * Other errors appear to be ignored?
   *
   * unk1 is 0
   * unk2 is a pointer to &unk3[2], possibly a 6-byte buffer
   * unk3 is a pointer to a 20-byte buffer
   */
  return 0xC000009D;
}
DECLARE_XBOXKRNL_EXPORT1(HidReadKeys, kInput, kStub);

dword_result_t XInputdFFGetDeviceInfo_entry(dword_t user, dword_t info) {
  return DispatchWheelFFB(kernel_state(), WheelFFBRequest::kInfo, user, info, 0,
                          0);
}
DECLARE_XBOXKRNL_EXPORT1(XInputdFFGetDeviceInfo, kInput, kSketchy);

dword_result_t XInputdFFSetEffect_entry(dword_t user, dword_t effects,
                                        dword_t count, dword_t completion) {
  return DispatchWheelFFB(kernel_state(), WheelFFBRequest::kSet, user, effects,
                          count, completion);
}
DECLARE_XBOXKRNL_EXPORT1(XInputdFFSetEffect, kInput, kSketchy);

dword_result_t XInputdFFUpdateEffect_entry(dword_t user, dword_t effect,
                                           dword_t flags, dword_t completion) {
  return DispatchWheelFFB(kernel_state(), WheelFFBRequest::kUpdate, user,
                          effect, flags, completion);
}
DECLARE_XBOXKRNL_EXPORT1(XInputdFFUpdateEffect, kInput, kSketchy);

dword_result_t XInputdFFEffectOperation_entry(dword_t user, dword_t operations,
                                              dword_t count, dword_t flags,
                                              dword_t completion) {
  return DispatchWheelFFB(kernel_state(), WheelFFBRequest::kOperation, user,
                          operations, count, completion);
}
DECLARE_XBOXKRNL_EXPORT1(XInputdFFEffectOperation, kInput, kSketchy);

dword_result_t XInputdFFDeviceControl_entry(dword_t user, dword_t command,
                                            dword_t completion) {
  return DispatchWheelFFB(kernel_state(), WheelFFBRequest::kControl, user,
                          command, 0, completion);
}
DECLARE_XBOXKRNL_EXPORT1(XInputdFFDeviceControl, kInput, kSketchy);

dword_result_t XInputdFFSetDeviceGain_entry(dword_t user, dword_t gain,
                                            dword_t completion) {
  return DispatchWheelFFB(kernel_state(), WheelFFBRequest::kGain, user, gain, 0,
                          completion);
}
DECLARE_XBOXKRNL_EXPORT1(XInputdFFSetDeviceGain, kInput, kSketchy);

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

DECLARE_XBOXKRNL_EMPTY_REGISTER_EXPORTS(Hid);
