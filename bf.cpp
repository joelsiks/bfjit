#include <cstdio>
#include <cstring>
#include <iostream>
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
    case BFNodeKind::MoveFrom: return "MoveFrom";
    default: assert(false);
  }
};

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
  std::cout << node_kind_name(_kind) << std::endl;
}

BFCountNode::BFCountNode(BFNodeKind kind) : BFNode(kind), _count(1) {}

void BFCountNode::print(size_t indentation) const {
  for (int i = 0; i < indentation; i++) std::cout << " ";
  std::cout << node_kind_name(kind()) << " (" << (size_t)_count << ")" << std::endl;
}

void BFCountNode::increment_count() { assert(_count < (uint8_t)-1); _count++; }
uint8_t BFCountNode::count() const { return _count; }

BFLoopNode::BFLoopNode(NodeList children)
  : BFNode(BFNodeKind::Loop),
    _nodes(children),
    _compiled_method(nullptr) {}

BFLoopNode::~BFLoopNode() {
  for (const BFNode* n : _nodes) {
    delete n;
  }
}

NodeList* BFLoopNode::nodes() { return &_nodes; }

BFCompiledMethod* BFLoopNode::compiled_method() { return _compiled_method; }

void BFLoopNode::set_compiled_method(BFCompiledMethod* compiled_method) { _compiled_method = compiled_method; }

void BFLoopNode::print(size_t indentation) const {
  BFNode::print(indentation);
  for (const BFNode* n : _nodes) {
    n->print(indentation + 1);
  }
}

BFClearNode::BFClearNode()
  : BFNode(BFNodeKind::Clear) {}

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

void BFOptimizer::detect_clear_cell(NodeList* list) {
  for (size_t i = 0; i < list->size(); i++) {
    BFNode* current = list->at(i);
    if (current->kind() == BFNodeKind::Loop) {
      BFLoopNode* list_node = static_cast<BFLoopNode*>(current);
      if (list_node->nodes()->size() == 1 &&
          list_node->nodes()->at(0)->kind() == BFNodeKind::ByteDec) {
        list_node->BFLoopNode::~BFLoopNode();
        delete list_node;

        list->at(i) = new BFClearNode();
      } else {
        detect_clear_cell(list_node->nodes());
      }
    }
  }
}

void BFInterpreter::increment_dp(uint32_t n) {
  *_data_pointer += n;
  const size_t new_size = *_data_pointer + 1;
  if (new_size > _data->size()) {
    _data->resize(new_size);
  }
}

void BFInterpreter::decrement_dp(uint32_t n) {
  if (_data_pointer != 0) {
    *_data_pointer -= n;
  }
}

void BFInterpreter::increment_byte(uint8_t n) { _data->at(*_data_pointer) += n; }

void BFInterpreter::decrement_byte(uint8_t n) { _data->at(*_data_pointer) -= n; }

void BFInterpreter::print_data() { std::cout << (char)(_data->at(*_data_pointer)) << std::flush; }

void BFInterpreter::set_data(uint8_t input) { _data->at(*_data_pointer) = input; }

uint8_t BFInterpreter::current_data() { return _data->at(*_data_pointer); }


BFInterpreter::BFInterpreter(std::vector<uint8_t>* data, uint32_t* data_pointer)
  : _data(data),
    _data_pointer(data_pointer) {}

void BFInterpreter::interpret_node(BFNode* node) {
  // Handle loop node separately
  if (node->kind() == BFNodeKind::Loop) {
    BFLoopNode* loop_node = as_loop_node(node);

    while (current_data() != 0) {
      for (BFNode* n : *loop_node->nodes()) {
        interpret_node(n);
      }
    }

    return;
  }

  BFCountNode* leaf_node = static_cast<BFCountNode*>(node);
  const uint8_t n = leaf_node->count();

  switch (node->kind()) {
    case BFNodeKind::DpInc:
      increment_dp(n);
      break;
    case BFNodeKind::DpDec:
      decrement_dp(n);
      break;
    case BFNodeKind::ByteInc:
      increment_byte(n);
      break;
    case BFNodeKind::ByteDec:
      decrement_byte(n);
      break;
    case BFNodeKind::DpOutput:
      for (size_t i = 0; i < n; i++) {
        print_data();
      }
      break;
    case BFNodeKind::DpInput:
      // No input string from the user, prompt the user for a character
      set_data((uint8_t)getchar());
      break;
    case BFNodeKind::Loop:
      assert(false); // Should not reach here...
      break;
    case BFNodeKind::Clear:
      set_data(0);
      break;
    case BFNodeKind::MoveFrom:
      // TODO: Implement
      assert(false);
      break;
  }
}

BFCompiler::BFCompiler()
  : _code_blob(2 * 1024 * 1024),
    _assembler(&_code_blob) {}

