// console_pnach.inc - console-mod/pnach: a C++ port of console-skill/console-mod/pnach.py (Jake Schincariol, MIT).
// Ports PCSX2 patches (.pnach) to a PS2Recomp port: every write is sorted by where it lands in the game's ELF
// (CODE, DATA, BSS, HEAP, OUTSIDE, CAVE). parse lists the lines, classify sorts them, toml prints
// [[patches.instructions]] for the code writes, override writes a game override module for the data writes
// (via console-mod/override's render_module), crc prints the CRC-32 / PCSX2 CRC / pnach file name.
namespace packtools::console_pnach {
using namespace pyrt;
using namespace packtools::console_common;
using console_override::hex32;
using console_override::c_string;
using console_override::slugify;

const char* const PROG = "pnach";
constexpr long long RAM_SIZE = 32LL * 1024 * 1024, RAM_MASK = RAM_SIZE - 1, KERNEL_END = 0x00080000;
constexpr uint32_t PF_X = 1, PF_W = 2, SHF_ALLOC = 2, SHF_EXECINSTR = 4, SHT_NOBITS = 8;

// ---------------------------------------------------------------- small helpers
inline std::string fmt_x(unsigned long long v, int width = 0) { return cfmt("%0*llX", width, v); }
inline std::string bytes_hex(const std::string& b) { std::string o; for (const unsigned char c : b) o += cfmt("%02X", c); return o; }
inline unsigned long long le_value(const std::string& b) { unsigned long long v = 0; for (std::size_t i = b.size(); i-- > 0;) v = (v << 8) | static_cast<unsigned char>(b[i]); return v; }
// int(text): optional sign, Unicode decimal digits, single underscores between them (saturates)
inline std::optional<long long> int10(const std::string& s0) {
    const auto v = cps(strip(s0)); std::size_t i = 0; bool neg = false;
    if (i < v.size() && (v[i] == "+" || v[i] == "-")) { neg = v[i] == "-"; ++i; }
    if (i >= v.size()) return std::nullopt;
    long long r = 0; bool prev = false;
    for (; i < v.size(); ++i) {
        if (v[i] == "_") { if (!prev || i + 1 >= v.size()) return std::nullopt; prev = false; continue; }
        const uint32_t c = cp_of(v[i]);
        if (!uprop::decimal(c)) return std::nullopt;
        uint32_t k = 0; while (k < c && uprop::decimal(c - k - 1)) ++k;
        if (r < LLONG_MAX / 100) r = r * 10 + static_cast<long long>(k % 10);
        prev = true;
    }
    return neg ? -r : r;
}
// os.path.splitext(name)[0] for a base name
inline std::string stem(const std::string& name) {
    const auto k = name.rfind('.');
    if (k == std::string::npos || k == 0) return name;
    if (name.find_first_not_of('.') >= k) return name;
    return name.substr(0, k);
}
// parse_hex: optional 0x, then hex digits only; kept as the significant digits so wide values print in full
struct Hex { std::string digits; };   // uppercase, no leading zeros, "0" for zero
inline Hex parse_hex(const std::string& text, const std::string& what) {
    std::string t = strip(text);
    if (t.size() >= 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) t = t.substr(2);
    bool okh = !t.empty();
    for (const char c : t) if (!std::isxdigit(static_cast<unsigned char>(c))) okh = false;
    if (!okh) throw InputError(what + " is not a hex number: " + str_repr(text));
    std::size_t a = t.find_first_not_of('0');
    std::string d = a == std::string::npos ? "0" : t.substr(a);
    for (auto& c : d) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return {d};
}
inline bool wider(const Hex& h, std::size_t nbytes) { return h.digits.size() > 2 * nbytes; }
inline unsigned long long low(const Hex& h, std::size_t nbytes) {
    const std::string d = h.digits.size() > 2 * nbytes ? h.digits.substr(h.digits.size() - 2 * nbytes) : h.digits;
    return std::stoull(d, nullptr, 16);
}
inline long long parse_addr_arg(const std::string& text, const std::string& what) {
    const auto v = int16(text);
    if (!v) throw InputError(what + " is not a hex number: " + str_repr(text));
    if (*v < 0 || *v > 0xFFFFFFFFLL) throw InputError(what + " is out of range: " + str_repr(text));
    return *v;
}
inline std::string read_input(const std::string& path, const std::string& what) {
    if (!is_file(path)) throw InputError(what + " not found: " + path);
    std::FILE* f = open_file(path, "rb");
    if (!f) throw InputError("cannot read " + what + " " + path + ": " + strerror_of(errno));
    std::string raw; char buf[65536]; std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) raw.append(buf, n);
    std::fclose(f);
    return raw;
}

// ---------------------------------------------------------------- PS2 address space
inline long long ram_offset(long long addr) {   // -1 = None
    addr &= 0xFFFFFFFFLL;
    if (addr < RAM_SIZE) return addr;
    if (0x20000000LL <= addr && addr < 0x40000000LL) return addr & RAM_MASK;
    if (0x80000000LL <= addr && addr < 0xC0000000LL) { const long long p = addr & 0x1FFFFFFF; return p < RAM_SIZE ? p : -1; }
    return -1;
}
inline std::string outside_reason(long long addr) {
    if (0x70000000LL <= addr && addr < 0x70004000LL) return "scratchpad (16 KB at 0x70000000)";
    const long long p = addr >= 0x80000000LL ? (addr & 0x1FFFFFFF) : addr;
    if (0x10000000LL <= p && p < 0x10010000LL) return "EE hardware registers";
    if (0x11000000LL <= p && p < 0x11010000LL) return "VU0/VU1 memory";
    if (0x12000000LL <= p && p < 0x12002000LL) return "GS privileged registers";
    if (0x1FC00000LL <= p && p < 0x20000000LL) return "BIOS ROM";
    return "not main RAM";
}

// ---------------------------------------------------------------- Ghidra CSV map
struct FuncMap {
    std::vector<std::tuple<long long, long long, std::string>> ranges; std::set<long long> starts;
    explicit FuncMap(const std::string& path) {
        const std::string text = decode_utf8_replace(read_input(path, "Ghidra map"));
        int n = 0;
        for (const auto& raw : splitlines(text)) {
            ++n;
            const std::string line = strip(raw);
            if (line.empty() || lower(line).rfind("name,", 0) == 0) continue;
            std::vector<std::string> parts; for (const auto& p : split(line, ",")) parts.push_back(strip(p));
            if (parts.size() < 3) throw InputError(cfmt("Ghidra map line %d is not Name,Start,End,Size: ", n) + str_repr(line));
            const auto s = int16(parts[1]), e = int16(parts[2]);
            if (!s || !e) throw InputError(cfmt("Ghidra map line %d has a bad address: ", n) + str_repr(line));
            if (*e > *s) ranges.emplace_back(*s, *e, parts[0]);
        }
        std::sort(ranges.begin(), ranges.end());
        for (const auto& r : ranges) starts.insert(std::get<0>(r));
    }
    bool covers(long long v) const {
        for (const auto& [s, e, nm] : ranges) { (void)nm; if (s <= v && v < e) return true; if (s > v) break; }
        return false;
    }
};

