#ifndef ASM_X86_HPP
#define ASM_X86_HPP

#include <cstdint>
#include <cstdlib>

#include "../codeblob.hpp"

class AssemblerX86 {
public:
  // Used to encode Registers
  enum class Register : uint8_t {
    A   = 0x0,
    C   = 0x1,
    D   = 0x2,
    B   = 0x3,
    SP  = 0x4,
    BP  = 0x5,
    SI  = 0x6,
    DI  = 0x7,
    R8  = 0x8,
    R9  = 0x9,
    R10 = 0xA,
    R11 = 0xB,
    R12 = 0xC,
    R13 = 0xD,
    R14 = 0xE,
    R15 = 0xF,
  };

  // REX (register extension) is used for extending the instruction
  enum class RexMode : uint8_t {
    B = 1 << 0, // extends ModRM.r/m, SIB.base or opcode reg field
    X = 1 << 1, // extends SIB.index field
    R = 1 << 2, // extends ModRM.reg field
    W = 1 << 3, // 64-bit operand size
  };

  // The ModRM byte is a byte that, if required, follows the opcode and specifies
  // zero, one or two operands for the instruction.
  // "mod"  "reg" "r/m"
  // 00     000   000
  //
  // The "reg" field can only ever hold a register, so depending on the instruction
  // it might be either the source or destination register.
  //
  // The mod field specifies the addressing mode for the register/memory ("r/m")
  // operand. If the field is 0b11 (Reg), the "r/m" field encodes a register,
  // otherwise a memory location, with an optional displacement.
  enum class ModRM : uint8_t {
    MemNoDisplacement = 0b00,
    Mem8bit           = 0b01,
    Mem32bit          = 0b10,
    Reg               = 0b11,
  };

  enum class ModRMExtension : uint8_t {
    ADD = 0b000,
    SUB = 0b101,
  };

  // SIB = Scaled index byte mode
  // "scale" "index" "base"
  // 00      000     000
  enum class SIBScaling : uint8_t {
    One   = 0b00,
    Two   = 0b01,
    Four  = 0b10,
    Eight = 0b11,
  };

  static const uint8_t SIBEnable = 0b100;

private:
  CodeBlob* _code_blob;

  static uint32_t twos_complement(int32_t value);

  template <typename T>
  void emit_immediate(T immediate);

  uint8_t build_modrm(ModRM mod, uint8_t reg, uint8_t rm);

public:
  AssemblerX86(CodeBlob* code_blob);

  void push_reg(Register reg);
  void pop_reg(Register reg);

  void ret_near();

  void mov_reg64_to_reg64(Register source, Register dest);
  void mov_imm32_reg32(uint32_t immediate, Register dest);
  void mov_imm8_mem8(uint8_t immediate, Register dest);
  void mov_mem8_reg8(Register source, Register dest);

  void sub_imm8_mem8(uint8_t immediate, Register dest);
  void sub_imm32_reg64(uint32_t immediate, Register dest);

  void add_imm8_mem8(uint8_t immediate, Register dest);
  void add_imm32_reg64(uint32_t immediate, Register dest);

  void test_reg8(Register reg);

  void jmp_imm32(int32_t relative_offset);
  void jnz_rel32(int32_t relative_offset);

  void jmp_imm32_backpatch(void* jmp_instr_addr, int32_t relative_offset);

  void syscall();
};

#endif // ASM_X86_HPP
