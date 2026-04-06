
#include <cassert>
#include <cstdio>

#include "bfcompiler.hpp"

#if defined(_ARCH_X86)
#include "x86/bfcompiler_x86.hpp"
#elif defined(_ARCH_AARCH64)
#include "aarch64/bfcompiler_aarch64.hpp"
#endif

BFCompiledMethod::BFCompiledMethod(CompiledMethod compiled_method, size_t bytes)
  : _compiled_method(compiled_method),
    _bytes(bytes) {}

CompiledMethod BFCompiledMethod::method() const { return _compiled_method; }

size_t BFCompiledMethod::bytes() const { return _bytes; }

void BFCompiledMethod::print_method(bool print_address) const {
  printf("Compiled method %p size %zu:\n", _compiled_method, _bytes);
  for (size_t i = 0; i < _bytes; i++) {
    if (i % 8 == 0) {
      if (i != 0) {
        printf("\n");
      }

      if (print_address) {
        printf("%08lx: ", (uint64_t)((uint8_t*)_compiled_method + i));
      }
    }

    printf("%02x ", ((uint8_t*)_compiled_method)[i]);
  }
  printf("\n");
}

void BFCompiler::compiler_thread_fn() {
  while (_compiler_thread_running.load()) {
    std::unique_lock lock(_compile_queue_lock);
    _compile_queue_cv.wait(lock);
    if (_compile_queue.empty()) {
      continue;
    }

    BFLoopNode* loop_node = _compile_queue.front();
    _compile_queue.pop();

    // Compile method
    BFCompiledMethod* compiled_method = compile_loop_node(loop_node, true);
    loop_node->profile()->set_compiled_method(compiled_method);
  }
}

int32_t BFCompiler::calculate_offset(void* to) {
  return (uintptr_t)_code_blob.get_current_entrypoint() - (uintptr_t)to;
}

BFCompiler::BFCompiler(size_t code_blob_size, bool start_compiler_thread, bool debug)
  : _compile_queue_lock(),
    _compile_queue_cv(),
    _compile_queue(),
    _compiler_thread_running(true),
    _compiler_thread(nullptr),
    _code_blob(code_blob_size),
    _debug(debug) {

  if (start_compiler_thread) {
    _compiler_thread = std::make_unique<std::thread>(&BFCompiler::compiler_thread_fn, this);
  }
}

BFCompiler::~BFCompiler() {
  if (_compiler_thread != nullptr) {
    _compiler_thread_running.store(false);
    _compile_queue_cv.notify_one();
    _compiler_thread->join();
  }
}

BFCompiler* BFCompiler::create(bool is_jit, bool debug) {
#if defined(_ARCH_X86)
  return new BFCompilerX86(is_jit, debug);
#elif defined(_ARCH_AARCH64)
  return new BFCompilerAArch64(is_jit, debug);
#else
  // Unsupported platform. This should not happen.
  assert(false);
  return nullptr;
#endif
}

void BFCompiler::send_compilation_request(BFLoopNode* loop_node) {
  assert(_compiler_thread != nullptr && _compiler_thread_running);

  {
    std::unique_lock lock(_compile_queue_lock);
    _compile_queue.push(loop_node);
  }

  // Notify thread
  _compile_queue_cv.notify_one();
}
