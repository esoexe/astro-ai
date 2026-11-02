#pragma once
#include <cstdint>
#include <cstring>
constexpr uint32_t PS2_RAM_SIZE = 32u * 1024u * 1024u;
constexpr uint32_t PS2_RAM_MASK = PS2_RAM_SIZE - 1u;
struct alignas(16) R5900Context { uint32_t r[32][4]; uint32_t pc; float f[32]; };
inline uint32_t getRegU32(const R5900Context *ctx, int reg) { return (reg <= 0 || reg > 31) ? 0u : ctx->r[reg][0]; }
inline void setReturnU32(R5900Context *ctx, uint32_t value) { ctx->r[2][0] = value; }
inline void setReturnS32(R5900Context *ctx, int32_t value) { ctx->r[2][0] = static_cast<uint32_t>(value); }
class PS2Memory { public: uint8_t *getRDRAM() { return m_rdram; } private: uint8_t *m_rdram = nullptr; };
class PS2Runtime {
public:
    using RecompiledFunction = void (*)(uint8_t *, R5900Context *, PS2Runtime *);
    bool replaceFunction(uint32_t address, RecompiledFunction func);
    bool registerFunction(uint32_t address, RecompiledFunction func);
    RecompiledFunction lookupFunction(uint32_t address);
    bool hasFunction(uint32_t address) const;
    inline PS2Memory &memory() { return m_memory; }
private:
    PS2Memory m_memory;
};