// ---------------------------------------------------------------- ELF32 little-endian MIPS reader
struct Seg { long long index, offset, vaddr, filesz, memsz, flags, ram; };
struct Sec { std::string name; long long type, flags, addr, size, ram; };
struct CodeTest { bool code; std::string conf, why; };
struct Elf {
    std::string name, data; long long entry = 0;
    std::vector<Seg> segments; std::vector<Sec> sections;
    explicit Elf(const std::string& path) {
        name = basename(path);
        data = read_input(path, "ELF");
        const long long len = static_cast<long long>(data.size());
        if (len < 52) throw InputError(cfmt("%s is too short to be an ELF (%lld bytes)", name.c_str(), len));
        if (data.compare(0, 4, "\x7f" "ELF") != 0) throw InputError(name + " is not an ELF file (bad magic)");
        if (data[4] != 1) throw InputError(name + " is not a 32-bit ELF (PS2 game ELFs are ELF32)");
        if (data[5] != 1) throw InputError(name + " is not little-endian (PS2 EE ELFs are little-endian)");
        const long long machine = u16le(data, 18);
        entry = u32le(data, 24);
        const long long phoff = u32le(data, 28), shoff = u32le(data, 32), phentsize = u16le(data, 42), phnum = u16le(data, 44);
        const long long shentsize = u16le(data, 46), shnum = u16le(data, 48), shstrndx = u16le(data, 50);
        if (machine != 8) throw InputError(cfmt("%s is not a MIPS ELF (machine %lld, the PS2 EE is 8)", name.c_str(), machine));
        if (phnum && phentsize < 32) throw InputError(cfmt("%s has a bad program header size (%lld)", name.c_str(), phentsize));
        if (phoff + phnum * phentsize > len) throw InputError(name + " is truncated: program headers run past the end of the file");
        for (long long i = 0; i < phnum; ++i) {
            const std::size_t at = static_cast<std::size_t>(phoff + i * phentsize);
            const long long p_type = u32le(data, at), off = u32le(data, at + 4), vaddr = u32le(data, at + 8), filesz = u32le(data, at + 16);
            const long long memsz = u32le(data, at + 20), flags = u32le(data, at + 24);
            if (p_type != 1 || memsz == 0) continue;
            if (filesz > memsz) throw InputError(cfmt("%s segment %lld has filesz > memsz", name.c_str(), i));
            if (off + filesz > len) throw InputError(cfmt("%s is truncated: segment %lld runs past the end of the file", name.c_str(), i));
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
                if (!(s[2] & SHF_ALLOC) || s[5] == 0) continue;
                std::string nm;
                if (s[0] < static_cast<long long>(strtab.size())) {
                    const auto end = strtab.find('\0', static_cast<std::size_t>(s[0]));
                    for (const unsigned char c : strtab.substr(static_cast<std::size_t>(s[0]), end == std::string::npos ? std::string::npos : end - static_cast<std::size_t>(s[0])))
                        nm += c < 0x80 ? std::string(1, static_cast<char>(c)) : std::string("\xEF\xBF\xBD");
                }
                sections.push_back({nm, s[1], s[2], s[3], s[5], ram_offset(s[3])});
            }
        }
    }
    long long pcsx2_crc() const { uint32_t c = 0; for (std::size_t i = 0; i + 4 <= data.size(); i += 4) c ^= u32le(data, i); return c; }
    long long crc32() const { return zip::crc32(data); }
    std::pair<const Seg*, bool> locate(long long ram) const {
        for (const auto& s : segments) {
            if (s.ram < 0) continue;
            if (s.ram <= ram && ram < s.ram + s.filesz) return {&s, true};
            if (s.ram + s.filesz <= ram && ram < s.ram + s.memsz) return {&s, false};
        }
        return {nullptr, false};
    }
    const Sec* section_at(long long ram) const { for (const auto& s : sections) if (s.ram >= 0 && s.ram <= ram && ram < s.ram + s.size) return &s; return nullptr; }
    std::optional<std::string> read_ram(long long ram, long long size) const {
        const auto [seg, backed] = locate(ram);
        if (!seg || !backed || ram + size > seg->ram + seg->filesz) return std::nullopt;
        return data.substr(static_cast<std::size_t>(seg->offset + (ram - seg->ram)), static_cast<std::size_t>(size));
    }
    long long vaddr_of(long long ram) const { const auto [seg, b] = locate(ram); (void)b; return seg ? ((seg->vaddr + (ram - seg->ram)) & 0xFFFFFFFFLL) : ram; }
    CodeTest is_code(long long ram, const FuncMap* funcs) const {
        const auto [seg, backed] = locate(ram);
        if (!seg || !backed) return {false, "high", ""};
        if (const Sec* sec = section_at(ram)) {
            const std::string nm = sec->name.empty() ? "?" : sec->name;
            if (sec->flags & SHF_EXECINSTR) return {true, "high", "section " + nm + " is executable"};
            return {false, "high", "section " + nm + " is data"};
        }
        if (funcs) {
            if (funcs->covers(vaddr_of(ram))) return {true, "high", "inside a function in the Ghidra map"};
            return {false, "medium", "no function in the Ghidra map covers it"};
        }
        if (seg->flags & PF_X) {
            if (seg->flags & PF_W) return {true, "low", cfmt("segment %lld is read-write-execute (code and data mixed, no sections)", seg->index)};
            return {true, "medium", cfmt("segment %lld is executable", seg->index)};
        }
        return {false, "high", ""};
    }
};

// ---------------------------------------------------------------- a tiny MIPS describer, only for readable comments
const char* const REG[32] = {"zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
                             "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"};
inline const char* op_name(uint32_t op) {
    static const std::map<uint32_t, const char*> OPS{{0x02, "j"}, {0x03, "jal"}, {0x04, "beq"}, {0x05, "bne"}, {0x06, "blez"}, {0x07, "bgtz"}, {0x08, "addi"},
        {0x09, "addiu"}, {0x0A, "slti"}, {0x0B, "sltiu"}, {0x0C, "andi"}, {0x0D, "ori"}, {0x0E, "xori"}, {0x0F, "lui"},
        {0x14, "beql"}, {0x15, "bnel"}, {0x16, "blezl"}, {0x17, "bgtzl"}, {0x18, "daddi"}, {0x19, "daddiu"},
        {0x1A, "ldl"}, {0x1B, "ldr"}, {0x1C, "mmi"}, {0x1E, "lq"}, {0x1F, "sq"}, {0x20, "lb"}, {0x21, "lh"}, {0x22, "lwl"},
        {0x23, "lw"}, {0x24, "lbu"}, {0x25, "lhu"}, {0x26, "lwr"}, {0x27, "lwu"}, {0x28, "sb"}, {0x29, "sh"}, {0x2A, "swl"},
        {0x2B, "sw"}, {0x2C, "sdl"}, {0x2D, "sdr"}, {0x2E, "swr"}, {0x2F, "cache"}, {0x31, "lwc1"}, {0x33, "pref"},
        {0x36, "lqc2"}, {0x37, "ld"}, {0x39, "swc1"}, {0x3E, "sqc2"}, {0x3F, "sd"}, {0x01, "regimm"}, {0x10, "cop0"},
        {0x11, "cop1"}, {0x12, "cop2"}};
    const auto it = OPS.find(op); return it == OPS.end() ? nullptr : it->second;
}
inline const char* special_name(uint32_t fn) {
    static const std::map<uint32_t, const char*> SP{{0x00, "sll"}, {0x02, "srl"}, {0x03, "sra"}, {0x04, "sllv"}, {0x06, "srlv"}, {0x07, "srav"}, {0x08, "jr"},
        {0x09, "jalr"}, {0x0A, "movz"}, {0x0B, "movn"}, {0x0C, "syscall"}, {0x0D, "break"}, {0x0F, "sync"},
        {0x10, "mfhi"}, {0x11, "mthi"}, {0x12, "mflo"}, {0x13, "mtlo"}, {0x18, "mult"}, {0x19, "multu"}, {0x1A, "div"},
        {0x1B, "divu"}, {0x20, "add"}, {0x21, "addu"}, {0x22, "sub"}, {0x23, "subu"}, {0x24, "and"}, {0x25, "or"},
        {0x26, "xor"}, {0x27, "nor"}, {0x2A, "slt"}, {0x2B, "sltu"}, {0x2D, "daddu"}, {0x2F, "dsubu"}, {0x38, "dsll"},
        {0x3A, "dsrl"}, {0x3C, "dsll32"}, {0x3E, "dsrl32"}, {0x3F, "dsra32"}};
    const auto it = SP.find(fn); return it == SP.end() ? "special" : it->second;
}
inline bool in(uint32_t v, std::initializer_list<uint32_t> xs) { return std::find(xs.begin(), xs.end(), v) != xs.end(); }
inline std::string describe(uint32_t word, std::optional<long long> vaddr) {
    const uint32_t op = word >> 26, rs = (word >> 21) & 31, rt = (word >> 16) & 31, rd = (word >> 11) & 31, imm = word & 0xFFFF;
    if (word == 0) return "nop";
    if (op == 0) {
        const uint32_t fn = word & 0x3F;
        if (fn == 0x08) return std::string("jr $") + REG[rs];
        if (fn == 0x0C) return "syscall";
        if (in(fn, {0x21, 0x25, 0x2D}) && rt == 0) return std::string("move $") + REG[rd] + ", $" + REG[rs];
        return special_name(fn);
    }
    if (op == 0x02 || op == 0x03) {
        long long target = static_cast<long long>(word & 0x03FFFFFF) << 2;
        if (vaddr) target |= (*vaddr + 4) & 0xF0000000LL;
        return std::string(op_name(op)) + cfmt(" 0x%08llX", static_cast<unsigned long long>(target));
    }
    if (in(op, {0x04, 0x05, 0x14, 0x15, 0x06, 0x07, 0x16, 0x17})) {
        const std::string regs = in(op, {0x04, 0x05, 0x14, 0x15}) ? std::string("$") + REG[rs] + ", $" + REG[rt] : std::string("$") + REG[rs];
        const long long off = static_cast<long long>(imm & 0x8000 ? static_cast<long long>(imm) - 0x10000 : imm) * 4;
        const std::string where = vaddr ? cfmt("0x%08llX", static_cast<unsigned long long>((*vaddr + 4 + off) & 0xFFFFFFFFLL)) : cfmt("%+lld", off + 4);
        return std::string(op_name(op)) + " " + regs + ", " + where;
    }
    if (op == 0x0F) return std::string("lui $") + REG[rt] + cfmt(", 0x%04X", imm);
    if (in(op, {0x09, 0x0D, 0x08, 0x0C, 0x0E})) return std::string(op_name(op)) + " $" + REG[rt] + ", $" + REG[rs] + cfmt(", 0x%04X", imm);
    if (op == 0x11) {
        if (rs == 0x04) return std::string("mtc1 $") + REG[rt] + cfmt(", $f%u", rd);
        if (rs == 0x00) return std::string("mfc1 $") + REG[rt] + cfmt(", $f%u", rd);
        return "cop1";
    }
    if (in(op, {0x20, 0x21, 0x23, 0x24, 0x25, 0x27, 0x28, 0x29, 0x2B, 0x31, 0x37, 0x39, 0x3F, 0x1E, 0x1F})) {
        const std::string reg = in(op, {0x31, 0x39}) ? cfmt("$f%u", rt) : std::string("$") + REG[rt];
        const long long off = imm & 0x8000 ? static_cast<long long>(imm) - 0x10000 : imm;
        return std::string(op_name(op)) + " " + reg + ", " + (off < 0 ? cfmt("-0x%llX", static_cast<unsigned long long>(-off)) : cfmt("0x%llX", static_cast<unsigned long long>(off))) + "($" + REG[rs] + ")";
    }
    const char* n = op_name(op);
    return n ? n : cfmt("op%02X", op);
}
inline std::optional<long long> jump_target(uint32_t word, long long vaddr) {
    const uint32_t op = word >> 26;
    if (op == 0x02 || op == 0x03) return ((vaddr + 4) & 0xF0000000LL) | (static_cast<long long>(word & 0x03FFFFFF) << 2);
    if (in(op, {0x04, 0x05, 0x06, 0x07, 0x14, 0x15, 0x16, 0x17, 0x01})) {
        const uint32_t imm = word & 0xFFFF; const long long off = imm & 0x8000 ? static_cast<long long>(imm) - 0x10000 : imm;
        return (vaddr + 4 + off * 4) & 0xFFFFFFFFLL;
    }
    return std::nullopt;
}
// direct jal / j references to `target` in the ELF's code (instruction addresses)
inline std::pair<std::vector<long long>, std::vector<long long>> call_sites(const Elf& elf, long long target) {
    std::vector<long long> jal, j;
    if (target & 3) return {jal, j};
    std::vector<std::tuple<long long, long long, long long>> regions;
    for (const auto& sec : elf.sections) {
        if ((sec.flags & SHF_EXECINSTR) && sec.type != SHT_NOBITS && sec.ram >= 0) {
            const auto [seg, backed] = elf.locate(sec.ram);
            if (seg && backed) regions.emplace_back(sec.addr, seg->offset + sec.ram - seg->ram, std::min(sec.size, seg->ram + seg->filesz - sec.ram));
        }
    }
    if (regions.empty()) for (const auto& s : elf.segments) if (s.flags & PF_X) regions.emplace_back(s.vaddr, s.offset, s.filesz);
    const uint32_t index = static_cast<uint32_t>((target >> 2) & 0x03FFFFFF);
    const long long len = static_cast<long long>(elf.data.size());
    for (const auto& [base, off, size] : regions) {
        if (((base + 4) & 0xF0000000LL) != (target & 0xF0000000LL)) continue;
        for (const auto& [op, list] : {std::pair<uint32_t, std::vector<long long>*>{3, &jal}, {2, &j}}) {
            const uint32_t w = (op << 26) | index;
            const char needle[4] = {static_cast<char>(w & 0xFF), static_cast<char>((w >> 8) & 0xFF), static_cast<char>((w >> 16) & 0xFF), static_cast<char>(w >> 24)};
            const long long end = std::min(off + size, len);
            for (long long i = off; i + 4 <= end; ++i) {
                if (std::memcmp(elf.data.data() + i, needle, 4) == 0 && (i - off) % 4 == 0) list->push_back(base + i - off);
            }
        }
    }
    return {jal, j};
}
inline std::pair<std::vector<std::string>, bool> hook_advice(const Elf& elf, long long addr) {
    const auto [jal, j] = call_sites(elf, addr);
    std::vector<std::string> lines{hex32(addr) + cfmt(" is reached by %zu jal and %zu j instruction(s) in the ELF.", jal.size(), j.size())};
    bool serious = false;
    if (!j.empty()) {
        std::vector<std::string> js; for (std::size_t k = 0; k < j.size() && k < 4; ++k) js.push_back(hex32(j[k]));
        lines.push_back(cfmt("%zu j tail jump(s) to it (", j.size()) + join(js, ", ") + ") compile to a direct C++ call that skips the function table: the hook does not run on those paths.");
    }
    if (!j.empty() && jal.empty()) {
        serious = true;
        lines.push_back("Nothing reaches it with jal: pick the caller one level up (a function called with jal) or check the callers in Ghidra.");
    } else if (jal.empty() && j.empty()) {
        lines.push_back("No direct jal or j: fine if it is called through a pointer (jalr goes through the table); check its references in Ghidra.");
    }
    return {lines, serious};
}
inline std::string patch_class_of(uint32_t original) {
    const uint32_t op = original >> 26;
    if (op == 0 && (original & 0x3F) == 0x0C) return "syscall";
    if (op == 0x10) return "cop0";
    if (op == 0x2F) return "cache";
    return "generic";
}

