#include <cstdio>
#include <iostream>
#include <cassert>

#include <stack>

#include "bf.hpp"

BFCompiledMethod::BFCompiledMethod(JITFn compiled_method, size_t bytes)
  : _compiled_method(compiled_method),
    _bytes(bytes) {}

JITFn BFCompiledMethod::method() const { return _compiled_method; }

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
  for (int i = 0; i < indentation; i++) std::cout << " ";
  std::cout << static_cast<char>(kind()) << std::endl;
}

BFNodeLeaf::BFNodeLeaf(BFNodeKind kind) : BFNode(kind), _count(1) {}

void BFNodeLeaf::print(size_t indentation) const {
  for (int i = 0; i < indentation; i++) std::cout << " ";
  std::cout << static_cast<char>(kind()) << " (" << _count << ")" << std::endl;
}

void BFNodeLeaf::increment_count() { _count++; }
uint8_t BFNodeLeaf::count() const { return _count; }

BFNodeList::BFNodeList(NodeList children)
  : BFNode(BFNodeKind::LOOP),
    _nodes(children),
    _compiled_method(nullptr) {}

BFNodeList::~BFNodeList() {
  for (const BFNode* n : _nodes) {
    delete n;
  }
}

NodeList* BFNodeList::nodes() { return &_nodes; }

BFCompiledMethod* BFNodeList::compiled_method() { return _compiled_method; }

void BFNodeList::set_compiled_method(BFCompiledMethod* compiled_method) { _compiled_method = compiled_method; }

void BFNodeList::print(size_t indentation) const {
  BFNode::print(indentation);
  for (const BFNode* n : _nodes) {
    n->print(indentation + 1);
  }
  for (int i = 0; i < indentation; i++) std::cout << " ";
  std::cout << ']' << std::endl;
}

BFAST::BFAST(NodeList&& nodes) : _nodes(nodes) {}

BFAST::~BFAST() {
  for (const BFNode* n : _nodes) {
    delete n;
  }
}

NodeList* BFAST::nodes() { return &_nodes; }

void BFAST::print() const {
  for (const BFNode* n : _nodes) {
    n->print(0);
  }
}

bool BFParser::is_valid_character(char c) {
  switch (c) {
    case static_cast<char>(BFNodeKind::INC_DP):
    case static_cast<char>(BFNodeKind::DEC_DP):
    case static_cast<char>(BFNodeKind::INC_BYTE):
    case static_cast<char>(BFNodeKind::DEC_BYTE):
    case static_cast<char>(BFNodeKind::OUTPUT_DP):
    case static_cast<char>(BFNodeKind::INPUT_DP):
    case static_cast<char>(BFNodeKind::LOOP):
    case ']': // Used as a parsing signal
      return true;
    default:
      return false;
  };
}

BFParser::BFParser(const std::string& program) : _program(program) { }

BFAST BFParser::parse() {
  // Stack based lists
  std::stack<std::vector<BFNode*>> node_lists;
  node_lists.emplace();

  for (char c : _program) {
    if (!is_valid_character(c)) {
      continue;
    }

    if (c == '[') {
      // Start a new loop, push a new list of nodes to the "stack"
      node_lists.emplace();
    } else if (c == ']') {
      // Detect un-matching loops
      if (node_lists.size() == 1) {
        printf("Invalid loop end '%s'\n", _program.c_str());
        exit(1);
      }

      // End of a loop, pop the list from the stack onto the new node
      BFNodeList* node = new BFNodeList(node_lists.top());
      node_lists.pop();
      node_lists.top().push_back(node);
    } else {
      // Any other node than a loop start/end. Push a new node
      BFNodeKind kind = static_cast<BFNodeKind>(c);
      BFNodeLeaf* node = new BFNodeLeaf(kind);
      node_lists.top().push_back(node);
    }
  }

  // Detect un-matching loops
  if (node_lists.size() != 1) {
    printf("Missing a ']' to match loop sequence at program end: '%s'\n", _program.c_str());
    exit(1);
  }

  return BFAST(std::move(node_lists.top()));
}

