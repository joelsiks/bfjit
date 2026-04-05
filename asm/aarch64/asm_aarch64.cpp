
#include <cassert>
#include <limits>

#include "asm_aarch64.hpp"

static bool check_no_invalid_bits_imm(uint32_t imm, size_t actual_size) {
  return (imm >> actual_size) == 0;
}

static uint32_t truncate_to_size_imm(uint32_t imm, size_t size) {
  const size_t shift = 32 - size;
  return (imm << shift) >> shift;
}

static uint64_t rotate_right(uint64_t val, int amount) {
  return (val >> amount) | (val << (64 - amount));
}

bool AssemblerAArch64::encode_immediate(uint64_t immediate, AssemblerAArch64::BitmaskImmediate& out) {
  // Based on: https://dougallj.wordpress.com/2021/10/30/bit-twiddling-optimising-aarch64-logical-immediate-encoding-and-decoding/

  // The spec states that we can't represent all zeros or all ones
  if (immediate == 0 || immediate == std::numeric_limits<uint64_t>::max()) {
    return false;
  }

  // Find rotation: clear trailing ones, count trailing zeros
  const size_t rotation = __builtin_ctzll(immediate & (immediate + 1));
  const uint64_t normalized = rotate_right(immediate, rotation & 0b111111);

  // Count leading zeros and trailing ones of normalized value
  const uint8_t zeroes = __builtin_clzll(normalized);
  const uint8_t ones   = __builtin_ctzll(~normalized);
  const uint8_t size   = zeroes + ones;

  // Validate that the pattern repeats to fill 64 bits
  if (rotate_right(immediate, size & 63) != immediate) {
    return false;
  }

  out = {
    ._n = (uint8_t)(size >> 6),
    ._immr = (uint8_t)((-rotation) & (size - 1)),
    ._imms = (uint8_t)((-(size << 1) | (ones - 1)) & 0b111111),
  };

  return true;
}

bool AssemblerAArch64::encode_immediate32(uint32_t immediate, AssemblerAArch64::BitmaskImmediate& out) {
  const uint64_t wide_immediate = (uint64_t)immediate << 32 | immediate;
  return encode_immediate(wide_immediate, out);
}

uint32_t AssemblerAArch64::build_addsub(Register Rd, Register Rn, uint32_t imm12, AddSubMode mode) {
  assert(check_no_invalid_bits_imm(imm12, 12));

  const uint32_t sf = 0b1 << 31;
  const uint32_t op = (uint32_t)mode << 30;
  const uint32_t S = 0b0 << 29;
  const uint32_t opcode_fixed = 0b100010 << 23;
  const uint32_t sh = 0b0 << 22;
  const uint32_t immediate = (uint32_t)imm12 << 10;
  const uint32_t rn_bits = (uint32_t)Rn << 5;
  const uint32_t rd_bits = (uint32_t)Rd;

  const uint32_t instruction = sf | op | S | opcode_fixed | sh | immediate | rn_bits | rd_bits;

  return instruction;
}

uint32_t AssemblerAArch64::build_ldrbstrb(Register Rn, Register Rt, uint32_t imm12, LoadStoreMode mode) {
  assert(check_no_invalid_bits_imm(imm12, 12));

  // This implements the "unsigned offset" version, which uses a 12 bit immediate.
  // The pre-/post-index alternatives uses a 9 bit immediate instead.

  const uint32_t size = 0b00 << 30;
  const uint32_t op = 0b111 << 27;
  const uint32_t version = 0b01 << 24; // 0b01 means the "unsigned offset" version
  const uint32_t opc = (uint32_t)mode << 22;
  const uint32_t immediate = imm12 << 10;
  const uint32_t rn_bits = (uint32_t)Rn << 5;
  const uint32_t rt_bits = (uint32_t)Rt;

  const uint32_t instruction = size | op | version | opc | immediate | rn_bits | rt_bits;

  return instruction;
}

AssemblerAArch64::AssemblerAArch64(CodeBlob* code_blob)
  : _code_blob(code_blob) {}

void AssemblerAArch64::ret() {
  const uint32_t instruction = 0xD65F03C0;
  _code_blob->emit_dword(instruction);
}

void AssemblerAArch64::svc() {
  const uint32_t instruction = 0xD4000001;
  _code_blob->emit_dword(instruction);
}

void AssemblerAArch64::tst32bit(Register Rn, Register Rm) {
  ands(Register::zr, Rn, Rm, SizeFlag::W32, 0);
}

void AssemblerAArch64::bcond(uint32_t imm19_offset, BranchCondition bc) {
  assert(check_no_invalid_bits_imm(imm19_offset, 19));

  const uint32_t opc = 0b01010100 << 24;
  const uint32_t imm = imm19_offset << 5;
  const uint32_t o0 = 0b0 << 4;
  const uint32_t bc_bits = (uint32_t)bc;

  const uint32_t instruction = opc | imm | o0 | bc_bits;

  _code_blob->emit_dword(instruction);
}