// ---------------------------------------------------------------- .pnach parser
inline const char* place_text(int p) {
    static const char* const P[] = {"once on load", "continuously (every vsync)", "on load and continuously", "on load or when enabled"};
    return P[p];
}
inline std::size_t size_of(const std::string& t) {
    static const std::map<std::string, std::size_t> S{{"byte", 1}, {"short", 2}, {"word", 4}, {"double", 8}, {"beshort", 2}, {"beword", 4}, {"bedouble", 8}};
    const auto it = S.find(t); return it == S.end() ? 0 : it->second;
}
inline std::string ext_name(long long cmd) {
    static const std::map<long long, const char*> E{{0x0, "8-bit write"}, {0x1, "16-bit write"}, {0x2, "32-bit write"}, {0x3, "increment/decrement"},
        {0x4, "serial (repeated) write"}, {0x5, "copy bytes"}, {0x6, "pointer write"}, {0x7, "bitwise operation"},
        {0xC, "32-bit conditional (gates every following code)"}, {0xD, "16-bit conditional (gates the next code)"}, {0xE, "multi-line conditional"}};
    const auto it = E.find(cmd); return it == E.end() ? cfmt("unsupported CodeBreaker command %llX", static_cast<unsigned long long>(cmd)) : it->second;
}
struct Write { int line = 0; std::string section; int place = 0; std::string cpu, type, source, value; long long addr = 0; std::string data, extended; std::vector<std::string> notes; };
struct Manual { int line = 0; std::string section, reason, source; };
struct BadLine { int line; std::string text, error; };
struct Pnach {
    std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> meta;   // key -> [(section, value)]
    std::vector<std::string> sections; std::vector<Write> writes; std::vector<Manual> manual; std::vector<BadLine> errors;
    std::string name;
    void add_meta(const std::string& k, const std::string& section, const std::string& v) {
        for (auto& [key, vals] : meta) if (key == k) { vals.emplace_back(section, v); return; }
        meta.push_back({k, {{section, v}}});
    }
};
inline Manual manual_of(const Write& w, const std::string& reason) { return {w.line, w.section, reason, w.source}; }

