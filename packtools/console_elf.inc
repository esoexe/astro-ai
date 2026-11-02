// console_elf.inc - console-reverse/elf: a C++ port of console-skill/console-reverse/elf.py (Jake Schincariol, MIT).
// Reads a PS2 boot ELF (ELF32, little-endian, MIPS): segments, sections, symbols, the two CRCs (CRC-32 and PCSX2's XOR),
// the serial in the file name and the IRX modules / disc paths in its strings; writes a starter PS2Recomp TOML.
// Same output and exit codes as the original: 0 ok, 1 needs attention, 2 bad input with a one-line message.
namespace packtools::console_elf {
using namespace pyrt;
using namespace packtools::console_common;

constexpr unsigned long long MAX_ELF_BYTES = 256ULL * 1024 * 1024;
constexpr uint32_t EM_MIPS = 8, PT_LOAD = 1, SHT_SYMTAB = 2;
const char* const TOOL_TAG = "console-reverse elf";
const char* const PROG = "elf";

inline std::string et_name(uint32_t t) {
    switch (t) {
        case 0: return "NONE"; case 1: return "REL (relocatable)"; case 2: return "EXEC (executable)";
        case 3: return "DYN (shared object)"; case 4: return "CORE"; case 0xFF80: return "SCE IOP relocatable (IRX module)";
        default: return cfmt("0x%04X", t);
    }
}
inline std::string region_of(const std::string& pre) {
    static const std::map<std::string, std::string> R{{"SLUS", "USA"}, {"SCUS", "USA"}, {"SLES", "Europe"}, {"SCES", "Europe"},
        {"SLPS", "Japan"}, {"SLPM", "Japan"}, {"SCPS", "Japan"}, {"SLKA", "Korea"}, {"SCKA", "Korea"}, {"SCAJ", "Asia"}};
    const auto it = R.find(pre); return it == R.end() ? "unknown region prefix" : it->second;
}

struct Seg { uint32_t index, type, offset, vaddr, paddr, filesz, memsz, flags, align; };
struct Sec { uint32_t index; std::string name; uint32_t type, flags, addr, offset, size, entsize; };
struct Elf {
    uint32_t type = 0, machine = 0, entry = 0, flags = 0;
    std::vector<Seg> segments; std::vector<Sec> sections;
    unsigned long long symbols = 0; bool has_symtab = false, has_dwarf = false, is_irx = false;
    std::vector<std::string> warnings;
};

// ---------------------------------------------------------------- parsing
inline std::string read_file(const std::string& path) {
    std::error_code ec;
    const auto st = std::filesystem::status(fspath(path), ec);
    if (ec || !std::filesystem::exists(st)) throw InputError("cannot open " + path + ": " + path_error(path));
    if (std::filesystem::is_directory(st)) throw InputError(path + " is a folder, not an ELF file");
    const auto size = std::filesystem::file_size(fspath(path), ec);
    if (!ec && size > MAX_ELF_BYTES) throw InputError(cfmt("%s is %llu bytes: too big to be a PS2 program", path.c_str(), static_cast<unsigned long long>(size)));
    try { return read_bytes(path); } catch (const std::exception&) { throw InputError("cannot open " + path + ": " + path_error(path)); }
}

inline Elf parse_elf(const std::string& data, const std::string& path) {
    const std::size_t n = data.size();
    if (n < 52) throw InputError(cfmt("%s is too short to be an ELF (%zu bytes)", path.c_str(), n));
    if (data.compare(0, 4, "\x7f" "ELF") != 0) throw InputError(path + " is not an ELF file (no 0x7F 'ELF' magic)");
    if (data[4] != 1) throw InputError(path + " is a 64-bit ELF; PS2 programs are ELF32");
    if (data[5] != 1) throw InputError(path + " is big-endian; PS2 programs are little-endian");
    const uint32_t e_type = u16le(data, 16), e_machine = u16le(data, 18), e_entry = u32le(data, 24), e_phoff = u32le(data, 28),
                   e_shoff = u32le(data, 32), e_flags = u32le(data, 36), e_phentsize = u16le(data, 42), e_phnum = u16le(data, 44),
                   e_shentsize = u16le(data, 46), e_shnum = u16le(data, 48), e_shstrndx = u16le(data, 50);
    if (e_machine != EM_MIPS) throw InputError(cfmt("%s is not a MIPS program (machine %u, expected 8)", path.c_str(), e_machine));
    Elf elf; elf.type = e_type; elf.machine = e_machine; elf.entry = e_entry; elf.flags = e_flags; elf.is_irx = e_type == 0xFF80;
    if (e_phnum) {
        if (e_phentsize < 32) throw InputError(cfmt("%s has a bad program header size (%u)", path.c_str(), e_phentsize));
        const unsigned long long end = static_cast<unsigned long long>(e_phoff) + static_cast<unsigned long long>(e_phnum) * e_phentsize;
        if (e_phoff == 0 || end > n) throw InputError(path + " is truncated: program headers run past the end of the file");
        for (uint32_t i = 0; i < e_phnum; ++i) {
            const std::size_t o = e_phoff + static_cast<std::size_t>(i) * e_phentsize;
            Seg s{i, u32le(data, o), u32le(data, o + 4), u32le(data, o + 8), u32le(data, o + 12), u32le(data, o + 16), u32le(data, o + 20), u32le(data, o + 24), u32le(data, o + 28)};
            if (s.type == PT_LOAD) {
                const unsigned long long need = static_cast<unsigned long long>(s.offset) + s.filesz;
                if (need > n) throw InputError(cfmt("%s is truncated: segment %u needs bytes up to 0x%llX, the file has 0x%zX", path.c_str(), i, need, n));
                if (s.memsz < s.filesz) elf.warnings.push_back(cfmt("segment %u has memsz < filesz", i));
            }
            elf.segments.push_back(s);
        }
    }
    if (e_shoff && e_shnum) {
        if (e_shentsize < 40) {
            elf.warnings.push_back(cfmt("bad section header size %u: sections ignored", e_shentsize));
        } else if (static_cast<unsigned long long>(e_shoff) + static_cast<unsigned long long>(e_shnum) * e_shentsize > n) {
            elf.warnings.push_back("section headers run past the end of the file: sections ignored");
        } else {
            std::vector<std::array<uint32_t, 10>> raw;
            for (uint32_t i = 0; i < e_shnum; ++i) {
                const std::size_t o = e_shoff + static_cast<std::size_t>(i) * e_shentsize;
                std::array<uint32_t, 10> v{}; for (int k = 0; k < 10; ++k) v[k] = u32le(data, o + 4 * k);
                raw.push_back(v);
            }
            std::string names;
            if (e_shstrndx < raw.size()) {
                const auto& s = raw[e_shstrndx];
                if (static_cast<unsigned long long>(s[4]) + s[5] <= n) names = data.substr(s[4], s[5]);
            }
            for (uint32_t i = 0; i < raw.size(); ++i) {
                const auto& r = raw[i];
                std::string name;
                if (!names.empty() && r[0] < names.size()) {
                    const std::size_t end = names.find('\0', r[0]);
                    name = latin1(names.substr(r[0], (end == std::string::npos ? names.size() : end) - r[0]));
                }
                elf.sections.push_back(Sec{i, name, r[1], r[2], r[3], r[4], r[5], r[9]});
                if (r[1] == SHT_SYMTAB) {
                    elf.has_symtab = true;
                    const uint32_t ent = r[9] ? r[9] : 16;
                    const long long cnt = static_cast<long long>(r[5] / ent) - 1;
                    elf.symbols += cnt > 0 ? static_cast<unsigned long long>(cnt) : 0;
                }
                if (name.rfind(".debug", 0) == 0 || name == ".mdebug") elf.has_dwarf = true;
                if (name == ".iopmod") elf.is_irx = true;
            }
        }
    }
    return elf;
}

inline std::string seg_flags(uint32_t f) { return std::string(f & 4 ? "R" : "-") + (f & 2 ? "W" : "-") + (f & 1 ? "X" : "-"); }

inline uint32_t crc_pnach(const std::string& d) {   // PCSX2's game CRC: XOR of every little-endian 32-bit word (tail ignored)
    uint32_t acc = 0; const std::size_t usable = d.size() - d.size() % 4;
    for (std::size_t o = 0; o < usable; o += 4) acc ^= u32le(d, o);
    return acc;
}

struct Serial { std::string serial, file, prefix; };
inline std::optional<Serial> parse_serial(const std::string& text) {
    const auto parts = re::split(R"([\\/:])", strip(text));
    const std::string base = parts.empty() ? std::string() : parts.back();
    const std::string p1 = R"(^([A-Za-z]{4})_(\d{3})\.(\d{2})(?:;\d+)?$)";
    if (const auto m = re::match(p1, base)) {
        const std::string pre = upper(m.group(1));
        return Serial{pre + "-" + m.group(2) + m.group(3), pre + "_" + m.group(2) + "." + m.group(3), pre};
    }
    const std::string p2 = R"(^([A-Za-z]{4})[-_ ]?(\d{5})$)";
    if (const auto m = re::match(p2, base)) {
        const std::string pre = upper(m.group(1)), dg = m.group(2);
        const auto cp = cps(dg);   // \d matches any Unicode digit: slice by characters, as Python does
        std::string a, b; for (std::size_t k = 0; k < cp.size(); ++k) (k < 3 ? a : b) += cp[k];
        return Serial{pre + "-" + dg, pre + "_" + a + "." + b, pre};
    }
    return std::nullopt;
}

// The two bytes patterns of the original, scanned the way re.finditer does (case-insensitive ASCII):
//   DEVICE  (?:cdrom0?|rom0|host0?|mc0):[\\/]?[A-Za-z0-9_\\/.\-;]{1,120}
//   BARE    (?<![A-Za-z0-9_\\/.\-:])[A-Za-z0-9_\-]{1,40}\.IRX(?:;\d)?
inline char lc(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }
inline bool alnum_(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }
inline bool path_ch(char c) { return alnum_(c) || c == '\\' || c == '/' || c == '.' || c == '-' || c == ';'; }
inline bool name_ch(char c) { return alnum_(c) || c == '-'; }
inline bool before_ch(char c) { return alnum_(c) || c == '\\' || c == '/' || c == '.' || c == '-' || c == ':'; }
inline bool ieq(const std::string& d, std::size_t i, const char* w) {
    for (std::size_t k = 0; w[k]; ++k) if (i + k >= d.size() || lc(d[i + k]) != w[k]) return false;
    return true;
}
inline std::size_t device_at(const std::string& d, std::size_t i) {   // match length at i, 0 = none
    std::size_t j = std::string::npos;
    if (ieq(d, i, "cdrom")) { j = i + 5; if (j < d.size() && d[j] == '0') ++j; }
    else if (ieq(d, i, "rom0")) j = i + 4;
    else if (ieq(d, i, "host")) { j = i + 4; if (j < d.size() && d[j] == '0') ++j; }
    else if (ieq(d, i, "mc0")) j = i + 3;
    if (j == std::string::npos || j >= d.size() || d[j] != ':') return 0;
    ++j;
    auto run = [&](std::size_t from) { std::size_t k = from; while (k < d.size() && k - from < 120 && path_ch(d[k])) ++k; return k - from; };
    if (j < d.size() && (d[j] == '\\' || d[j] == '/')) {   // [\\/]? is greedy; without a class char after it, it gives the slash back
        const std::size_t r = run(j + 1);
        return r ? (j + 1 + r) - i : (j + 1) - i;
    }
    const std::size_t r = run(j);
    return r ? (j + r) - i : 0;
}
inline std::size_t bare_at(const std::string& d, std::size_t i) {
    if (i > 0 && before_ch(d[i - 1])) return 0;
    std::size_t k = i; while (k < d.size() && k - i <= 40 && name_ch(d[k])) ++k;
    const std::size_t r = k - i;
    if (r < 1 || r > 40 || !ieq(d, k, ".irx")) return 0;
    k += 4;
    if (k + 1 < d.size() && d[k] == ';' && d[k + 1] >= '0' && d[k + 1] <= '9') k += 2;
    return k - i;
}
inline std::string ascii_lower(std::string s) { for (auto& c : s) c = lc(c); return s; }

inline std::pair<std::vector<std::string>, std::vector<std::string>> find_references(const std::string& d) {
    std::vector<std::string> irx, other; std::set<std::string> seen;
    for (std::size_t i = 0; i < d.size();) {
        const std::size_t m = device_at(d, i);
        if (!m) { ++i; continue; }
        std::string s = d.substr(i, m); i += m;
        while (!s.empty() && s.back() == '.') s.pop_back();
        if (!seen.insert(s).second) continue;
        const std::string low = ascii_lower(s);
        if (low.find(".irx") != std::string::npos || low.rfind("rom0:", 0) == 0) irx.push_back(s); else other.push_back(s);
    }
    for (std::size_t i = 0; i < d.size();) {
        const std::size_t m = bare_at(d, i);
        if (!m) { ++i; continue; }
        const std::string s = d.substr(i, m); i += m;
        bool skip = seen.count(s) > 0;
        const std::string sl = ascii_lower(s);
        for (const auto& x : irx) if (!skip && ascii_lower(x).find(sl) != std::string::npos) skip = true;
        if (skip) continue;
        seen.insert(s); irx.push_back(s);
    }
    return {irx, other};
}

// ---------------------------------------------------------------- describe
inline Val describe(const std::string& path, const std::string& data, const Elf& elf) {
    List segs; std::vector<std::string> warnings = elf.warnings; std::vector<std::string> secnames;
    for (const auto& s : elf.sections) if (!s.name.empty()) secnames.push_back(s.name);
    for (const auto& s : elf.segments) {
        if (s.type != PT_LOAD) continue;
        const unsigned long long end = static_cast<unsigned long long>(s.vaddr) + s.memsz;
        segs.push_back(dict_of({{"vaddr", Val::str(cfmt("0x%08X", s.vaddr))}, {"end", Val::str(cfmt("0x%08llX", end))},
            {"filesz", Val::integer(s.filesz)}, {"memsz", Val::integer(s.memsz)}, {"bss", Val::integer(s.memsz > s.filesz ? s.memsz - s.filesz : 0)},
            {"offset", Val::str(cfmt("0x%X", s.offset))}, {"flags", Val::str(seg_flags(s.flags))}}));
        if (static_cast<unsigned long long>(s.vaddr & 0x1FFFFFFF) + s.memsz > 0x02000000ULL)
            warnings.push_back(cfmt("segment at 0x%08X runs past the 32 MB of RDRAM", s.vaddr));
    }
    const std::string pn = cfmt("%08X", crc_pnach(data));
    const auto ser = parse_serial(basename(path));
    const auto [irx, other] = find_references(data);
    return dict_of({{"file", Val::str(abspath(path))}, {"name", Val::str(basename(path))}, {"bytes", Val::integer(static_cast<long long>(data.size()))},
        {"type", Val::str(et_name(elf.type))}, {"machine", Val::str(cfmt("MIPS (%u)", elf.machine))}, {"entry", Val::str(cfmt("0x%08X", elf.entry))},
        {"flags", Val::str(cfmt("0x%08X", elf.flags))}, {"is_irx", Val::boolean(elf.is_irx)}, {"segments", Val::list(segs)},
        {"sections", str_list(secnames)}, {"section_count", Val::integer(static_cast<long long>(elf.sections.size()))},
        {"symbols", Val::integer(static_cast<long long>(elf.symbols))}, {"stripped", Val::boolean(!elf.has_symtab)}, {"debug_info", Val::boolean(elf.has_dwarf)},
        {"crc32", Val::str(cfmt("0x%08X", zip::crc32(data)))}, {"pnach_crc", Val::str("0x" + pn)},
        {"serial", ser ? Val::str(ser->serial) : Val::none()}, {"region", ser ? Val::str(region_of(ser->prefix)) : Val::none()},
        {"pnach_file", Val::str(ser ? ser->serial + "_" + pn + ".pnach" : "<SERIAL>_" + pn + ".pnach")},
        {"warnings", str_list(warnings)}, {"irx_modules", str_list(irx)}, {"disc_paths", str_list(other)}});
}

// ---------------------------------------------------------------- commands
inline std::pair<std::string, Elf> load(const std::string& path) {
    std::string data = read_file(path);
    Elf e = parse_elf(data, basename(path));
    return {std::move(data), std::move(e)};
}
inline std::string gs(const Val& d, const std::string& k) { return pystr(get(d, k)); }
inline long long gi(const Val& d, const std::string& k) { return get(d, k).i; }

inline int cmd_info(const Val& a) {
    const auto [data, elf] = load(S(a, "elf"));
    const Val info = describe(S(a, "elf"), data, elf);
    if (B(a, "json")) { out(dumps2(info)); return 0; }
    out(cfmt("File         %s (%lld bytes)", gs(info, "name").c_str(), gi(info, "bytes")));
    if (truthy(get(info, "serial"))) out("Serial       " + gs(info, "serial") + " (" + gs(info, "region") + ")");
    out("Type         " + gs(info, "type") + ", " + gs(info, "machine") + ", ELF32 little-endian");
    if (truthy(get(info, "is_irx"))) out("             This is an IOP module (IRX), not the game's main program. ps2xIOP runs it.");
    out("Entry        " + gs(info, "entry"));
    const List& segs = *get(info, "segments").l;
    out(cfmt("Segments     %zu PT_LOAD", segs.size()));
    for (const auto& s : segs) {
        const long long bss = gi(s, "bss");
        const std::string extra = bss ? cfmt("  (+0x%llX zero-filled bss)", bss) : "";
        out(cfmt("  %s  %s-%s  file 0x%llX bytes at %s%s", gs(s, "flags").c_str(), gs(s, "vaddr").c_str(), gs(s, "end").c_str(), gi(s, "filesz"), gs(s, "offset").c_str(), extra.c_str()));
    }
    const List& names = *get(info, "sections").l;
    if (gi(info, "section_count")) {
        std::vector<std::string> first; for (std::size_t k = 0; k < names.size() && k < 16; ++k) first.push_back(names[k].s);
        out(cfmt("Sections     %lld: ", gi(info, "section_count")) + join(first, " ") + (names.size() > 16 ? " ..." : ""));
    } else {
        out("Sections     none (no section headers)");
    }
    if (truthy(get(info, "stripped"))) out("Symbols      none: stripped. Use the Ghidra export (ExportPS2Functions.java) for function bounds");
    else out(cfmt("Symbols      %lld in .symtab", gi(info, "symbols")));
    if (truthy(get(info, "debug_info"))) out("Debug info   yes: ps2_analyzer can read function names and bounds from it");
    out("CRC-32       " + gs(info, "crc32") + "  (PS2_REGISTER_GAME_OVERRIDE key; standard CRC-32 of the whole file)");
    out("PCSX2 CRC    " + gs(info, "pnach_crc") + "  (pnach file: " + gs(info, "pnach_file") + ")");
    const List& irx = *get(info, "irx_modules").l;
    if (!irx.empty()) {
        out(cfmt("IRX modules  %zu referenced:", irx.size()));
        for (std::size_t k = 0; k < irx.size() && k < 40; ++k) out("  " + irx[k].s);
    } else {
        out("IRX modules  none named in the strings");
    }
    const List& other = *get(info, "disc_paths").l;
    if (!other.empty()) {
        out(cfmt("Disc paths   %zu other device paths (first 20):", other.size()));
        for (std::size_t k = 0; k < other.size() && k < 20; ++k) out("  " + other[k].s);
    }
    for (const auto& w : *get(info, "warnings").l) out("Warning      " + w.s);
    return 0;
}

inline int cmd_crc(const Val& a) {
    const auto [data, elf] = load(S(a, "elf"));
    const Val info = describe(S(a, "elf"), data, elf);
    const Val o = dict_of({{"crc32", get(info, "crc32")}, {"pnach_crc", get(info, "pnach_crc")}, {"pnach_file", get(info, "pnach_file")},
                           {"entry", get(info, "entry")}, {"elf_name", get(info, "name")}});
    if (B(a, "json")) { out(dumps2(o)); return 0; }
    out("CRC-32     " + gs(o, "crc32") + "  (runtime override key)");
    out("PCSX2 CRC  " + gs(o, "pnach_crc") + "  (" + gs(o, "pnach_file") + ")");
    out("Entry      " + gs(o, "entry") + "   ELF name " + gs(o, "elf_name"));
    return 0;
}

inline std::string toml_str(const std::string& s) { return "\"" + replace_all(replace_all(s, "\\", "\\\\"), "\"", "\\\"") + "\""; }

inline std::string render_toml(const std::string& elf_path, const Val& info, const std::string& ghidra_csv, const std::string& output_dir, long long rows) {
    const std::string serial = truthy(get(info, "serial")) ? gs(info, "serial") : gs(info, "name");
    std::vector<std::string> L{
        "# PS2Recomp starter config for " + serial + ", written by " + TOOL_TAG + ".",
        "# Every key below is one ps2xRecomp/src/lib/config_manager.cpp reads (PS2Recomp, Oct 2026).",
        "# Prefer the TOML that ps2xRecomp/tools/ghidra/ExportPS2Functions.java writes from Ghidra:",
        "# it fills `stubs` with the runtime handlers it recognises. Merge your own lines into it.",
        "# Run: ps2_recomp <this file>   (the executable prints its usage as `ps2recomp`)",
        cfmt("# ELF: entry %s, CRC-32 %s, %zu PT_LOAD segments, %s.", gs(info, "entry").c_str(), gs(info, "crc32").c_str(), get(info, "segments").l->size(),
             truthy(get(info, "stripped")) ? "stripped" : cfmt("%lld symbols", gi(info, "symbols")).c_str()),
        "",
        "[general]",
        "input = " + toml_str(abspath(elf_path)),
        "# Generated C++ lands here. Copy the .cpp files into ps2xRuntime/src/runner/ and the two",
        "# generated headers (ps2_recompiled_functions.h, ps2_recompiled_stubs.h) into ps2xRuntime/include/.",
        "# It is derived from the game's code: never publish it.",
        "output = " + toml_str(abspath(output_dir)),
    };
    if (!ghidra_csv.empty()) {
        L.push_back(cfmt("# Ghidra function map (Name,Start,End,Size): %lld functions. Its bounds win over heuristics.", rows));
        L.push_back("ghidra_output = " + toml_str(abspath(ghidra_csv)));
    } else {
        L.push_back("# Recommended for stripped retail ELFs: the CSV ExportPS2Functions.java writes.");
        L.push_back("# ghidra_output = \"/path/to/functions.csv\"");
    }
    for (const char* x : {
        "single_file_output = false   # one .cpp per function keeps rebuilds incremental",
        "low_memory_mode = false",
        "output_worker_threads = 0    # 0 = one less than your CPU threads",
        "patch_syscalls = false       # leave false: SYSCALL patches only apply when this is true",
        "patch_cop0 = true            # recompiler default; COP0 patches apply only when true",
        "patch_cache = true           # recompiler default; CACHE patches apply only when true",
        "",
        "# Bind a function start to a runtime handler by name: \"sceCdRead@0x00123456\".",
        "# The name must exist in PS2_SYSCALL_LIST or PS2_STUB_LIST (ps2xRuntime/include/ps2_call_list.h).",
        "# Temporary triage handlers: \"ret0@0x...\", \"ret1@0x...\", \"reta0@0x...\" (record each in STATE.md).",
        "stubs = [",
        "]",
        "",
        "# Extra function starts the recompiler should emit, for example \"0x00123456\" after the runtime",
        "# logs `Error: No exact recompiled function for guest PC 0x00123456 ... codeRegion=yes`.",
        "entry_points = [",
        "]",
        "",
        "# Legacy. A skipped function is NOT stepped over: calling it logs",
        "# `Unimplemented PS2 stub called` and stops the run. Use ret0@/ret1@ stubs instead.",
        "skip = []",
        "",
        "# Instruction patches: the raw 32-bit replacement word at an address, applied before translation.",
        "# Plain patches always apply; SYSCALL/COP0/CACHE patches only when patch_syscalls/patch_cop0/patch_cache",
        "# are true. Example (a MIPS nop):",
        "# [[patches.instructions]]",
        "# address = \"0x00100230\"",
        "# value = \"0x00000000\"",
        "",
    }) L.push_back(x);
    return join(L, "\n");
}

// universal-newline lines of a text file read with errors="replace"
inline std::vector<std::string> text_lines(const std::string& raw) {
    std::vector<std::string> lines; std::string cur; bool any = false;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const char c = raw[i]; any = true;
        if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n') ++i;
            lines.push_back(cur); cur.clear(); any = false;
        } else {
            cur += c;
        }
    }
    if (any) lines.push_back(cur);
    return lines;
}

