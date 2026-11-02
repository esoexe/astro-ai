// console_tune.inc - console-physics/tune: a C++ port of console-skill/console-physics/tune.py (Jake Schincariol, MIT).
// Finds the float32 numbers that decide how a PS2 game moves: scan (a value stored in data, with the instructions
// that read it, or built in code by lui [+ ori/addiu] then mtc1), fps (frame-timing candidates) and patch (the pnach
// line to test a new value in PCSX2, plus the TOML instruction patches or the override write for the port).
namespace packtools::console_tune {
using namespace pyrt;
using namespace packtools::console_common;
using console_override::hex32;

const char* const PROG = "tune";
constexpr int WINDOW = 8;   // instructions after a lui that may finish building the value
constexpr long long RAM_SIZE = 32LL * 1024 * 1024;
constexpr uint32_t OP_LUI = 0x0F, OP_ORI = 0x0D, OP_ADDIU = 0x09, OP_COP1 = 0x11, OP_LWC1 = 0x31, OP_LW = 0x23;

inline long long f2u(double f) { const float x = static_cast<float>(f); uint32_t u; std::memcpy(&u, &x, 4); return u; }
inline double u2f(long long u) { const uint32_t w = static_cast<uint32_t>(u & 0xFFFFFFFFLL); float x; std::memcpy(&x, &w, 4); return x; }
inline std::string fmt(double f) {
    if (std::isnan(f)) return "nan";
    if (std::isinf(f)) return f < 0 ? "-inf" : "inf";
    return cfmt("%.9g", f);
}
inline long long sext16(long long v) { return v & 0x8000 ? v - 0x10000 : v; }
// "%08X" % v (Python pads after the sign)
inline std::string hex08u(long long v) {
    const unsigned long long m = v < 0 ? 0ULL - static_cast<unsigned long long>(v) : static_cast<unsigned long long>(v);
    std::string h = cfmt("%llX", m); while (h.size() + (v < 0 ? 1 : 0) < 8) h = "0" + h;
    return (v < 0 ? "-" : "") + h;
}
inline long long ram_offset(long long addr) {   // -1 = None
    addr &= 0xFFFFFFFFLL;
    if (addr < RAM_SIZE) return addr;
    if (0x20000000LL <= addr && addr < 0x40000000LL) return addr & (RAM_SIZE - 1);
    if (0x80000000LL <= addr && addr < 0xC0000000LL) { const long long p = addr & 0x1FFFFFFF; return p < RAM_SIZE ? p : -1; }
    return -1;
}
[[noreturn]] inline void none_error(const char* op) { throw PyErr("TypeError", std::string("unsupported operand type(s) for ") + op + ": 'NoneType' and 'int'"); }

// ---------------------------------------------------------------- MIPS: only what float constants need
const char* const REG[32] = {"zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
                             "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"};
inline bool in(uint32_t v, std::initializer_list<uint32_t> xs) { return std::find(xs.begin(), xs.end(), v) != xs.end(); }
inline const char* name_of(uint32_t op) {
    static const std::map<uint32_t, const char*> N{{0x02, "j"}, {0x03, "jal"}, {0x04, "beq"}, {0x05, "bne"}, {0x06, "blez"}, {0x07, "bgtz"}, {0x14, "beql"},
        {0x15, "bnel"}, {0x23, "lw"}, {0x2B, "sw"}, {0x31, "lwc1"}, {0x39, "swc1"}, {0x0C, "andi"}, {0x0A, "slti"},
        {0x1C, "mmi"}, {0x1E, "lq"}, {0x1F, "sq"}, {0x37, "ld"}, {0x3F, "sd"}, {0x24, "lbu"}, {0x28, "sb"}, {0x21, "lh"},
        {0x25, "lhu"}, {0x29, "sh"}, {0x20, "lb"}, {0x10, "cop0"}, {0x12, "cop2"}, {0x01, "regimm"}};
    const auto it = N.find(op); return it == N.end() ? nullptr : it->second;
}
inline std::string describe(uint32_t w, std::optional<long long> vaddr = std::nullopt) {
    const uint32_t op = w >> 26, rs = (w >> 21) & 31, rt = (w >> 16) & 31, rd = (w >> 11) & 31, imm = w & 0xFFFF;
    if (w == 0) return "nop";
    if (op == OP_LUI) return std::string("lui $") + REG[rt] + cfmt(", 0x%04X", imm);
    if (op == OP_ORI || op == OP_ADDIU) return std::string(op == OP_ORI ? "ori" : "addiu") + " $" + REG[rt] + ", $" + REG[rs] + cfmt(", 0x%04X", imm);
    if (op == OP_COP1 && rs == 4) return std::string("mtc1 $") + REG[rt] + cfmt(", $f%u", rd);
    if (op == OP_COP1 && rs == 0) return std::string("mfc1 $") + REG[rt] + cfmt(", $f%u", rd);
    if (op == OP_LWC1 || op == 0x39) return std::string(name_of(op)) + cfmt(" $f%u, %lld($", rt, sext16(imm)) + REG[rs] + ")";
    if (op == OP_LW || op == 0x2B) return std::string(name_of(op)) + " $" + REG[rt] + cfmt(", %lld($", sext16(imm)) + REG[rs] + ")";
    if ((op == 0x02 || op == 0x03) && vaddr)
        return std::string(name_of(op)) + cfmt(" 0x%08llX", static_cast<unsigned long long>(((*vaddr + 4) & 0xF0000000LL) | (static_cast<long long>(w & 0x03FFFFFF) << 2)));
    if (op == 0) {
        const uint32_t fn = w & 0x3F;
        if (fn == 0x08) return std::string("jr $") + REG[rs];
        switch (fn) { case 0x09: return "jalr"; case 0x0C: return "syscall"; case 0x21: return "addu"; case 0x25: return "or"; case 0x2D: return "daddu"; case 0x00: return "sll"; default: return "special"; }
    }
    if (op == OP_COP1) return "cop1";
    const char* n = name_of(op);
    return n ? n : cfmt("op%02X", op);
}
// the GPR an instruction writes (approximate: enough to stop tracking), or -1
inline int dest_reg(uint32_t w) {
    const uint32_t op = w >> 26;
    if (op == 0) { const uint32_t fn = w & 0x3F; if (in(fn, {0x08, 0x0C, 0x0D, 0x0F, 0x11, 0x13, 0x18, 0x19, 0x1A, 0x1B})) return -1; return static_cast<int>((w >> 11) & 31); }
    if (in(op, {0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x18, 0x19, 0x1A, 0x1B, 0x1E, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x37})) return static_cast<int>((w >> 16) & 31);
    if (op == 0x03) return 31;
    if (op == OP_COP1 && in((w >> 21) & 31, {0, 2})) return static_cast<int>((w >> 16) & 31);
    if (op == 0x1C) return static_cast<int>((w >> 11) & 31);
    return -1;
}
inline bool is_transfer(uint32_t w) {
    const uint32_t op = w >> 26;
    if (in(op, {0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x14, 0x15, 0x16, 0x17, 0x01})) return true;
    return op == 0 && in(w & 0x3F, {0x08, 0x09});
}

// ---------------------------------------------------------------- ELF32 little-endian MIPS reader
struct Seg { long long index, offset, vaddr, filesz, memsz, flags, ram; };
struct Sec { std::string name; long long type, flags, addr, size, offset, ram; };
struct Region { long long base, off, size; };
struct Xref { long long at; std::string insn, via; };
struct Const { long long lui, lo; std::string lo_op; long long mtc1; int freg, reg; long long bits; };   // lo -1 = None
struct Analysis { std::optional<long long> gp; std::vector<Const> consts; std::map<long long, std::vector<Xref>> xrefs; };

struct Elf {
    std::string name, data; long long entry = 0;
    std::vector<Seg> segments; std::vector<Sec> sections;
    mutable std::optional<Analysis> analysis;
    explicit Elf(const std::string& path) {
        if (!is_file(path)) throw InputError("ELF not found: " + path);
        std::FILE* f = open_file(path, "rb");
        if (!f) throw OSError(errno, path);
        char buf[65536]; std::size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, n);
        std::fclose(f);
        name = basename(path);
        const long long len = static_cast<long long>(data.size());
        if (len < 52 || data.compare(0, 4, "\x7f" "ELF") != 0) throw InputError(name + " is not an ELF file");
        if (data[4] != 1 || data[5] != 1) throw InputError(name + " is not a 32-bit little-endian ELF (PS2 EE ELFs are)");
        const long long machine = u16le(data, 18);
        entry = u32le(data, 24);
        const long long phoff = u32le(data, 28), shoff = u32le(data, 32), phentsize = u16le(data, 42), phnum = u16le(data, 44);
        const long long shentsize = u16le(data, 46), shnum = u16le(data, 48), shstrndx = u16le(data, 50);
        if (machine != 8) throw InputError(cfmt("%s is not a MIPS ELF (machine %lld, the PS2 EE is 8)", name.c_str(), machine));
        if (phnum && (phentsize < 32 || phoff + phnum * phentsize > len)) throw InputError(name + " is truncated or has a bad program header table");
        for (long long i = 0; i < phnum; ++i) {
            const std::size_t at = static_cast<std::size_t>(phoff + i * phentsize);
            const long long p_type = u32le(data, at), off = u32le(data, at + 4), vaddr = u32le(data, at + 8), filesz = u32le(data, at + 16);
            const long long memsz = u32le(data, at + 20), flags = u32le(data, at + 24);
            if (p_type != 1 || memsz == 0) continue;
            if (filesz > memsz || off + filesz > len) throw InputError(cfmt("%s is truncated: segment %lld runs past the end of the file", name.c_str(), i));
            segments.push_back({i, off, vaddr, filesz, memsz, flags, ram_offset(vaddr)});
        }
        if (segments.empty()) throw InputError(name + " has no loadable (PT_LOAD) segments");
        if (shnum && shoff && shentsize >= 40 && shoff + shnum * shentsize <= len) {
            std::vector<std::array<long long, 10>> raw;
            for (long long i = 0; i < shnum; ++i) {
                std::array<long long, 10> s{};
                for (int k = 0; k < 10; ++k) s[static_cast<std::size_t>(k)] = u32le(data, static_cast<std::size_t>(shoff + i * shentsize + 4 * k));
                raw.push_back(s);
            }
            std::string strtab;
            if (shstrndx < static_cast<long long>(raw.size())) {
                const auto& s = raw[static_cast<std::size_t>(shstrndx)];
                if (s[4] + s[5] <= len) strtab = data.substr(static_cast<std::size_t>(s[4]), static_cast<std::size_t>(s[5]));
            }
            for (const auto& s : raw) {
                if (!(s[2] & 2) || s[5] == 0) continue;
                std::string nm;
                if (s[0] < static_cast<long long>(strtab.size())) {
                    const auto end = strtab.find('\0', static_cast<std::size_t>(s[0]));
                    if (end != std::string::npos)
                        for (const unsigned char c : strtab.substr(static_cast<std::size_t>(s[0]), end - static_cast<std::size_t>(s[0])))
                            nm += c < 0x80 ? std::string(1, static_cast<char>(c)) : std::string("\xEF\xBF\xBD");
                }
                sections.push_back({nm, s[1], s[2], s[3], s[5], s[4], ram_offset(s[3])});
            }
        }
    }
    std::pair<const Seg*, bool> locate(long long ram) const {
        for (const auto& s : segments) {
            if (s.ram < 0) continue;
            if (s.ram <= ram && ram < s.ram + s.filesz) return {&s, true};
            if (s.ram + s.filesz <= ram && ram < s.ram + s.memsz) return {&s, false};
        }
        return {nullptr, false};
    }
    const Sec* section_at(long long ram) const { for (const auto& s : sections) if (s.ram >= 0 && s.ram <= ram && ram < s.ram + s.size) return &s; return nullptr; }
    std::optional<uint32_t> word(long long vaddr) const {
        const long long ram = ram_offset(vaddr);
        if (ram < 0) return std::nullopt;
        const auto [seg, backed] = locate(ram);
        if (!seg || !backed || ram + 4 > seg->ram + seg->filesz) return std::nullopt;
        return u32le(data, static_cast<std::size_t>(seg->offset + ram - seg->ram));
    }
    std::string where(long long vaddr) const {
        const long long ram = ram_offset(vaddr);
        if (ram >= 0) if (const Sec* sec = section_at(ram)) return sec->name.empty() ? "section" : sec->name;
        const auto [seg, backed] = ram >= 0 ? locate(ram) : std::pair<const Seg*, bool>{nullptr, false};
        if (!seg) return "outside the ELF";
        std::string perm; if (seg->flags & 4) perm += "R"; if (seg->flags & 2) perm += "W"; if (seg->flags & 1) perm += "X";
        return cfmt("segment %lld (", seg->index) + perm + ")" + (backed ? "" : " zero-filled");
    }
    std::pair<std::vector<Region>, std::vector<Region>> regions() const {
        std::vector<Region> code, dat;
        for (const auto& sec : sections) {
            if (sec.type == 8 || sec.ram < 0) continue;
            const auto [seg, backed] = locate(sec.ram);
            if (!seg || !backed) continue;
            const Region r{sec.addr, seg->offset + sec.ram - seg->ram, std::min(sec.size, seg->ram + seg->filesz - sec.ram)};
            (sec.flags & 4 ? code : dat).push_back(r);
        }
        if (code.empty() && dat.empty()) {
            for (const auto& s : segments) {
                if (!s.filesz) continue;
                if (s.flags & 1) code.push_back({s.vaddr, s.offset, s.filesz});
                if (!(s.flags & 1) || (s.flags & 2)) dat.push_back({s.vaddr, s.offset, s.filesz});   // RWX: code and data mixed
            }
        }
        return {code, dat};
    }
    bool is_code(long long vaddr) const { for (const auto& r : regions().first) if (r.base <= vaddr && vaddr < r.base + r.size) return true; return false; }
};

