#ifndef CODEBLOB_X86_HPP
#define CODEBLOB_X86_HPP

#include <stdint.h>
#include <stdlib.h>

class CodeBlob {
  uint8_t* _code_blob;
  size_t _blob_size;
  size_t _current_byte_in_blob;

  static uint8_t* allocate_memory(size_t size);

public:
  CodeBlob(size_t size);

  void* get_current_entrypoint();
  void emit_byte(uint8_t byte);
};

#endif // CODEBLOB_X86_HPP