inline Pnach parse_pnach_text(const std::string& text) {
    Pnach r; std::string current;
    struct Ext { long long cont = 0; int cont_owner = 0; std::string cont_name; bool ptr_second = false; long long cond_lines = 0; bool cond_all = false; int cond_owner = 0; } ext;
    int n = 0;
    static const std::set<std::string> META{"gametitle", "comment", "author", "description", "gsaspectratio", "gsinterlacemode"};
    for (const auto& raw : splitlines(text)) {
        ++n;
        const std::string line = strip(raw);
        if (line.empty() || line.rfind("//", 0) == 0 || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[' && line.find(']') != std::string::npos) {
            current = strip(line.substr(1, line.find(']') - 1));
            if (std::find(r.sections.begin(), r.sections.end(), current) == r.sections.end()) r.sections.push_back(current);
            ext = Ext{};
            continue;
        }
        const auto bad = [&](const std::string& e) { r.errors.push_back({n, line, e}); };
        const auto eq = line.find('=');
        if (eq == std::string::npos) { bad("not key=value"); continue; }
        const std::string key = lower(strip(line.substr(0, eq))), value = strip(line.substr(eq + 1));
        if (META.count(key)) { r.add_meta(key, current, value); continue; }
        if (key == "dpatch") { r.manual.push_back({n, current, "dpatch (pattern-matched dynamic patch): port by hand once you know the address", line}); continue; }
        if (key != "patch") { r.add_meta(key, current, value); continue; }
        const std::string body = strip(split(value, "//", 1)[0]);
        std::vector<std::string> parts; for (const auto& p : split(body, ",")) parts.push_back(strip(p));
        if (parts.size() != 5) { bad("patch= needs 5 fields: place,cpu,address,type,value"); continue; }
        const std::string ptype = lower(parts[3]), cpu = upper(parts[1]), val_s = parts[4];
        const auto place = int10(parts[0]);
        if (!place || *place < 0 || *place > 3) { bad("place must be 0, 1, 2 or 3"); continue; }
        if (cpu != "EE" && cpu != "IOP") { bad("cpu must be EE or IOP"); continue; }
        Hex addr_h, val;
        try {
            addr_h = parse_hex(parts[2], "address");
            if (ptype != "bytes") val = parse_hex(val_s, "value");
        } catch (const InputError& e) { bad(e.what()); continue; }
        if (wider(addr_h, 4)) { bad("address wider than 32 bits"); continue; }
        const long long addr = static_cast<long long>(low(addr_h, 4));
        Write base; base.line = n; base.section = current; base.place = static_cast<int>(*place); base.cpu = cpu; base.type = ptype; base.source = line; base.value = val_s;
        if (cpu == "IOP") { r.manual.push_back(manual_of(base, "IOP patch: the port runs IRX modules in ps2xIOP; port by hand")); continue; }
        if (ptype == "bytes") {
            const std::string hx = lower(val_s).rfind("0x", 0) == 0 ? val_s.substr(2) : val_s;
            bool okb = !hx.empty() && hx.size() % 2 == 0;
            for (const char c : hx) if (!std::isxdigit(static_cast<unsigned char>(c))) okb = false;
            if (!okb) { bad("bytes value must be an even-length hex string"); continue; }
            base.addr = addr;
            for (std::size_t k = 0; k < hx.size(); k += 2) base.data += static_cast<char>(std::stoi(hx.substr(k, 2), nullptr, 16));
            r.writes.push_back(base);
            continue;
        }
        if (const std::size_t size = size_of(ptype)) {
            if (wider(val, size)) base.notes.push_back("value 0x" + val.digits + " is wider than a " + ptype + cfmt("; PCSX2 keeps the low %zu bytes", size));
            const unsigned long long v = low(val, size);
            std::string d; for (std::size_t k = 0; k < size; ++k) d += static_cast<char>((v >> (8 * k)) & 0xFF);
            if (ptype.rfind("be", 0) == 0) std::reverse(d.begin(), d.end());
            base.addr = addr; base.data = d;
            r.writes.push_back(base);
            continue;
        }
        if (ptype != "extended") { bad("unknown type " + str_repr(ptype)); continue; }
        // CodeBreaker-style extended code: the address's top nibble is the command
        if (ext.cont > 0) {
            ext.cont -= 1;
            if (ext.ptr_second) { ext.ptr_second = false; const long long levels = std::max(addr & 0xFFFF, 1LL); ext.cont += levels / 2; }
            if (ext.cond_lines > 0) ext.cond_lines -= 1;
            r.manual.push_back(manual_of(base, "second line of the " + ext.cont_name + cfmt(" code on line %d", ext.cont_owner)));
            continue;
        }
        const long long cmd = addr >> 28;
        const bool gated = ext.cond_all || ext.cond_lines > 0;
        const int gate_owner = ext.cond_owner;
        if (ext.cond_lines > 0) ext.cond_lines -= 1;
        const std::string nm = ext_name(cmd);
        if (cmd <= 2) {
            if (gated) { r.manual.push_back(manual_of(base, nm + cfmt(" that only runs when the conditional on line %d is true", gate_owner))); continue; }
            const std::size_t size = cmd == 0 ? 1 : cmd == 1 ? 2 : 4;
            const unsigned long long v = low(val, size);
            std::string d; for (std::size_t k = 0; k < size; ++k) d += static_cast<char>((v >> (8 * k)) & 0xFF);
            base.addr = addr & 0x0FFFFFFF; base.data = d;
            std::string vs = val.digits; while (vs.size() < 8) vs = "0" + vs;
            base.extended = nm + cfmt(" (code %08llX ", static_cast<unsigned long long>(addr)) + vs + ")";
            r.writes.push_back(base);
            continue;
        }
        std::string reason = "extended " + nm + ": needs a manual port";
        if (gated) reason += cfmt(" (and it is gated by the conditional on line %d)", gate_owner);
        if (cmd == 3) { if (in(static_cast<uint32_t>((addr >> 20) & 0xFF), {4, 5})) { ext.cont = 1; ext.cont_owner = n; ext.cont_name = nm; } }
        else if (cmd == 4 || cmd == 5) { ext.cont = 1; ext.cont_owner = n; ext.cont_name = nm; }
        else if (cmd == 6) { ext.cont = 1; ext.cont_owner = n; ext.cont_name = nm; ext.ptr_second = true; }
        else if (cmd == 0xC) { ext.cond_all = true; ext.cond_owner = n; }
        else if (cmd == 0xD) { ext.cond_lines += 1; ext.cond_owner = n; }
        else if (cmd == 0xE) { ext.cond_lines += (addr >> 16) & 0xFF; ext.cond_owner = n; }
        r.manual.push_back(manual_of(base, reason));
    }
    return r;
}

inline Pnach load_pnach(const std::string& path, const std::vector<std::string>& only) {
    const std::string raw = read_input(path, "pnach");
    if (raw.substr(0, 4096).find('\0') != std::string::npos) throw InputError(basename(path) + " looks binary, not a .pnach text file");
    Pnach r = parse_pnach_text(decode_utf8_replace(raw));
    r.name = basename(path);
    if (!only.empty()) {
        std::vector<std::string> missing;
        for (const auto& s : only) if (!s.empty() && std::find(r.sections.begin(), r.sections.end(), s) == r.sections.end()) missing.push_back(s);
        if (!missing.empty()) {
            std::vector<std::string> have; for (const auto& s : r.sections) have.push_back(s);
            throw InputError("section(s) not in " + r.name + ": " + join(missing, ", ") + " (have: " + (have.empty() ? std::string("none") : join(have, ", ")) + ")");
        }
        const std::set<std::string> wanted(only.begin(), only.end());
        std::vector<Write> w; for (const auto& x : r.writes) if (wanted.count(x.section)) w.push_back(x);
        std::vector<Manual> m; for (const auto& x : r.manual) if (wanted.count(x.section)) m.push_back(x);
        r.writes = w; r.manual = m;
    }
    return r;
}
inline std::optional<long long> crc_from_pnach_name(const std::string& name) {
    static const std::string P = R"(^(?:[A-Za-z]{4}[-_]\d{5}_)?([0-9A-Fa-f]{8})\.pnach$)";
    const auto m = re::match(P, name, re::I);
    if (!m) return std::nullopt;
    return int16(m.group(1));
}
inline std::optional<std::string> serial_from_name(const std::string& name) {
    static const std::string P = R"(^([A-Za-z]{4})[_-](\d{3})\.?(\d{2}))";
    const auto m = re::match(P, name);
    if (!m) return std::nullopt;
    std::string a = m.group(1); for (auto& c : a) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return a + "-" + m.group(2) + m.group(3);
}
inline std::optional<bool> check_crc(const Pnach& pn, const Elf& elf, bool ignore) {
    const auto want = crc_from_pnach_name(pn.name);
    if (!want) return std::nullopt;
    const long long have = elf.pcsx2_crc();
    if (*want != have && !ignore)
        throw InputError(pn.name + cfmt(" is for the build with PCSX2 CRC %08llX but ", static_cast<unsigned long long>(*want)) + elf.name
                         + cfmt(" has %08llX: the addresses will not line up (use --ignore-crc only if you are sure)", static_cast<unsigned long long>(have)));
    return *want == have;
}

// ---------------------------------------------------------------- classification
struct Item { std::size_t w; long long ram; std::string data, cls, conf, why; };
struct CodeWord { long long ram, vaddr; uint32_t original, neww; std::vector<int> lines; std::vector<std::string> sources, sections; std::string conf, why; std::vector<std::string> notes; };
struct Result { std::vector<Item> items; std::vector<CodeWord> code; std::vector<Manual> manual; };

inline std::vector<std::pair<long long, long long>> merge_ranges(std::vector<std::pair<long long, long long>> r) {
    std::sort(r.begin(), r.end());
    std::vector<std::pair<long long, long long>> o;
    for (const auto& [a, b] : r) { if (!o.empty() && a <= o.back().second) o.back().second = std::max(o.back().second, b); else o.emplace_back(a, b); }
    return o;
}
inline std::vector<CodeWord> code_words(const std::vector<Item>& items, const Pnach& pn, const Elf& elf) {
    struct W { std::string orig, neww; std::vector<int> lines; std::vector<std::string> sources, sections; std::string conf, why; };
    std::map<long long, W> words;
    for (const auto& it : items) {
        if (it.cls != "CODE") continue;
        for (std::size_t i = 0; i < it.data.size(); ++i) {
            const long long r = it.ram + static_cast<long long>(i), wr = r & ~3LL;
            auto f = words.find(wr);
            if (f == words.end()) {
                const std::string orig = elf.read_ram(wr, 4).value_or(std::string(4, '\0'));
                f = words.emplace(wr, W{orig, orig, {}, {}, {}, it.conf, it.why}).first;
            }
            W& cw = f->second;
            cw.neww[static_cast<std::size_t>(r - wr)] = it.data[i];
            const Write& w = pn.writes[it.w];
            if (std::find(cw.lines.begin(), cw.lines.end(), w.line) == cw.lines.end()) { cw.lines.push_back(w.line); cw.sources.push_back(w.source); }
            if (std::find(cw.sections.begin(), cw.sections.end(), w.section) == cw.sections.end()) cw.sections.push_back(w.section);
        }
    }
    std::vector<CodeWord> code;
    for (const auto& [wr, cw] : words) {
        const uint32_t o = u32le(cw.orig, 0), nw = u32le(cw.neww, 0);
        CodeWord e{wr, elf.vaddr_of(wr), o, nw, cw.lines, cw.sources, cw.sections, cw.conf, cw.why, {}};
        if (o == nw) e.notes.push_back("no change: the ELF already holds this value");
        const std::string pc = patch_class_of(o);
        if (pc != "generic") {
            const std::string flag = pc == "syscall" ? "patch_syscalls" : pc == "cop0" ? "patch_cop0" : "patch_cache";
            e.notes.push_back("the original is a " + upper(pc) + " instruction: ps2_recomp only applies this patch when " + flag + " = true in [general]");
        }
        code.push_back(e);
    }
    return code;
}

