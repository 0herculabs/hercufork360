// Common Xbox 360 force-feedback boundary. No title addresses or signatures.
#include "xenia/kernel/util/wheel_ffb_bridge.h"
#include <atomic>
#include "xenia/base/logging.h"
#include "xenia/emulator.h"
#include "xenia/hid/input_system.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"
namespace xe::kernel {
namespace {
using hid::WheelCommand;
using hid::WheelEffect;
bool Buffer(Memory* memory, uint32_t address, uint32_t size,
            bool write = false) {
  if (!address || (address & 3) || !size ||
      uint64_t(address) + size > 0x100000000ull) {
    return false;
  }
  uint64_t cursor = address, end = cursor + size;
  while (cursor < end) {
    auto* heap = memory->LookupHeap(uint32_t(cursor));
    HeapAllocationInfo info{};
    if (!heap || !heap->QueryRegionInfo(uint32_t(cursor), &info) ||
        !(info.state & kMemoryAllocationCommit) ||
        !(info.protect & kMemoryProtectRead) ||
        (write && !IsWritableProtect(info.protect))) {
      return false;
    }
    uint64_t next = uint64_t(info.base_address) + info.region_size;
    if (next <= cursor) {
      return false;
    }
    cursor = next;
  }
  return true;
}

bool Command(KernelState* kernel, uint32_t user, WheelCommand command,
             const WheelEffect& effect = {}) {
  auto* input = kernel->emulator()->input_system();
  auto lock = input->lock();
  return input->WheelFFB(user, command, effect);
}

WheelEffect ReadEffect(Memory* memory, uint32_t p) {
  auto read = [&](uint32_t offset) {
    return int32_t(
        xe::load_and_swap<uint32_t>(memory->TranslateVirtual(p + offset)));
  };
  WheelEffect e;
  e.id = read(0);
  e.type = read(4);
  e.gain = read(12);
  e.magnitude = e.offset = read(44);
  e.positive_coefficient = read(48);
  e.negative_coefficient = read(52);
  e.positive_saturation = read(56);
  e.negative_saturation = read(60);
  e.deadband = read(64);
  return e;
}

}  // namespace
void ResetWheelFFB(KernelState* kernel) {
  Command(kernel, 0, WheelCommand::kReset);
}
void InitializeWheelFFB(KernelState* kernel, UserModule* module) {
  if (!module->is_dll_module()) {
    ResetWheelFFB(kernel);
  }
}
uint32_t ProcessWheelFFB(Memory* memory, const WheelFFBSink& sink,
                         WheelFFBRequest request, uint32_t user, uint32_t p,
                         uint32_t count, uint32_t completion) {
  constexpr uint32_t invalid = 0xC000000D, unsupported = 0xC00000BB;
  if (!sink(user, WheelCommand::kAvailable, {})) {
    return 0xC000009D;
  }
  if (completion && !Buffer(memory, completion, 8, true)) {
    return invalid;
  }
  if (request == WheelFFBRequest::kInfo) {
    if (!Buffer(memory, p, 8, true)) {
      return invalid;
    }
    xe::store_and_swap<uint32_t>(memory->TranslateVirtual(p), 0);
    xe::store_and_swap<uint32_t>(memory->TranslateVirtual(p + 4), 5);
    static std::atomic<bool> logged_info{false};
    if (!logged_info.exchange(true)) {
      XELOGI("Wheel native FFB: device information requested by player {}",
             user);
    }
  } else if (request == WheelFFBRequest::kSet ||
             request == WheelFFBRequest::kUpdate) {
    if (request == WheelFFBRequest::kUpdate) {
      count = 1;
    }
    if (!count || count > 5 || !Buffer(memory, p, count * 116)) {
      return invalid;
    }
    for (uint32_t i = 0; i < count; ++i) {
      auto e = ReadEffect(memory, p + i * 116);
      if (e.id < 1 || e.id > 5 || e.gain < 0 || e.gain > 255) {
        return invalid;
      }
      if (e.type != 1 && (e.type < 8 || e.type > 11)) {
        XELOGW("Wheel native FFB: unsupported effect type {}", e.type);
        return unsupported;
      }
    }
    for (uint32_t i = 0; i < count; ++i) {
      sink(user,
           request == WheelFFBRequest::kSet ? WheelCommand::kCreate
                                            : WheelCommand::kUpdate,
           ReadEffect(memory, p + i * 116));
    }
  } else if (request == WheelFFBRequest::kOperation) {
    if (!count || count > 5 || !Buffer(memory, p, count * 8)) {
      return invalid;
    }
    auto read = [&](uint32_t offset) {
      return xe::load_and_swap<uint32_t>(memory->TranslateVirtual(p + offset));
    };
    for (uint32_t i = 0; i < count; ++i) {
      if (read(i * 8) < 1 || read(i * 8) > 5) {
        return invalid;
      }
      if (read(i * 8 + 4) > 2) {
        return unsupported;
      }
    }
    for (uint32_t i = 0; i < count; ++i) {
      WheelEffect e;
      e.id = read(i * 8);
      // FM2 submits operation 2 after loading all slots, then only updates
      // their parameters while driving. This operation must activate them.
      sink(user,
           read(i * 8 + 4) == 2 ? WheelCommand::kStart : WheelCommand::kStop,
           e);
    }
  } else if (request == WheelFFBRequest::kControl) {
    // Command 3 is the reset operation used by the Xbox wheel SDK.
    if (p != 3) {
      XELOGW("Wheel native FFB: unsupported device control {}", p);
      return unsupported;
    }
    sink(user, WheelCommand::kReset, {});
  } else if (request == WheelFFBRequest::kGain) {
    if (p > 255) {
      return invalid;
    }
    WheelEffect e;
    e.gain = p;
    sink(user, WheelCommand::kGain, e);
  } else {
    return unsupported;
  }
  // Native IO_STATUS_BLOCK, not XAM's larger XOVERLAPPED. All work completes
  // synchronously, so the SDK does not need an asynchronous notification.
  if (completion) {
    xe::store_and_swap<uint32_t>(memory->TranslateVirtual(completion), 0);
    xe::store_and_swap<uint32_t>(memory->TranslateVirtual(completion + 4), 0);
  }
  return 0;
}
uint32_t DispatchWheelFFB(KernelState* kernel, WheelFFBRequest request,
                          uint32_t user, uint32_t p, uint32_t count,
                          uint32_t completion) {
  auto* input = kernel->emulator()->input_system();
  auto lock = input->lock();
  const auto result = ProcessWheelFFB(
      kernel->memory(),
      [input](uint32_t index, WheelCommand command, const WheelEffect& effect) {
        return input->WheelFFB(index, command, effect);
      },
      request, user, p, count, completion);
  static std::atomic<uint32_t> errors{0}, accepted{0};
  if (result && errors.fetch_add(1) < 16) {
    XELOGW(
        "Wheel native FFB: request {} rejected ({:08X}), user {}, argument "
        "{:08X}, count/flags {}",
        uint32_t(request), result, user, p, count);
  } else if (!result && request != WheelFFBRequest::kInfo &&
             accepted.fetch_add(1) < 16) {
    XELOGI(
        "Wheel native FFB: request {} accepted, argument {:08X}, count/flags "
        "{}",
        uint32_t(request), p, count);
  }
  return result;
}
}  // namespace xe::kernel
