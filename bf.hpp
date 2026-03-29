#ifndef BF_HPP
#define BF_HPP

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "asm.hpp"

class BFNode;
class BFProgramExecutor;

typedef std::vector<BFNode*> BFNodeList;

// A compiled method takes in a "compressed" state of the VM, representing
// both the array and index with a single pointer.
typedef uint8_t* (*CompiledMethod)(uint8_t*);

class BFCompiledMethod {
private:
  const CompiledMethod _compiled_method;
  const size_t _bytes;

public:
  BFCompiledMethod(CompiledMethod compiled_method, size_t bytes);

  CompiledMethod method() const;
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

  // Extension(s)
  Clear,
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

struct BFLoopProfile {
  size_t _execution_count;
  std::atomic<BFCompiledMethod*> _compiled_method;
  std::atomic<bool> _compilation_queued;

public:
  BFLoopProfile();

  size_t execution_count();
  void inc_execution_count();

  bool mark_compilation_queued();

  void set_compiled_method(BFCompiledMethod* compiled_method);
  BFCompiledMethod* compiled_method();
};

class BFLoopNode : public BFNode {
private:
  BFNodeList _nodes;
  BFLoopProfile _profile;

public:
  BFLoopNode(BFNodeList children);
  ~BFLoopNode() override;

  BFNodeList* nodes();
  BFLoopProfile* profile();

  void print(size_t indentation) const override;
};

class BFClearNode : public BFNode {
public:
  BFClearNode();
};

class BFAST {
private:
  BFNodeList _nodes;

public:
  BFAST(BFNodeList&& nodes);
  ~BFAST();

  BFNodeList* nodes();
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

class BFCompiler {
private:

  static const size_t CodeBlobSize = 2 * 1024 * 1024; // 2MB
  static const Assembler::Register DataArrayRegister = Assembler::Register::SI;

  CodeBlob _code_blob;
  Assembler _assembler;

  std::mutex _compile_queue_lock;
  std::condition_variable _compile_queue_cv;
  std::queue<BFLoopNode*> _compile_queue;
  std::atomic<bool> _compiler_thread_running;
  std::unique_ptr<std::thread> _compiler_thread;

  void compiler_thread_fn();

  void compile_node_list(BFNodeList* node_list);

public:
  BFCompiler(bool start_compiler_thread);
  ~BFCompiler();

  void send_compilation_request(BFLoopNode* loop_node);

  BFCompiledMethod* compile_loop_node(BFLoopNode* loop_node, bool is_entry);
  BFCompiledMethod* compile_aot(BFNodeList* node_list);
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
  BFCompiler _compiler;

public:
  BFProgramExecutor(BFAST* ast, ExecutionMode execution_mode);

  ExecutionMode execution_mode();

  void profile_loop_node(BFLoopNode* loop_node);
  void execute();

  void debug_print();
};

#endif // BF_HPP