// one pass over the code: float constants built in registers, data loads, and $gp
inline const Analysis& analyze(const Elf& elf) {
    if (elf.analysis) return *elf.analysis;
    Analysis A;
    std::set<long long> seen_mtc1;
    for (const auto& [base, off, size] : elf.regions().first) {
        const long long n = size / 4;
        std::vector<uint32_t> ws; for (long long i = 0; i < n; ++i) ws.push_back(u32le(elf.data, static_cast<std::size_t>(off + 4 * i)));
        if (!A.gp) {   // crt0 sets $gp near the entry with lui $gp + addiu/ori $gp
            const long long start = (base <= elf.entry && elf.entry < base + size) ? std::max(0LL, (elf.entry - base) / 4) : 0;
            std::vector<long long> order; for (long long i = start; i < std::min(n, start + 256); ++i) order.push_back(i); for (long long i = 0; i < n; ++i) order.push_back(i);
            for (const long long i : order) {
                const uint32_t w = ws[static_cast<std::size_t>(i)];
                if (w >> 26 == OP_LUI && ((w >> 16) & 31) == 28) {
                    for (long long j = i + 1; j < std::min(n, i + 5); ++j) {
                        const uint32_t v = ws[static_cast<std::size_t>(j)];
                        if ((v >> 26 == OP_ADDIU || v >> 26 == OP_ORI) && ((v >> 21) & 31) == 28 && ((v >> 16) & 31) == 28) {
                            const long long hi = static_cast<long long>(w & 0xFFFF) << 16;
                            A.gp = v >> 26 == OP_ORI ? (hi | (v & 0xFFFF)) : (hi + sext16(v & 0xFFFF)) & 0xFFFFFFFFLL;
                            break;
                        }
                    }
                    if (A.gp) break;
                }
            }
        }
        struct K { long long bits, lo; std::string lo_op; };
        for (long long i = 0; i < n; ++i) {
            const uint32_t w = ws[static_cast<std::size_t>(i)];
            if (w >> 26 != OP_LUI) continue;
            const uint32_t rt = (w >> 16) & 31;
            if (rt == 0 || rt == 28) continue;   // $zero, and $gp (its loads are found by the $gp pass below)
            std::map<uint32_t, K> known{{rt, {static_cast<long long>(w & 0xFFFF) << 16, -1, ""}}};
            std::optional<long long> stop_at;
            for (long long j = i + 1; j < std::min(n, i + 1 + WINDOW); ++j) {
                const uint32_t v = ws[static_cast<std::size_t>(j)];
                const uint32_t op = v >> 26, rs = (v >> 21) & 31, rt2 = (v >> 16) & 31;
                if ((op == OP_ORI || op == OP_ADDIU) && known.count(rs) && known[rs].lo < 0 && rt2 != 0) {
                    const K k = known[rs];
                    const long long bits = op == OP_ORI ? (k.bits | (v & 0xFFFF)) : (k.bits + sext16(v & 0xFFFF)) & 0xFFFFFFFFLL;
                    known[rt2] = {bits, base + 4 * j, op == OP_ORI ? "ori" : "addiu"};
                } else if (op == OP_COP1 && rs == 4 && known.count(rt2)) {
                    const K& k = known[rt2]; const long long at = base + 4 * j;
                    if (!seen_mtc1.count(at)) { seen_mtc1.insert(at); A.consts.push_back({base + 4 * i, k.lo, k.lo_op, at, static_cast<int>((v >> 11) & 31), static_cast<int>(rt2), k.bits}); }
                } else if ((op == OP_LWC1 || op == OP_LW) && known.count(rs)) {
                    const long long addr = (known[rs].bits + sext16(v & 0xFFFF)) & 0xFFFFFFFFLL;
                    A.xrefs[addr].push_back({base + 4 * j, describe(v), known[rs].lo < 0 ? "lui" : "lui+" + known[rs].lo_op});
                    if (op == OP_LW) known.erase(rt2);
                } else {
                    const int d = dest_reg(v);
                    if (d >= 0) known.erase(static_cast<uint32_t>(d));
                }
                if (known.empty()) break;
                if (stop_at && j >= *stop_at) break;
                if (is_transfer(v)) stop_at = j + 1;   // follow the delay slot, then stop
            }
        }
        if (A.gp) {
            for (long long i = 0; i < n; ++i) {
                const uint32_t w = ws[static_cast<std::size_t>(i)];
                if ((w >> 26 == OP_LWC1 || w >> 26 == OP_LW) && ((w >> 21) & 31) == 28) {
                    auto& lst = A.xrefs[(*A.gp + sext16(w & 0xFFFF)) & 0xFFFFFFFFLL];
                    const long long at = base + 4 * i;
                    if (std::none_of(lst.begin(), lst.end(), [&](const Xref& x) { return x.at == at; })) lst.push_back({at, describe(w), "$gp"});
                }
            }
        }
    }
    elf.analysis = std::move(A);
    return *elf.analysis;
}
inline std::vector<Xref> xrefs_at(const Elf& elf, long long addr) { const auto& X = analyze(elf).xrefs; const auto it = X.find(addr); return it == X.end() ? std::vector<Xref>{} : it->second; }