inline Result classify(const Pnach& pn, const Elf& elf, const FuncMap* funcs) {
    Result res; res.manual = pn.manual;
    for (std::size_t wi = 0; wi < pn.writes.size(); ++wi) {
        const Write& w = pn.writes[wi];
        const long long ram = ram_offset(w.addr);
        if (ram < 0 || ram + static_cast<long long>(w.data.size()) > RAM_SIZE) {
            res.manual.push_back(manual_of(w, "OUTSIDE: " + outside_reason(w.addr) + " at " + hex32(w.addr) + ": no RDRAM write can reach it, port by hand"));
            continue;
        }
        if (ram < KERNEL_END) {
            res.manual.push_back(manual_of(w, "OUTSIDE: EE kernel memory at " + hex32(w.addr) + cfmt(" (below 0x%08llX): the port's kernel is ", static_cast<unsigned long long>(KERNEL_END))
                                              + "high-level emulation, so this write does not do what it did on a PS2"));
            continue;
        }
        struct Run { std::string cls, conf, why; std::size_t start, end; };
        std::vector<Run> runs;
        for (std::size_t i = 0; i < w.data.size(); ++i) {
            const long long r = ram + static_cast<long long>(i);
            const auto [seg, backed] = elf.locate(r);
            std::string cls, conf, why;
            if (!seg) { cls = "HEAP"; conf = "high"; why = "RAM outside every ELF segment (heap, stack or loaded files)"; }
            else if (!backed) { cls = "BSS"; conf = "high"; why = cfmt("zero-filled part of segment %lld", seg->index); }
            else {
                const Sec* sec = elf.section_at(r);
                if (sec && sec->type == SHT_NOBITS) { cls = "BSS"; conf = "high"; why = "section " + sec->name + " (zero-filled)"; }
                else {
                    const CodeTest t = elf.is_code(r, funcs);
                    conf = t.conf; why = t.why; cls = t.code ? "CODE" : "DATA";
                    if (!t.code && why.empty()) why = cfmt("file-backed data in segment %lld", seg->index);
                }
            }
            if (!runs.empty() && runs.back().cls == cls && runs.back().end == i) runs.back().end = i + 1;
            else runs.push_back({cls, conf, why, i, i + 1});
        }
        for (const auto& run : runs) res.items.push_back({wi, ram + static_cast<long long>(run.start), w.data.substr(run.start, run.end - run.start), run.cls, run.conf, run.why});
    }
    // code caves: new code written into memory, or jumps and branches into patched areas
    std::vector<std::pair<long long, long long>> spans;
    for (const auto& it : res.items) spans.emplace_back(it.ram, it.ram + static_cast<long long>(it.data.size()));
    const auto written = merge_ranges(spans);
    const auto block_of = [&](long long r) -> std::optional<std::pair<long long, long long>> {
        for (const auto& b : written) if (b.first <= r && r < b.second) return b;
        return std::nullopt;
    };
    std::set<std::pair<long long, long long>> cave_blocks;
    std::map<int, std::string> cave_lines;
    res.code = code_words(res.items, pn, elf);
    for (const auto& cw : res.code) {
        if (cw.neww == cw.original) continue;
        if (funcs && !funcs->covers(cw.vaddr)) {
            for (const int ln : cw.lines) cave_lines.emplace(ln, "code written at " + hex32(cw.vaddr) + ", where no function in the Ghidra map is (ps2_recomp never decodes it)");
            continue;
        }
        const auto tgt = jump_target(cw.neww, cw.vaddr);
        if (!tgt) continue;
        const long long tr = ram_offset(*tgt);
        if (tr < 0) continue;
        const std::string kind = split_ws(describe(cw.neww, cw.vaddr))[0];
        std::string why;
        if (const auto blk = block_of(tr)) { cave_blocks.insert(*blk); why = kind + " to " + hex32(*tgt) + " lands in memory this pnach writes"; }
        else if (!elf.is_code(tr, funcs).code) why = kind + " to " + hex32(*tgt) + " lands outside the game's code";
        else if (funcs && in(cw.neww >> 26, {0x02, 0x03}) && !funcs->covers(*tgt)) why = kind + " to " + hex32(*tgt) + " lands where no function in the Ghidra map is";
        if (!why.empty()) for (const int ln : cw.lines) cave_lines.emplace(ln, why);
    }
    for (const auto& [a, b] : written) {
        if (cave_blocks.count({a, b}) || b - a < 8) continue;
        bool any = false;
        std::string buf(static_cast<std::size_t>(b - a), '\0');
        for (const auto& it : res.items) {
            if (!(a <= it.ram && it.ram < b)) continue;
            if (it.cls == "DATA" || it.cls == "BSS" || it.cls == "HEAP") any = true;
            buf.replace(static_cast<std::size_t>(it.ram - a), it.data.size(), it.data);
        }
        if (!any) continue;
        for (std::size_t off = static_cast<std::size_t>((4 - a % 4) % 4); off + 3 < buf.size(); off += 4) {
            if (u32le(buf, off) == 0x03E00008u) { cave_blocks.insert({a, b}); break; }   // jr $ra: a function body
        }
    }
    for (const auto& it : res.items) {
        for (const auto& [a, b] : cave_blocks) if (a <= it.ram && it.ram < b) { cave_lines.emplace(pn.writes[it.w].line, "new code written at " + hex32(it.ram)); break; }
    }
    if (!cave_lines.empty()) {
        for (auto& it : res.items) if (cave_lines.count(pn.writes[it.w].line)) it.cls = "CAVE";
        res.code = code_words(res.items, pn, elf);
        std::set<int> seen;
        for (const auto& it : res.items) {
            const Write& w = pn.writes[it.w];
            if (it.cls == "CAVE" && !seen.count(w.line)) {
                seen.insert(w.line);
                res.manual.push_back(manual_of(w, "CAVE: " + cave_lines[w.line] + "; rewrite its logic as an override function (override.py new --replace or --wrap)"));
            }
        }
    }
    std::stable_sort(res.manual.begin(), res.manual.end(), [](const Manual& x, const Manual& y) { return x.line < y.line; });
    return res;
}

// ---------------------------------------------------------------- output builders
inline std::string value_text(const std::string& d) {
    if (d.size() == 1 || d.size() == 2 || d.size() == 4 || d.size() == 8) return "0x" + fmt_x(le_value(d), static_cast<int>(2 * d.size()));
    return bytes_hex(d);
}
inline std::vector<std::string> write_bucket(const Item& it, const Pnach& pn) {
    const int place = pn.writes[it.w].place; const bool every = place == 1 || place == 2;
    if (it.cls == "DATA" || (it.cls == "CODE" && it.conf == "low")) return every ? std::vector<std::string>{"load", "every"} : std::vector<std::string>{"load"};
    if (it.cls == "BSS" || it.cls == "HEAP") return {every ? "every" : "first"};
    return {};
}
inline std::size_t code_changes(const Result& res) { std::size_t n = 0; for (const auto& cw : res.code) if (cw.original != cw.neww) ++n; return n; }

inline std::string toml_text(const Pnach& pn, const Elf& elf, const Result& res) {
    std::vector<std::string> o{"# Console skill: instruction patches from " + pn.name + " for " + elf.name,
        "# Paste at the end of the game's ps2_recomp TOML (as top-level [[patches.instructions]] blocks),",
        "# then re-run ps2_recomp and rebuild. ps2_recomp only patches words inside functions it recompiles."};
    std::set<std::string> gates;
    for (const auto& cw : res.code) {
        if (cw.original == cw.neww) continue;
        o.push_back("");
        std::vector<std::string> ss; for (const auto& s : cw.sections) if (!s.empty()) ss.push_back("[" + s + "]");
        const std::string sec = join(ss, ", ");
        for (std::size_t k = 0; k < cw.lines.size(); ++k) o.push_back("# " + (sec.empty() ? "" : sec + " ") + cfmt("line %d: ", cw.lines[k]) + cw.sources[k]);
        o.push_back("# original " + hex32(cw.original) + " (" + describe(cw.original, cw.vaddr) + ") -> new " + hex32(cw.neww) + " (" + describe(cw.neww, cw.vaddr) + ")");
        for (const auto& note : cw.notes) {
            o.push_back("# NOTE: " + note);
            const auto k = note.find("patch_");
            if (k != std::string::npos) gates.insert(split_ws(note.substr(k + 6))[0]);
        }
        if (cw.conf == "low") o.push_back("# NOTE: " + cw.why + "; the override also writes it to RDRAM in case it is data");
        o.push_back("[[patches.instructions]]");
        o.push_back("address = \"" + hex32(cw.vaddr) + "\"");
        o.push_back("value = \"" + hex32(cw.neww) + "\"");
    }
    if (!code_changes(res)) { o.push_back(""); o.push_back("# No code patches: nothing in this pnach lands in the game's code."); }
    std::size_t others = 0; for (const auto& it : res.items) if (it.cls == "DATA" || it.cls == "BSS" || it.cls == "HEAP") ++others;
    if (others) { o.push_back(""); o.push_back(cfmt("# %zu data/BSS/heap write(s) are not instruction patches: use  pnach.py override  for them.", others)); }
    if (!res.manual.empty()) o.push_back(cfmt("# %zu line(s) need a manual port: see  pnach.py classify.", res.manual.size()));
    if (!gates.empty()) { std::vector<std::string> g; for (const auto& x : gates) g.push_back("patch_" + x); o.push_back("# Some originals are gated: set " + join(g, " and ") + " = true in [general] or those patches are skipped."); }
    return join(o, "\n") + "\n";
}

