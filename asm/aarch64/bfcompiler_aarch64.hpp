#ifndef COMPILER_AARCH64_HPP
#define COMPILER_AARCH64_HPP

#include "../bfcompiler.hpp"
#include "asm_aarch64.hpp"

class BFCompilerAArch64 : public BFCompiler {
private:
  static const size_t CodeBlobSize = 2 * 1024 * 1024; // 2MB
  static const AssemblerAArch64::Register DataArrayRegister = AssemblerAArch64::Register::r1;

  // AArch64 uses a fixed instruction length of 32 bits (4 bytes)
  static const size_t InstructionSize = 4;

  AssemblerAArch64 _assembler;

  void compile_node_list(BFNodeList* node_list) override;

public:
  BFCompilerAArch64(bool start_compiler_thread);

  BFCompiledMethod* compile_loop_node(BFLoopNode* loop_node, bool is_entry) override;
  BFCompiledMethod* compile_aot(BFNodeList* node_list) override;
};

#endif // COMPILER_AARCH64_HPP
