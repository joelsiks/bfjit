#include <cstdio>
#include <cstring>
#include <cassert>
#include <stack>

#include "bf.hpp"

static bool debug_mode = false;

const char* node_kind_name(BFNodeKind kind) {
  switch (kind) {
    case BFNodeKind::DpInc:    return "DpInc";
    case BFNodeKind::DpDec:    return "DpDec";
    case BFNodeKind::ByteInc:  return "ByteInc";
    case BFNodeKind::ByteDec:  return "ByteDec";
    case BFNodeKind::DpOutput: return "Output";
    case BFNodeKind::DpInput:  return "Input";
    case BFNodeKind::Loop:     return "Loop";
    case BFNodeKind::Clear:    return "Clear";
    default: assert(false);
  }
};

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

BFNode::BFNode(BFNodeKind kind) : _kind(kind) {}

BFNode::~BFNode() {}

BFNodeKind BFNode::kind() const { return _kind; }

void BFNode::print(size_t indentation) const {
  for (int i = 0; i < indentation; i++) printf(" ");
  printf("%s\n", node_kind_name(_kind));
}

BFCountNode::BFCountNode(BFNodeKind kind) : BFNode(kind), _count(1) {}

void BFCountNode::print(size_t indentation) const {
  for (int i = 0; i < indentation; i++) printf(" ");
  printf("%s (%d)\n", node_kind_name(_kind), _count);
}

void BFCountNode::increment_count() { assert(_count < (uint8_t)-1); _count++; }
uint8_t BFCountNode::count() const { return _count; }

BFLoopProfile::BFLoopProfile()
  : _execution_count(0),
    _compiled_method(nullptr),
    _compilation_queued(false) {}

size_t BFLoopProfile::execution_count() { return _execution_count; }

void BFLoopProfile::inc_execution_count() { _execution_count++; }

bool BFLoopProfile::mark_compilation_queued() {
  bool expected = false;
  return _compilation_queued.compare_exchange_strong(expected, true);
}

void BFLoopProfile::set_compiled_method(BFCompiledMethod* compiled_method) { _compiled_method.store(compiled_method); }

BFCompiledMethod* BFLoopProfile::compiled_method() { return _compiled_method.load(); }

BFLoopNode::BFLoopNode(BFNodeList children)
  : BFNode(BFNodeKind::Loop),
    _nodes(children),
    _profile() {}

BFLoopNode::~BFLoopNode() {
  for (const BFNode* n : _nodes) {
    delete n;
  }
  delete _profile._compiled_method;
}

BFNodeList* BFLoopNode::nodes() { return &_nodes; }

BFLoopProfile* BFLoopNode::profile() { return &_profile; }

void BFLoopNode::print(size_t indentation) const {
  BFNode::print(indentation);
  for (const BFNode* n : _nodes) {
    n->print(indentation + 1);
  }
}

BFClearNode::BFClearNode()
  : BFNode(BFNodeKind::Clear) {}

BFAST::BFAST(BFNodeList&& nodes) : _nodes(nodes) {}

BFAST::~BFAST() {
  for (const BFNode* n : _nodes) {
    delete n;
  }
}

BFNodeList* BFAST::nodes() { return &_nodes; }

void BFAST::print() const {
  for (const BFNode* n : _nodes) {
    n->print(0);
  }
}

BFParser::BFParser(const std::string& program) : _program(program) { }

BFAST BFParser::parse() {
  std::stack<std::vector<BFNode*>> node_lists;
  node_lists.emplace();

  for (char c : _program) {
    switch (c) {
      case '[':
        node_lists.emplace();
        break;
      case ']': {
        // Detect un-matching loops
        if (node_lists.size() == 1) {
          printf("Invalid loop end '%s'\n", _program.c_str());
          exit(1);
        }

        // End of a loop, pop the list from the stack onto the new node
        BFLoopNode* node = new BFLoopNode(node_lists.top());
        node_lists.pop();
        node_lists.top().push_back(node);
        break;
      }
      case '>': node_lists.top().push_back(new BFCountNode(BFNodeKind::DpInc)); break;
      case '<': node_lists.top().push_back(new BFCountNode(BFNodeKind::DpDec)); break;
      case '+': node_lists.top().push_back(new BFCountNode(BFNodeKind::ByteInc)); break;
      case '-': node_lists.top().push_back(new BFCountNode(BFNodeKind::ByteDec)); break;
      case '.': node_lists.top().push_back(new BFCountNode(BFNodeKind::DpOutput)); break;
      case ',': node_lists.top().push_back(new BFCountNode(BFNodeKind::DpInput)); break;
      default: continue; // Skip comment and whitespace
    }
  }

  // Detect un-matching loops
  if (node_lists.size() != 1) {
    printf("Missing a ']' to match loop sequence at program end: '%s'\n", _program.c_str());
    exit(1);
  }

  return BFAST(std::move(node_lists.top()));
}

