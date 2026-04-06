#ifndef COMPILER_X86_HPP
#define COMPILER_X86_HPP

#include "../bfcompiler.hpp"
#include "asm_x86.hpp"

class BFCompilerX86 : public BFCompiler {
private:
  static const size_t CodeBlobSize = 2 * 1024 * 1024; // 2MB
  static const AssemblerX86::Register DataArrayRegister = AssemblerX86::Register::SI;

  AssemblerX86 _assembler;

  void compile_node_list(BFNodeList* node_list) override;

  void rearrange_dataarrayregister();

public:
  BFCompilerX86(bool start_compiler_thread, bool debug);

  BFCompiledMethod* compile_loop_node(BFLoopNode* loop_node, bool is_entry) override;
  BFCompiledMethod* compile_aot(BFNodeList* node_list) override;
};

#endif // COMPILER_X86_HPP
