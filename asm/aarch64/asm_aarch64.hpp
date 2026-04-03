#ifndef ASM_AARCH64_HPP
#define ASM_AARCH64_HPP

#include <cstdint>
#include <cstdlib>

#include "../codeblob.hpp"

class AssemblerAArch64 {
public:
  // Used to encode Registers
  enum class Register : uint8_t {
  };

private:
  CodeBlob* _code_blob;

public:
  AssemblerAArch64(CodeBlob* code_blob);
};

#endif // ASM_AACH64_HPP