void AssemblerAArch64::b(int32_t imm26_offset) {
  assert(imm26_offset < 0 || check_no_invalid_bits_imm(imm26_offset, 26));

  const uint32_t op = 0b0 << 31;
  const uint32_t opc = 0b00101 << 26;
  const uint32_t imm = truncate_to_size_imm(imm26_offset, 26);

  const uint32_t instruction = op | opc | imm;

  _code_blob->emit_dword(instruction);
}

void AssemblerAArch64::b_backpatch(uint32_t* addr, int32_t imm26_offset) {
  // We've got a branch instruction at addr which needs to have its offset
  // updated (backpatched). The lower 26 bits represents the immediate, so
  // we mask out all other bits and OR in the new offset.
  const uint32_t mask = 0b111111 << 26;
  const uint32_t new_instruction = (*addr & mask) | truncate_to_size_imm(imm26_offset, 26);
  *addr = new_instruction;
}

void AssemblerAArch64::orr(Register Rm, Register Rd, Register Rn) {
  const uint32_t sf = 1 << 31;
  const uint32_t opc = 0b01 << 29;
  const uint32_t mode = 0b01010 << 24;
  const uint32_t shift = 0b00 << 22;
  const uint32_t N = 0b0 << 21;
  const uint32_t rm_bits = (uint32_t)Rm << 16;
  const uint32_t imm6 = 0b000000 << 10;
  const uint32_t rn_bits = (uint32_t)Rn << 5;
  const uint32_t rd_bits = (uint32_t)Rd;

  const uint32_t instruction = sf | opc | mode | shift | N | rm_bits | imm6 | rn_bits | rd_bits;

  _code_blob->emit_dword(instruction);
}

void AssemblerAArch64::mov(Register Rd, Register Rn) {
  orr(Rn, Rd, Register::zr);
}

void AssemblerAArch64::mov(Register Rd, uint32_t imm16) {
  assert(check_no_invalid_bits_imm(imm16, 16));

  const uint32_t sf = 1 << 31;
  const uint32_t opc = 0b10 << 29;
  const uint32_t mode = 0b100101 << 23;
  const uint32_t hw = 0b00 << 21;
  const uint32_t immediate = imm16 << 5;
  const uint32_t rd_bits = (uint32_t)Rd;

  const uint32_t instruction = sf | opc | mode | hw | immediate | rd_bits;
  _code_blob->emit_dword(instruction);
}

void AssemblerAArch64::add_imm12(Register Rd, Register Rn, uint32_t imm12) {
  _code_blob->emit_dword(build_addsub(Rd, Rn, imm12, AddSubMode::Add));
}

void AssemblerAArch64::sub_imm12(Register Rd, Register Rn, uint32_t imm12) {
  _code_blob->emit_dword(build_addsub(Rd, Rn, imm12, AddSubMode::Sub));
}

void AssemblerAArch64::strb_imm12(Register Rn, Register Rt, uint32_t imm12) {
  _code_blob->emit_dword(build_ldrbstrb(Rn, Rt, imm12, LoadStoreMode::Store));
}

void AssemblerAArch64::ldrb_imm12(Register Rn, Register Rt, uint32_t imm12) {
  _code_blob->emit_dword(build_ldrbstrb(Rn, Rt, imm12, LoadStoreMode::Load));
}

void AssemblerAArch64::and_imm12_32bit(Register Rn, Register Rd, uint32_t imm12) {
  assert(check_no_invalid_bits_imm(imm12, 12));

  const uint32_t sf = 0b0 << 31; // Is 0 for 32-bit instructions
  const uint32_t opc = (uint32_t)0b00 << 29;
  const uint32_t opcode_fixed = 0b100100 << 23;

  BitmaskImmediate bmi;
  const bool result = encode_immediate32((uint64_t)imm12, bmi);
  assert(result);

  const uint32_t rn_bits = (uint32_t)Rn << 5;
  const uint32_t rd_bits = (uint32_t)Rd;

  uint32_t instruction = sf | opc | opcode_fixed | (bmi._n << 22) | (bmi._immr << 16) | (bmi._imms << 10) | rn_bits | rd_bits;

  _code_blob->emit_dword(instruction);
}

void AssemblerAArch64::ands(Register Rd, Register Rn, Register Rm, SizeFlag size_flag, uint32_t imm6_shift) {
  assert(check_no_invalid_bits_imm(imm6_shift, 6));

  const uint32_t sf = (uint32_t)size_flag << 31;
  const uint32_t opc = 0b11 << 29;
  const uint32_t mode = 0b01010 << 24;
  const uint32_t shift = 0b00 << 22; // 0b00 = lsl (logical shift left) TODO: Fix more modes?
  const uint32_t N = 0b0 << 21;
  const uint32_t rm_bits = (uint32_t)Rm;
  const uint32_t imm6 = imm6_shift << 10;
  const uint32_t rn_bits = (uint32_t)Rn << 5;
  const uint32_t rd_bits = (uint32_t)Rd;

  const uint32_t instruction = sf | opc | mode | shift | N | rm_bits | imm6 | rn_bits | rd_bits;

  _code_blob->emit_dword(instruction);
}