void BFOptimizer::apply_run_length_encoding(NodeList* list) {
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

    if (current->kind() == BFNodeKind::LOOP) {
      BFNodeList* loop_node = static_cast<BFNodeList*>(current);
      apply_run_length_encoding(loop_node->nodes());

      last_node = nullptr;
    } else if (last_node != nullptr && last_node->kind() == current->kind()) {
      // Increment the count of the last node
      BFNodeLeaf* leaf_node = static_cast<BFNodeLeaf*>(last_node);
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

void BFOptimizer::constant_folding(NodeList* list) {
  // TODO: Implement this
}

BFInterpreter::BFInterpreter(BFProgramExecutor* executor)
  : _executor(executor) {}

void BFInterpreter::interpret_node(BFNode* node) {
  // Handle loop node separately
  if (node->kind() == BFNodeKind::LOOP) {
    BFNodeList* loop_node = static_cast<BFNodeList*>(node);

    while (_executor->current_data() != 0) {
      for (BFNode* n : *loop_node->nodes()) {
        interpret_node(n);
      }
    }

    return;
  }

  BFNodeLeaf* leaf_node = static_cast<BFNodeLeaf*>(node);
  const uint8_t n = leaf_node->count();

  switch (node->kind()) {
    case BFNodeKind::INC_DP:
      _executor->increment_dp(n);
      break;
    case BFNodeKind::DEC_DP:
      _executor->decrement_dp(n);
      break;
    case BFNodeKind::INC_BYTE:
      _executor->increment_byte(n);
      break;
    case BFNodeKind::DEC_BYTE:
      _executor->decrement_byte(n);
      break;
    case BFNodeKind::OUTPUT_DP:
      for (size_t i = 0; i < n; i++) {
        _executor->print_data();
      }
      break;
    case BFNodeKind::INPUT_DP:
      // No input string from the user, prompt the user for a character
      _executor->set_data((uint8_t)getchar());
      break;
    case BFNodeKind::LOOP:
      assert(false); // Should not reach here...
      break;
  }
}

BFCompiler::BFCompiler()
  : _code_blob(2 * 1024),
    _assembler(&_code_blob) {}

void BFCompiler::compile_list_node(BFNodeList* node) {
  if (node->compiled_method() != nullptr) {
    // Node already has a compiled method
    return;
  }

  void* entrypoint = _code_blob.get_current_entrypoint();

  // The arguments are in RDI (pointer to the data array) and RSI (a pointer to the data pointer)
  Assembler::Register data_array_reg = Assembler::Register::DI;
  Assembler::Register data_pointer_reg = Assembler::Register::SI;

  void* zero_check_start = _code_blob.get_current_entrypoint();

  // Loop start/end condition check: If the check is true, i.e., the data at the
  // current data pointer is 0, then we don't jump and go straight to the return
  _assembler.mov_mem64_to_reg64(data_pointer_reg, Assembler::Register::A); // mov rax, [rsi]
  _assembler.mov_mem8_reg8disp_to_reg8(Assembler::Register::A, data_array_reg, Assembler::Register::A), // mov rax, [rdi+rax]
  _assembler.test_reg8(Assembler::Register::A);
  _assembler.jnz_rel32(1);

  // Return block
  _assembler.ret_near();

  for (BFNode* n : *node->nodes()) {
    BFNodeLeaf* node_leaf = static_cast<BFNodeLeaf*>(n);
    if (n->kind() == BFNodeKind::DEC_DP) {
      _assembler.sub_imm8_mem8(node_leaf->count(), data_pointer_reg);
    } else if (n->kind() == BFNodeKind::INC_DP) {
      _assembler.add_imm8_mem8(node_leaf->count(), data_pointer_reg);
    } else if (n->kind() == BFNodeKind::DEC_BYTE) {
      _assembler.mov_mem64_to_reg64(data_pointer_reg, Assembler::Register::A);
      _assembler.sub_imm8_mem8(node_leaf->count(), data_array_reg, Assembler::Register::A);
    } else if (n->kind() == BFNodeKind::INC_BYTE) {
      _assembler.mov_mem64_to_reg64(data_pointer_reg, Assembler::Register::A);
      _assembler.add_imm8_mem8(node_leaf->count(), data_array_reg, Assembler::Register::A);
    } else if (n->kind() == BFNodeKind::OUTPUT_DP) {
      _assembler.push_reg(data_array_reg);
      _assembler.push_reg(data_pointer_reg);

      // Address is stored in rsi
      _assembler.mov_reg64_to_reg64(Assembler::Register::DI, Assembler::Register::SI);

      // The number of the syscall is stored in rax
      _assembler.mov_imm32_reg32(1, Assembler::Register::A);
      // File handle is stored in rdi
      _assembler.mov_imm32_reg32(1, Assembler::Register::DI);
      // Number of bytes is stored in rdx
      _assembler.mov_imm32_reg32(1, Assembler::Register::D);

      _assembler.syscall();

      _assembler.pop_reg(data_pointer_reg);
      _assembler.pop_reg(data_array_reg);
    } else if (n->kind() == BFNodeKind::INPUT_DP) {
      _assembler.push_reg(data_array_reg);
      _assembler.push_reg(data_pointer_reg);

      // Address is stored in rsi
      _assembler.mov_reg64_to_reg64(Assembler::Register::DI, Assembler::Register::SI);

      // The number of the syscall is stored in rax
      _assembler.mov_imm32_reg32(0, Assembler::Register::A);
      // File handle is stored in rdi
      _assembler.mov_imm32_reg32(0, Assembler::Register::DI);
      // Number of bytes is stored in rdx
      _assembler.mov_imm32_reg32(1, Assembler::Register::D);

      _assembler.syscall();

      _assembler.pop_reg(data_pointer_reg);
      _assembler.pop_reg(data_array_reg);
    }
  }

  void* loop_body_end = _code_blob.get_current_entrypoint();
  int relative_offset_to_zero_check = (uintptr_t)loop_body_end - (uintptr_t)zero_check_start;
  relative_offset_to_zero_check += 5; // For the jmp instruction itself

  _assembler.jmp_imm32(-relative_offset_to_zero_check);

  void* method_end = _code_blob.get_current_entrypoint();
  size_t method_size = (uintptr_t)method_end - (uintptr_t)entrypoint;

  BFCompiledMethod* compiled_method = new BFCompiledMethod((JITFn)entrypoint, method_size);
  node->set_compiled_method(compiled_method);

  compiled_method->print_method(false);
}

BFProgramExecutor::BFProgramExecutor(BFAST* ast, const std::string& program_input)
  : _interpreter(this),
    _compiler(),
    _ast(ast),
    _data_pointer(0),
    _data(10, 0),
    _current_input_idx(0),
    _input(program_input) {}

void BFProgramExecutor::execute() {
  for (BFNode* n : *_ast->nodes()) {

    // Compiled method check
    if (n->kind() == BFNodeKind::LOOP) {
      BFNodeList* list_node = static_cast<BFNodeList*>(n);
      BFCompiledMethod* compiled_method = list_node->compiled_method();
      if (compiled_method == nullptr) {
        //_compiler.compile_list_node(list_node);
        compiled_method = list_node->compiled_method();
      }

      if (compiled_method != nullptr) {
        // If the list node had a compiled method, call it and continue to the
        // next node
        (*compiled_method->method())(_data.data(), &_data_pointer);
        continue;
      }
    }

    // Interpret the node
    _interpreter.interpret_node(n);
  }
}

void BFProgramExecutor::increment_dp(uint32_t n) {
  _data_pointer += n;
  const size_t new_size = _data_pointer + 1;
  if (new_size > _data.size()) {
    _data.resize(new_size);
  }
}

void BFProgramExecutor::decrement_dp(uint32_t n) {
  if (_data_pointer != 0) {
    _data_pointer -= n;
  }
}

void BFProgramExecutor::increment_byte(uint8_t n) { _data[_data_pointer] += n; }

void BFProgramExecutor::decrement_byte(uint8_t n) { _data[_data_pointer] -= n; }

void BFProgramExecutor::print_data() { std::cout << (char)(_data[_data_pointer]) << std::flush; }

void BFProgramExecutor::set_data(uint8_t input) { _data[_data_pointer] = input; }

uint8_t BFProgramExecutor::current_data() { return _data[_data_pointer]; }

void BFProgramExecutor::debug_print() {
  printf("Data state:\n");
  for (size_t i = 0; i < _data.size(); i++) {
    printf("data[%zu]: %d\n", i, _data[i]);
  }
}

int main(int argc, const char** argv) {
  if (argc < 2) {
    printf("No input given. Usage: %s <program sequence>\n", argv[0]);
    return 1;
  }

  std::string program(argv[1]);
  std::string program_input;

  // Only set the program_input if the user has supplied it
  if (argc > 2) {
    program_input = argv[2];
  }

  BFParser parser(program);
  BFAST ast = parser.parse();
  //ast.print();

  BFOptimizer::apply_run_length_encoding(ast.nodes());
  //printf("After optimization:\n");
  //ast.print();

  BFProgramExecutor executor(&ast, program_input);
  executor.execute();
  //executor.debug_print();
}