void BFOptimizer::apply_run_length_encoding(BFNodeList* list) {
  // We want to visit all nodes in the AST and detect if there are consecutive
  // nodes with the same kind. If so, they should be "collapsed" into a single
  // node, having a count set to the number of times it should be executed.

  // Right now the nodes are in a vector. This means that when we take a pass
  // over the list and remove nodes, we'd potentially have to shift a lot of
  // elements in that list. A better approach here would be a linked-list, which
  // would allow us to "clip" off nodes much faster. The downside of a linked-list
  // is that memory is scattered across the heap, whilst a vector holds memory
  // reasonably local (good for cache-efficiency).

  BFNode* last_node = nullptr;
  for (size_t i = 0; i < list->size(); i++) {
    BFNode* current = list->at(i);

    if (current->kind() == BFNodeKind::Loop) {
      BFLoopNode* loop_node = static_cast<BFLoopNode*>(current);
      apply_run_length_encoding(loop_node->nodes());

      last_node = nullptr;
    } else if (last_node != nullptr && last_node->kind() == current->kind()) {
      // Increment the count of the last node
      BFCountNode* leaf_node = static_cast<BFCountNode*>(last_node);
      leaf_node->increment_count();

      // Delete the node's underlying memory and then remove the dangling pointer
      // from the list
      delete current;
      list->erase(list->begin() + i);

      // Decrement since we just removed an element, and we'll increment at the
      // end of the loop
      i--;
    } else {
      last_node = current;
    }
  }
}

void BFOptimizer::detect_clear_cell(BFNodeList* list) {
  for (size_t i = 0; i < list->size(); i++) {
    BFNode* current = list->at(i);
    if (current->kind() == BFNodeKind::Loop) {
      BFLoopNode* list_node = static_cast<BFLoopNode*>(current);
      if (list_node->nodes()->size() == 1 &&
          list_node->nodes()->at(0)->kind() == BFNodeKind::ByteDec) {
        delete list_node;

        list->at(i) = new BFClearNode();
      } else {
        detect_clear_cell(list_node->nodes());
      }
    }
  }
}

BFMemory::BFMemory()
  : _data(MemorySize),
    _data_pointer(0) {}

void BFMemory::increment_dp(size_t n) {
  _data_pointer += n;
  const size_t new_size = _data_pointer + 1;
  if (new_size > _data.size()) {
    _data.resize(new_size);
  }
}

void BFMemory::decrement_dp(size_t n) {
  if (_data_pointer != 0) {
    _data_pointer -= n;
  }
}

void BFMemory::increment_byte(uint8_t n) { _data.at(_data_pointer) += n; }

void BFMemory::decrement_byte(uint8_t n) { _data.at(_data_pointer) -= n; }

uint8_t* BFMemory::current_data_addr() { return _data.data() + _data_pointer; }

void BFMemory::update_data_pointer(uint8_t* new_data_addr) { _data_pointer = new_data_addr - _data.data(); }

void BFMemory::print_data() { printf("%c",_data.at(_data_pointer)); }

void BFMemory::set_data(uint8_t input) { _data.at(_data_pointer) = input; }

uint8_t BFMemory::current_data() { return _data.at(_data_pointer); }

uint8_t BFMemory::data_at(size_t i) { return _data.at(i); }

BFInterpreter::BFInterpreter(BFMemory* memory, bool enable_jit, std::function<void(BFLoopNode*)> profile_callback)
  : _memory(memory),
    _profile_callback(profile_callback),
    _enable_jit(enable_jit) {}