// ---------------------------------------------------------------- searching for values
inline long long bits_ceil(double x) { long long b = f2u(x); if (u2f(b) < x) b += 1; return b; }
inline long long bits_floor(double x) { long long b = f2u(x); if (u2f(b) > x) b -= 1; return b; }
// inclusive float32 bit-pattern intervals whose values lie in [value - tol, value + tol]
inline std::vector<std::pair<long long, long long>> pattern_ranges(double value, double tol) {
    const double big = 3.4028234663852886e38;
    const double lo = std::max(value - tol, -big), hi = std::min(value + tol, big);
    std::vector<std::pair<long long, long long>> o, r;
    if (hi >= 0) o.emplace_back(bits_ceil(std::max(lo, 0.0)), std::min(bits_floor(hi), 0x7F7FFFFFLL));
    if (lo < 0) o.emplace_back(0x80000000LL | bits_ceil(std::max(-hi, 0.0)), 0x80000000LL | std::min(bits_floor(-lo), 0x7F7FFFFFLL));
    for (const auto& x : o) if (x.first <= x.second) r.push_back(x);
    return r;
}
inline double default_tol(double v) { return std::max(std::fabs(v) * 1e-3, 1e-6); }
inline void check_value(double v) {
    if (std::isnan(v) || std::isinf(v)) throw InputError("the value must be a finite number");
    if (v == 0) throw InputError("0.0 is in every zero word: search for a non-zero value (the one the game shows, or 1.0)");
    if (std::fabs(v) > 3.4e38) throw InputError(float_repr(v) + " does not fit in a float32");
}
struct Hit { std::string kind; long long addr, bits; double value; std::string target, where; std::vector<Xref> read_by; long long lui = 0, lo = -1; std::string lo_op; long long mtc1 = 0; int freg = 0, reg = 0; };
struct Target { std::string label; double value, tol; };
inline std::vector<Hit> search(const Elf& elf, const std::vector<Target>& targets, long long limit) {
    const auto [code, dat] = elf.regions(); (void)code;
    const Analysis& ana = analyze(elf);
    struct R { long long a, b; std::string label; };
    std::vector<R> ranges;
    for (const auto& t : targets) for (const auto& [a, b] : pattern_ranges(t.value, t.tol)) ranges.push_back({a, b, t.label});
    std::vector<Hit> hits;
    if (ranges.empty()) return hits;
    long long lo_all = ranges[0].a, hi_all = ranges[0].b;
    for (const auto& r : ranges) { lo_all = std::min(lo_all, r.a); hi_all = std::max(hi_all, r.b); }
    for (const auto& [base, off, size] : dat) {
        const long long start_pad = ((-base) % 4 + 4) % 4;
        const long long n = static_cast<long long>(std::floor(static_cast<double>(size - start_pad) / 4));
        if (n <= 0) continue;
        for (long long i = 0; i < n; ++i) {
            const long long w = u32le(elf.data, static_cast<std::size_t>(off + start_pad + 4 * i));
            if (w < lo_all || w > hi_all) continue;
            for (const auto& r : ranges) {
                if (r.a <= w && w <= r.b) {
                    const long long addr = base + start_pad + 4 * i;
                    Hit h; h.kind = "data"; h.addr = addr; h.bits = w; h.value = u2f(w); h.target = r.label; h.where = elf.where(addr); h.read_by = xrefs_at(elf, addr);
                    hits.push_back(h);
                    break;
                }
            }
        }
    }
    for (const auto& c : ana.consts) {
        for (const auto& r : ranges) {
            if (r.a <= c.bits && c.bits <= r.b) {
                Hit h; h.kind = "code"; h.addr = c.lui; h.bits = c.bits; h.value = u2f(c.bits); h.target = r.label; h.where = elf.where(c.lui);
                h.lui = c.lui; h.lo = c.lo; h.lo_op = c.lo_op; h.mtc1 = c.mtc1; h.freg = c.freg; h.reg = c.reg;
                hits.push_back(h);
                break;
            }
        }
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& x, const Hit& y) { return x.addr != y.addr ? x.addr < y.addr : x.kind < y.kind; });
    if (limit) {   // hits[:limit], negative counts from the end
        const long long sz = static_cast<long long>(hits.size());
        const long long keep = limit > 0 ? std::min(limit, sz) : std::max(0LL, sz + limit);
        hits.resize(static_cast<std::size_t>(keep));
    }
    return hits;
}

