#pragma once
#include <cstdint>
#include <cstring>
#include "ps2_runtime.h"
static inline uint8_t Ps2FastRead8(const uint8_t *rdram, uint32_t addr) { return rdram[addr & PS2_RAM_MASK]; }
static inline uint16_t Ps2FastRead16(const uint8_t *rdram, uint32_t addr) { uint16_t v; std::memcpy(&v, rdram + (addr & PS2_RAM_MASK), 2); return v; }
static inline uint32_t Ps2FastRead32(const uint8_t *rdram, uint32_t addr) { uint32_t v; std::memcpy(&v, rdram + (addr & PS2_RAM_MASK), 4); return v; }
static inline uint64_t Ps2FastRead64(const uint8_t *rdram, uint32_t addr) { uint64_t v; std::memcpy(&v, rdram + (addr & PS2_RAM_MASK), 8); return v; }
static inline void Ps2FastWrite8(uint8_t *rdram, uint32_t addr, uint8_t value) { rdram[addr & PS2_RAM_MASK] = value; }
static inline void Ps2FastWrite16(uint8_t *rdram, uint32_t addr, uint16_t value) { std::memcpy(rdram + (addr & PS2_RAM_MASK), &value, 2); }
static inline void Ps2FastWrite32(uint8_t *rdram, uint32_t addr, uint32_t value) { std::memcpy(rdram + (addr & PS2_RAM_MASK), &value, 4); }
static inline void Ps2FastWrite64(uint8_t *rdram, uint32_t addr, uint64_t value) { std::memcpy(rdram + (addr & PS2_RAM_MASK), &value, 8); }
