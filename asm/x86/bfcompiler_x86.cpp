
#include <cassert>

#include "bfcompiler_x86.hpp"

void BFCompilerX86::compile_node_list(BFNodeList* node_list) {
  // Arguments for syscalls are:
  //  1. syscall number in rax (read = 0, write = 1)
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
      _assembler.mov_imm32_reg32((uint32_t)AssemblerX86::Syscall::Write, AssemblerX86::Register::A);
      _assembler.mov_imm32_reg32(1, AssemblerX86::Register::DI);
      _assembler.mov_imm32_reg32(1, AssemblerX86::Register::D);
      _assembler.syscall();
    } else if (n->kind() == BFNodeKind::DpInput) {
      _assembler.mov_imm32_reg32((uint32_t)AssemblerX86::Syscall::Read, AssemblerX86::Register::A);
      _assembler.mov_imm32_reg32(0, AssemblerX86::Register::DI);
      _assembler.mov_imm32_reg32(1, AssemblerX86::Register::D);
      _assembler.syscall();
    } else if (n->kind() == BFNodeKind::Loop) {
      // We don't care about the result here as the inner loop's body is emitted
      // into our "outer" loop body
      (void)compile_loop_node(as_loop_node(n), false /* is_entry */);
    } else if (n->kind() == BFNodeKind::Clear) {
      _assembler.mov_imm8_mem8(0, DataArrayRegister);
    }
  }
}

BFCompilerX86::BFCompilerX86(bool start_compiler_thread, bool debug)
  : BFCompiler(CodeBlobSize, start_compiler_thread, debug),
    _assembler(&_code_blob) {}

BFCompiledMethod* BFCompilerX86::compile_loop_node(BFLoopNode* loop_node, bool is_entry) {
  // A loop should only be compiled as an entry loop once
  assert(loop_node->profile()->compiled_method() == nullptr || !is_entry);

  void* const entrypoint = _code_blob.get_current_entrypoint();

  if (is_entry) {
    // The argument is in rdi (pointer to the data array), but we move it to rsi
    // immediately since the syscalls (read and write) expect to have the memory
    // location to print from there, so we don't have to juggle registers.
    _assembler.mov_reg64_to_reg64(AssemblerX86::Register::DI, DataArrayRegister);
  }

  void* const zero_check_start = _code_blob.get_current_entrypoint();

  // Loop start/end condition check: If the check is true, i.e., the data at the
  // current data pointer is 0, then we don't jump and go straight to the return
  _assembler.cmp_mem8(DataArrayRegister, 0);
  _assembler.jnz_imm32(0 /* placeholder */);
  void* const backpatch_jnz_addr = _code_blob.get_current_entrypoint();

  void* backpatch_jmp_addr = nullptr;

  if (is_entry) {
    // If this is the entry loop, just emit a return instruction to return back
    _assembler.mov_reg64_to_reg64(DataArrayRegister, AssemblerX86::Register::A);
    _assembler.ret_near();
  } else {
    // If this is not the entry loop, we're inside a nested loop, so the "return"
    // condition should jump to the instruction just after the current loop. We
    // achieve this by emitting an instruction and then "backpatching" it to jump
    // to the offset just after emitting the entire loop body.
    _assembler.jmp_imm32(0 /* placeholder */);
    backpatch_jmp_addr = _code_blob.get_current_entrypoint();
  }

  _assembler.jmp_backpatch(backpatch_jnz_addr, calculate_offset(backpatch_jnz_addr));

  // Compile loop body
  compile_node_list(loop_node->nodes());

  // Jump back to zero-check
  _assembler.jmp_imm32(-(calculate_offset(zero_check_start) + 5));

  if (is_entry) {
    // Only install the compiled method for entry loop nodes
    const size_t method_size = calculate_offset(entrypoint);
    BFCompiledMethod* compiled_method = new BFCompiledMethod((CompiledMethod)entrypoint, method_size);

    if (_debug) {
      compiled_method->print_method(false);
    }

    return compiled_method;
  } else {
    assert(backpatch_jmp_addr != nullptr);
    _assembler.jmp_backpatch(backpatch_jmp_addr, calculate_offset(backpatch_jmp_addr));
    return nullptr;
  }
}

BFCompiledMethod* BFCompilerX86::compile_aot(BFNodeList* node_list) {
  void* const entrypoint = _code_blob.get_current_entrypoint();

  // The argument is in rdi (pointer to the data array), but we move it to rsi
  // immediately since the syscalls (read and write) expect to have the memory
  // location to print from there, so we don't have to juggle registers.
  _assembler.mov_reg64_to_reg64(AssemblerX86::Register::DI, DataArrayRegister);

  compile_node_list(node_list);

  // Return from the function
  _assembler.ret_near();

  const size_t method_size = calculate_offset(entrypoint);
  BFCompiledMethod* compiled_method = new BFCompiledMethod((CompiledMethod)entrypoint, method_size);

  if (_debug) {
    compiled_method->print_method(false);
  }

  return compiled_method;
}
