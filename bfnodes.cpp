
#include <cassert>
#include <cstdio>

#include "bfnodes.hpp"
#include "asm/bfcompiler.hpp"

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

