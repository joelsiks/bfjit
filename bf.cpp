#include <cstdio>
#include <cstring>
#include <cassert>
#include <stack>

#include "bf.hpp"
#include "asm/bfcompiler.hpp"


static bool debug_mode = false;
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

BFProgramExecutor::BFProgramExecutor(BFAST* ast, BFProgramExecutor::ExecutionMode execution_mode)
  : _execution_mode(execution_mode),
    _ast(ast),
    _memory(),
    _interpreter(&_memory, execution_mode == ExecutionMode::JustInTime, [this](BFLoopNode* loop_node) { profile_loop_node(loop_node); }),
    _compiler(nullptr) {

  const bool is_jit = execution_mode == BFProgramExecutor::ExecutionMode::JustInTime;
  _compiler = BFCompiler::create(is_jit);
}

BFProgramExecutor::ExecutionMode BFProgramExecutor::execution_mode() { return _execution_mode; }

void BFProgramExecutor::profile_loop_node(BFLoopNode* loop_node) {
  loop_node->profile()->inc_execution_count();

  // Check if we should send a compilation requets for this loop to be compiled
  if (loop_node->nodes()->size() > CompileThresholdLoopSize ||
      loop_node->profile()->execution_count() > CompileThresholdExecutionCount) {
    if (loop_node->profile()->mark_compilation_queued()) {
      _compiler->send_compilation_request(loop_node);
    }
  }
}

void BFProgramExecutor::execute() {
  if (_execution_mode == ExecutionMode::AheadOfTime) {
    BFCompiledMethod* compiled_method = _compiler->compile_aot(_ast->nodes());
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
