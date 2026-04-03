#ifndef BF_HPP
#define BF_HPP

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "bfnodes.hpp"
#include "asm/bfcompiler.hpp"

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
  static void apply_run_length_encoding(BFNodeList* list);
  static void detect_clear_cell(BFNodeList* list);
};

class BFMemory {
private:
  static const size_t MemorySize = 30000;

  std::vector<uint8_t> _data;
  size_t _data_pointer;

public:
  BFMemory();

  void increment_dp(size_t n);
  void decrement_dp(size_t n);

  void increment_byte(uint8_t n);
  void decrement_byte(uint8_t n);

  uint8_t* current_data_addr();
  void update_data_pointer(uint8_t* new_data_addr);

  void print_data();
  void set_data(uint8_t input);
  uint8_t current_data();

  uint8_t data_at(size_t i);
};

class BFInterpreter {
private:
  BFMemory* _memory;
  std::function<void(BFLoopNode*)> _profile_callback;
  bool _enable_jit;

public:
  BFInterpreter(BFMemory* memory, bool enable_jit, std::function<void(BFLoopNode*)> profile_callback);

  void interpret_node(BFNode* node);
};

class BFProgramExecutor {
public:
  enum class ExecutionMode {
    AheadOfTime,
    Interpreter,
    JustInTime
  };

private:
  static const size_t CompileThresholdLoopSize = 10;
  static const size_t CompileThresholdExecutionCount = 50;

  ExecutionMode _execution_mode;
  BFAST* _ast;
  BFMemory _memory;

  BFInterpreter _interpreter;
  BFCompiler* _compiler;

public:
  BFProgramExecutor(BFAST* ast, ExecutionMode execution_mode);

  ExecutionMode execution_mode();

  void profile_loop_node(BFLoopNode* loop_node);
  void execute();

  void debug_print();
};

#endif // BF_HPP