inline void emit_write(std::vector<std::string>& lines, const Item& it, const Pnach& pn, const std::string& ind = "    ") {
    const Write& w = pn.writes[it.w];
    lines.push_back(ind + "// " + (w.section.empty() ? "" : "[" + w.section + "] ") + cfmt("line %d (%s): ", w.line, place_text(w.place)) + replace_all(w.source, "*/", "* /"));
    if (it.cls == "CODE") lines.push_back(ind + "// also an instruction patch in the TOML; written here too because " + it.why);
    const auto& d = it.data; const unsigned long long ram = static_cast<unsigned long long>(it.ram);
    if (d.size() == 1) lines.push_back(ind + cfmt("Ps2FastWrite8(rdram, 0x%08llXu, 0x%02llXu);", ram, le_value(d)));
    else if (d.size() == 2) lines.push_back(ind + cfmt("Ps2FastWrite16(rdram, 0x%08llXu, 0x%04llXu);", ram, le_value(d)));
    else if (d.size() == 4) lines.push_back(ind + cfmt("Ps2FastWrite32(rdram, 0x%08llXu, 0x%08llXu);", ram, le_value(d)));
    else if (d.size() == 8) lines.push_back(ind + cfmt("Ps2FastWrite64(rdram, 0x%08llXu, 0x%016llXULL);", ram, le_value(d)));
    else {
        std::vector<std::string> arr; for (const unsigned char c : d) arr.push_back(cfmt("0x%02X", c));
        lines.push_back(ind + "{");
        lines.push_back(ind + "    static const uint8_t kBytes[] = {" + join(arr, ", ") + "};");
        lines.push_back(ind + "    for (uint32_t i = 0; i < sizeof(kBytes); ++i)");
        lines.push_back(ind + cfmt("        Ps2FastWrite8(rdram, 0x%08llXu + i, kBytes[i]);", ram));
        lines.push_back(ind + "}");
    }
}

struct Parts { std::vector<std::string> fn, apply; std::map<std::string, std::size_t> counts; };
inline Parts override_parts(const Pnach& pn, const Result& res, std::optional<long long> frame_hook, const std::string& mod_name) {
    std::map<std::string, std::vector<const Item*>> buckets{{"load", {}}, {"first", {}}, {"every", {}}};
    for (const auto& it : res.items) for (const auto& b : write_bucket(it, pn)) buckets[b].push_back(&it);
    Parts p;
    const auto func = [&](const std::string& name, const std::vector<std::string>& comment, const std::vector<const Item*>& items, bool unused) {
        for (const auto& c : comment) p.fn.push_back("// " + c);
        p.fn.push_back(std::string(unused ? "[[maybe_unused]] " : "") + "void " + name + "(uint8_t *rdram)");
        p.fn.push_back("{");
        p.fn.push_back("    (void)rdram;");
        for (const Item* it : items) emit_write(p.fn, *it, pn);
        p.fn.push_back("}");
        p.fn.push_back("");
    };
    const bool hook = frame_hook.has_value();
    const std::string unwired = "NOT WIRED UP: no --frame-hook was given (see the note at the top).";
    func("applyLoadWrites", {"Data patches: written into RDRAM once, when the ELF is loaded (before the game's", "first instruction)."}, buckets["load"], false);
    std::vector<std::string> c1{"Zero-filled (BSS) or heap memory the game clears during boot: written once,", "on the first call of the frame hook, after boot cleared it."};
    if (!hook) c1.push_back(unwired);
    func("applyFirstFrameWrites", c1, buckets["first"], !hook);
    std::vector<std::string> c2{"Patches PCSX2 re-applies every vsync (place 1 and 2) outside the code:", "written on every call of the frame hook, before the game's frame function runs."};
    if (!hook) c2.push_back(unwired);
    func("applyEveryFrameWrites", c2, buckets["every"], !hook);
    p.apply = {"    applyLoadWrites(rdram);"};
    if (hook) {
        const unsigned long long h = static_cast<unsigned long long>(*frame_hook);
        p.fn.insert(p.fn.end(), {cfmt("constexpr uint32_t kFrameHookAddress = 0x%08llXu;", h), "PS2Runtime::RecompiledFunction g_originalFrameFunction = nullptr;",
            "bool g_firstFrameDone = false;", "", "// Runs instead of the game's once-per-frame function, then calls the real one.",
            "void frameHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)", "{", "    if (!g_firstFrameDone)", "    {",
            "        g_firstFrameDone = true;", "        applyFirstFrameWrites(rdram);", "    }", "    applyEveryFrameWrites(rdram);",
            "    g_originalFrameFunction(rdram, ctx, runtime);", "}"});
        p.apply.insert(p.apply.end(), {"", "    if (!runtime.hasFunction(kFrameHookAddress))", "    {",
            "        std::cerr << \"[console-mod] " + c_string(mod_name) + cfmt(": no recompiled function at frame hook 0x%08llX, \"", h),
            "                     \"per-frame patches are off\" << std::endl;", "        return;", "    }",
            "    PS2Runtime::RecompiledFunction current = runtime.lookupFunction(kFrameHookAddress);", "    if (current != &frameHook)", "    {",
            "        g_originalFrameFunction = current; // keep the game's function (safe if loadELF runs twice)", "    }",
            "    g_firstFrameDone = false;", "    runtime.replaceFunction(kFrameHookAddress, &frameHook);"});
    }
    for (const auto& [k, v] : buckets) p.counts[k] = v.size();
    return p;
}

inline std::string override_text(const Pnach& pn, const Elf& elf, const Result& res, std::optional<long long> hook, const std::string& mod_name, const std::string& slug, const Parts& parts) {
    std::vector<std::string> secs; for (const auto& s : pn.sections) secs.push_back(s);
    auto& c = parts.counts;
    std::vector<std::string> notes{
        "Source: " + pn.name + (secs.empty() ? "" : " (sections: " + join(secs, ", ") + ")"),
        "Generated by console-mod pnach.py override on " + console_override::today() + ".",
        cfmt("Writes at load: %zu, on the first frame: %zu, every frame: %zu.", c.at("load"), c.at("first"), c.at("every")),
        cfmt("Code patches are NOT here: %zu instruction patch(es) go in the game's TOML (pnach.py toml).", code_changes(res))};
    if (!hook && (c.at("first") || c.at("every"))) {
        notes.push_back(cfmt("WARNING: %zu write(s) need a frame hook and are NOT wired up. Find a function the game", c.at("first") + c.at("every")));
        notes.push_back("calls once per frame (Ghidra: the main loop's per-frame call, or whatever waits for vsync)");
        notes.push_back("and re-run: pnach.py override ... --frame-hook 0xADDR");
    } else if (hook) {
        notes.push_back(cfmt("Frame hook: the game's function at 0x%08llX is wrapped. Only calls that go through the", static_cast<unsigned long long>(*hook)));
        notes.push_back("runtime's function table see the wrapper (jal, jalr); a j tail jump to it is a direct C++ call.");
        for (const auto& l : hook_advice(elf, *hook).first) notes.push_back(l);
    }
    if (!res.manual.empty()) {
        notes.push_back(cfmt("Not ported (%zu, do these by hand):", res.manual.size()));
        for (std::size_t k = 0; k < res.manual.size() && k < 40; ++k) notes.push_back(cfmt("  line %d: ", res.manual[k].line) + res.manual[k].reason);
        if (res.manual.size() > 40) notes.push_back(cfmt("  ... and %zu more (pnach.py classify lists them all)", res.manual.size() - 40));
    }
    return console_override::render_module(mod_name, elf.name, elf.entry, elf.crc32(), slug, join(parts.fn, "\n"), join(parts.apply, "\n"), notes);
}

// ---------------------------------------------------------------- commands
inline std::vector<std::string> sections_arg(const Val& a) {
    std::vector<std::string> v; const Val s = get(a, "section");
    if (s.t == Val::LIST) for (const auto& x : *s.l) v.push_back(x.s);
    return v;
}
inline Val manual_json(const std::vector<Manual>& ms) {
    List l; for (const auto& m : ms) l.push_back(dict_of({{"line", Val::integer(m.line)}, {"section", Val::str(m.section)}, {"reason", Val::str(m.reason)}, {"source", Val::str(m.source)}}));
    return Val::list(std::move(l));
}
inline void print(const std::string& s) { out(s); }

