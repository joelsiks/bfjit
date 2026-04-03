
#include <cassert>
#include <cstdio>
#include <sys/mman.h>

#include "asm_x86.hpp"

uint32_t AssemblerX86::twos_complement(int32_t value) {
  if (value >= 0) {
    return value;
  }

  uint32_t pos_value = (-value);
  uint32_t intermediate = (~pos_value) + 1;
  return intermediate;
}

template <typename T>
void AssemblerX86::emit_immediate(T immediate) {
  for (size_t i = 0; i < sizeof(T); i++) {
    _code_blob->emit_byte((uint8_t)immediate);

    if (sizeof(T) * 8 > 8) {
      immediate >>= 8;
    }
  }
}

uint8_t AssemblerX86::build_modrm(ModRM mod, uint8_t reg, uint8_t rm) {
  return (uint8_t)((uint8_t)mod << 6 | reg << 3 | rm);
}

AssemblerX86::AssemblerX86(CodeBlob* code_blob)
  : _code_blob(code_blob) {}

void AssemblerX86::push_reg(Register reg) {
  const uint8_t instruction = (0x50 + (uint8_t)reg);
  _code_blob->emit_byte(instruction);
}

void AssemblerX86::pop_reg(Register reg) {
  const uint8_t instruction = (0x58 + (uint8_t)reg);
  _code_blob->emit_byte(instruction);
};

void AssemblerX86::ret_near() {
  _code_blob->emit_byte(0xC3);
}

void AssemblerX86::mov_reg64_to_reg64(Register source, Register dest) {
  const uint8_t rex = 0x40 | (uint8_t)RexMode::W;
  const uint8_t opcode = 0x89;
  const uint8_t modrm = build_modrm(ModRM::Reg, (uint8_t)source, (uint8_t)dest);

  _code_blob->emit_byte(rex);
  _code_blob->emit_byte(opcode);
  _code_blob->emit_byte(modrm);
};

void AssemblerX86::mov_imm32_reg32(uint32_t immediate, Register dest) {
  uint8_t dest_b = (uint8_t)dest;

  const uint8_t opcode = 0xB8 + (uint8_t)dest;

  _code_blob->emit_byte(opcode);
  emit_immediate(immediate);
}

void AssemblerX86::mov_imm8_mem8(uint8_t immediate, Register dest) {
  const uint8_t opcode = 0xC6;
  const uint8_t modrm = build_modrm(ModRM::MemNoDisplacement, 0b000, (uint8_t)dest);

  _code_blob->emit_byte(opcode);
  _code_blob->emit_byte(modrm);
  emit_immediate(immediate);
}

void AssemblerX86::mov_mem8_reg8(Register source, Register dest) {
  const uint8_t opcode = 0x8A;
  const uint8_t modrm = build_modrm(ModRM::MemNoDisplacement, (uint8_t)dest, (uint8_t)source);

  _code_blob->emit_byte(opcode);
  _code_blob->emit_byte(modrm);
}

void AssemblerX86::sub_imm8_mem8(uint8_t immediate, Register dest) {
  const uint8_t opcode = 0x80;
  const uint8_t modrm = build_modrm(ModRM::MemNoDisplacement, (uint8_t)ModRMExtension::SUB, (uint8_t)dest);

  _code_blob->emit_byte(opcode);
  _code_blob->emit_byte(modrm);
  emit_immediate(immediate);
}

void AssemblerX86::sub_imm32_reg64(uint32_t immediate, Register dest) {
  const uint8_t rex = 0x40 | (uint8_t)RexMode::W;
  const uint8_t opcode = 0x81;
  const uint8_t modrm = build_modrm(ModRM::Reg, (uint8_t)ModRMExtension::SUB, (uint8_t)dest);

  _code_blob->emit_byte(rex);
  _code_blob->emit_byte(opcode);
  _code_blob->emit_byte(modrm);
  emit_immediate(immediate);
}

void AssemblerX86::add_imm8_mem8(uint8_t immediate, Register dest) {
  const uint8_t opcode = 0x80;
  const uint8_t modrm = build_modrm(ModRM::MemNoDisplacement, (uint8_t)ModRMExtension::ADD, (uint8_t)dest);

  _code_blob->emit_byte(opcode);
  _code_blob->emit_byte(modrm);
  emit_immediate(immediate);
}

void AssemblerX86::add_imm32_reg64(uint32_t immediate, Register dest) {
  const uint8_t rex = 0x40 | (uint8_t)RexMode::W;
  const uint8_t opcode = 0x81;
  const uint8_t modrm = build_modrm(ModRM::Reg, (uint8_t)ModRMExtension::ADD, (uint8_t)dest);

  _code_blob->emit_byte(rex);
  _code_blob->emit_byte(opcode);
  _code_blob->emit_byte(modrm);
  emit_immediate(immediate);
}

void AssemblerX86::test_reg8(Register reg) {
  const uint8_t opcode = 0x84;
  const uint8_t modrm = build_modrm(ModRM::Reg, (uint8_t)reg, (uint8_t)reg);

  _code_blob->emit_byte(opcode);
  _code_blob->emit_byte(modrm);
}

void AssemblerX86::jmp_imm32(int32_t relative_offset) {
  const uint8_t opcode = 0xE9;
  const uint32_t immediate = twos_complement(relative_offset);

  _code_blob->emit_byte(opcode);
  emit_immediate(immediate);
}

void AssemblerX86::jnz_rel32(int32_t relative_offset) {
  const uint8_t opcode_1 = 0x0F;
  const uint8_t opcode_0 = 0x85;
  const uint32_t immediate = twos_complement(relative_offset);

  _code_blob->emit_byte(opcode_1);
  _code_blob->emit_byte(opcode_0);
  emit_immediate(immediate);
}

void AssemblerX86::jmp_imm32_backpatch(void* jmp_instr_addr, int32_t relative_offset) {
  // jmp_instr_addr points to the end of the jmp instruction, so we need to "go
  // back" the size of the instruction, which is 5 bytes, to get to the actual
  // instruction. With that in mind, we just go back 4 bytes, since we want to
  // patch the immediate.

  const uint32_t immediate = twos_complement(relative_offset);
  char* const jmp_instr = (char*)jmp_instr_addr - 4;

  *(jmp_instr + 0) = (uint8_t)(immediate >> 0);
  *(jmp_instr + 1) = (uint8_t)(immediate >> 8);
  *(jmp_instr + 2) = (uint8_t)(immediate >> 16);
  *(jmp_instr + 3) = (uint8_t)(immediate >> 24);
}

void AssemblerX86::syscall() {
  const uint8_t opcode_1 = 0x0F;
  const uint8_t opcode_0 = 0x05;

  _code_blob->emit_byte(opcode_1);
  _code_blob->emit_byte(opcode_0);
}
