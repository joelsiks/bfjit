
#include <cassert>
#include <cstdio>
#include <sys/mman.h>

#include "asm.hpp"

uint8_t* CodeBlob::allocate_memory(size_t size) {
  void* result = mmap(nullptr, size, PROT_EXEC | PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
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

uint32_t Assembler::twos_complement(int32_t value) {
  if (value >= 0) {
    return value;
  }

  uint32_t pos_value = (-value);
  uint32_t intermediate = (~pos_value) + 1;
  return intermediate;
}

template <typename T>
void Assembler::emit_immediate(T immediate) {
  for (size_t i = 0; i < sizeof(T); i++) {
    _code_blob->emit_byte((uint8_t)immediate);

    if (sizeof(T) * 8 > 8) {
      immediate >>= 8;
    }
  }
}

uint8_t Assembler::build_modrm(ModRM mod, uint8_t reg, uint8_t rm) {
  return (uint8_t)((uint8_t)mod << 6 | reg << 3 | rm);
}

Assembler::Assembler(CodeBlob* code_blob)
  : _code_blob(code_blob) {}

void Assembler::push_reg(Register reg) {
  const uint8_t instruction = (0x50 + (uint8_t)reg);
  _code_blob->emit_byte(instruction);
}

void Assembler::pop_reg(Register reg) {
  const uint8_t instruction = (0x58 + (uint8_t)reg);
  _code_blob->emit_byte(instruction);
};

void Assembler::ret_near() {
  _code_blob->emit_byte(0xC3);
}

void Assembler::mov_reg64_to_reg64(Register source, Register dest) {
  const uint8_t rex = 0x40 | (uint8_t)RexMode::W;
  const uint8_t opcode = 0x89;
  const uint8_t modrm = build_modrm(ModRM::Reg, (uint8_t)source, (uint8_t)dest);

  _code_blob->emit_byte(rex);
  _code_blob->emit_byte(opcode);
  _code_blob->emit_byte(modrm);
};

void Assembler::mov_mem64_to_reg64(Register source, Register dest) {
  const uint8_t rex = 0x40 | (uint8_t)RexMode::W;
  const uint8_t opcode = 0x8B;
  const uint8_t modrm = build_modrm(ModRM::MemNoDisplacement, (uint8_t)dest, (uint8_t)source);

  _code_blob->emit_byte(rex);
  _code_blob->emit_byte(opcode);
  _code_blob->emit_byte(modrm);
}

void Assembler::mov_mem8_reg8disp_to_reg8(Register dest, Register base, Register index) {
  const uint8_t opcode = 0x8A;
  const uint8_t modrm = build_modrm(ModRM::MemNoDisplacement, (uint8_t)dest, SIBEnable);

  const uint8_t sib_scaling = (uint8_t)SIBScaling::One << 6;
  const uint8_t sib = (sib_scaling | ((uint8_t)index << 3) | (uint8_t)base);

  _code_blob->emit_byte(opcode);
  _code_blob->emit_byte(modrm);
  _code_blob->emit_byte(sib);
}

void Assembler::mov_imm32_reg32(uint32_t immediate, Register dest) {
  uint8_t dest_b = (uint8_t)dest;

  const uint8_t opcode = 0xB8 + (uint8_t)dest;

  _code_blob->emit_byte(opcode);
  emit_immediate(immediate);
}

void Assembler::mov_imm8_mem8(uint8_t immediate, Register base, Register index) {
  // C6 /0 ib
  const uint8_t opcode = 0xC6;
  const uint8_t modrm = build_modrm(ModRM::MemNoDisplacement, 0b000, SIBEnable);
  const uint8_t sib_scaling = (uint8_t)SIBScaling::One << 6;
  const uint8_t sib = (sib_scaling | ((uint8_t)index << 3) | (uint8_t)base);

  _code_blob->emit_byte(opcode);
  _code_blob->emit_byte(modrm);
  _code_blob->emit_byte(sib);
  emit_immediate(immediate);
}

void Assembler::sub_imm8_mem8(uint8_t immediate, Register base, Register index) {
  const uint8_t opcode = 0x80;
  const uint8_t modrm = build_modrm(ModRM::MemNoDisplacement, (uint8_t)ModRMExtension::SUB, SIBEnable);

  const uint8_t sib_scaling = (uint8_t)SIBScaling::One << 6;
  const uint8_t sib = (sib_scaling | ((uint8_t)index << 3) | (uint8_t)base);

  _code_blob->emit_byte(opcode);
  _code_blob->emit_byte(modrm);
  _code_blob->emit_byte(sib);
  emit_immediate(immediate);
}

void Assembler::sub_imm8_mem32(uint8_t immediate, Register dest) {
  const uint8_t opcode = 0x83;
  const uint8_t modrm = build_modrm(ModRM::MemNoDisplacement, (uint8_t)ModRMExtension::SUB, (uint8_t)dest);

  _code_blob->emit_byte(opcode);
  _code_blob->emit_byte(modrm);
  emit_immediate(immediate);
}

void Assembler::add_imm8_mem8(uint8_t immediate, Register base, Register index) {
  const uint8_t opcode = 0x80;
  const uint8_t modrm = build_modrm(ModRM::MemNoDisplacement, (uint8_t)ModRMExtension::ADD, SIBEnable);

  const uint8_t sib_scaling = (uint8_t)SIBScaling::One << 6;
  const uint8_t sib = (sib_scaling | ((uint8_t)index << 3) | (uint8_t)base);

  _code_blob->emit_byte(opcode);
  _code_blob->emit_byte(modrm);
  _code_blob->emit_byte(sib);
  emit_immediate(immediate);
}

void Assembler::add_imm8_mem32(uint8_t immediate, Register dest) {
  const uint8_t opcode = 0x83;
  const uint8_t modrm = build_modrm(ModRM::MemNoDisplacement, (uint8_t)ModRMExtension::ADD, (uint8_t)dest);

  _code_blob->emit_byte(opcode);
  _code_blob->emit_byte(modrm);
  emit_immediate(immediate);
}

void Assembler::add_mem64_reg64(Register source, Register dest) {
  const uint8_t rex = 0x40 | (uint8_t)RexMode::W;
  const uint8_t opcode = 0x03;
  const uint8_t modrm = build_modrm(ModRM::MemNoDisplacement, (uint8_t)dest, (uint8_t)source);

  _code_blob->emit_byte(rex);
  _code_blob->emit_byte(opcode);
  _code_blob->emit_byte(modrm);
}

void Assembler::test_reg8(Register reg) {
  const uint8_t opcode = 0x84;
  const uint8_t modrm = build_modrm(ModRM::Reg, (uint8_t)reg, (uint8_t)reg);

  _code_blob->emit_byte(opcode);
  _code_blob->emit_byte(modrm);
}

void Assembler::jmp_imm32(int32_t relative_offset) {
  const uint8_t opcode = 0xE9;
  const uint32_t immediate = twos_complement(relative_offset);

  _code_blob->emit_byte(opcode);
  emit_immediate(immediate);
}

void Assembler::jmp_imm32_backpatch(void* jmp_instr_addr, int32_t relative_offset) {
  // jmp_instr_addr points to the end of the jmp instruction, so we need to "go
  // back" the size of the instruction, which is 5 bytes, to get to the actual
  // instruction. However, we just go back 4 bytes, since we want to patch the immediate.

  const uint32_t immediate = twos_complement(relative_offset);
  char* const jmp_instr = (char*)jmp_instr_addr - 4;

  *(jmp_instr + 0) = (uint8_t)(immediate >> 0);
  *(jmp_instr + 1) = (uint8_t)(immediate >> 8);
  *(jmp_instr + 2) = (uint8_t)(immediate >> 16);
  *(jmp_instr + 3) = (uint8_t)(immediate >> 24);
}

void Assembler::jnz_rel32(int32_t relative_offset) {
  const uint8_t opcode_1 = 0x0F;
  const uint8_t opcode_0 = 0x85;
  const uint32_t immediate = twos_complement(relative_offset);

  _code_blob->emit_byte(opcode_1);
  _code_blob->emit_byte(opcode_0);
  emit_immediate(immediate);
}

void Assembler::syscall() {
  const uint8_t opcode_1 = 0x0F;
  const uint8_t opcode_0 = 0x05;

  _code_blob->emit_byte(opcode_1);
  _code_blob->emit_byte(opcode_0);
}
