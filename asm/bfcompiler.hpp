#ifndef COMPILER_HPP
#define COMPILER_HPP

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <queue>

#include "codeblob.hpp"
#include "../bfnodes.hpp"

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

class BFCompiler {
protected:
  std::mutex _compile_queue_lock;
  std::condition_variable _compile_queue_cv;
  std::queue<BFLoopNode*> _compile_queue;
  std::atomic<bool> _compiler_thread_running;
  std::unique_ptr<std::thread> _compiler_thread;

  CodeBlob _code_blob;

  void compiler_thread_fn();

  virtual void compile_node_list(BFNodeList* node_list) = 0;

public:
  BFCompiler(size_t code_blob_size, bool start_compiler_thread);
  ~BFCompiler();

  static BFCompiler* create(bool is_jit);

  void send_compilation_request(BFLoopNode* loop_node);

  virtual BFCompiledMethod* compile_loop_node(BFLoopNode* loop_node, bool is_entry) = 0;
  virtual BFCompiledMethod* compile_aot(BFNodeList* node_list) = 0;
};

#endif // COMPILER_HPP