inline std::vector<std::string> context(const Elf& elf, const Hit& h, int before = 2, int after = 2) {
    const long long first = h.lui - 4 * before, last = std::max(h.mtc1, h.lo < 0 ? 0 : h.lo) + 4 * after;
    std::vector<std::string> o;
    for (long long a = first; a < last + 4; a += 4) {
        const auto w = elf.word(a);
        if (!w) continue;
        const bool mark = a == h.lui || a == h.lo || a == h.mtc1;
        o.push_back(std::string(mark ? ">" : " ") + " " + hex32(a) + cfmt("  %08X  ", *w) + describe(*w, a));
    }
    return o;
}
inline void print_hits(const Elf& elf, const std::vector<Hit>& hits, const std::string& title, bool show_target = false) {
    std::size_t nd = 0; for (const auto& h : hits) if (h.kind == "data") ++nd;
    out(title + cfmt(": %zu in data, %zu built in code", nd, hits.size() - nd));
    for (const auto& h : hits) {
        const std::string tgt = show_target ? "  [" + h.target + "]" : "";
        if (h.kind == "data") {
            out("\nDATA  " + hex32(h.addr) + "  " + ljust(h.where, 22) + " " + fmt(h.value) + " (" + hex32(h.bits) + ")" + tgt);
            if (!h.read_by.empty()) {
                std::vector<std::string> rb; for (std::size_t k = 0; k < h.read_by.size() && k < 6; ++k) rb.push_back(h.read_by[k].insn + " at " + hex32(h.read_by[k].at) + " (" + h.read_by[k].via + ")");
                out("      read by: " + join(rb, ", ") + (h.read_by.size() > 6 ? cfmt(" and %zu more", h.read_by.size() - 6) : ""));
            } else {
                out("      read by: nothing found by lui or $gp addressing (check xrefs in Ghidra)");
            }
        } else {
            const std::string how = h.lo < 0 ? "lui" : "lui + " + h.lo_op;
            out("\nCODE  " + hex32(h.lui) + "  " + ljust(h.where, 22) + " " + fmt(h.value) + " (" + hex32(h.bits) + ") built by " + how + cfmt(", then mtc1 into $f%d", h.freg) + tgt);
            for (const auto& l : context(elf, h)) out("      " + l);
        }
    }
}