void BFCompiler::compile_list_node(BFLoopNode* node, bool is_entry) {
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

  void* backpatch_jmp_addr = nullptr;

  if (is_entry) {
    // If this is the entry loop, then we just emit a return instruction to return
    // back to the VM/interpreter
    _assembler.jnz_rel32(1);
    _assembler.ret_near();
  } else {
    // If this is not the entry loop, we're inside a nested loop, so the "return"
    // condition should jump to the instruction just after the current loop. One
    // way to achieve this is to emit an instruction and then "backpatch" it to
    // jump to the "right" offset.
    // TODO: Emit jump and store to backpatch array
    _assembler.jnz_rel32(5);
    _assembler.jmp_imm32(0);
    backpatch_jmp_addr = _code_blob.get_current_entrypoint();
  }

  for (BFNode* n : *node->nodes()) {
    BFCountNode* node_leaf = static_cast<BFCountNode*>(n);
    if (n->kind() == BFNodeKind::DpInc) {
      _assembler.add_imm8_mem32((uint32_t)node_leaf->count(), data_pointer_reg);
    } else if (n->kind() == BFNodeKind::DpDec) {
      _assembler.sub_imm8_mem32((uint32_t)node_leaf->count(), data_pointer_reg);
    } else if (n->kind() == BFNodeKind::ByteInc) {
      _assembler.mov_mem64_to_reg64(data_pointer_reg, Assembler::Register::A);
      _assembler.add_imm8_mem8(node_leaf->count(), data_array_reg, Assembler::Register::A);
    } else if (n->kind() == BFNodeKind::ByteDec) {
      _assembler.mov_mem64_to_reg64(data_pointer_reg, Assembler::Register::A);
      _assembler.sub_imm8_mem8(node_leaf->count(), data_array_reg, Assembler::Register::A);
    } else if (n->kind() == BFNodeKind::DpOutput) {
      _assembler.push_reg(data_array_reg);
      _assembler.push_reg(data_pointer_reg);

      // Address is stored in rsi
      _assembler.add_mem64_reg64(Assembler::Register::SI, Assembler::Register::DI);
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
    } else if (n->kind() == BFNodeKind::DpInput) {
      _assembler.push_reg(data_array_reg);
      _assembler.push_reg(data_pointer_reg);

      // Address is stored in rsi
      _assembler.add_mem64_reg64(Assembler::Register::SI, Assembler::Register::DI);
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
    } else if (n->kind() == BFNodeKind::Loop) {
      compile_list_node((BFLoopNode*)n, false);
    } else if (n->kind() == BFNodeKind::Clear) {
      _assembler.mov_mem64_to_reg64(data_pointer_reg, Assembler::Register::A);
      _assembler.mov_imm8_mem8(0, data_array_reg, Assembler::Register::A);
    }
  }

  void* loop_body_end = _code_blob.get_current_entrypoint();
  int relative_offset_to_zero_check = (uintptr_t)loop_body_end - (uintptr_t)zero_check_start;
  relative_offset_to_zero_check += 5; // For the jmp instruction itself

  _assembler.jmp_imm32(-relative_offset_to_zero_check);

  if (is_entry) {
    // Only install the compiled method for the entry loop node
    void* method_end = _code_blob.get_current_entrypoint();
    size_t method_size = (uintptr_t)method_end - (uintptr_t)entrypoint;

    BFCompiledMethod* compiled_method = new BFCompiledMethod((JITFn)entrypoint, method_size);
    node->set_compiled_method(compiled_method);

    if (debug_mode) {
      compiled_method->print_method(false);
    }
  } else {
    assert(backpatch_jmp_addr != nullptr);

    // Calculate the jmp offset for the backpatch
    void* nested_loop_end = _code_blob.get_current_entrypoint();
    int relative_offset_to_jmp = (uintptr_t)nested_loop_end - (uintptr_t)backpatch_jmp_addr;

    _assembler.jmp_imm32_backpatch(backpatch_jmp_addr, relative_offset_to_jmp);
  }
}

BFProgramExecutor::BFProgramExecutor(BFAST* ast, const std::string& program_input)
  : _ast(ast),
    _data(30000, 0),
    _data_pointer(0),
    _interpreter(&_data, &_data_pointer),
    _compiler(),
    _current_input_idx(0),
    _input(program_input) {}

void BFProgramExecutor::execute() {
  for (BFNode* n : *_ast->nodes()) {

    // Compiled method check
    if (n->kind() == BFNodeKind::Loop) {
      BFLoopNode* loop_node = as_loop_node(n);
      BFCompiledMethod* compiled_method = loop_node->compiled_method();
      if (compiled_method == nullptr) {
        _compiler.compile_list_node(loop_node, true);
        compiled_method = loop_node->compiled_method();
      }

      if (compiled_method != nullptr) {
        // If the list node had a compiled method, call it and continue to the
        // next node
        uint8_t* data = _data.data();
        (*compiled_method->method())(data, &_data_pointer);
        continue;
      }
    }

    // Interpret the node
    _interpreter.interpret_node(n);
  }
}

void BFProgramExecutor::debug_print() {
  printf("Data state:\n");
  for (size_t i = 0; i < 10; i++) {
    printf("data[%zu]: %d\n", i, _data[i]);
  }
}

static void print_helper(const char* debug_flag, const char* executable) {
  printf("No input given. Usage: %s [%s] <program sequence>\n", debug_flag, executable);
}

int main(int argc, const char** argv) {
  const char* debug_flag = "--debug";

  if (argc < 2) {
    print_helper(debug_flag, argv[0]);
    return 1;
  }

  // Check if the first argument is the debug flag
  if (strcmp(argv[1], debug_flag) == 0) {
    // The second argument is the debug flag, check if there is a third argument or not
    if (argc < 3) {
      print_helper(debug_flag, argv[0]);
      return 1;
    }

    debug_mode = true;
  }

  const int argv_offset = debug_mode ? 1 : 0;

  std::string program(argv[1 + argv_offset]);
  std::string program_input;

  // Only set the program_input if the user has supplied it
  if (argc > (2 + argv_offset)) {
    program_input = argv[2 + argv_offset];
  }

  BFParser parser(program);
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

  BFProgramExecutor executor(&ast, program_input);
  executor.execute();
  if (debug_mode) {
    executor.debug_print();
  }
}
