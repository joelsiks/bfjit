#ifndef BFNODES_HPP
#define BFNODES_HPP

#include <atomic>
#include <cstdlib>
#include <cstdint>
#include <vector>

class BFCompiledMethod;
class BFNode;

typedef std::vector<BFNode*> BFNodeList;

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

#endif // BFNODES_HPP
