#ifndef ASM_AARCH64_HPP
#define ASM_AARCH64_HPP

#include <cstdint>
#include <cstdlib>

#include "../codeblob.hpp"

class AssemblerAArch64 {
public:
  // Some general notes:
  //
  // - Doing a 32-bit (w) instruction, lets say a ldrb, will zero the upper 32 bits
  //   of the register. For example, a "ldrb w1, [x0]", will zero the upper 32 bits.
  //
  // - The zero registers, XZR/WZR, always read as 0 and ignore writes.
  //
  // - X30 is used as the Link Register...
  //
  // - The ldrb (immediate) "load register byte" instruction have many different encodings:
  //   Post-index, Pre-index, Unsigned offset
  //
  // - The immediate encoding for instructions like "and" is not a simple immediate value.
  //   It uses a special compressed encoding (N, immr, imms).

  // Used to encode Registers
  enum class Register : uint8_t {
    r0  = 0,
    r1  = 1,
    r2  = 2,
    r8  = 8,

    // The zero register and the stack pointer register share the same number
    // and are mutually exclusive in certain instructions.
    zr  = 31,
    sp  = 31,
  };

  enum class BranchCondition : uint32_t {
    EQ = 0b0000,
    NE = 0b0001,
  };

  enum class Syscall : uint32_t {
    Read = 63,
    Write = 64,
  };

private:
  CodeBlob* _code_blob;

  enum class AddSubMode : uint32_t {
    Add = 0b0,
    Sub = 0b1,
  };

  enum class LoadStoreMode : uint32_t {
    Store = 0b0,
    Load  = 0b1,
  };

  enum class SizeFlag : uint32_t {
    W32 = 0b0,
    X64 = 0b1,
  };

  struct BitmaskImmediate {
    uint8_t _n;
    uint8_t _immr;
    uint8_t _imms;
  };

  static bool encode_immediate(uint64_t immediate, BitmaskImmediate& out);
  static bool encode_immediate32(uint32_t immediate, BitmaskImmediate& out);
  static uint32_t build_addsub(Register Rd, Register Rn, uint32_t imm12, AddSubMode mode);
  static uint32_t build_ldrbstrb(Register Rn, Register Rt, uint32_t imm12, LoadStoreMode mode);

public:
  AssemblerAArch64(CodeBlob* code_blob);

  void ret();
  void svc();

  void tst32bit(Register Rn, Register Rm);
  void bcond(uint32_t imm19_offset, BranchCondition bc);
  void b(int32_t imm26_offset);
  void b_backpatch(uint32_t* addr, int32_t imm26_offset);

  void orr(Register Rm, Register Rd, Register Rn);

  void mov(Register Rd, Register Rn);
  void mov(Register Rd, uint32_t imm16);

  void sub_imm12(Register Rd, Register Rn, uint32_t imm12);

  void add_imm12(Register Rd, Register Rn, uint32_t imm12);

  // Rn is the base address register. Rt is the register being loaded into or stored from.
  void strb_imm12(Register Rn, Register Rt, uint32_t imm12 = 0);

  // Rn is the base address register. Rt is the register being loaded into or stored from.
  void ldrb_imm12(Register Rn, Register Rt, uint32_t imm12 = 0);

  void and_imm12_32bit(Register Rn, Register Rd, uint32_t imm12);

  void ands(Register Rd, Register Rn, Register Rm, SizeFlag size_flag = SizeFlag::X64, uint32_t imm6_shift = 0);
};

#endif // ASM_AACH64_HPP
