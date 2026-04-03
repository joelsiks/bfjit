
#include <cassert>

#include "bfcompiler_x86.hpp"

void BFCompilerX86::compile_node_list(BFNodeList* node_list) {
  // Arguments for syscalls are:
  //  1. sycall number in rax
  //  2. file handle in rdi (stdin/stdout)
  //  3. pointer to memory in rsi (our data pointer live there so no need to do anything)
  //  4. number of bytes in rdx

  for (BFNode* n : *node_list) {
    if (n->kind() == BFNodeKind::DpInc) {
      _assembler.add_imm32_reg64((uint32_t)as_count_node(n)->count(), DataArrayRegister);
    } else if (n->kind() == BFNodeKind::DpDec) {
      _assembler.sub_imm32_reg64((uint32_t)as_count_node(n)->count(), DataArrayRegister);
    } else if (n->kind() == BFNodeKind::ByteInc) {
      _assembler.add_imm8_mem8(as_count_node(n)->count(), DataArrayRegister);
    } else if (n->kind() == BFNodeKind::ByteDec) {
      _assembler.sub_imm8_mem8(as_count_node(n)->count(), DataArrayRegister);
    } else if (n->kind() == BFNodeKind::DpOutput) {
      _assembler.mov_imm32_reg32(1, AssemblerX86::Register::A);
      _assembler.mov_imm32_reg32(1, AssemblerX86::Register::DI);
      _assembler.mov_imm32_reg32(1, AssemblerX86::Register::D);
      _assembler.syscall();
    } else if (n->kind() == BFNodeKind::DpInput) {
      _assembler.mov_imm32_reg32(0, AssemblerX86::Register::A);
      _assembler.mov_imm32_reg32(0, AssemblerX86::Register::DI);
      _assembler.mov_imm32_reg32(1, AssemblerX86::Register::D);
      _assembler.syscall();
    } else if (n->kind() == BFNodeKind::Loop) {
      // We don't care about the result here as the inner loop's body is emitted
      // into our "outer" loop body
      (void)compile_loop_node((BFLoopNode*)n, false);
    } else if (n->kind() == BFNodeKind::Clear) {
      _assembler.mov_imm8_mem8(0, DataArrayRegister);
    }
  }
}

BFCompilerX86::BFCompilerX86(bool start_compiler_thread)
  : BFCompiler(CodeBlobSize, start_compiler_thread),
    _assembler(&_code_blob) {}

BFCompiledMethod* BFCompilerX86::compile_loop_node(BFLoopNode* loop_node, bool is_entry) {
  if (loop_node->profile()->compiled_method() != nullptr) {
    // A loop should only be compiled as an entry loop once
    assert(!is_entry);
  }

  void* entrypoint = _code_blob.get_current_entrypoint();

  if (is_entry) {
    // The argument is in rdi (pointer to the data array), but we move it to rsi
    // immediately since the syscalls (read and write) expect to have the memory
    // location to print from there, so we don't have to juggle registers.
    _assembler.mov_reg64_to_reg64(AssemblerX86::Register::DI, DataArrayRegister);
  }

  void* zero_check_start = _code_blob.get_current_entrypoint();

  // Loop start/end condition check: If the check is true, i.e., the data at the
  // current data pointer is 0, then we don't jump and go straight to the return
  _assembler.mov_mem8_reg8(DataArrayRegister, AssemblerX86::Register::A);
  _assembler.test_reg8(AssemblerX86::Register::A);

  void* backpatch_jmp_addr = nullptr;

  if (is_entry) {
    // If this is the entry loop, just emit a return instruction to return back
    _assembler.jnz_rel32(4);
    _assembler.mov_reg64_to_reg64(DataArrayRegister, AssemblerX86::Register::A);
    _assembler.ret_near();
  } else {
    // If this is not the entry loop, we're inside a nested loop, so the "return"
    // condition should jump to the instruction just after the current loop. We
    // achieve this by emitting an instruction and then "backpatching" it to jump
    // to the offset just after emitting the entire loop body.
    _assembler.jnz_rel32(5);
    _assembler.jmp_imm32(0);
    backpatch_jmp_addr = _code_blob.get_current_entrypoint();
  }

  // Compile loop body
  compile_node_list(loop_node->nodes());

  void* loop_body_end = _code_blob.get_current_entrypoint();
  int relative_offset_to_zero_check = (uintptr_t)loop_body_end - (uintptr_t)zero_check_start;
  relative_offset_to_zero_check += 5; // For the jmp instruction itself

  _assembler.jmp_imm32(-relative_offset_to_zero_check);

  if (is_entry) {
    // Only install the compiled method for the entry loop node
    void* method_end = _code_blob.get_current_entrypoint();
    size_t method_size = (uintptr_t)method_end - (uintptr_t)entrypoint;

    BFCompiledMethod* compiled_method = new BFCompiledMethod((CompiledMethod)entrypoint, method_size);
    // TODO: Fix debug mode
    if (true) {
      compiled_method->print_method(false);
    }

    return compiled_method;
  } else {
    assert(backpatch_jmp_addr != nullptr);

    // Calculate the jmp offset for the backpatch
    void* nested_loop_end = _code_blob.get_current_entrypoint();
    int relative_offset_to_jmp = (uintptr_t)nested_loop_end - (uintptr_t)backpatch_jmp_addr;

    _assembler.jmp_imm32_backpatch(backpatch_jmp_addr, relative_offset_to_jmp);
    return nullptr;
  }
}

BFCompiledMethod* BFCompilerX86::compile_aot(BFNodeList* node_list) {
  void* entrypoint = _code_blob.get_current_entrypoint();

  // The argument is in rdi (pointer to the data array), but we move it to rsi
  // immediately since the syscalls (read and write) expect to have the memory
  // location to print from there, so we don't have to juggle registers.
  _assembler.mov_reg64_to_reg64(AssemblerX86::Register::DI, DataArrayRegister);

  compile_node_list(node_list);

  // Return from the function
  _assembler.ret_near();

  void* method_end = _code_blob.get_current_entrypoint();
  size_t method_size = (uintptr_t)method_end - (uintptr_t)entrypoint;

  BFCompiledMethod* compiled_method = new BFCompiledMethod((CompiledMethod)entrypoint, method_size);
  // TODO: Fix debug mode
  if (true) {
    compiled_method->print_method(false);
  }

  return compiled_method;
}
