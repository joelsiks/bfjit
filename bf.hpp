#ifndef BF_HPP
#define BF_HPP

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "asm.hpp"

class BFNode;
class BFProgramExecutor;

typedef std::vector<BFNode*> NodeList;

// A compiled method takes in the "state" of the VM, that is:
// 1) A pointer to the "data"-array
// 2) A pointer to the current data pointer, i.e., a pointer to a number
//    which is used to index the data-array
typedef void (*JITFn)(uint8_t*, uint32_t*);

class BFCompiledMethod {
private:
  const JITFn _compiled_method;
  const size_t _bytes;

public:
  BFCompiledMethod(JITFn compiled_method, size_t bytes);

  JITFn method() const;
  size_t bytes() const;

  void print_method(bool print_address) const;
};

enum class BFNodeKind {
  // Associated with input characters
  DpInc,
  DpDec,
  ByteInc,
  ByteDec,
  DpOutput,
  DpInput,
  Loop,

  // Extensions
  Clear,
  MoveFrom,
};

const char* node_kind_name(BFNodeKind kind);

class BFNode {
protected:
  BFNodeKind _kind;

public:
  BFNode(BFNodeKind kind);
  virtual ~BFNode();
  BFNodeKind kind() const;

  virtual void print(size_t indentation) const;
};

class BFCountNode : public BFNode {
private:
  uint8_t _count;

public:
  BFCountNode(BFNodeKind kind);

  void print(size_t indentation) const override;

  void increment_count();
  uint8_t count() const;
};

class BFLoopNode : public BFNode {
private:
  NodeList _nodes;
  BFCompiledMethod* _compiled_method;

public:
  BFLoopNode(NodeList children);
  ~BFLoopNode() override;

  NodeList* nodes();
  BFCompiledMethod* compiled_method();
  void set_compiled_method(BFCompiledMethod* compiled_method);

  void print(size_t indentation) const override;
};

class BFClearNode : public BFNode {
public:
  BFClearNode();
};

class BFAST {
private:
  NodeList _nodes;

public:
  BFAST(NodeList&& nodes);
  ~BFAST();

  NodeList* nodes();
  void print() const;
};

// Cast utility
inline BFCountNode* as_count_node(BFNode* n) { return static_cast<BFCountNode*>(n); }
inline BFLoopNode* as_loop_node(BFNode* n) { return static_cast<BFLoopNode*>(n); }

class BFParser {
private:
  std::string _program;
  size_t _num_data_slots;

public:
  BFParser(const std::string& program);

  BFAST parse();
};

class BFOptimizer {
public:
  static void apply_run_length_encoding(NodeList* list);
  static void detect_clear_cell(NodeList* list);

  // TODO: Some optimization pass that detects whether to pass the data pointer
  // by reference or by value to the compiled method. By reference means we need
  // to load it from memory into a register every time we need it, and by value
  // we read it when we enter the compiled method, work on or local copy, and then
  // restore it when exiting.
};

class BFInterpreter {
private:
  std::vector<uint8_t>* _data;
  uint32_t* _data_pointer;

  void increment_dp(uint32_t n);
  void decrement_dp(uint32_t n);

  void increment_byte(uint8_t n);
  void decrement_byte(uint8_t n);

  void print_data();
  void set_data(uint8_t input);
  uint8_t current_data();

public:
  BFInterpreter(std::vector<uint8_t>* data, uint32_t* data_pointer);

  void interpret_node(BFNode* node);
};

class BFCompiler {
private:
  CodeBlob _code_blob;
  Assembler _assembler;

public:
  BFCompiler();

  void compile_list_node(BFLoopNode* node, bool is_entry);
};

class BFProgramExecutor {
public:
  enum class ExecutionMode {
    AheadOfTime,
    Interpreter,
    JustInTime
  };

private:
  ExecutionMode _execution_mode;
  BFAST* _ast;
  std::vector<uint8_t> _data;
  uint32_t _data_pointer;

  BFInterpreter _interpreter;
  BFCompiler _compiler;

  size_t _current_input_idx;
  const std::string& _input;

public:

  BFProgramExecutor(BFAST* ast, const std::string& program_input, ExecutionMode execution_mode);

  void execute();

  void debug_print();
};

#endif // BF_HPP
