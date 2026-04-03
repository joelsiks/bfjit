
#include <cstdio>
#include <sys/mman.h>

#include "codeblob.hpp"

uint8_t* CodeBlob::allocate_memory(size_t size) {
  void* result = mmap(nullptr,
                      size,
                      PROT_EXEC | PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS,
                      -1,
                      0);

  if (result == MAP_FAILED) {
    return nullptr;
  }

  return static_cast<uint8_t*>(result);
}

CodeBlob::CodeBlob(size_t size)
  : _code_blob(allocate_memory(size)),
    _blob_size(size),
    _current_byte_in_blob(0) {}

void* CodeBlob::get_current_entrypoint() {
  return (void*)(_code_blob + _current_byte_in_blob);
}

void CodeBlob::emit_byte(uint8_t byte) {
  *(_code_blob + _current_byte_in_blob) = byte;

  _current_byte_in_blob += 1;

  if (_current_byte_in_blob >= _blob_size) {
    printf("Writing out of bounds in blob. size %zu, idx: %zu\n", _blob_size, _current_byte_in_blob);
    exit(1);
  }
}

void CodeBlob::emit_dword(uint32_t dword) {
  *((uint32_t*)(_code_blob + _current_byte_in_blob)) = dword;

  _current_byte_in_blob += sizeof(uint32_t);

  if (_current_byte_in_blob >= _blob_size) {
    printf("Writing out of bounds in blob. size %zu, idx: %zu\n", _blob_size, _current_byte_in_blob);
    exit(1);
  }
}