// ---------------------------------------------------------------- commands
inline Val xref_json(const Xref& x) { return dict_of({{"at", Val::str(hex32(x.at))}, {"insn", Val::str(x.insn)}, {"via", Val::str(x.via)}}); }
inline Val hit_json(const Hit& h) {
    Val d = dict_of({{"kind", Val::str(h.kind)}, {"addr", Val::str(hex32(h.addr))}, {"bits", Val::str(hex32(h.bits))}, {"value", Val::str(fmt(h.value))},
                     {"target", Val::str(h.target)}, {"where", Val::str(h.where)}});
    if (h.kind == "data") { List l; for (const auto& x : h.read_by) l.push_back(xref_json(x)); d.d->set("read_by", Val::list(std::move(l))); }
    else {
        d.d->set("lui", Val::str(hex32(h.lui))); d.d->set("lo", h.lo < 0 ? Val::none() : Val::str(hex32(h.lo))); d.d->set("lo_op", h.lo < 0 ? Val::none() : Val::str(h.lo_op));
        d.d->set("mtc1", Val::str(hex32(h.mtc1))); d.d->set("freg", Val::integer(h.freg)); d.d->set("reg", Val::integer(h.reg));
    }
    return d;
}
inline Val hits_json(const std::vector<Hit>& hits) { List l; for (const auto& h : hits) l.push_back(hit_json(h)); return Val::list(std::move(l)); }

