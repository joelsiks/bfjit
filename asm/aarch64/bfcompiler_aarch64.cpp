
#include <cassert>

#include "bfcompiler_aarch64.hpp"

void BFCompilerAArch64::compile_node_list(BFNodeList* node_list) {
  // TODO: Document why the "and w0, w0, 255" works...

  // Arguments for syscalls are:
  //  1. syscall number in r8 (read = 63, write = 64)
  //  2. file handle in r0 (stdin/stdout)
  //  3. pointer to memory in r1 (our data pointer live there so no need to do anything)
  //  4. number of bytes in r2

  for (BFNode* n : *node_list) {
    if (n->kind() == BFNodeKind::DpInc) {
      _assembler.add_imm12(DataArrayRegister, DataArrayRegister, as_count_node(n)->count());
    } else if (n->kind() == BFNodeKind::DpDec) {
      _assembler.sub_imm12(DataArrayRegister, DataArrayRegister, as_count_node(n)->count());
    } else if (n->kind() == BFNodeKind::ByteInc) {
      // ldrb w0, [x1]
      // add  w0, w0, N
      // and  w0, w0, 255
      // strb w0, [x1]
      _assembler.ldrb_imm12(DataArrayRegister, AssemblerAArch64::Register::r0);
      _assembler.add_imm12(AssemblerAArch64::Register::r0, AssemblerAArch64::Register::r0, as_count_node(n)->count());
      _assembler.and_imm12_32bit(AssemblerAArch64::Register::r0, AssemblerAArch64::Register::r0, 255);
      _assembler.strb_imm12(DataArrayRegister, AssemblerAArch64::Register::r0);
    } else if (n->kind() == BFNodeKind::ByteDec) {
      // ldrb w0, [x1]
      // sub  w0, w0, N
      // and  w0, w0, 255
      // strb w0, [x1]
      _assembler.ldrb_imm12(DataArrayRegister, AssemblerAArch64::Register::r0);
      _assembler.sub_imm12(AssemblerAArch64::Register::r0, AssemblerAArch64::Register::r0, as_count_node(n)->count());
      _assembler.and_imm12_32bit(AssemblerAArch64::Register::r0, AssemblerAArch64::Register::r0, 255);
      _assembler.strb_imm12(DataArrayRegister, AssemblerAArch64::Register::r0);
    } else if (n->kind() == BFNodeKind::DpOutput) {
      _assembler.mov(AssemblerAArch64::Register::r8, (uint32_t)AssemblerAArch64::Syscall::Write);
      _assembler.mov(AssemblerAArch64::Register::r0, 1);
      _assembler.mov(AssemblerAArch64::Register::r2, 1);
      _assembler.svc();
    } else if (n->kind() == BFNodeKind::DpInput) {
      _assembler.mov(AssemblerAArch64::Register::r8, (uint32_t)AssemblerAArch64::Syscall::Read);
      _assembler.mov(AssemblerAArch64::Register::r0, 0);
      _assembler.mov(AssemblerAArch64::Register::r2, 1);
      _assembler.svc();
    } else if (n->kind() == BFNodeKind::Loop) {
      // We don't care about the result here as the inner loop's body is emitted
      // into our "outer" loop body
      (void)compile_loop_node(as_loop_node(n), false /* is_entry */);
    } else if (n->kind() == BFNodeKind::Clear) {
      _assembler.strb_imm12(DataArrayRegister, AssemblerAArch64::Register::zr);
    }
  }
}

BFCompilerAArch64::BFCompilerAArch64(bool start_compiler_thread)
  : BFCompiler(CodeBlobSize, start_compiler_thread),
    _assembler(&_code_blob) {}

BFCompiledMethod* BFCompilerAArch64::compile_loop_node(BFLoopNode* loop_node, bool is_entry) {
  // A loop should only be compiled as an entry loop once
  assert(loop_node->profile()->compiled_method() == nullptr || !is_entry);

  void* const entrypoint = _code_blob.get_current_entrypoint();

  if (is_entry) {
    // The argument is in r0 (pointer to the data array), but we move it to r1
    // immediately since the syscalls (read and write) expect to have the memory
    // location to print from there, so we don't have to juggle registers.
    _assembler.mov(DataArrayRegister, AssemblerAArch64::Register::r0);
  }

  void* const zero_check_start = _code_blob.get_current_entrypoint();

  // Loop start/end condition check: If the check is true, i.e., the data at the
  // current data pointer is 0, then we don't jump and go straight to the return
  _assembler.ldrb_imm12(DataArrayRegister, AssemblerAArch64::Register::r0);
  _assembler.tst32bit(AssemblerAArch64::Register::r0, AssemblerAArch64::Register::r0);

  void* backpatch_b_addr = nullptr;

  if (is_entry) {
    // If this is the entry loop, just emit a return instruction to return back
    _assembler.bcond(3, AssemblerAArch64::BranchCondition::NE);
    _assembler.mov(AssemblerAArch64::Register::r0, DataArrayRegister);
    _assembler.ret();
  } else {
    // If this is not the entry loop, we're inside a nested loop, so the "return"
    // condition should jump to the instruction just after the current loop. We
    // achieve this by emitting an instruction and then "backpatching" it to jump
    // to the offset just after emitting the entire loop body.
    _assembler.bcond(2, AssemblerAArch64::BranchCondition::NE);
    backpatch_b_addr = _code_blob.get_current_entrypoint();
    _assembler.b(8);
  }

  // Compile loop body
  compile_node_list(loop_node->nodes());

  // Jump back to the zero-check
  void* const loop_body_end = _code_blob.get_current_entrypoint();
  const int32_t relative_offset_to_zero_check = -((uintptr_t)loop_body_end - (uintptr_t)zero_check_start) / InstructionSize;

  _assembler.b(relative_offset_to_zero_check);

  if (is_entry) {
    // Only install the compiled method for the entry loop node
    void* const method_end = _code_blob.get_current_entrypoint();
    const size_t method_size = (uintptr_t)method_end - (uintptr_t)entrypoint;

    BFCompiledMethod* compiled_method = new BFCompiledMethod((CompiledMethod)entrypoint, method_size);
    // TODO: Fix debug mode
    compiled_method->print_method(false);

    return compiled_method;
  } else {
    assert(backpatch_b_addr != 0);

    // Calculate the jmp offset for the backpatch
    void* const nested_loop_end = _code_blob.get_current_entrypoint();
    const int32_t relative_offset_to_b = ((uintptr_t)nested_loop_end - (uintptr_t)backpatch_b_addr) / InstructionSize;

    _assembler.b_backpatch((uint32_t*)backpatch_b_addr, relative_offset_to_b);
    return nullptr;
  }
}

BFCompiledMethod* BFCompilerAArch64::compile_aot(BFNodeList* node_list) {
  void* const entrypoint = _code_blob.get_current_entrypoint();

  // The argument is in r0 (pointer to the data array), but we move it to r1
  // immediately since the syscalls (read and write) expect to have the memory
  // location to print from there, so we don't have to juggle registers.
  _assembler.mov(DataArrayRegister, AssemblerAArch64::Register::r0);

  compile_node_list(node_list);

  _assembler.ret();

  void* const method_end = _code_blob.get_current_entrypoint();
  const size_t method_size = (uintptr_t)method_end - (uintptr_t)entrypoint;

  BFCompiledMethod* compiled_method = new BFCompiledMethod((CompiledMethod)entrypoint, method_size);
  // TODO: Fix debug mode
  compiled_method->print_method(false);

  return compiled_method;
}