inline int cmd_parse(const Val& a) {
    const Pnach pn = load_pnach(S(a, "pnach"), sections_arg(a));
    if (B(a, "json")) {
        Val meta = Val::dict();
        for (const auto& [k, vals] : pn.meta) { List l; for (const auto& [s, v] : vals) l.push_back(dict_of({{"section", Val::str(s)}, {"value", Val::str(v)}})); meta.d->set(k, Val::list(std::move(l))); }
        List ws;
        for (const auto& w : pn.writes) {
            Val d = dict_of({{"line", Val::integer(w.line)}, {"section", Val::str(w.section)}, {"place", Val::integer(w.place)}, {"type", Val::str(w.type)},
                             {"cpu", Val::str(w.cpu)}, {"address", Val::str(hex32(w.addr))}, {"bytes", Val::str(bytes_hex(w.data))}, {"source", Val::str(w.source)}});
            if (!w.extended.empty()) d.d->set("extended", Val::str(w.extended));
            if (!w.notes.empty()) d.d->set("notes", str_list(w.notes));
            ws.push_back(d);
        }
        List es; for (const auto& e : pn.errors) es.push_back(dict_of({{"line", Val::integer(e.line)}, {"text", Val::str(e.text)}, {"error", Val::str(e.error)}}));
        print(dumps2(dict_of({{"file", Val::str(pn.name)}, {"meta", meta}, {"sections", str_list(pn.sections)}, {"writes", Val::list(std::move(ws))},
                              {"manual", manual_json(pn.manual)}, {"errors", Val::list(std::move(es))}})));
    } else {
        print(pn.name + cfmt(": %zu write(s), %zu needing a manual port, %zu bad line(s)", pn.writes.size(), pn.manual.size(), pn.errors.size()));
        for (const auto& [k, vals] : pn.meta) for (const auto& [s, v] : vals) print("  " + ljust(k, 15) + " " + v + (s.empty() ? "" : "   [" + s + "]"));
        if (!pn.sections.empty()) { std::vector<std::string> ss; for (const auto& s : pn.sections) ss.push_back("[" + s + "]"); print("  sections        " + join(ss, ", ")); }
        std::optional<std::string> last;
        for (const auto& w : pn.writes) {
            if (!last || w.section != *last) { print(w.section.empty() ? "\n(no section)" : "\n[" + w.section + "]"); last = w.section; }
            print("  line " + ljust(std::to_string(w.line), 4) + " " + ljust(place_text(w.place), 26) + " " + hex32(w.addr) + " " + ljust(w.type, 8) + " " + upper(w.value)
                  + (w.extended.empty() ? "" : "  <- " + w.extended));
            for (const auto& note : w.notes) print("            note: " + note);
        }
        if (!pn.manual.empty()) { print("\nNeeds a manual port:"); for (const auto& m : pn.manual) print("  line " + ljust(std::to_string(m.line), 4) + " " + m.reason); }
        if (!pn.errors.empty()) { print("\nBad lines:"); for (const auto& e : pn.errors) print("  line " + ljust(std::to_string(e.line), 4) + " " + e.error + ": " + e.text); }
    }
    return (!pn.errors.empty() || !pn.manual.empty()) ? 1 : 0;
}

struct Common { Pnach pn; Elf elf; std::optional<FuncMap> funcs; std::optional<bool> crc_ok; Result res; };
inline Common common(const Val& a) {
    Pnach pn = load_pnach(S(a, "pnach"), sections_arg(a));
    Elf elf(S(a, "elf"));
    std::optional<FuncMap> funcs;
    if (!get(a, "map").is_none() && !S(a, "map").empty()) funcs.emplace(S(a, "map"));
    const auto crc_ok = check_crc(pn, elf, B(a, "ignore_crc"));
    Result res = classify(pn, elf, funcs ? &*funcs : nullptr);
    return {std::move(pn), std::move(elf), std::move(funcs), crc_ok, std::move(res)};
}

inline int cmd_classify(const Val& a) {
    const Common c = common(a);
    const auto& pn = c.pn; const auto& elf = c.elf; const auto& res = c.res;
    std::vector<std::pair<std::string, long long>> counts;
    for (const auto& it : res.items) {
        auto f = std::find_if(counts.begin(), counts.end(), [&](const auto& x) { return x.first == it.cls; });
        if (f == counts.end()) counts.emplace_back(it.cls, 1); else ++f->second;
    }
    if (B(a, "json")) {
        List ws;
        for (const auto& it : res.items) {
            const Write& w = pn.writes[it.w];
            ws.push_back(dict_of({{"line", Val::integer(w.line)}, {"section", Val::str(w.section)}, {"place", Val::integer(w.place)}, {"class", Val::str(it.cls)},
                                  {"confidence", Val::str(it.conf)}, {"why", Val::str(it.why)}, {"ram", Val::str(hex32(it.ram))}, {"bytes", Val::str(bytes_hex(it.data))},
                                  {"override", str_list(write_bucket(it, pn))}}));
        }
        List cps_;
        for (const auto& cw : res.code) {
            List lines; for (const int l : cw.lines) lines.push_back(Val::integer(l));
            cps_.push_back(dict_of({{"address", Val::str(hex32(cw.vaddr))}, {"original", Val::str(hex32(cw.original))}, {"new", Val::str(hex32(cw.neww))},
                                    {"original_asm", Val::str(describe(cw.original, cw.vaddr))}, {"new_asm", Val::str(describe(cw.neww, cw.vaddr))},
                                    {"lines", Val::list(std::move(lines))}, {"confidence", Val::str(cw.conf)}, {"notes", str_list(cw.notes)}}));
        }
        Val cnt = Val::dict(); for (const auto& [k, v] : counts) cnt.d->set(k, Val::integer(v));
        print(dumps2(dict_of({{"pnach", Val::str(pn.name)}, {"elf", Val::str(elf.name)}, {"pcsx2_crc", Val::str(cfmt("%08llX", static_cast<unsigned long long>(elf.pcsx2_crc())))},
                              {"crc_match", c.crc_ok ? Val::boolean(*c.crc_ok) : Val::none()}, {"writes", Val::list(std::move(ws))}, {"code_patches", Val::list(std::move(cps_))},
                              {"manual", manual_json(res.manual)}, {"counts", cnt}})));
        return res.manual.empty() ? 0 : 1;
    }
    print(pn.name + " on " + elf.name + cfmt(" (PCSX2 CRC %08llX", static_cast<unsigned long long>(elf.pcsx2_crc()))
          + (!c.crc_ok ? "" : *c.crc_ok ? ", matches the file name" : ", DOES NOT match the file name") + ")");
    std::vector<std::string> cs;
    for (const char* k : {"CODE", "DATA", "BSS", "HEAP", "CAVE"})
        for (const auto& [n, v] : counts) if (n == k) cs.push_back(cfmt("%s %lld", k, v));
    print("  " + join(cs, ", ") + (res.manual.empty() ? "" : cfmt(", manual %zu", res.manual.size())));
    print("");
    for (const auto& it : res.items) {
        const Write& w = pn.writes[it.w];
        std::string dest = it.cls == "CODE" ? "TOML instruction patch" : it.cls == "CAVE" ? "manual" : "";
        if (it.cls == "DATA" || it.cls == "BSS" || it.cls == "HEAP") {
            std::vector<std::string> bs; for (const auto& b : write_bucket(it, pn)) bs.push_back(b == "load" ? "at load" : b == "first" ? "first frame" : "every frame");
            dest = "override: " + join(bs, " + ");
        }
        const std::string conf = it.conf == "high" ? "" : " (" + it.conf + " confidence: " + it.why + ")";
        print("  line " + ljust(std::to_string(w.line), 4) + " " + ljust(it.cls, 5) + " " + hex32(it.ram) + " " + ljust(value_text(it.data), 18) + " -> " + dest + conf);
    }
    if (!res.code.empty()) {
        print("\nCode words after merging (original from the ELF -> new):");
        for (const auto& cw : res.code) {
            print("  " + hex32(cw.vaddr) + "  " + hex32(cw.original) + " " + ljust(describe(cw.original, cw.vaddr), 24) + " -> " + hex32(cw.neww) + " " + describe(cw.neww, cw.vaddr));
            for (const auto& note : cw.notes) print("      note: " + note);
        }
    }
    if (!res.manual.empty()) { print("\nNeeds a manual port:"); for (const auto& m : res.manual) print("  line " + ljust(std::to_string(m.line), 4) + " " + m.reason); }
    return res.manual.empty() ? 0 : 1;
}

