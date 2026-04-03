#ifndef CODEBLOB_HPP
#define CODEBLOB_HPP

#include <cstdint>
#include <cstdlib>

class CodeBlob {
  // TODO: Revisit the storage type and make sure casts work
  uint8_t* _code_blob;
  size_t _blob_size;
  size_t _current_byte_in_blob;

  static uint8_t* allocate_memory(size_t size);

public:
  CodeBlob(size_t size);

  void* get_current_entrypoint();

  void emit_byte(uint8_t byte);
  void emit_dword(uint32_t dword);
};

#endif // CODEBLOB_HPP