inline long long count_csv(const std::string& path) {
    std::string raw;
    try { raw = read_bytes(path); } catch (const std::exception&) { throw InputError("cannot read the Ghidra CSV " + path + ": " + path_error(path)); }
    const auto lines = text_lines(decode_utf8_replace(raw));
    const std::string header = lines.empty() ? std::string() : strip(lines[0]);
    long long rows = 0; for (std::size_t k = 1; k < lines.size(); ++k) if (!strip(lines[k]).empty()) ++rows;
    if (lower(replace_all(header, " ", "")) != "name,start,end,size")
        throw InputError(path + " does not look like an ExportPS2Functions.java map (header " + str_repr(cpslice(header, 0, 40)) + ", expected 'Name,Start,End,Size')");
    return rows;
}

inline int cmd_toml(const Val& a) {
    const std::string elf_path = S(a, "elf");
    const auto [data, elf] = load(elf_path);
    if (elf.is_irx) throw InputError(elf_path + " is an IRX module; recompile the EE boot ELF instead");
    if (elf.type != 2) throw InputError(elf_path + " is not an executable ELF (type " + et_name(elf.type) + ")");
    const Val info = describe(elf_path, data, elf);
    std::string o = S(a, "out");
    if (get(a, "out").is_none()) {
        const std::string stem = truthy(get(info, "serial")) ? gs(info, "serial") : replace_all(basename(elf_path), ".", "_");
        o = path_join("console", stem + ".toml");
    }
    if (exists(o) && !B(a, "force")) {
        std::fprintf(stderr, "%s: %s already exists; not overwriting your edits (pass --force to replace it)\n", PROG, o.c_str());
        return 1;
    }
    long long rows = 0;
    const std::string csv = S(a, "ghidra_csv");
    if (!csv.empty()) {
        if (!is_file(csv)) throw InputError("no such Ghidra CSV: " + csv);
        rows = count_csv(csv);
    }
    const std::string output_dir = !S(a, "output_dir").empty() ? S(a, "output_dir") : path_join(dirname(abspath(o)), "output");
    const std::string text = render_toml(elf_path, info, csv, output_dir, rows);
    try {
        makedirs(dirname(abspath(o)));
        std::FILE* f = open_file(o, "wb");
        if (!f) throw OSError(errno, o);
        std::fwrite(text.data(), 1, text.size(), f); std::fclose(f);
    } catch (const std::exception&) {
        throw InputError("cannot write " + o + ": " + path_error(o));
    }
    if (B(a, "json")) {
        out(dumps2(dict_of({{"toml", Val::str(abspath(o))}, {"output", Val::str(abspath(output_dir))}, {"ghidra_rows", Val::integer(rows)},
                            {"entry", get(info, "entry")}, {"crc32", get(info, "crc32")}})));
    } else {
        out("Wrote " + o);
        out("Generated C++ will go to " + abspath(output_dir));
        if (csv.empty() && truthy(get(info, "stripped"))) {
            out("Stripped ELF: export functions from Ghidra (ExportPS2Functions.java) and pass --ghidra-csv,");
            out("or use the TOML the export writes. ps2_analyzer is the quick fallback.");
        }
        out("Next: ps2_recomp " + o);
    }
    return 0;
}