void BFInterpreter::interpret_node(BFNode* node) {
  // A loop node is a bit more involved so handle it first
  if (node->kind() == BFNodeKind::Loop) {
    BFLoopNode* loop_node = as_loop_node(node);

    while (_memory->current_data() != 0) {
      if (_enable_jit) {
        BFCompiledMethod* compiled_method = loop_node->profile()->compiled_method();
        if (compiled_method != nullptr) {
          uint8_t* new_data_addr = (*compiled_method->method())(_memory->current_data_addr());
          _memory->update_data_pointer(new_data_addr);
          break;
        }
      }

      // Interpret the loop node's children
      for (BFNode* n : *loop_node->nodes()) {
        interpret_node(n);
      }

      if (_enable_jit) {
        _profile_callback(loop_node);
      }
    }

    return;
  }

  BFCountNode* leaf_node = static_cast<BFCountNode*>(node);
  const uint8_t n = leaf_node->count();

  switch (node->kind()) {
    case BFNodeKind::DpInc:
      _memory->increment_dp(n);
      break;
    case BFNodeKind::DpDec:
      _memory->decrement_dp(n);
      break;
    case BFNodeKind::ByteInc:
      _memory->increment_byte(n);
      break;
    case BFNodeKind::ByteDec:
      _memory->decrement_byte(n);
      break;
    case BFNodeKind::DpOutput:
      for (size_t i = 0; i < n; i++) {
        _memory->print_data();
      }
      break;
    case BFNodeKind::DpInput:
      _memory->set_data((uint8_t)getchar());
      break;
    case BFNodeKind::Loop:
      // The loop node is already handled above
      assert(false);
      break;
    case BFNodeKind::Clear:
      _memory->set_data(0);
      break;
  }
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

void BFCompiler::compile_node_list(BFNodeList* node_list) {
  // The argument is in rdi (pointer to the data array), but we move it to rsi
  // immediately since the syscalls (read and write) expect to have the memory
  // location to print from there, so we don't have to juggle registers.
  Assembler::Register data_array_reg = Assembler::Register::SI;

  // Arguments for syscalls are:
  //  1. sycall number in rax
  //  2. file handle in rdi (stdin/stdout)
  //  3. pointer to memory in rsi (our data pointer live there so no need to do anything)
  //  4. number of bytes in rdx

  for (BFNode* n : *node_list) {
    if (n->kind() == BFNodeKind::DpInc) {
      _assembler.add_imm32_reg64((uint32_t)as_count_node(n)->count(), data_array_reg);
    } else if (n->kind() == BFNodeKind::DpDec) {
      _assembler.sub_imm32_reg64((uint32_t)as_count_node(n)->count(), data_array_reg);
    } else if (n->kind() == BFNodeKind::ByteInc) {
      _assembler.add_imm8_mem8(as_count_node(n)->count(), data_array_reg);
    } else if (n->kind() == BFNodeKind::ByteDec) {
      _assembler.sub_imm8_mem8(as_count_node(n)->count(), data_array_reg);
    } else if (n->kind() == BFNodeKind::DpOutput) {
      _assembler.mov_imm32_reg32(1, Assembler::Register::A);
      _assembler.mov_imm32_reg32(1, Assembler::Register::DI);
      _assembler.mov_imm32_reg32(1, Assembler::Register::D);
      _assembler.syscall();
    } else if (n->kind() == BFNodeKind::DpInput) {
      _assembler.mov_imm32_reg32(0, Assembler::Register::A);
      _assembler.mov_imm32_reg32(0, Assembler::Register::DI);
      _assembler.mov_imm32_reg32(1, Assembler::Register::D);
      _assembler.syscall();
    } else if (n->kind() == BFNodeKind::Loop) {
      // We don't care about the result here as the inner loop's body is emitted
      // into our "outer" loop body
      (void)compile_loop_node((BFLoopNode*)n, false);
    } else if (n->kind() == BFNodeKind::Clear) {
      _assembler.mov_imm8_mem8(0, data_array_reg);
    }
  }
}

BFCompiler::BFCompiler(bool start_compiler_thread)
  : _code_blob(CodeBlobSize),
    _assembler(&_code_blob),
    _compile_queue_lock(),
    _compile_queue_cv(),
    _compile_queue(),
    _compiler_thread_running(true),
    _compiler_thread(nullptr) {

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

void BFCompiler::send_compilation_request(BFLoopNode* loop_node) {
  assert(_compiler_thread != nullptr && _compiler_thread_running);

  {
    std::unique_lock lock(_compile_queue_lock);
    _compile_queue.push(loop_node);
  }

  // Notify thread
  _compile_queue_cv.notify_one();
}

BFCompiledMethod* BFCompiler::compile_loop_node(BFLoopNode* loop_node, bool is_entry) {
  if (loop_node->profile()->compiled_method() != nullptr) {
    // A loop should only be compiled as en entry loop once
    assert(!is_entry);
  }

  void* entrypoint = _code_blob.get_current_entrypoint();

  if (is_entry) {
    // The argument is in rdi (pointer to the data array), but we move it to rsi
    // immediately since the syscalls (read and write) expect to have the memory
    // location to print from there, so we don't have to juggle registers.
    _assembler.mov_reg64_to_reg64(Assembler::Register::DI, DataArrayRegister);
  }

  void* zero_check_start = _code_blob.get_current_entrypoint();

  // Loop start/end condition check: If the check is true, i.e., the data at the
  // current data pointer is 0, then we don't jump and go straight to the return
  _assembler.mov_mem8_reg8(DataArrayRegister, Assembler::Register::A);
  _assembler.test_reg8(Assembler::Register::A);

  void* backpatch_jmp_addr = nullptr;

  if (is_entry) {
    // If this is the entry loop, just emit a return instruction to return back
    _assembler.jnz_rel32(4);
    _assembler.mov_reg64_to_reg64(DataArrayRegister, Assembler::Register::A);
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
    if (debug_mode) {
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

BFCompiledMethod* BFCompiler::compile_aot(BFNodeList* node_list) {
  void* entrypoint = _code_blob.get_current_entrypoint();

  _assembler.mov_reg64_to_reg64(Assembler::Register::DI, DataArrayRegister);

  compile_node_list(node_list);

  // Return from the function
  _assembler.ret_near();

  void* method_end = _code_blob.get_current_entrypoint();
  size_t method_size = (uintptr_t)method_end - (uintptr_t)entrypoint;

  BFCompiledMethod* compiled_method = new BFCompiledMethod((CompiledMethod)entrypoint, method_size);
  if (debug_mode) {
    compiled_method->print_method(false);
  }

  return compiled_method;
}

BFProgramExecutor::BFProgramExecutor(BFAST* ast, BFProgramExecutor::ExecutionMode execution_mode)
  : _execution_mode(execution_mode),
    _ast(ast),
    _memory(),
    _interpreter(&_memory, execution_mode == ExecutionMode::JustInTime, [this](BFLoopNode* loop_node) { profile_loop_node(loop_node); }),
    _compiler(execution_mode == ExecutionMode::JustInTime) {}

BFProgramExecutor::ExecutionMode BFProgramExecutor::execution_mode() { return _execution_mode; }

void BFProgramExecutor::profile_loop_node(BFLoopNode* loop_node) {
  loop_node->profile()->inc_execution_count();

  // Check if we should send a compilation requets for this loop to be compiled
  if (loop_node->nodes()->size() > CompileThresholdLoopSize ||
      loop_node->profile()->execution_count() > CompileThresholdExecutionCount) {
    if (loop_node->profile()->mark_compilation_queued()) {
      _compiler.send_compilation_request(loop_node);
    }
  }
}

void BFProgramExecutor::execute() {
  if (_execution_mode == ExecutionMode::AheadOfTime) {
    BFCompiledMethod* compiled_method = _compiler.compile_aot(_ast->nodes());
    (void)(*compiled_method->method())(_memory.current_data_addr());
    delete compiled_method;
    return;
  }

  for (BFNode* n : *_ast->nodes()) {
    _interpreter.interpret_node(n);
  }
}

void BFProgramExecutor::debug_print() {
  printf("Data state:\n");
  for (size_t i = 0; i < 10; i++) {
    printf("data[%zu]: %d\n", i, _memory.data_at(i));
  }
}

struct CLIOptions {
  bool debug = false;
  BFProgramExecutor::ExecutionMode execution_mode = BFProgramExecutor::ExecutionMode::JustInTime;
  std::string program;
};

static void print_usage(const char* executable) {
  printf("Usage: %s [--debug] [--aot|--jit|--interp] <program>\n", executable);
  printf("Execution mode defaults to JIT when not specified\n");
}

static bool parse_args(int argc, const char** argv, CLIOptions& opts) {
  int i = 1;

  if (argc < 2) {
    return false;
  }

  if (strcmp(argv[i], "--debug") == 0) {
    opts.debug = true;
    i++;
  }

  if (i < argc && strcmp(argv[i], "--aot") == 0) {
    opts.execution_mode = BFProgramExecutor::ExecutionMode::AheadOfTime;
    i++;
  } else if (i < argc && strcmp(argv[i], "--jit") == 0) {
    opts.execution_mode = BFProgramExecutor::ExecutionMode::JustInTime;
    i++;
  } else if (i < argc && strcmp(argv[i], "--interp") == 0) {
    opts.execution_mode = BFProgramExecutor::ExecutionMode::Interpreter;
    i++;
  }

  if (i >= argc) {
    return false;
  }

  opts.program = argv[i];
  return true;
}

int main(int argc, const char** argv) {
  CLIOptions opts;
  if (!parse_args(argc, argv, opts)) {
    print_usage(argv[0]);
    return 1;
  }

  debug_mode = opts.debug;

  BFParser parser(opts.program);
  BFAST ast = parser.parse();

  if (debug_mode) {
    ast.print();
  }

  BFOptimizer::apply_run_length_encoding(ast.nodes());
  BFOptimizer::detect_clear_cell(ast.nodes());

  if (debug_mode) {
    printf("After optimization:\n");
    ast.print();
  }

  BFProgramExecutor executor(&ast, opts.execution_mode);
  executor.execute();

  if (debug_mode) {
    executor.debug_print();
  }
}
