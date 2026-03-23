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

enum class BFNodeKind : char {
  DEC_DP    = '<',
  INC_DP    = '>',
  INC_BYTE  = '+',
  DEC_BYTE  = '-',
  OUTPUT_DP = '.',
  INPUT_DP  = ',',
  LOOP      = '[',
};

class BFNode {
protected:
  BFNodeKind _kind;

public:
  BFNode(BFNodeKind kind);
  virtual ~BFNode();
  BFNodeKind kind() const;

  virtual void print(size_t indentation) const;
};

class BFNodeLeaf : public BFNode {
private:
  uint8_t _count;

public:
  BFNodeLeaf(BFNodeKind kind);

  void print(size_t indentation) const override;

  void increment_count();
  uint8_t count() const;
};

class BFNodeList : public BFNode {
private:
  NodeList _nodes;
  BFCompiledMethod* _compiled_method;

public:
  BFNodeList(NodeList children);
  ~BFNodeList() override;

  NodeList* nodes();
  BFCompiledMethod* compiled_method();
  void set_compiled_method(BFCompiledMethod* compiled_method);

  void print(size_t indentation) const override;
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

class BFParser {
private:
  std::string _program;
  size_t _num_data_slots;

  static bool is_valid_character(char c);

public:
  BFParser(const std::string& program);

  BFAST parse();
};

class BFOptimizer {
public:
  static void apply_run_length_encoding(NodeList* list);
  static void constant_folding(NodeList* list);

  // TODO: Some optimization pass that detects whether to pass the data pointer
  // by reference or by value to the compiled method. By reference means we need
  // to load it from memory into a register every time we need it, and by value
  // we read it when we enter the compiled method, work on or local copy, and then
  // restore it when exiting.
};

class BFInterpreter {
private:
  BFProgramExecutor* _executor;

public:
  BFInterpreter(BFProgramExecutor* executor);

  void interpret_node(BFNode* node);
};

class BFCompiler {
private:
  CodeBlob _code_blob;
  Assembler _assembler;

public:
  BFCompiler();

  void compile_list_node(BFNodeList* node);
};

class BFProgramExecutor {
  friend class BFInterpreter;

private:
  BFInterpreter _interpreter;
  BFCompiler _compiler;

  BFAST* _ast;
  uint32_t _data_pointer;
  std::vector<uint8_t> _data;

  size_t _current_input_idx;
  const std::string& _input;

public:
  BFProgramExecutor(BFAST* ast, const std::string& program_input);

  void execute();

  void increment_dp(uint32_t n);
  void decrement_dp(uint32_t n);

  void increment_byte(uint8_t n);
  void decrement_byte(uint8_t n);

  void print_data();
  void set_data(uint8_t input);
  uint8_t current_data();

  void debug_print();
};

#endif // BF_HPP