inline int cmd_scan(const Val& a) {
    const double value = get(a, "value").f;
    check_value(value);
    const double tol = get(a, "tol").is_none() ? default_tol(value) : get(a, "tol").f;
    if (tol < 0) throw InputError("--tol must be zero or more");
    const Elf elf(S(a, "elf"));
    const long long limit = get(a, "limit").i;
    const auto hits = search(elf, {{fmt(value), value, tol}}, limit);
    if (B(a, "json")) {
        const auto& gp = analyze(elf).gp;
        out(dumps2(dict_of({{"elf", Val::str(elf.name)}, {"value", Val::flt(value)}, {"tol", Val::flt(tol)}, {"gp", gp ? Val::str(hex32(*gp)) : Val::none()}, {"hits", hits_json(hits)}})));
        return hits.empty() ? 1 : 0;
    }
    print_hits(elf, hits, fmt(value) + " (+/- " + fmt(tol) + ") in " + elf.name);
    if (hits.empty()) {
        out("\nNothing. Try a wider --tol, the value in other units (9.8 m/s2 may be 980 cm/s2, 0.98 per frame, or 9.8/60), or find it while playing with PCSX2's memory search.");
        return 1;
    }
    if (static_cast<long long>(hits.size()) >= limit) out(cfmt("\n(first %lld shown: --limit to see more)", limit));
    out("\nNext: try one in PCSX2:  tune.py patch " + elf.name + " --addr <address> --float <new value>");
    return 0;
}

const char* const FPS_CAVEAT[] = {
    "These are candidates, not answers. Many games pace frames with a vsync count (wait 2 vsyncs = 30 fps),",
    "an integer the scan cannot see: look in Ghidra for the loop that waits on vsync (sceGsSyncV, a vsync",
    "semaphore) and the number it waits for.",
    "A 60 fps unlock also needs the game's logic to scale by the frame time: if movement, physics or",
    "animation are counted per frame, doubling the frame rate doubles the game speed. Change one",
    "candidate at a time and test in PCSX2 first (tune.py patch), then bake it in with /console-mod."};
inline int cmd_fps(const Val& a) {
    const Elf elf(S(a, "elf"));
    const std::vector<std::pair<const char*, double>> T{{"1/60 s, one NTSC frame", 1.0 / 60.0}, {"1/30 s, a 30 fps frame", 1.0 / 30.0},
        {"60 per second", 60.0}, {"30 per second", 30.0}, {"1/50 s, one PAL frame", 1.0 / 50.0},
        {"1/25 s, a 25 fps PAL frame", 1.0 / 25.0}, {"50 per second (PAL)", 50.0}, {"25 per second (PAL)", 25.0}};
    std::vector<Target> targets; for (const auto& [l, v] : T) targets.push_back({l, v, std::fabs(v) * 0.005});
    const auto hits = search(elf, targets, get(a, "limit").i);
    if (B(a, "json")) {
        std::vector<std::string> cv(std::begin(FPS_CAVEAT), std::end(FPS_CAVEAT));
        out(dumps2(dict_of({{"elf", Val::str(elf.name)}, {"hits", hits_json(hits)}, {"caveat", Val::str(join(cv, " "))}})));
        return hits.empty() ? 1 : 0;
    }
    print_hits(elf, hits, "Frame-timing candidates in " + elf.name, true);
    out("");
    for (const char* l : FPS_CAVEAT) out(l);
    return hits.empty() ? 1 : 0;
}

struct Pair { long long lui, lo; std::string lo_op; long long mtc1; int freg; };   // lo / mtc1 -1 = None
inline std::optional<Pair> find_pair(const Elf& elf, long long addr) {
    const auto w0 = elf.word(addr);
    if (!w0) return std::nullopt;
    for (const auto& c : analyze(elf).consts) if (addr == c.lui || (c.lo >= 0 && addr == c.lo) || addr == c.mtc1) return Pair{c.lui, c.lo, c.lo_op, c.mtc1, c.freg};
    const uint32_t w = *w0, op = w >> 26;
    if (op == OP_LUI) {
        const uint32_t rt = (w >> 16) & 31;
        for (int j = 1; j <= WINDOW; ++j) {
            const auto v = elf.word(addr + 4 * j);
            if (!v) break;
            if ((*v >> 26 == OP_ORI || *v >> 26 == OP_ADDIU) && ((*v >> 21) & 31) == rt) return Pair{addr, addr + 4 * j, *v >> 26 == OP_ORI ? "ori" : "addiu", -1, 0};
            if (dest_reg(*v) == static_cast<int>(rt)) break;
        }
        return Pair{addr, -1, "", -1, 0};
    }
    if (op == OP_ORI || op == OP_ADDIU) {
        const uint32_t rs = (w >> 21) & 31;
        for (int j = 1; j <= WINDOW; ++j) {
            const auto v = elf.word(addr - 4 * j);
            if (!v) break;
            if (*v >> 26 == OP_LUI && ((*v >> 16) & 31) == rs) return Pair{addr - 4 * j, addr, op == OP_ORI ? "ori" : "addiu", -1, 0};
        }
    }
    return std::nullopt;
}
inline std::pair<long long, long long> split_hi_lo(long long bits, const std::string& lo_op) {
    if (lo_op == "addiu") return {((bits + 0x8000) >> 16) & 0xFFFF, bits & 0xFFFF};
    return {bits >> 16, bits & 0xFFFF};
}
inline std::string pnach_line(long long place, long long addr, long long value) { return cfmt("patch=%lld,EE,", place) + hex08u(addr) + ",word," + hex08u(value); }