inline void guard_out(const std::string& path) {
    const std::string norm = replace_all(abspath(path), "\\", "/");
    if ((norm + "/").find("/ps2xRuntime/src/runner/") != std::string::npos) throw InputError("refusing to write into ps2xRuntime/src/runner/ (generated code): use console/patches/");
    const auto ends = [&](const std::string& x) { return norm.size() >= x.size() && norm.compare(norm.size() - x.size(), x.size(), x) == 0; };
    if (ends(".h") || ends(".hpp")) throw InputError("refusing to write a header: header edits force a full rebuild");
}
inline void write_out(const std::string& path, const std::string& text, bool force) {
    guard_out(path);
    if (exists(path) && !force) throw InputError(path + " exists: pass --force to overwrite it (it may hold your own edits)");
    makedirs(dirname(abspath(path)));
    console_override::write_all(path, text);
}

inline int cmd_toml(const Val& a) {
    const Common c = common(a);
    const std::string text = toml_text(c.pn, c.elf, c.res);
    const std::string o = get(a, "out").is_none() ? "" : S(a, "out");
    if (!o.empty()) { write_out(o, text, B(a, "force")); flush(); std::fprintf(stderr, "wrote %s\n", o.c_str()); }
    else std::fwrite(text.data(), 1, text.size(), stdout);
    return c.res.manual.empty() ? 0 : 1;
}

inline int cmd_override(const Val& a) {
    const Common c = common(a);
    const auto& pn = c.pn; const auto& elf = c.elf; const auto& res = c.res;
    std::optional<long long> hook; std::vector<std::string> advice; bool serious = false;
    const std::string fh = get(a, "frame_hook").is_none() ? "" : S(a, "frame_hook");
    if (!fh.empty()) {
        hook = parse_addr_arg(fh, "--frame-hook");
        if (*hook & 3) throw InputError("--frame-hook " + hex32(*hook) + " is not 4-byte aligned");
        const long long r = ram_offset(*hook);
        if (r < 0 || !elf.is_code(r, c.funcs ? &*c.funcs : nullptr).code) throw InputError("--frame-hook " + hex32(*hook) + " is not in the ELF's code");
        if (c.funcs && !c.funcs->starts.count(*hook)) {
            flush(); std::fprintf(stderr, "warning: %s is not a function start in the Ghidra map; the hook must be a function's first instruction\n", hex32(*hook).c_str());
        }
        std::tie(advice, serious) = hook_advice(elf, *hook);
    }
    const std::string base = stem(pn.name);
    const std::string name = get(a, "name").is_none() ? "" : S(a, "name");
    const std::string mod_name = !name.empty() ? name : "pnach " + base;
    const std::string slug = slugify(!name.empty() ? name : base);
    const std::string outp = !get(a, "out").is_none() && !S(a, "out").empty() ? S(a, "out") : path_join(path_join("console", "patches"), slug + ".cpp");
    const Parts parts = override_parts(pn, res, hook, mod_name);
    const std::string text = override_text(pn, elf, res, hook, mod_name, slug, parts);
    write_out(outp, text, B(a, "force"));
    const auto cm = console_override::ensure_cmake_include(dirname(abspath(outp)));
    const auto& cnt = parts.counts;
    print("wrote " + outp + "  (keys: " + elf.name + ", entry " + hex32(elf.entry) + ", CRC-32 " + hex32(elf.crc32()) + ")");
    print(cfmt("  at load %zu, first frame %zu, every frame %zu", cnt.at("load"), cnt.at("first"), cnt.at("every")));
    if (hook) for (const auto& l : advice) print("  frame hook: " + l);
    const bool unwired = !hook && (cnt.at("first") || cnt.at("every"));
    if (unwired) print(cfmt("  WARNING: %zu write(s) need --frame-hook 0xADDR (a function the game calls once per frame)", cnt.at("first") + cnt.at("every")));
    if (const auto n = code_changes(res)) print(cfmt("  %zu code patch(es) are not in this file: run  pnach.py toml  and add them to the game's TOML", n));
    if (!res.manual.empty()) print(cfmt("  %zu line(s) need a manual port (listed at the top of the file)", res.manual.size()));
    print("  install: " + (cm ? *cm : std::string("console_mods.cmake already next to it")));
    print("  (the include adds every .cpp in that folder to the ps2EntryRunner executable itself: in the static");
    print("  ps2_runtime library the linker would drop the registration)");
    return (!res.manual.empty() || unwired || (hook && serious)) ? 1 : 0;
}

inline int cmd_crc(const Val& a) {
    const Elf elf(S(a, "elf"));
    const std::string given = get(a, "serial").is_none() ? "" : S(a, "serial");
    const std::optional<std::string> serial = !given.empty() ? std::optional<std::string>(given) : serial_from_name(elf.name);
    const unsigned long long pc = static_cast<unsigned long long>(elf.pcsx2_crc()), rc = static_cast<unsigned long long>(elf.crc32());
    const std::string pnach = serial ? *serial + cfmt("_%08llX.pnach", pc) : "", legacy = cfmt("%08llX.pnach", pc);
    if (B(a, "json")) {
        print(dumps2(dict_of({{"elf", Val::str(elf.name)}, {"size", Val::integer(static_cast<long long>(elf.data.size()))}, {"serial", serial ? Val::str(*serial) : Val::none()},
                              {"crc32", Val::str(cfmt("%08llX", rc))}, {"pcsx2_crc", Val::str(cfmt("%08llX", pc))}, {"entry", Val::str(hex32(elf.entry))},
                              {"pnach", serial ? Val::str(pnach) : Val::none()}, {"pnach_legacy", Val::str(legacy)}})));
        return 0;
    }
    print("ELF          " + elf.name + cfmt("  (%zu bytes, entry ", elf.data.size()) + hex32(elf.entry) + ")");
    print("Serial       " + (serial ? *serial : std::string("unknown (name the ELF like SLUS_203.12 or pass --serial SLUS-20312)")));
    print(cfmt("CRC-32       %08llX  standard CRC-32 of the whole file: the PS2Recomp game override key", rc));
    print(cfmt("PCSX2 CRC    %08llX  XOR of every little-endian 32-bit word: the pnach key", pc));
    if (serial) print("Pnach file   " + pnach + "  (PCSX2 2.x: the patches/ or cheats/ folder)");
    print("Old name     " + legacy + "  (PCSX2 1.6 and older)");
    return 0;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p(PROG, "Port PCSX2 .pnach patches to a PS2Recomp port: code writes become TOML instruction patches, data writes become a game override that writes RDRAM at load or every frame.");
    p.epilog = "Exit codes: 0 ok, 1 something needs a manual port or a bad line, 2 bad input.";
    p.subcommands("cmd", false);
    const auto common_args = [](Parser& sp, bool elf) {
        sp.pos("pnach").help("the .pnach file");
        if (elf) {
            sp.opt({"--elf"}).required().help("the game's main ELF (e.g. SLUS_203.12) the pnach was written for");
            sp.opt({"--map"}).help("Ghidra CSV (Name,Start,End,Size) from ExportPS2Functions.java, to tell code from data");
            sp.opt({"--ignore-crc"}).store_true().help("port even if the pnach file name's CRC does not match the ELF");
        }
        sp.opt({"--section"}).append().metavar("NAME").help("only this [section] (repeat for more); default every line");
    };
    { Parser& s = p.sub("parse"); s.help_line = "read a .pnach and list every write, section and line kind"; common_args(s, false);
      s.opt({"--json"}).store_true().help("machine-readable output"); }
    { Parser& s = p.sub("classify"); s.help_line = "sort every write into CODE, DATA, BSS, HEAP, OUTSIDE or CAVE for your ELF"; common_args(s, true);
      s.opt({"--json"}).store_true().help("machine-readable output"); }
    { Parser& s = p.sub("toml"); s.help_line = "print [[patches.instructions]] blocks for the code writes"; common_args(s, true);
      s.opt({"--out"}).help("write to this file instead of stdout");
      s.opt({"--force"}).store_true().help("overwrite --out if it exists"); }
    { Parser& s = p.sub("override"); s.help_line = "write a PS2_REGISTER_GAME_OVERRIDE module for the data writes"; common_args(s, true);
      s.opt({"--frame-hook"}).metavar("0xADDR").help("a function the game calls once per frame (from Ghidra): wraps it to re-apply place 1/2 and BSS/heap writes");
      s.opt({"--name"}).help("the mod's name (default: from the pnach file name)");
      s.opt({"--out"}).help("output .cpp (default console/patches/<name>.cpp)");
      s.opt({"--force"}).store_true().help("overwrite the output if it exists"); }
    { Parser& s = p.sub("crc"); s.help_line = "print the ELF's CRC-32 (override key), PCSX2 CRC and pnach file name";
      s.pos("elf").help("the game's main ELF");
      s.opt({"--serial"}).help("disc serial like SLUS-20312 when the ELF is not named after it");
      s.opt({"--json"}).store_true().help("machine-readable output"); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { flush(); const std::string t = ap::help_text(ap::norm(p, p.prog)); std::fwrite(t.data(), 1, t.size(), stdout); return 2; }
    return guard(std::string(PROG) + ": error", [&]() -> int {
        if (cmd == "parse") return cmd_parse(a);
        if (cmd == "classify") return cmd_classify(a);
        if (cmd == "toml") return cmd_toml(a);
        if (cmd == "override") return cmd_override(a);
        return cmd_crc(a);
    });
}
PACKTOOL("console-mod/pnach", main);
}  // namespace packtools::console_pnach
