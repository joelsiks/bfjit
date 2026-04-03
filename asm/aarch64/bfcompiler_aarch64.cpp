
#include <cassert>

#include "bfcompiler_aarch64.hpp"

void BFCompilerAArch64::compile_node_list(BFNodeList* node_list) {
}

BFCompilerAArch64::BFCompilerAArch64(bool start_compiler_thread)
  : BFCompiler(CodeBlobSize, start_compiler_thread),
    _assembler(&_code_blob) {}

BFCompiledMethod* BFCompilerAArch64::compile_loop_node(BFLoopNode* loop_node, bool is_entry) {
  return nullptr;
}

BFCompiledMethod* BFCompilerAArch64::compile_aot(BFNodeList* node_list) {
  return nullptr;
}