inline int cmd_patch(const Val& a) {
    const Elf elf(S(a, "elf"));
    const std::string addr_s = S(a, "addr");
    const auto addr_o = int16(addr_s);
    if (!addr_o) throw InputError("--addr is not a hex address: " + str_repr(addr_s));
    const long long addr = *addr_o;
    if (addr & 3) throw InputError("--addr " + hex32(addr) + " is not 4-byte aligned");
    const double nv = get(a, "float").f;
    if (std::isnan(nv) || std::fabs(nv) > 3.4e38) throw InputError("--float must be a finite float32 value");
    long long bits = f2u(nv);
    const long long ram = ram_offset(addr);
    if (ram < 0) throw InputError(hex32(addr) + " is not main RAM");
    const auto [seg, backed] = elf.locate(ram);
    const long long place_arg = get(a, "place").is_none() ? -1 : get(a, "place").i;   // -1 = default
    Val o = dict_of({{"elf", Val::str(elf.name)}, {"addr", Val::str(hex32(addr))}, {"new_value", Val::str(fmt(u2f(bits)))}, {"new_bits", Val::str(hex32(bits))}});
    int status = 0;
    std::vector<std::string> pnach, toml, words_txt;
    std::vector<Xref> reads; std::string kind, note, used_by, override_line;
    std::optional<std::string> old_value;
    struct Word { long long at, orig, neww; };
    std::vector<Word> wl;
    if (!seg || !backed) {
        const long long place = (place_arg < 0 ? 1 : place_arg);
        kind = "runtime"; pnach = {pnach_line(place, addr, bits)};
        note = hex32(addr) + " is " + (seg ? "zero-filled (BSS)" : "outside the ELF") + ": the game sets this value while it runs, so nothing in the ELF to change. "
               "Write it every frame: put the pnach line in a .pnach and run /console-mod pnach.py override --frame-hook.";
        o.d->set("kind", Val::str(kind)); o.d->set("pnach", str_list(pnach)); o.d->set("toml", Val::list()); o.d->set("note", Val::str(note));
    } else if (elf.is_code(addr)) {
        const auto pair = find_pair(elf, addr);
        if (!pair) {
            const auto w = elf.word(addr);
            if (!w) none_error(">>");
            throw InputError(hex32(addr) + " holds '" + describe(*w, addr) + "', not part of a float constant: give a data float or the lui/ori/addiu of a constant (tune.py scan lists them)");
        }
        const long long lui_w = *elf.word(pair->lui);
        const long long place = (place_arg < 0 ? 1 : place_arg);
        long long old_bits = (lui_w & 0xFFFF) << 16;
        if (pair->lo >= 0) {
            const long long lo_w = *elf.word(pair->lo);
            old_bits = pair->lo_op == "ori" ? (old_bits | (lo_w & 0xFFFF)) : (old_bits + sext16(lo_w & 0xFFFF)) & 0xFFFFFFFFLL;
            const auto [hi, lo] = split_hi_lo(bits, pair->lo_op);
            wl = {{pair->lui, lui_w, (lui_w & 0xFFFF0000LL) | hi}, {pair->lo, lo_w, (lo_w & 0xFFFF0000LL) | lo}};
            kind = "code lui+" + pair->lo_op;
            o.d->set("kind", Val::str(kind));
        } else {
            kind = "code lui";
            o.d->set("kind", Val::str(kind));
            if (bits & 0xFFFF) {
                const long long nearest_bits = (bits + 0x8000) & 0xFFFF0000LL;
                if (B(a, "exact"))
                    throw InputError(fmt(nv) + " needs a low half (" + hex32(bits) + ") but this constant is a lone lui with no room for one; the nearest lui-only value is " + fmt(u2f(nearest_bits)));
                note = fmt(nv) + " needs a low half but this constant is a lone lui: using the nearest value it can hold, " + fmt(u2f(nearest_bits)) + " (off by "
                       + fmt(std::fabs(u2f(nearest_bits) - nv)) + "). For the exact value, write the C++ by hand (/console-mod).";
                o.d->set("note", Val::str(note));
                bits = nearest_bits;
                o.d->set("new_value", Val::str(fmt(u2f(bits)))); o.d->set("new_bits", Val::str(hex32(bits)));
                status = 1;
            }
            wl = {{pair->lui, lui_w, (lui_w & 0xFFFF0000LL) | (bits >> 16)}};
        }
        old_value = fmt(u2f(old_bits));
        o.d->set("old_value", Val::str(*old_value));
        List wj;
        for (const auto& x : wl) wj.push_back(dict_of({{"addr", Val::str(hex32(x.at))}, {"original", Val::str(hex32(x.orig))}, {"new", Val::str(hex32(x.neww))},
                                                       {"original_asm", Val::str(describe(static_cast<uint32_t>(x.orig)))}, {"new_asm", Val::str(describe(static_cast<uint32_t>(x.neww)))}}));
        o.d->set("words", Val::list(std::move(wj)));
        std::vector<Word> changed; for (const auto& x : wl) if (x.orig != x.neww) changed.push_back(x);
        if (changed.empty()) { note = "the constant already holds this value: nothing to patch"; o.d->set("note", Val::str(note)); }
        for (const auto& x : changed) { pnach.push_back(pnach_line(place, x.at, x.neww)); toml.push_back("[[patches.instructions]]\naddress = \"" + hex32(x.at) + "\"\nvalue = \"" + hex32(x.neww) + "\""); }
        o.d->set("pnach", str_list(pnach)); o.d->set("toml", str_list(toml));
        used_by = pair->mtc1 > 0 ? cfmt("mtc1 $f%d at ", pair->freg) + hex32(pair->mtc1) : "no mtc1 seen nearby: check it really is a float";
        o.d->set("used_by", Val::str(used_by));
    } else {
        const long long place = (place_arg < 0 ? 0 : place_arg);
        const auto old = elf.word(addr);
        if (!old) none_error("&");
        reads = xrefs_at(elf, addr);
        kind = "data"; old_value = fmt(u2f(*old)); pnach = {pnach_line(place, addr, bits)};
        override_line = cfmt("Ps2FastWrite32(rdram, 0x%08llXu, 0x%08llXu); // ", static_cast<unsigned long long>(ram), static_cast<unsigned long long>(bits)) + fmt(u2f(bits)) + "f";
        List rj; for (const auto& x : reads) rj.push_back(xref_json(x));
        o.d->set("kind", Val::str(kind)); o.d->set("old_value", Val::str(*old_value)); o.d->set("old_bits", Val::str(hex32(*old))); o.d->set("pnach", str_list(pnach));
        o.d->set("toml", Val::list()); o.d->set("where", Val::str(elf.where(addr))); o.d->set("read_by", Val::list(std::move(rj))); o.d->set("override", Val::str(override_line));
        if (reads.size() > 1) {
            note = cfmt("%zu instructions read this constant: changing it changes every one of them. To change only one, patch that instruction's code instead (Ghidra).", reads.size());
            o.d->set("note", Val::str(note));
        }
    }
    if (B(a, "json")) { out(dumps2(o)); return status; }
    out(S(o, "addr") + "  " + kind + " in " + elf.name);
    if (old_value) out("  value       " + *old_value + " -> " + S(o, "new_value") + " (" + S(o, "new_bits") + ")");
    for (const auto& x : wl) out("  " + hex32(x.at) + "  " + hex32(x.orig) + "  " + ljust(describe(static_cast<uint32_t>(x.orig)), 24) + " -> " + hex32(x.neww) + "  " + describe(static_cast<uint32_t>(x.neww)));
    if (!used_by.empty()) out("  used by     " + used_by);
    if (!reads.empty()) { std::vector<std::string> r; for (std::size_t k = 0; k < reads.size() && k < 8; ++k) r.push_back(reads[k].insn + " at " + hex32(reads[k].at)); out("  read by     " + join(r, ", ")); }
    if (!note.empty()) out("  NOTE: " + note);
    out("\nTest in PCSX2 first (add to the game's .pnach, see /console-mod pnach.py crc):");
    for (const auto& l : pnach) out("  " + l);
    if (!toml.empty()) {
        out("\nThen the port: paste into the game's ps2_recomp TOML, re-run ps2_recomp, rebuild:");
        for (const auto& b : toml) out("  " + replace_all(b, "\n", "\n  "));
    } else if (kind == "data") {
        out("\nThen the port: it is data, so a game override writes it at load. Put the pnach line in a .pnach and");
        out("run /console-mod: pnach.py override <file.pnach> --elf " + elf.name + ", or by hand in an override:");
        out("  " + override_line);
    }
    return status;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p(PROG, "Find the float32 numbers that decide how a PS2 game moves (speed, gravity, grip, jump height, frame rate) and print the patches to change them.");
    p.epilog = "Exit codes: 0 ok, 1 nothing found or an approximation was used, 2 bad input.";
    p.subcommands("cmd", false);
    { Parser& s = p.sub("scan"); s.help_line = "find a float value in data and in lui/ori/mtc1 code";
      s.pos("elf").help("the game's main ELF");
      s.opt({"--value"}).number().required().help("the number to look for, e.g. 9.8");
      s.opt({"--tol"}).number().help("absolute tolerance (default 0.1% of the value)");
      s.opt({"--limit"}).integer().def(Val::integer(50)).help("most hits to show (default 50)");
      s.opt({"--json"}).store_true().help("machine-readable output"); }
    { Parser& s = p.sub("fps"); s.help_line = "list frame-timing candidates (1/60, 1/30, 60, 30, PAL 50/25)";
      s.pos("elf").help("the game's main ELF");
      s.opt({"--limit"}).integer().def(Val::integer(100)).help("most hits to show (default 100)");
      s.opt({"--json"}).store_true().help("machine-readable output"); }
    { Parser& s = p.sub("patch"); s.help_line = "print pnach + TOML/override lines that set a new float value";
      s.pos("elf").help("the game's main ELF");
      s.opt({"--addr"}).required().help("a data float, or the lui / ori / addiu of a constant (hex)");
      s.opt({"--float"}).number().required().help("the new value, e.g. 12.5");
      s.opt({"--place"}).integer().choices({"0", "1", "2"}).help("pnach place (default 0 for data, 1 for code)");
      s.opt({"--exact"}).store_true().help("refuse instead of rounding when a lone lui cannot hold the value");
      s.opt({"--json"}).store_true().help("machine-readable output"); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { flush(); const std::string t = ap::help_text(ap::norm(p, p.prog)); std::fwrite(t.data(), 1, t.size(), stdout); return 2; }
    return guard(std::string(PROG) + ": error", [&]() -> int {
        if (cmd == "scan") return cmd_scan(a);
        if (cmd == "fps") return cmd_fps(a);
        return cmd_patch(a);
    });
}
PACKTOOL("console-physics/tune", main);
}  // namespace packtools::console_tune
