// Verify the real PPC JIT hook and re-entry path without a game or hardware.
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "xenia/cpu/backend/backend.h"
#include "xenia/cpu/processor.h"
#include "xenia/kernel/util/wheel_ffb_bridge.h"
#if XE_ARCH_AMD64
#include "xenia/cpu/backend/x64/x64_backend.h"
#else
#include "xenia/cpu/backend/a64/a64_backend.h"
#endif

namespace {
void Check(bool ok, const char* message) {
  if (!ok) {
    std::fprintf(stderr, "FAILED: %s\n", message);
    std::exit(1);
  }
  std::fprintf(stderr, "OK: %s\n", message);
  std::fflush(stderr);
}
class GuestModule : public xe::cpu::Module {
 public:
  GuestModule(xe::cpu::Processor* processor, uint32_t base)
      : Module(processor), base_(base) {}
  const std::string& name() const override { return name_; }
  bool is_executable() const override { return true; }
  bool ContainsAddress(uint32_t address) override {
    return address >= base_ && address < base_ + 4096;
  }

 private:
  std::unique_ptr<xe::cpu::Function> CreateFunction(uint32_t address) override {
    return std::unique_ptr<xe::cpu::Function>(
        processor_->backend()->CreateGuestFunction(this, address));
  }
  std::string name_ = "Wheel hook test";
  uint32_t base_;
};
int calls = 0;
uint32_t original_address = 0;
void Hook(xe::cpu::ppc::PPCContext* ctx, xe::kernel::KernelState*) {
  ++calls;
  // Same entry convention as FM4WheelBridge::Original.
  ctx->r[12] = ctx->lr;
  auto* original =
      ctx->thread_state->processor()->ResolveFunction(original_address + 4);
  Check(original != nullptr, "resolve original body");
  original->Call(ctx->thread_state, uint32_t(ctx->lr));
  ctx->r[3] += 100;
}
}  // namespace
int main() {
  xe::Memory memory;
  Check(memory.Initialize(), "guest memory initialization");
  xe::cpu::Processor processor(&memory, nullptr);
#if XE_ARCH_AMD64
  auto backend = std::make_unique<xe::cpu::backend::x64::X64Backend>();
#else
  auto backend = std::make_unique<xe::cpu::backend::a64::A64Backend>();
#endif
  Check(processor.Setup(std::move(backend)), "processor initialization");
  processor.PreLaunch();
  // Xenia's JIT indirection table covers the XEX code range, not system heap
  // addresses. Reserve the synthetic program where a retail XEX would live.
  const uint32_t code = 0x82000000;
  Check(memory.LookupHeap(code)->AllocFixed(
            code, 65536, 65536,
            xe::kMemoryAllocationReserve | xe::kMemoryAllocationCommit,
            xe::kMemoryProtectRead | xe::kMemoryProtectWrite),
        "guest code allocation");
  // Exercise the shared native protocol without connecting a force device.
  using xe::hid::WheelCommand;
  using xe::hid::WheelEffect;
  using xe::kernel::WheelFFBRequest;
  std::vector<std::pair<WheelCommand, WheelEffect>> commands;
  auto sink = [&](uint32_t user, WheelCommand command,
                  const WheelEffect& effect) {
    if (user != 0) {
      return false;
    }
    if (command != WheelCommand::kAvailable) {
      commands.emplace_back(command, effect);
    }
    return true;
  };
  const uint32_t data = code + 4096, completion = data + 1024;
  auto write = [&](uint32_t offset, uint32_t value) {
    xe::store_and_swap<uint32_t>(memory.TranslateVirtual(data + offset), value);
  };
  auto call = [&](WheelFFBRequest request, uint32_t p, uint32_t count = 0,
                  uint32_t io = 0, uint32_t user = 0) {
    return xe::kernel::ProcessWheelFFB(&memory, sink, request, user, p, count,
                                       io);
  };
  Check(call(WheelFFBRequest::kInfo, data) == 0 &&
            xe::load_and_swap<uint32_t>(memory.TranslateVirtual(data + 4)) == 5,
        "native device information and slots");
  write(0, 1);
  write(4, 1);
  write(12, 255);
  write(44, uint32_t(-127));
  write(1024 + 8, 0xDEADBEEF);
  Check(call(WheelFFBRequest::kSet, data, 1, completion) == 0 &&
            commands.back().first == WheelCommand::kCreate &&
            commands.back().second.magnitude == -127 &&
            xe::load_and_swap<uint32_t>(memory.TranslateVirtual(completion)) ==
                0 &&
            xe::load_and_swap<uint32_t>(
                memory.TranslateVirtual(completion + 8)) == 0xDEADBEEF,
        "native effect decoding and bounded synchronous completion");
  Check(call(WheelFFBRequest::kUpdate, data) == 0 &&
            commands.back().first == WheelCommand::kUpdate,
        "native update preserves operation");
  const auto before = commands.size();
  write(116, 6);
  Check(call(WheelFFBRequest::kSet, data, 2) == 0xC000000D &&
            commands.size() == before,
        "invalid batch has no partial effects");
  write(4, 99);
  Check(call(WheelFFBRequest::kSet, data, 1) == 0xC00000BB,
        "unsupported effect returns NTSTATUS");
  write(4, 2);
  Check(call(WheelFFBRequest::kOperation, data, 1) == 0 &&
            commands.back().first == WheelCommand::kStart,
        "native effect start");
  write(4, 1);
  Check(call(WheelFFBRequest::kOperation, data, 1) == 0 &&
            commands.back().first == WheelCommand::kStop,
        "native operation 1 stops the effect");
  // FM2 loads its five slots, activates them with operation 2, then
  // updates parameters without issuing another operation.
  commands.clear();
  for (uint32_t i = 0; i < 5; ++i) {
    write(i * 8, i + 1);
    write(i * 8 + 4, 2);
  }
  Check(call(WheelFFBRequest::kOperation, data, 5, completion) == 0 &&
            commands.size() == 5,
        "FM2 five-slot activation batch");
  for (const auto& command : commands) {
    Check(command.first == WheelCommand::kStart,
          "FM2 operation 2 activates each loaded slot");
  }
  const auto stopped = commands.size();
  write(4, 3);
  Check(call(WheelFFBRequest::kOperation, data, 5) == 0xC00000BB &&
            commands.size() == stopped,
        "invalid operation rejects whole batch");
  Check(call(WheelFFBRequest::kGain, 128) == 0 &&
            commands.back().second.gain == 128,
        "native global gain");
  Check(call(WheelFFBRequest::kControl, 3) == 0 &&
            commands.back().first == WheelCommand::kReset,
        "native device reset");
  Check(call(WheelFFBRequest::kInfo, 0) == 0xC000000D &&
            call(WheelFFBRequest::kSet, data, 1, 0xFFFFFFFC) == 0xC000000D &&
            call(WheelFFBRequest::kSet, data, 6) == 0xC000000D &&
            call(WheelFFBRequest::kGain, 256) == 0xC000000D &&
            call(WheelFFBRequest::kInfo, data, 0, 0, 1) == 0xC000009D,
        "native invalid memory, count, gain and disconnected user");
  uint32_t stack = memory.SystemHeapAlloc(65536);
  Check(stack != 0, "guest stack allocation");
  original_address = code;
  // mflr r12; li r3,7; blr. The hook must suppress this original entry,
  // enter +4 exactly once, and preserve the return/stack convention.
  const uint32_t words[] = {0x7D8802A6, 0x38600007, 0x4E800020, 0x3860002A,
                            0x4E800020};
  for (size_t i = 0; i < 5; ++i) {
    xe::store_and_swap<uint32_t>(
        memory.TranslateVirtual(code + uint32_t(i * 4)), words[i]);
  }
  auto module = std::make_unique<GuestModule>(&processor, code);
  xe::cpu::Function* fn = nullptr;
  module->DeclareFunction(code, &fn);
  static_cast<xe::cpu::GuestFunction*>(fn)->SetupGuestHook(Hook);
  fn->set_status(xe::cpu::Symbol::Status::kDeclared);
  Check(processor.AddModule(std::move(module)), "module registration");
  processor.backend()->CommitExecutableRange(code, code + 4096);
  {
    xe::cpu::ThreadState thread(&processor, 0, stack + 65536, 0);
    auto* ctx = thread.context();
    const uint64_t sp = ctx->r[1];
    ctx->lr = 0x12345678;
    for (int i = 0; i < 3; ++i) {
      Check(processor.Execute(&thread, code), "execute guest hook");
      Check(ctx->r[3] == 107 && calls == i + 1,
            "hook and original body result");
      Check(ctx->r[1] == sp && ctx->lr == 0x12345678,
            "stack and return address preserved");
    }
    Check(
        processor.Execute(&thread, code + 12) && ctx->r[3] == 42 && calls == 3,
        "ordinary guest function unaffected");
  }
  std::puts("PPC guest hook and original-body re-entry tests passed.");
  return 0;
}