inline int cmd_serial(const Val& a) {
    const std::string name = S(a, "name");
    const auto ser = parse_serial(name);
    if (!ser) throw InputError(str_repr(name) + " is not a PS2 serial (expected SLUS-20312, SLUS_203.12 or SLUS20312)");
    const std::string region = region_of(ser->prefix);
    if (B(a, "json")) {
        out(dumps2(dict_of({{"serial", Val::str(ser->serial)}, {"elf_name", Val::str(ser->file)}, {"prefix", Val::str(ser->prefix)}, {"region", Val::str(region)}})));
    } else {
        out("Serial     " + ser->serial); out("ELF name   " + ser->file); out("Region     " + region);
    }
    return 0;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p(PROG, "Read a PS2 boot ELF (ELF32 LE MIPS): segments, CRCs, IRX modules; write a starter PS2Recomp TOML.");
    p.subcommands("cmd", true); p.sub_metavar = "COMMAND";
    { Parser& s = p.sub("info"); s.help_line = "segments, sections, symbols, CRC-32 and PCSX2 CRC, IRX modules";
      s.pos("elf").help("the boot ELF, e.g. console/disc/SLUS_203.12"); s.opt({"--json"}).store_true().help("machine-readable output"); }
    { Parser& s = p.sub("crc"); s.help_line = "CRC-32 (runtime override key) and PCSX2 pnach CRC";
      s.pos("elf").help("the boot ELF"); s.opt({"--json"}).store_true().help("machine-readable output"); }
    { Parser& s = p.sub("toml"); s.help_line = "write a starter PS2Recomp config (refuses to overwrite)";
      s.pos("elf").help("the boot ELF"); s.opt({"--out"}).help("config path (default console/<serial>.toml)");
      s.opt({"--ghidra-csv"}).metavar("PATH").help("function map from ExportPS2Functions.java");
      s.opt({"--output-dir"}).metavar("DIR").help("where ps2_recomp writes C++ (default: an output/ folder next to the TOML)");
      s.opt({"--force"}).store_true().help("overwrite an existing config"); s.opt({"--json"}).store_true().help("machine-readable output"); }
    { Parser& s = p.sub("serial"); s.help_line = "convert SLUS_203.12 <-> SLUS-20312 and name the region";
      s.pos("name").help("a serial or boot ELF name (a path is fine)"); s.opt({"--json"}).store_true().help("machine-readable output"); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    return guard(PROG, [&]() -> int {
        if (cmd == "info") return cmd_info(a);
        if (cmd == "crc") return cmd_crc(a);
        if (cmd == "toml") return cmd_toml(a);
        return cmd_serial(a);
    });
}
PACKTOOL("console-reverse/elf", main);
}  // namespace packtools::console_elf
