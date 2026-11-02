// A.S.T.R.O AI - Astro RE: reverse engineering built into the engine (C++, no extra dependencies).
//
// Astro's own take on agent-driven reverse engineering, inspired by REA (github.com/morluto/rea, MIT). No REA code is
// used; this is a separate implementation. It reads a local file and returns facts with their evidence (file offsets,
// addresses, section names) plus what it could NOT determine, so the agent never has to guess.
//
//   built in (static, never runs the target):
//     ELF / PE / Mach-O (thin + fat): header, sections, segments, entropy, symbols, imports, exports, libraries,
//       GNU build-id, mitigations (NX, PIE, RELRO, stack canary hint, ASLR/DEP/CFG), PE certificate presence,
//       Go build info, Rust cargo-auditable dependency lists
//     ZIP family (APK, JAR, IPA, VSIX, wheel, Office) inventories, Electron ASAR inventories, WebAssembly sections,
//       imports and exports, GGUF and safetensors model metadata, SQLite schema, Java class, DEX, Godot PCK,
//       Unity IL2CPP metadata header, source maps, strings (ASCII + UTF-16LE), hex view, byte search
//   optional providers (detected on PATH or installed from Settings > Tools, never required):
//     disassembly: rizin, llvm-objdump, objdump, otool        decompilation: Ghidra (headless), JADX for Android/Java
//
// Routes (local only, never from a paired phone):  POST /api/re/{inspect,strings,symbols,hex,find,disasm,decompile,
//   providers,zip}   Each takes {"path": "<absolute or project-relative path>"}.
// Responsible use: analyse software you own or are allowed to study. Astro RE reads files; it does not crack licences,
// bypass DRM or build malware, and the agent's RE mode is told the same.

#include <zlib.h>

namespace re {
using eng::lower; using eng::clip; using eng::jtrue;

constexpr std::size_t kMaxFile = 768ull * 1024 * 1024;   // larger files are read in part (see limitations)

struct Blob {
    std::string path, data;        // data = file bytes (maybe truncated to kMaxFile)
    unsigned long long size = 0;   // real size on disk
    long long mtime = 0;
    bool truncated = false;
};

std::mutex g_cache_mu;
std::vector<std::shared_ptr<Blob>> g_cache;   // tiny LRU: the last 3 files

std::shared_ptr<Blob> load(const std::string& path) {
    std::error_code ec;
    const fs::path p(path);
    if (!fs::is_regular_file(p, ec)) throw std::runtime_error("Not a file: " + path);
    const auto size = fs::file_size(p, ec);
    const auto mt = fs::last_write_time(p, ec).time_since_epoch().count();
    {
        std::lock_guard lk(g_cache_mu);
        for (std::size_t i = 0; i < g_cache.size(); ++i)
            if (g_cache[i]->path == path && g_cache[i]->size == size && g_cache[i]->mtime == (long long)mt) {
                auto b = g_cache[i]; g_cache.erase(g_cache.begin() + (long)i); g_cache.push_back(b); return b;
            }
    }
    auto b = std::make_shared<Blob>();
    b->path = path; b->size = size; b->mtime = (long long)mt;
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open " + path);
    const std::size_t n = (std::size_t)std::min<unsigned long long>(size, kMaxFile);
    b->data.resize(n);
    in.read(b->data.data(), (std::streamsize)n);
    b->data.resize((std::size_t)in.gcount());
    b->truncated = b->data.size() < size;
    std::lock_guard lk(g_cache_mu);
    g_cache.push_back(b);
    if (g_cache.size() > 3) g_cache.erase(g_cache.begin());
    return b;
}

// ---------- little helpers ----------
struct Rd {
    const std::string& d; bool le = true;
    bool ok(unsigned long long off, unsigned long long n) const { return off <= d.size() && n <= d.size() - off; }
    uint8_t u8(unsigned long long o) const { return ok(o, 1) ? (uint8_t)d[o] : 0; }
    uint16_t u16(unsigned long long o) const { if (!ok(o, 2)) return 0; const auto* p = (const unsigned char*)d.data() + o; return le ? (uint16_t)(p[0] | p[1] << 8) : (uint16_t)(p[1] | p[0] << 8); }
    uint32_t u32(unsigned long long o) const { if (!ok(o, 4)) return 0; const auto* p = (const unsigned char*)d.data() + o; return le ? (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24 : (uint32_t)p[3] | (uint32_t)p[2] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[0] << 24; }
    uint64_t u64(unsigned long long o) const { const uint64_t a = u32(o), b = u32(o + 4); return le ? a | b << 32 : b | a << 32; }
    std::string cstr(unsigned long long o, std::size_t max = 4096) const {
        std::string s; if (o >= d.size()) return s;
        for (std::size_t i = o; i < d.size() && s.size() < max && d[i]; ++i) s += d[i];
        return s;
    }
};
std::string hex(unsigned long long v) { char b[24]; std::snprintf(b, sizeof b, "0x%llx", v); return b; }
Json num(double v) { return Json(v); }
double jnum(const Json& x) { if (const double* p = std::get_if<double>(&x.value)) return *p; return 0; }
Json jstr(const std::string& s) {   // keep JSON valid UTF-8: replace bytes that are not printable ASCII or valid UTF-8
    std::string o; o.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        if (c >= 0x20 && c < 0x7f) { o += (char)c; ++i; continue; }
        if (c == '\t' || c == '\n' || c == '\r') { o += (char)c; ++i; continue; }
        int n = c >= 0xF0 && c < 0xF5 ? 4 : c >= 0xE0 ? 3 : c >= 0xC2 && c < 0xE0 ? 2 : 0;
        bool good = n > 0 && i + n <= s.size();
        for (int k = 1; good && k < n; ++k) good = ((unsigned char)s[i + k] & 0xC0) == 0x80;
        if (good) { o.append(s, i, n); i += n; } else { char b[8]; std::snprintf(b, sizeof b, "\\x%02x", c); o += b; ++i; }
    }
    return Json(o);
}
double entropy(const char* p, std::size_t n) {
    if (!n) return 0;
    std::array<std::size_t, 256> f{}; for (std::size_t i = 0; i < n; ++i) ++f[(unsigned char)p[i]];
    double e = 0; for (auto c : f) if (c) { const double q = (double)c / n; e -= q * std::log2(q); }
    return std::round(e * 1000) / 1000;
}
double entropy_of(const std::string& d, unsigned long long off, unsigned long long n) {
    if (off >= d.size()) return 0;
    n = std::min<unsigned long long>(n, d.size() - off); return entropy(d.data() + off, (std::size_t)n);
}
bool starts(const std::string& d, std::string_view m, std::size_t off = 0) { return d.size() >= off + m.size() && std::memcmp(d.data() + off, m.data(), m.size()) == 0; }

// ---------- the common model every format fills in ----------
struct Section { std::string name; unsigned long long addr = 0, off = 0, size = 0, vsize = 0; std::string flags; bool exec = false, write = false; };
struct Symbol { std::string name, kind, bind, lib; unsigned long long addr = 0, size = 0; bool imported = false, exported = false; };
struct Report {
    std::string format, kind, arch, os, endian = "little", lang;
    int bits = 0;
    unsigned long long entry = 0; bool has_entry = false, base_set = false;
    unsigned long long image_base = 0;
    std::vector<Section> sections, segments;
    std::vector<Symbol> symbols;
    std::vector<std::string> libraries, notes, findings, limitations, unknowns;
    Json::Object extra;   // format-specific facts
    Json::Object mitigations;
};

std::string flagstr(bool r, bool w, bool x) { return std::string(r ? "r" : "-") + (w ? "w" : "-") + (x ? "x" : "-"); }

// ---------- ELF ----------
const char* elf_machine(unsigned m) {
    switch (m) { case 3: return "x86"; case 62: return "x86-64"; case 40: return "arm"; case 183: return "arm64"; case 8: return "mips";
        case 20: return "powerpc"; case 21: return "powerpc64"; case 243: return "riscv"; case 258: return "loongarch"; case 2: return "sparc";
        case 43: return "sparcv9"; case 22: return "s390"; case 190: return "cuda"; case 224: return "amdgpu"; case 247: return "bpf"; default: return "unknown"; }
}
void parse_elf(const Blob& b, Report& r) {
    const std::string& d = b.data;
    Rd rd{d};
    const bool is64 = d[4] == 2; rd.le = d[5] != 2;
    r.format = "ELF"; r.bits = is64 ? 64 : 32; r.endian = rd.le ? "little" : "big";
    const unsigned type = rd.u16(16), mach = rd.u16(18);
    r.arch = elf_machine(mach);
    r.kind = type == 1 ? "relocatable object" : type == 2 ? "executable" : type == 3 ? "shared object or PIE executable" : type == 4 ? "core dump" : "other";
    static const char* osabi[] = {"System V/Linux", "HP-UX", "NetBSD", "Linux", "GNU Hurd", "?", "Solaris", "AIX", "IRIX", "FreeBSD", "Tru64", "Modesto", "OpenBSD"};
    r.os = d[7] < 13 ? osabi[(unsigned char)d[7]] : "other";
    const unsigned long long entry = is64 ? rd.u64(24) : rd.u32(24);
    const unsigned long long phoff = is64 ? rd.u64(32) : rd.u32(28), shoff = is64 ? rd.u64(40) : rd.u32(32);
    const unsigned phentsize = rd.u16(is64 ? 54 : 42), phnum = rd.u16(is64 ? 56 : 44), shentsize = rd.u16(is64 ? 58 : 46), shnum = rd.u16(is64 ? 60 : 48), shstrndx = rd.u16(is64 ? 62 : 50);
    if (entry) { r.entry = entry; r.has_entry = true; }
    // program headers
    bool nx = true, gnu_stack = false, relro = false, interp = false, has_dyn = false;
    unsigned long long dyn_off = 0, dyn_size = 0;
    for (unsigned i = 0; i < phnum && i < 4096; ++i) {
        const unsigned long long o = phoff + (unsigned long long)i * phentsize;
        if (!rd.ok(o, phentsize)) { r.limitations.push_back("Program header table runs past the end of the file."); break; }
        const uint32_t pt = rd.u32(o), fl = is64 ? rd.u32(o + 4) : rd.u32(o + 24);
        const unsigned long long off = is64 ? rd.u64(o + 8) : rd.u32(o + 4), va = is64 ? rd.u64(o + 16) : rd.u32(o + 8),
            fsz = is64 ? rd.u64(o + 32) : rd.u32(o + 16), msz = is64 ? rd.u64(o + 40) : rd.u32(o + 20);
        static const std::map<uint32_t, const char*> names = {{0, "NULL"}, {1, "LOAD"}, {2, "DYNAMIC"}, {3, "INTERP"}, {4, "NOTE"}, {6, "PHDR"}, {7, "TLS"},
            {0x6474e550, "GNU_EH_FRAME"}, {0x6474e551, "GNU_STACK"}, {0x6474e552, "GNU_RELRO"}, {0x6474e553, "GNU_PROPERTY"}};
        Section s; s.name = names.count(pt) ? names.at(pt) : hex(pt); s.addr = va; s.off = off; s.size = fsz; s.vsize = msz;
        s.exec = fl & 1; s.write = fl & 2; s.flags = flagstr(fl & 4, fl & 2, fl & 1);
        r.segments.push_back(s);
        if (pt == 0x6474e551) { gnu_stack = true; nx = !(fl & 1); }
        if (pt == 0x6474e552) relro = true;
        if (pt == 3) { interp = true; r.extra["interpreter"] = jstr(rd.cstr(off, 256)); }
        if (pt == 2) { has_dyn = true; dyn_off = off; dyn_size = fsz; }
    }
    // section headers
    unsigned long long shstr_off = 0;
    if (shstrndx < shnum && rd.ok(shoff + (unsigned long long)shstrndx * shentsize, shentsize))
        shstr_off = is64 ? rd.u64(shoff + (unsigned long long)shstrndx * shentsize + 24) : rd.u32(shoff + (unsigned long long)shstrndx * shentsize + 16);
    struct Sh { uint32_t type; unsigned long long off, size, link, entsize, addr; std::string name; };
    std::vector<Sh> shs;
    for (unsigned i = 0; i < shnum && i < 65535; ++i) {
        const unsigned long long o = shoff + (unsigned long long)i * shentsize;
        if (!rd.ok(o, shentsize)) { r.limitations.push_back("Section header table runs past the end of the file."); break; }
        Sh h; const uint32_t nm = rd.u32(o); h.type = rd.u32(o + 4);
        const unsigned long long fl = is64 ? rd.u64(o + 8) : rd.u32(o + 8);
        h.addr = is64 ? rd.u64(o + 16) : rd.u32(o + 12); h.off = is64 ? rd.u64(o + 24) : rd.u32(o + 16);
        h.size = is64 ? rd.u64(o + 32) : rd.u32(o + 20); h.link = rd.u32(o + (is64 ? 40 : 24)); h.entsize = is64 ? rd.u64(o + 56) : rd.u32(o + 36);
        h.name = rd.cstr(shstr_off + nm, 200);
        shs.push_back(h);
        if (i == 0) continue;
        Section s; s.name = h.name; s.addr = h.addr; s.off = h.off; s.size = h.type == 8 ? 0 : h.size; s.vsize = h.size;
        s.exec = fl & 4; s.write = fl & 1; s.flags = flagstr(fl & 2, fl & 1, fl & 4);
        r.sections.push_back(s);
    }
    if (shnum == 0) r.limitations.push_back("No section headers (stripped or packed): section-based facts are unavailable; segments are still listed.");
    // symbols: .symtab and .dynsym
    std::set<std::string> seen;
    for (const auto& h : shs) {
        if (h.type != 2 && h.type != 11) continue;
        if (h.link >= shs.size()) continue;
        const unsigned long long stroff = shs[h.link].off, esz = h.entsize ? h.entsize : (is64 ? 24 : 16);
        const unsigned long long n = std::min<unsigned long long>(h.size / esz, 400000);
        for (unsigned long long i = 1; i < n; ++i) {
            const unsigned long long o = h.off + i * esz;
            if (!rd.ok(o, esz)) break;
            const uint32_t nm = rd.u32(o);
            const uint8_t info = is64 ? rd.u8(o + 4) : rd.u8(o + 12);
            const uint16_t shndx = is64 ? rd.u16(o + 6) : rd.u16(o + 14);
            const unsigned long long val = is64 ? rd.u64(o + 8) : rd.u32(o + 4), sz = is64 ? rd.u64(o + 16) : rd.u32(o + 8);
            Symbol s; s.name = rd.cstr(stroff + nm, 512);
            if (s.name.empty()) continue;
            const int t = info & 15, bind = info >> 4;
            s.kind = t == 2 ? "function" : t == 1 ? "object" : t == 6 ? "tls" : t == 10 ? "ifunc" : t == 4 ? "file" : t == 3 ? "section" : "other";
            if (t == 4 || t == 3) continue;
            s.bind = bind == 1 ? "global" : bind == 2 ? "weak" : "local";
            s.addr = val; s.size = sz;
            s.imported = shndx == 0; s.exported = !s.imported && bind != 0 && h.type == 11;
            const std::string key = s.name + "@" + std::to_string(val) + (s.imported ? "i" : "");
            if (!seen.insert(key).second) continue;
            r.symbols.push_back(s);
        }
    }
    bool canary = false, fortify = false;
    for (const auto& s : r.symbols) { if (s.name == "__stack_chk_fail" || s.name == "__stack_chk_guard") canary = true; if (s.name.size() > 6 && s.name.rfind("_chk") == s.name.size() - 4) fortify = true; }
    // dynamic section: needed libraries, BIND_NOW, RUNPATH
    bool bind_now = false;
    if (has_dyn) {
        unsigned long long dynstr_va = 0; std::vector<unsigned long long> needed; std::vector<std::pair<std::string, unsigned long long>> paths;
        const unsigned long long esz = is64 ? 16 : 8;
        for (unsigned long long o = dyn_off; o + esz <= dyn_off + dyn_size && rd.ok(o, esz); o += esz) {
            const long long tag = is64 ? (long long)rd.u64(o) : (int32_t)rd.u32(o); const unsigned long long v = is64 ? rd.u64(o + 8) : rd.u32(o + 4);
            if (tag == 0) break;
            if (tag == 1) needed.push_back(v); else if (tag == 5) dynstr_va = v; else if (tag == 15) paths.push_back({"rpath", v}); else if (tag == 29) paths.push_back({"runpath", v});
            else if (tag == 14) paths.push_back({"soname", v});
            else if (tag == 24 || (tag == 30 && (v & 8)) || (tag == 0x6ffffffb && (v & 1))) bind_now = true;
        }
        unsigned long long dynstr_off = 0; bool mapped = false;   // map the string table's address to a file offset through LOAD segments
        for (const auto& s : r.segments) if (s.name == "LOAD" && dynstr_va >= s.addr && dynstr_va < s.addr + s.size) { dynstr_off = dynstr_va - s.addr + s.off; mapped = true; break; }
        if (mapped) {
            for (auto v : needed) r.libraries.push_back(rd.cstr(dynstr_off + v, 300));
            for (auto& [k, v] : paths) r.extra[k] = jstr(rd.cstr(dynstr_off + v, 1000));
        } else if (!needed.empty()) r.limitations.push_back("DT_NEEDED entries exist but the dynamic string table could not be mapped to the file.");
    }
    // notes: GNU build-id, ABI tag, Go build id
    for (const auto& h : shs) {
        if (h.type != 7) continue;
        unsigned long long o = h.off; const unsigned long long end = h.off + h.size;
        while (o + 12 <= end && rd.ok(o, 12)) {
            const uint32_t nsz = rd.u32(o), dsz = rd.u32(o + 4), nt = rd.u32(o + 8);
            const std::string owner = rd.cstr(o + 12, std::min<uint32_t>(nsz, 64));
            const unsigned long long desc = o + 12 + ((nsz + 3) & ~3u);
            if (!rd.ok(desc, dsz)) break;
            if (owner == "GNU" && nt == 3) { std::string id; for (uint32_t i = 0; i < dsz && i < 64; ++i) { char x[3]; std::snprintf(x, 3, "%02x", rd.u8(desc + i)); id += x; } r.extra["build_id"] = Json(id); r.notes.push_back("GNU build-id " + id + " (" + h.name + ")"); }
            else if (owner == "GNU" && nt == 1 && dsz >= 16) { static const char* oses[] = {"Linux", "GNU Hurd", "Solaris", "FreeBSD"}; const uint32_t os = rd.u32(desc); r.notes.push_back(std::string("ABI tag: ") + (os < 4 ? oses[os] : "?") + " " + std::to_string(rd.u32(desc + 4)) + "." + std::to_string(rd.u32(desc + 8)) + "." + std::to_string(rd.u32(desc + 12))); }
            else if (owner == "Go" && nt == 4) r.notes.push_back("Go build id \"" + d.substr(desc, std::min<uint32_t>(dsz, 200)) + "\"");
            else r.notes.push_back(owner + " note type " + std::to_string(nt) + " (" + h.name + ")");
            o = desc + ((dsz + 3) & ~3u);
            if (r.notes.size() > 40) break;
        }
    }
    const bool pie = type == 3 && interp;
    r.mitigations["nx"] = Json(gnu_stack ? nx : true);
    if (!gnu_stack) r.limitations.push_back("No PT_GNU_STACK segment: NX is the platform default (non-executable on modern Linux).");
    r.mitigations["pie"] = Json(type == 3 ? (pie ? "yes" : "shared library") : "no");
    r.mitigations["relro"] = Json(relro ? (bind_now ? "full" : "partial") : "none");
    r.mitigations["stack_canary"] = Json(canary ? "yes (imports __stack_chk_fail)" : "not seen");
    r.mitigations["fortify"] = Json(fortify ? "yes (_chk functions)" : "not seen");
}

// ---------- PE (Windows) ----------
std::string md5_hex(const std::string& s);   // defined below (used for the pefile-compatible import hash)
const char* pe_machine(unsigned m) {
    switch (m) { case 0x14c: return "x86"; case 0x8664: return "x86-64"; case 0xaa64: return "arm64"; case 0x1c0: case 0x1c4: return "arm"; case 0xa641: return "arm64ec"; case 0x200: return "ia64"; default: return "unknown"; }
}
void parse_pe(const Blob& b, Report& r) {
    const std::string& d = b.data; Rd rd{d};
    const unsigned long long pe = rd.u32(0x3c);
    if (!starts(d, std::string_view("PE\0\0", 4), pe)) throw std::runtime_error("MZ header without a PE signature (DOS program?)");
    const unsigned long long coff = pe + 4;
    const unsigned mach = rd.u16(coff), nsec = rd.u16(coff + 2), optsz = rd.u16(coff + 16), ch = rd.u16(coff + 18);
    const unsigned long long opt = coff + 20; const unsigned magic = rd.u16(opt); const bool p64 = magic == 0x20b;
    r.format = "PE"; r.arch = pe_machine(mach); r.bits = p64 ? 64 : 32; r.os = "Windows";
    r.kind = (ch & 0x2000) ? "DLL" : (ch & 2) ? "executable" : "object";
    r.extra["timestamp"] = num((double)rd.u32(coff + 4));
    if (optsz == 0) { r.limitations.push_back("No optional header (COFF object)."); return; }
    const unsigned long long ep = rd.u32(opt + 16);
    const unsigned long long base = p64 ? rd.u64(opt + 24) : rd.u32(opt + 28);
    r.image_base = base; r.base_set = true;
    if (ep) { r.entry = base + ep; r.has_entry = true; }
    const unsigned sub = rd.u16(opt + (p64 ? 68 : 68)), dllch = rd.u16(opt + 70);
    static const std::map<unsigned, const char*> subs = {{1, "native"}, {2, "Windows GUI"}, {3, "console"}, {9, "Windows CE"}, {10, "EFI application"}, {14, "Xbox"}, {16, "boot application"}};
    r.extra["subsystem"] = Json(subs.count(sub) ? subs.at(sub) : "other");
    r.mitigations["aslr"] = Json((dllch & 0x40) != 0);
    r.mitigations["high_entropy_aslr"] = Json((dllch & 0x20) != 0);
    r.mitigations["dep_nx"] = Json((dllch & 0x100) != 0);
    r.mitigations["cfg"] = Json((dllch & 0x4000) != 0);
    r.mitigations["no_seh"] = Json((dllch & 0x400) != 0);
    const unsigned long long dd = opt + (p64 ? 112 : 96);
    const unsigned ndd = rd.u32(opt + (p64 ? 108 : 92));
    auto dir = [&](unsigned i) -> std::pair<uint32_t, uint32_t> { if (i >= ndd) return {0, 0}; return {rd.u32(dd + i * 8), rd.u32(dd + i * 8 + 4)}; };
    const unsigned long long sect = opt + optsz;
    for (unsigned i = 0; i < nsec && i < 1000; ++i) {
        const unsigned long long o = sect + i * 40ull;
        if (!rd.ok(o, 40)) { r.limitations.push_back("Section table runs past the end of the file."); break; }
        Section s; s.name = d.substr(o, 8); s.name = s.name.c_str();
        s.vsize = rd.u32(o + 8); s.addr = base + rd.u32(o + 12); s.size = rd.u32(o + 16); s.off = rd.u32(o + 20);
        const uint32_t sc = rd.u32(o + 36);
        s.exec = sc & 0x20000000; s.write = sc & 0x80000000; s.flags = flagstr(sc & 0x40000000, s.write, s.exec);
        r.sections.push_back(s);
    }
    auto rva2off = [&](unsigned long long rva) -> long long {
        for (const auto& s : r.sections) { const unsigned long long va = s.addr - base, span = std::max(s.vsize, s.size); if (rva >= va && rva < va + span) return (long long)(rva - va + s.off); }
        return rva < 0x1000 ? (long long)rva : -1;
    };
    // imports (+ pefile-compatible imphash)
    std::string imp_for_hash;
    auto read_imports = [&](unsigned long long rva, bool delay) {
        long long o = rva2off(rva); if (o < 0) return;
        for (int n = 0; n < 4096; ++n, o += delay ? 32 : 20) {
            if (!rd.ok(o, 20)) break;
            const uint32_t name_rva = rd.u32(o + (delay ? 4 : 12));
            const uint32_t thunk = delay ? rd.u32(o + 16) : (rd.u32(o) ? rd.u32(o) : rd.u32(o + 16));
            const uint32_t iat = rd.u32(o + (delay ? 12 : 16));
            if (!name_rva) break;
            const long long no = rva2off(name_rva); if (no < 0) break;
            std::string lib = rd.cstr(no, 260);
            r.libraries.push_back(lib + (delay ? " (delay-load)" : ""));
            std::string libl = lower(lib); for (const char* ext : {".dll", ".ocx", ".sys"}) if (libl.size() > 4 && libl.substr(libl.size() - 4) == ext) libl.resize(libl.size() - 4);
            long long to = rva2off(thunk);
            for (int k = 0; to >= 0 && k < 20000; ++k) {
                const unsigned long long v = p64 ? rd.u64(to) : rd.u32(to);
                if (!v) break;
                Symbol s; s.imported = true; s.lib = lib; s.kind = "function"; s.bind = "import";
                s.addr = base + iat + (unsigned long long)k * (p64 ? 8 : 4);
                const bool by_ord = p64 ? (v >> 63) : (v >> 31);
                if (by_ord) s.name = "ordinal " + std::to_string(v & 0xffff);
                else { const long long ho = rva2off(v & 0x7fffffff); s.name = ho >= 0 ? rd.cstr(ho + 2, 512) : "?"; }
                if (!delay) imp_for_hash += (imp_for_hash.empty() ? "" : ",") + libl + "." + lower(by_ord ? "ord" + std::to_string(v & 0xffff) : s.name);
                r.symbols.push_back(s);
                to += p64 ? 8 : 4;
            }
        }
    };
    if (auto [rva, sz] = dir(1); rva && sz) read_imports(rva, false);
    if (auto [rva, sz] = dir(13); rva && sz) read_imports(rva, true);
    if (!imp_for_hash.empty()) r.extra["imphash"] = Json(md5_hex(imp_for_hash));
    // exports
    if (auto [rva, sz] = dir(0); rva && sz) {
        const long long o = rva2off(rva);
        if (o >= 0 && rd.ok(o, 40)) {
            const long long nmo = rva2off(rd.u32(o + 12)); if (nmo >= 0) r.extra["export_name"] = jstr(rd.cstr(nmo, 260));
            const uint32_t obase = rd.u32(o + 16), nfun = rd.u32(o + 20), nnames = rd.u32(o + 24);
            const long long fo = rva2off(rd.u32(o + 28)), no = rva2off(rd.u32(o + 32)), oo = rva2off(rd.u32(o + 36));
            std::vector<std::string> names(std::min<uint32_t>(nfun, 100000));
            for (uint32_t i = 0; no >= 0 && oo >= 0 && i < nnames && i < 100000; ++i) {
                const uint16_t ord = rd.u16(oo + i * 2ull); const long long so = rva2off(rd.u32(no + i * 4ull));
                if (ord < names.size() && so >= 0) names[ord] = rd.cstr(so, 512);
            }
            for (uint32_t i = 0; fo >= 0 && i < names.size(); ++i) {
                const uint32_t frva = rd.u32(fo + i * 4ull); if (!frva) continue;
                Symbol s; s.exported = true; s.kind = "function"; s.bind = "export"; s.addr = base + frva;
                s.name = names[i].empty() ? "ordinal " + std::to_string(obase + i) : names[i];
                if (frva >= rva && frva < rva + sz) { s.kind = "forwarder"; const long long fw = rva2off(frva); if (fw >= 0) s.lib = rd.cstr(fw, 260); }
                r.symbols.push_back(s);
            }
        }
    }
    // certificates (Authenticode): the security directory holds a FILE offset, not an RVA
    if (auto [off, sz] = dir(4); off && sz) {
        Json::Array certs; unsigned long long o = off;
        while (o + 8 <= (unsigned long long)off + sz && rd.ok(o, 8) && certs.size() < 8) {
            const uint32_t len = rd.u32(o); const uint16_t rev = rd.u16(o + 4), typ = rd.u16(o + 6);
            if (len < 8) break;
            certs.push_back(Json(Json::Object{{"offset", Json(hex(o))}, {"length", num(len)}, {"revision", Json(hex(rev))}, {"type", Json(typ == 2 ? "PKCS#7 SignedData (Authenticode)" : typ == 1 ? "X.509" : hex(typ))}}));
            o += (len + 7) & ~7u;
        }
        r.extra["certificates"] = Json(certs);
        r.findings.push_back("Has an embedded Authenticode signature block (" + std::to_string(certs.size()) + " entry). Astro RE reports its presence; it does not verify the chain.");
    } else r.findings.push_back("No embedded Authenticode signature.");
    if (auto [rva, sz] = dir(14); rva && sz) { r.lang = ".NET (CLR)"; r.extra["clr_header_rva"] = Json(hex(rva)); r.findings.push_back("Managed .NET assembly: use a .NET decompiler (ILSpy, dnSpy) for C# code; native disassembly will only show the loader stub."); }
    if (auto [rva, sz] = dir(2); rva && sz) r.extra["has_resources"] = Json(true);
    if (auto [rva, sz] = dir(9); rva && sz) r.extra["has_tls_callbacks_directory"] = Json(true);
    if (auto [rva, sz] = dir(6); rva && sz) {   // debug directory: CodeView PDB path
        const long long o = rva2off(rva);
        for (unsigned i = 0; o >= 0 && i < sz / 28 && i < 16; ++i) {
            const unsigned long long e = o + i * 28ull; if (rd.u32(e + 12) != 2) continue;
            const unsigned long long po = rd.u32(e + 24); if (starts(d, "RSDS", po)) { r.extra["pdb_path"] = jstr(rd.cstr(po + 24, 400)); }
        }
    }
    const auto rich = d.find("Rich", 0x80); if (rich != std::string::npos && rich < pe) r.notes.push_back("Rich header present (built with Microsoft tools).");
}

// ---------- Mach-O (macOS / iOS) ----------
const char* macho_cpu(uint32_t c) {
    switch (c) { case 7: return "x86"; case 0x01000007: return "x86-64"; case 12: return "arm"; case 0x0100000c: return "arm64"; case 0x0200000c: return "arm64_32"; case 18: return "powerpc"; default: return "unknown"; }
}
void parse_macho_at(const Blob& b, Report& r, unsigned long long base_off) {
    const std::string& d = b.data; Rd rd{d};
    const uint32_t m = rd.u32(base_off);
    rd.le = (m == 0xfeedface || m == 0xfeedfacf);
    if (!rd.le) { Rd t{d}; t.le = false; if (t.u32(base_off) != 0xfeedface && t.u32(base_off) != 0xfeedfacf) throw std::runtime_error("Not a Mach-O image at " + hex(base_off)); }
    const bool is64 = rd.u32(base_off) == 0xfeedfacf;
    r.format = "Mach-O"; r.bits = is64 ? 64 : 32; r.endian = rd.le ? "little" : "big"; r.os = "Apple (macOS/iOS)";
    r.arch = macho_cpu(rd.u32(base_off + 4));
    const uint32_t ft = rd.u32(base_off + 12), ncmds = rd.u32(base_off + 16), flags = rd.u32(base_off + 24);
    static const std::map<uint32_t, const char*> fts = {{1, "object"}, {2, "executable"}, {6, "dynamic library"}, {7, "dynamic linker"}, {8, "bundle"}, {10, "debug symbols (dSYM)"}, {11, "kext"}};
    r.kind = fts.count(ft) ? fts.at(ft) : "other";
    r.mitigations["pie"] = Json((flags & 0x200000) != 0);
    r.mitigations["no_heap_execution"] = Json((flags & 0x1000000) != 0);
    unsigned long long o = base_off + (is64 ? 32 : 28);
    unsigned long long text_vm = 0; bool code_sig = false, encrypted = false;
    for (uint32_t i = 0; i < ncmds && i < 4096; ++i) {
        if (!rd.ok(o, 8)) { r.limitations.push_back("Load commands run past the end of the file."); break; }
        const uint32_t cmd = rd.u32(o), sz = rd.u32(o + 4);
        if (sz < 8) break;
        if (cmd == 0x19 || cmd == 0x1) {
            const bool s64 = cmd == 0x19;
            Section seg; seg.name = rd.cstr(o + 8, 16);
            seg.addr = s64 ? rd.u64(o + 24) : rd.u32(o + 24); seg.vsize = s64 ? rd.u64(o + 32) : rd.u32(o + 28);
            seg.off = base_off + (s64 ? rd.u64(o + 40) : rd.u32(o + 32)); seg.size = s64 ? rd.u64(o + 48) : rd.u32(o + 36);
            const uint32_t prot = rd.u32(o + (s64 ? 60 : 44)), nsect = rd.u32(o + (s64 ? 64 : 48));
            seg.exec = prot & 4; seg.write = prot & 2; seg.flags = flagstr(prot & 1, prot & 2, prot & 4);
            if (seg.name == "__TEXT") text_vm = seg.addr;
            r.segments.push_back(seg);
            unsigned long long so = o + (s64 ? 72 : 56);
            for (uint32_t k = 0; k < nsect && k < 512; ++k, so += s64 ? 80 : 68) {
                Section s; s.name = rd.cstr(so + 16, 16) + "," + rd.cstr(so, 16);
                s.addr = s64 ? rd.u64(so + 32) : rd.u32(so + 32); s.size = s64 ? rd.u64(so + 40) : rd.u32(so + 36); s.vsize = s.size;
                s.off = base_off + rd.u32(so + (s64 ? 48 : 40));
                const uint32_t typ = rd.u32(so + (s64 ? 64 : 56)) & 0xff; if (typ == 1 || typ == 0xc) { s.size = 0; }
                s.exec = seg.exec; s.write = seg.write; s.flags = seg.flags;
                r.sections.push_back(s);
            }
        } else if (cmd == 0xc || cmd == 0x80000018 || cmd == 0x8000001f || cmd == 0x80000023) {
            const std::string why = cmd == 0x80000018 ? " (weak)" : cmd == 0x8000001f ? " (re-export)" : cmd == 0x80000023 ? " (lazy)" : "";
            r.libraries.push_back(rd.cstr(o + rd.u32(o + 8), 1024) + why);
        } else if (cmd == 0xd) r.extra["install_name"] = jstr(rd.cstr(o + rd.u32(o + 8), 1024));
        else if (cmd == 0x8000001c) { r.notes.push_back("rpath " + rd.cstr(o + rd.u32(o + 8), 1024)); }
        else if (cmd == 0x1b) { std::string u; for (int k = 0; k < 16; ++k) { char x[3]; std::snprintf(x, 3, "%02X", rd.u8(o + 8 + k)); u += x; if (k == 3 || k == 5 || k == 7 || k == 9) u += '-'; } r.extra["uuid"] = Json(u); }
        else if (cmd == 0x80000028) { r.entry = rd.u64(o + 8); r.has_entry = true; r.extra["entry_is_file_offset"] = Json(true); }
        else if (cmd == 0x1d) code_sig = true;
        else if (cmd == 0x21 || cmd == 0x2c) { if (rd.u32(o + 16)) encrypted = true; }
        else if (cmd == 0x32) { static const char* pl[] = {"?", "macOS", "iOS", "tvOS", "watchOS", "bridgeOS", "Mac Catalyst", "iOS simulator", "tvOS simulator", "watchOS simulator", "DriverKit", "visionOS", "visionOS simulator"}; const uint32_t p = rd.u32(o + 8), v = rd.u32(o + 12); r.extra["platform"] = Json(std::string(p < 13 ? pl[p] : "other") + " " + std::to_string(v >> 16) + "." + std::to_string((v >> 8) & 0xff)); }
        else if (cmd == 0x2) {
            const uint32_t symoff = rd.u32(o + 8), nsyms = rd.u32(o + 12), stroff = rd.u32(o + 16);
            const unsigned esz = is64 ? 16 : 12;
            for (uint32_t k = 0; k < nsyms && k < 400000; ++k) {
                const unsigned long long e = base_off + symoff + (unsigned long long)k * esz; if (!rd.ok(e, esz)) break;
                const uint8_t typ = rd.u8(e + 4); if (typ & 0xe0) continue;   // debugging (stab) entries
                Symbol s; s.name = rd.cstr(base_off + stroff + rd.u32(e), 512); if (s.name.empty()) continue;
                s.addr = is64 ? rd.u64(e + 8) : rd.u32(e + 8);
                const unsigned t = typ & 0x0e; s.imported = t == 0 && (typ & 1); s.exported = (typ & 1) && t == 0x0e;
                s.bind = (typ & 1) ? "global" : "local"; s.kind = t == 0x0e ? "defined" : t == 0 ? "undefined" : "other";
                if (t == 0 && !(typ & 1)) continue;
                r.symbols.push_back(s);
            }
        }
        o += sz;
    }
    if (r.has_entry && r.extra.count("entry_is_file_offset")) { r.entry += text_vm; r.extra.erase("entry_is_file_offset"); }
    r.mitigations["code_signature"] = Json(code_sig);
    if (encrypted) { r.mitigations["encrypted"] = Json(true); r.limitations.push_back("The code is FairPlay-encrypted (App Store build): disassembly of the encrypted range is meaningless."); }
    bool canary = false; for (const auto& s : r.symbols) if (s.name == "___stack_chk_fail" || s.name == "___stack_chk_guard") canary = true;
    r.mitigations["stack_canary"] = Json(canary ? "yes" : "not seen");
}

// ---------- language markers ----------
bool uvarint(const std::string& d, std::size_t& p, unsigned long long& v) {
    v = 0; for (int s = 0; s < 64 && p < d.size(); s += 7) { const unsigned char c = (unsigned char)d[p++]; v |= (unsigned long long)(c & 0x7f) << s; if (!(c & 0x80)) return true; } return false;
}
void go_buildinfo(const Blob& b, Report& r) {
    const std::string magic = "\xff Go buildinf:";
    const auto at = b.data.find(magic); if (at == std::string::npos) return;
    r.lang = r.lang.empty() ? "Go" : r.lang;
    const unsigned char flags = (unsigned char)b.data[at + 15];
    if (!(flags & 2)) { r.findings.push_back("Go binary (build info at " + hex(at) + ", pre-1.18 pointer format: version and modules not decoded)."); return; }
    std::size_t p = at + 32; unsigned long long n = 0; std::string ver, mod;
    if (uvarint(b.data, p, n) && p + n <= b.data.size()) { ver = b.data.substr(p, n); p += n; }
    if (uvarint(b.data, p, n) && p + n <= b.data.size()) mod = b.data.substr(p, n);
    if (mod.size() >= 33) mod = mod.substr(16, mod.size() - 33);   // strip the 16-byte sentinels
    Json::Object go; go["version"] = jstr(ver); go["offset"] = Json(hex(at));
    Json::Array deps, settings;
    std::istringstream in(mod); std::string line;
    while (std::getline(in, line)) {
        std::vector<std::string> f; std::istringstream ls(line); std::string w; while (std::getline(ls, w, '\t')) f.push_back(w);
        if (f.empty()) continue;
        if (f[0] == "path" && f.size() > 1) go["main_package"] = jstr(f[1]);
        else if (f[0] == "mod" && f.size() > 2) go["module"] = jstr(f[1] + " " + f[2]);
        else if ((f[0] == "dep" || f[0] == "=>") && f.size() > 2 && deps.size() < 2000) deps.push_back(jstr((f[0] == "=>" ? "replaced by " : "") + f[1] + " " + f[2]));
        else if (f[0] == "build" && f.size() > 1 && settings.size() < 100) settings.push_back(jstr(f[1]));
    }
    go["dependencies"] = Json(deps); go["build_settings"] = Json(settings);
    r.extra["go"] = Json(go);
    r.findings.push_back("Go " + ver + " binary with " + std::to_string(deps.size()) + " module dependencies (embedded build info).");
}
std::string zlib_inflate(const std::string& in, std::size_t max_out) {
    z_stream z{}; if (inflateInit(&z) != Z_OK) return {};
    std::string out; char buf[65536];
    z.next_in = (Bytef*)in.data(); z.avail_in = (uInt)in.size();
    int rc = Z_OK;
    while (rc == Z_OK && out.size() < max_out) { z.next_out = (Bytef*)buf; z.avail_out = sizeof buf; rc = inflate(&z, Z_NO_FLUSH); out.append(buf, sizeof buf - z.avail_out); }
    inflateEnd(&z); return rc == Z_STREAM_END ? out : std::string();
}
void rust_audit(const Blob& b, Report& r) {
    const Section* s = nullptr;
    for (const auto& x : r.sections) if (x.name == ".dep-v0" || x.name == "__DATA,.dep-v0" || x.name == "__DATA,__dep_v0") { s = &x; break; }
    const bool rusty = b.data.find("/rustc/") != std::string::npos || b.data.find("rust_panic") != std::string::npos || b.data.find("RUST_BACKTRACE") != std::string::npos;
    if (!s) { if (rusty && r.lang.empty()) { r.lang = "Rust"; r.findings.push_back("Rust markers found (rustc paths / panic machinery); no cargo-auditable dependency section."); } return; }
    r.lang = "Rust";
    if (s->off + s->size > b.data.size()) return;
    const std::string js = zlib_inflate(b.data.substr(s->off, s->size), 8'000'000);
    if (js.empty()) { r.limitations.push_back("The .dep-v0 section did not decompress."); return; }
    try {
        const Json j = JsonParser(js).parse(); Json::Array pk;
        if (const Json* a = j.get("packages"); a && a->array_ptr()) for (const auto& p : *a->array_ptr()) { if (pk.size() >= 3000) break; pk.push_back(jstr(get_string(p, "name") + " " + get_string(p, "version") + (get_string(p, "source").empty() ? "" : " (" + get_string(p, "source") + ")"))); }
        r.findings.push_back("Rust binary with a cargo-auditable dependency list: " + std::to_string(pk.size()) + " crates.");
        r.extra["rust_crates"] = Json(pk);
    } catch (...) { r.limitations.push_back("The cargo-auditable JSON did not parse."); }
}

std::string md5_hex(const std::string& msg) {   // RFC 1321 (only used for the PE import hash, never for security)
    static const uint32_t K[64] = {0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
    static const int S[64] = {7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21};
    uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    std::string m = msg; const unsigned long long bits = (unsigned long long)msg.size() * 8;
    m += (char)0x80; while (m.size() % 64 != 56) m += (char)0;
    for (int i = 0; i < 8; ++i) m += (char)((bits >> (8 * i)) & 0xff);
    for (std::size_t c = 0; c < m.size(); c += 64) {
        uint32_t w[16]; for (int i = 0; i < 16; ++i) { const auto* p = (const unsigned char*)m.data() + c + i * 4; w[i] = p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
        uint32_t a = h[0], bb = h[1], cc = h[2], dd = h[3];
        for (int i = 0; i < 64; ++i) {
            uint32_t f; int g;
            if (i < 16) { f = (bb & cc) | (~bb & dd); g = i; } else if (i < 32) { f = (dd & bb) | (~dd & cc); g = (5 * i + 1) % 16; }
            else if (i < 48) { f = bb ^ cc ^ dd; g = (3 * i + 5) % 16; } else { f = cc ^ (bb | ~dd); g = (7 * i) % 16; }
            const uint32_t t = dd; dd = cc; cc = bb;
            const uint32_t x = a + f + K[i] + w[g]; bb = bb + ((x << S[i]) | (x >> (32 - S[i]))); a = t;
        }
        h[0] += a; h[1] += bb; h[2] += cc; h[3] += dd;
    }
    std::string out; for (auto v : h) for (int i = 0; i < 4; ++i) { char x[3]; std::snprintf(x, 3, "%02x", (v >> (8 * i)) & 0xff); out += x; }
    return out;
}

// ---------- strings ----------
struct Str { unsigned long long off; std::string text, enc; };
const Section* section_at(const Report& r, unsigned long long off) {
    for (const auto& s : r.sections) if (s.size && off >= s.off && off < s.off + s.size) return &s;
    for (const auto& s : r.segments) if (s.size && off >= s.off && off < s.off + s.size) return &s;
    return nullptr;
}
long long va_to_off(const Report& r, unsigned long long va) {
    for (const auto& v : {&r.sections, &r.segments}) for (const auto& s : *v) if (s.size && va >= s.addr && va < s.addr + s.size) return (long long)(va - s.addr + s.off);
    return -1;
}
long long off_to_va(const Report& r, unsigned long long off) {
    for (const auto& v : {&r.sections, &r.segments}) for (const auto& s : *v) if (s.size && s.addr && off >= s.off && off < s.off + s.size) return (long long)(off - s.off + s.addr);
    return -1;
}
// ASCII runs and UTF-16LE runs of printable characters, at least min_len long.
std::vector<Str> extract_strings(const std::string& d, std::size_t min_len, std::size_t max_count, const std::string& filter_lower) {
    std::vector<Str> out;
    auto pr = [](unsigned char c) { return (c >= 0x20 && c < 0x7f) || c == '\t'; };
    auto keep = [&](unsigned long long off, std::string s, const char* enc) {
        if (!filter_lower.empty() && lower(s).find(filter_lower) == std::string::npos) return;
        out.push_back({off, std::move(s), enc});
    };
    std::size_t i = 0, n = d.size();
    while (i < n && out.size() < max_count) {
        if (!pr((unsigned char)d[i])) { ++i; continue; }
        // UTF-16LE: printable, 0, printable, 0 ...
        if (i + 1 < n && d[i + 1] == 0) {
            std::size_t j = i; std::string s;
            while (j + 1 < n && pr((unsigned char)d[j]) && d[j + 1] == 0 && s.size() < 4000) { s += d[j]; j += 2; }
            if (s.size() >= min_len) { keep(i, s, "utf-16le"); i = j; continue; }
        }
        std::size_t j = i; while (j < n && pr((unsigned char)d[j]) && j - i < 4000) ++j;
        if (j - i >= min_len) keep(i, d.substr(i, j - i), "ascii");
        i = j + 1;
    }
    return out;
}
// Strings worth a look: URLs, paths, keys, crypto names, debug messages.
std::string string_tag(const std::string& s) {
    std::size_t alnum = 0; for (unsigned char c : s) alnum += std::isalnum(c) || c == 32 || c == 47 || c == 46 || c == 95; 
    if (alnum * 10 < s.size() * 8) return "";   /* mostly punctuation: noise from code bytes */
    static const std::regex url(R"((https?|wss?|ftp)://[^\s"'<>]{4,})", std::regex::icase), ip(R"(\b(\d{1,3}\.){3}\d{1,3}(:\d{2,5})?\b)"),
        path(R"(([A-Za-z]:\\|/(usr|etc|var|tmp|home|Users|Library|System|data|proc)/))"), key(R"((api[_-]?key|secret|token|passw|bearer|private key|BEGIN [A-Z ]+KEY))", std::regex::icase),
        crypto(R"(\b(AES|RSA|SHA-?(1|256|512)|MD5|ChaCha|Curve25519|HMAC|OpenSSL|BoringSSL|mbedTLS|libsodium)\b)", std::regex::icase),
        reg(R"(\b(HKEY_|SOFTWARE\\|CurrentVersion\\Run))"), fmt(R"(%[0-9.-]*[sdxufp])");
    if (std::regex_search(s, url)) return "url";
    if (std::regex_search(s, key)) return "secret-like";
    if (std::regex_search(s, reg)) return "registry";
    if (std::regex_search(s, ip)) return "ip";
    if (std::regex_search(s, crypto)) return "crypto";
    if (std::regex_search(s, path)) return "path";
    if (s.size() >= 8 && s.find(32) != std::string::npos && std::regex_search(s, fmt)) return "format";
    return "";
}

// ---------- ZIP family (APK, JAR, IPA, VSIX, wheel, Office, NuGet) ----------
struct ZipEntry { std::string name; unsigned long long csize = 0, usize = 0, local = 0; unsigned method = 0; uint32_t crc = 0; };
std::vector<ZipEntry> zip_entries(const std::string& d, std::string& why) {
    std::vector<ZipEntry> v; Rd rd{d};
    if (d.size() < 22) { why = "too small"; return v; }
    long long e = -1;
    for (long long i = (long long)d.size() - 22; i >= 0 && i >= (long long)d.size() - 65557; --i) if (rd.u32(i) == 0x06054b50) { e = i; break; }
    if (e < 0) { why = "no end-of-central-directory record"; return v; }
    unsigned long long cd = rd.u32(e + 16), n = rd.u16(e + 10);
    if (rd.u32(e + 16) == 0xffffffff || n == 0xffff) {   // ZIP64
        const long long l = e - 20; if (l >= 0 && rd.u32(l) == 0x07064b50) { const unsigned long long z = rd.u64(l + 8); if (rd.u32(z) == 0x06064b50) { n = rd.u64(z + 32); cd = rd.u64(z + 48); } }
    }
    unsigned long long o = cd;
    for (unsigned long long i = 0; i < n && i < 200000; ++i) {
        if (!rd.ok(o, 46) || rd.u32(o) != 0x02014b50) { why = "central directory is damaged at " + hex(o); break; }
        ZipEntry z; z.method = rd.u16(o + 10); z.crc = rd.u32(o + 16); z.csize = rd.u32(o + 20); z.usize = rd.u32(o + 24);
        const unsigned nl = rd.u16(o + 28), xl = rd.u16(o + 30), cl = rd.u16(o + 32); z.local = rd.u32(o + 42);
        z.name = d.substr(o + 46, nl);
        for (unsigned long long x = o + 46 + nl; x + 4 <= o + 46 + nl + xl; ) {   // ZIP64 extra field
            const unsigned id = rd.u16(x), sz = rd.u16(x + 2); unsigned long long q = x + 4;
            if (id == 1) { if (z.usize == 0xffffffff) { z.usize = rd.u64(q); q += 8; } if (z.csize == 0xffffffff) { z.csize = rd.u64(q); q += 8; } if (z.local == 0xffffffff) z.local = rd.u64(q); }
            x += 4 + sz;
        }
        v.push_back(z); o += 46 + nl + xl + cl;
    }
    return v;
}
std::optional<std::string> zip_read(const std::string& d, const ZipEntry& z, std::size_t max = 64 * 1024 * 1024) {
    Rd rd{d}; if (!rd.ok(z.local, 30) || rd.u32(z.local) != 0x04034b50) return std::nullopt;
    const unsigned long long data = z.local + 30 + rd.u16(z.local + 26) + rd.u16(z.local + 28);
    if (!rd.ok(data, z.csize) || z.usize > max) return std::nullopt;
    if (z.method == 0) return d.substr(data, z.csize);
    if (z.method != 8) return std::nullopt;
    z_stream s{}; if (inflateInit2(&s, -MAX_WBITS) != Z_OK) return std::nullopt;
    std::string out((std::size_t)z.usize, '\0');
    s.next_in = (Bytef*)d.data() + data; s.avail_in = (uInt)z.csize; s.next_out = (Bytef*)out.data(); s.avail_out = (uInt)out.size();
    const int rc = inflate(&s, Z_FINISH); inflateEnd(&s);
    if (rc != Z_STREAM_END) return std::nullopt;
    out.resize(s.total_out); return out;
}
const ZipEntry* zip_find(const std::vector<ZipEntry>& v, const std::string& name) { for (const auto& z : v) if (z.name == name) return &z; return nullptr; }

// Android binary XML (AndroidManifest.xml inside an APK) -> readable XML + the facts that matter.
struct Axml { std::string xml; std::vector<std::tuple<std::string, std::string, std::map<std::string, std::string>>> elems; };
Axml axml_decode(const std::string& d) {
    Axml out; Rd rd{d};
    if (d.size() < 8 || rd.u16(0) != 3) return out;
    std::vector<std::string> pool;
    std::size_t o = rd.u16(2); int depth = 0;
    std::vector<std::string> stack;
    while (o + 8 <= d.size()) {
        const unsigned type = rd.u16(o), hsz = rd.u16(o + 2); const uint32_t sz = rd.u32(o + 4);
        if (sz < 8 || o + sz > d.size()) break;
        if (type == 1) {   // string pool
            const uint32_t cnt = rd.u32(o + 8), flags = rd.u32(o + 16), sstart = rd.u32(o + 20); const bool utf8 = flags & 0x100;
            for (uint32_t i = 0; i < cnt && i < 200000; ++i) {
                std::size_t p = o + sstart + rd.u32(o + hsz + i * 4ull); std::string s;
                if (utf8) {
                    unsigned n1 = rd.u8(p++); if (n1 & 0x80) ++p;   // utf-16 length (skip)
                    unsigned n = rd.u8(p++); if (n & 0x80) n = ((n & 0x7f) << 8) | rd.u8(p++);
                    if (p + n <= d.size()) s = d.substr(p, n);
                } else {
                    unsigned n = rd.u16(p); p += 2; if (n & 0x8000) { n = ((n & 0x7fff) << 16) | rd.u16(p); p += 2; }
                    for (unsigned k = 0; k < n && p + 2 <= d.size(); ++k, p += 2) { const unsigned c = rd.u16(p); if (c < 0x80) s += (char)c; else if (c < 0x800) { s += (char)(0xC0 | c >> 6); s += (char)(0x80 | (c & 0x3f)); } else { s += (char)(0xE0 | c >> 12); s += (char)(0x80 | ((c >> 6) & 0x3f)); s += (char)(0x80 | (c & 0x3f)); } }
                }
                pool.push_back(s);
            }
        } else if (type == 0x102) {   // start element
            auto S = [&](uint32_t i) { return i < pool.size() ? pool[i] : std::string(); };
            const std::string name = S(rd.u32(o + 20));
            const unsigned acount = rd.u16(o + 28), astart = rd.u16(o + 24), asize = rd.u16(o + 26) ? rd.u16(o + 26) : 20;
            std::map<std::string, std::string> attrs; std::string line = std::string(depth * 2, ' ') + "<" + name;
            for (unsigned a = 0; a < acount && a < 500; ++a) {
                const std::size_t ao = o + 16 + astart + a * (std::size_t)asize;
                const std::string an = S(rd.u32(ao + 4)); const uint32_t raw = rd.u32(ao + 8); const uint8_t vt = rd.u8(ao + 15); const uint32_t vd = rd.u32(ao + 16);
                std::string v;
                if (raw != 0xffffffff) v = S(raw);
                else if (vt == 3) v = S(vd);
                else if (vt == 0x12) v = vd ? "true" : "false";
                else if (vt == 0x10) v = std::to_string((int32_t)vd);
                else if (vt == 0x11) v = hex(vd);
                else if (vt == 1) v = "@" + hex(vd);
                else if (vt == 2) v = "?attr/" + hex(vd);
                else v = hex(vd);
                attrs[an] = v; line += " " + an + "=\"" + v + "\"";
            }
            out.elems.emplace_back(name, stack.empty() ? std::string() : stack.back(), attrs);
            stack.push_back(name); out.xml += line + ">\n"; ++depth;
        } else if (type == 0x103) {
            if (depth > 0) --depth;
            if (!stack.empty()) stack.pop_back();
            if (out.xml.size() < 400000) out.xml += std::string(depth * 2, ' ') + "</" + (rd.u32(o + 20) < pool.size() ? pool[rd.u32(o + 20)] : "") + ">\n";
        }
        o += sz;
        if (out.xml.size() > 2'000'000) break;
    }
    return out;
}

std::string colon_hex(const std::string& h) { std::string o; for (std::size_t i = 0; i < h.size(); i += 2) { if (i) o += ':'; o += (char)std::toupper((unsigned char)h[i]); o += (char)std::toupper((unsigned char)h[i + 1]); } return o; }
// APK Signature Scheme v2 / v3 / v3.1: first certificate of each signer -> SHA-256 fingerprint.
Json apk_signers(const std::string& d) {
    Rd rd{d}; Json::Array out;
    long long e = -1; for (long long i = (long long)d.size() - 22; i >= 0 && i >= (long long)d.size() - 65557; --i) if (rd.u32(i) == 0x06054b50) { e = i; break; }
    if (e < 0) return Json(out);
    const unsigned long long cd = rd.u32(e + 16);
    if (cd < 24 || !starts(d, "APK Sig Block 42", cd - 16)) return Json(out);
    const unsigned long long bsz = rd.u64(cd - 24), start = cd - bsz - 8 + 8;
    unsigned long long p = start;
    while (p + 12 <= cd - 24 && out.size() < 8) {
        const unsigned long long len = rd.u64(p); const uint32_t id = rd.u32(p + 8);
        if (len < 4 || p + 8 + len > cd) break;
        const char* scheme = id == 0x7109871a ? "v2" : id == 0xf05368c0 ? "v3" : id == 0x1b93ad61 ? "v3.1" : nullptr;
        if (scheme) {
            unsigned long long q = p + 12; const unsigned long long qend = q + 4 + rd.u32(q); q += 4;   // signers
            while (q + 4 <= qend && q + 4 <= d.size()) {
                const unsigned long long sl = rd.u32(q); const unsigned long long s0 = q + 4;
                const unsigned long long sd = s0 + 4;                          // signed data (length-prefixed)
                const unsigned long long dig = sd; const unsigned long long digl = rd.u32(dig);
                const unsigned long long certs = dig + 4 + digl; const unsigned long long cl = rd.u32(certs);
                if (cl >= 4) { const uint32_t c1 = rd.u32(certs + 4); if (rd.ok(certs + 8, c1)) out.push_back(Json(Json::Object{{"scheme", Json(scheme)}, {"cert_sha256", Json(colon_hex(sha256_hex(d.substr(certs + 8, c1))))}, {"cert_bytes", num(c1)}})); }
                q = s0 + sl;
            }
        }
        p += 8 + len;
    }
    return Json(out);
}

void parse_zip(const Blob& b, Report& r) {
    std::string why; const auto ents = zip_entries(b.data, why);
    if (!why.empty()) r.limitations.push_back("ZIP: " + why + ".");
    r.format = "ZIP"; r.kind = "archive";
    auto has = [&](const std::string& n) { return zip_find(ents, n) != nullptr; };
    bool apk = has("AndroidManifest.xml") && (has("classes.dex") || has("resources.arsc"));
    bool ipa = false, jar = has("META-INF/MANIFEST.MF"), vsix = has("extension.vsixmanifest"), whl = false, office = has("[Content_Types].xml"), aab = has("base/manifest/AndroidManifest.xml");
    for (const auto& z : ents) { if (z.name.rfind("Payload/", 0) == 0 && z.name.find(".app/") != std::string::npos) ipa = true; if (z.name.find(".dist-info/WHEEL") != std::string::npos) whl = true; }
    r.kind = apk ? "Android app (APK)" : aab ? "Android App Bundle (AAB)" : ipa ? "iOS app (IPA)" : vsix ? "VS Code extension (VSIX)" : whl ? "Python wheel" : office ? "Office document" : jar ? "Java archive (JAR)" : "ZIP archive";
    if (apk) r.os = "Android";
    if (ipa) r.os = "iOS";
    unsigned long long total = 0; std::map<std::string, int> exts; Json::Array big, natives, dexes;
    for (const auto& z : ents) {
        total += z.usize;
        const auto dot = z.name.rfind('.'); const std::string ext = dot == std::string::npos || z.name.find('/', dot) != std::string::npos ? "(none)" : lower(z.name.substr(dot));
        ++exts[ext];
        if (z.name.rfind("lib/", 0) == 0 && ext == ".so" && natives.size() < 200) natives.push_back(jstr(z.name));
        if (ext == ".dex" && dexes.size() < 100) {
            Json::Object dx{{"name", jstr(z.name)}, {"bytes", num((double)z.usize)}};
            if (auto c = zip_read(b.data, z, 256 * 1024 * 1024); c && c->size() > 0x70 && starts(*c, "dex\n")) { Rd rd{*c}; dx["classes"] = num(rd.u32(0x60)); dx["methods"] = num(rd.u32(0x58)); dx["strings"] = num(rd.u32(0x38)); dx["version"] = jstr(c->substr(4, 3)); }
            dexes.push_back(Json(dx));
        }
    }
    std::vector<ZipEntry> bysize = ents; std::sort(bysize.begin(), bysize.end(), [](auto& a, auto& c) { return a.usize > c.usize; });
    for (std::size_t i = 0; i < bysize.size() && i < 15; ++i) big.push_back(Json(Json::Object{{"name", jstr(bysize[i].name)}, {"bytes", num((double)bysize[i].usize)}}));
    Json::Object ex; for (auto& [k, v] : exts) ex[k] = num(v);
    r.extra["entries"] = num((double)ents.size()); r.extra["uncompressed_bytes"] = num((double)total); r.extra["by_extension"] = Json(ex); r.extra["largest"] = Json(big);
    if (!natives.empty()) r.extra["native_libraries"] = Json(natives);
    if (!dexes.empty()) r.extra["dex"] = Json(dexes);
    if (apk || aab) {
        const ZipEntry* m = zip_find(ents, apk ? "AndroidManifest.xml" : "base/manifest/AndroidManifest.xml");
        if (m) if (auto c = zip_read(b.data, *m)) {
            const Axml ax = axml_decode(*c);
            if (ax.xml.empty() && aab) r.limitations.push_back("The AAB manifest is protobuf-encoded; Astro RE does not decode it yet.");
            Json::Array perms, comps;
            for (std::size_t ei = 0; ei < ax.elems.size(); ++ei) {
                const auto& [name, parent, at] = ax.elems[ei];
                auto A = [&](const char* k) { auto it = at.find(k); return it == at.end() ? std::string() : it->second; };
                if (name == "manifest") { r.extra["package"] = jstr(A("package")); r.extra["version_name"] = jstr(A("versionName")); r.extra["version_code"] = jstr(A("versionCode")); }
                else if (name == "uses-sdk") { r.extra["min_sdk"] = jstr(A("minSdkVersion")); r.extra["target_sdk"] = jstr(A("targetSdkVersion")); }
                else if (name == "uses-permission" || name == "uses-permission-sdk-23") perms.push_back(jstr(A("name")));
                else if (name == "activity" || name == "activity-alias" || name == "service" || name == "receiver" || name == "provider") {
                    bool filters = false; for (std::size_t j = ei + 1; j < ax.elems.size() && std::get<1>(ax.elems[j]) != "application"; ++j) if (std::get<0>(ax.elems[j]) == "intent-filter") { filters = true; break; }
                    const std::string exp = A("exported");
                    comps.push_back(Json(Json::Object{{"type", Json(name)}, {"name", jstr(A("name"))}, {"exported", Json(exp.empty() ? (filters ? "implicit (has intent filters)" : "unset") : exp)}}));
                }
                else if (name == "application") { if (A("debuggable") == "true") r.findings.push_back("android:debuggable=\"true\": a debug build."); if (A("usesCleartextTraffic") == "true") r.findings.push_back("Allows cleartext (http) traffic."); if (A("allowBackup") != "false") r.findings.push_back("allowBackup is not false: app data can be backed up."); }
            }
            r.extra["permissions"] = Json(perms); r.extra["components"] = Json(comps);
            r.extra["manifest_xml"] = jstr(ax.xml.substr(0, 60000));
            if (ax.xml.size() > 60000) r.limitations.push_back("manifest_xml is cut to 60 000 characters.");
        }
        Json sig = apk_signers(b.data); r.extra["signers"] = sig;
        bool v1 = false; for (const auto& z : ents) if (z.name.rfind("META-INF/", 0) == 0 && (z.name.ends_with(".RSA") || z.name.ends_with(".DSA") || z.name.ends_with(".EC"))) v1 = true;
        r.extra["v1_signature_files"] = Json(v1);
        if (sig.array_ptr() && sig.array_ptr()->empty() && !v1) r.findings.push_back("No APK signature found (unsigned build).");
        if (natives.empty()) r.findings.push_back("No native .so libraries: all code is in DEX (Java/Kotlin)."); else r.findings.push_back(std::to_string(natives.size()) + " native libraries under lib/ (inspect them one by one after extracting).");
        r.lang = "Java/Kotlin (DEX)";
        for (const auto& z : ents) if (z.name.find("libflutter.so") != std::string::npos) r.lang = "Flutter (Dart)"; else if (z.name.find("libreactnativejni") != std::string::npos || z.name == "assets/index.android.bundle") r.lang = "React Native"; else if (z.name.find("libunity.so") != std::string::npos) r.lang = "Unity"; else if (z.name.find("libil2cpp.so") != std::string::npos) r.lang = "Unity (IL2CPP)"; else if (z.name.find("libmonodroid") != std::string::npos) r.lang = ".NET MAUI/Xamarin";
    }
    if (jar) if (auto c = zip_read(b.data, *zip_find(ents, "META-INF/MANIFEST.MF"), 1'000'000)) r.extra["jar_manifest"] = jstr(c->substr(0, 4000));
    if (vsix) if (const auto* p = zip_find(ents, "extension/package.json")) if (auto c = zip_read(b.data, *p, 4'000'000)) { try { const Json j = JsonParser(*c).parse(); r.extra["extension"] = jstr(get_string(j, "publisher") + "." + get_string(j, "name") + " " + get_string(j, "version")); r.extra["extension_main"] = jstr(get_string(j, "main")); } catch (...) {} }
}

// ---------- Electron ASAR ----------
void parse_asar(const Blob& b, Report& r) {
    Rd rd{b.data}; const uint32_t hlen = rd.u32(12);
    r.format = "ASAR"; r.kind = "Electron app archive"; r.lang = "JavaScript (Electron)";
    if (!rd.ok(16, hlen)) { r.limitations.push_back("ASAR header runs past the end of the file."); return; }
    Json h; try { h = JsonParser(b.data.substr(16, hlen)).parse(); } catch (...) { r.limitations.push_back("ASAR header is not valid JSON."); return; }
    Json::Array files, natives; unsigned long long count = 0, unpacked = 0; std::string main;
    std::function<void(const Json&, const std::string&)> walk = [&](const Json& node, const std::string& pre) {
        const Json* f = node.get("files"); if (!f || !f->object_ptr()) return;
        for (const auto& [k, v] : *f->object_ptr()) {
            const std::string p = pre.empty() ? k : pre + "/" + k;
            if (v.get("files")) { walk(v, p); continue; }
            ++count; if (jtrue(v.get("unpacked"))) ++unpacked;
            if (p.ends_with(".node") && natives.size() < 200) natives.push_back(jstr(p));
            if ((p == "package.json") && main.empty()) main = "package.json";
            if (files.size() < 400) files.push_back(Json(Json::Object{{"path", jstr(p)}, {"bytes", num(json_int(v, "size", 0))}}));
        }
    };
    walk(h, "");
    r.extra["files"] = num((double)count); r.extra["unpacked_files"] = num((double)unpacked); r.extra["listing"] = Json(files);
    if (!natives.empty()) r.extra["native_addons"] = Json(natives);
    // package.json content: file data starts after the header (aligned to 4)
    const unsigned long long base = 16 + ((hlen + 3) & ~3u) - 8 + 8;
    if (const Json* f = h.get("files")) if (const Json* pj = f->get("package.json")) {
        const unsigned long long off = std::stoull(get_string(*pj, "offset", "0")); const int sz = json_int(*pj, "size", 0);
        unsigned long long start = 8 + rd.u32(4);   // header pickle size field
        if (rd.ok(start + off, sz)) { try { const Json j = JsonParser(b.data.substr(start + off, sz)).parse(); r.extra["app"] = jstr(get_string(j, "name") + " " + get_string(j, "version")); r.extra["main"] = jstr(get_string(j, "main")); } catch (...) {} }
        (void)base;
    }
    r.findings.push_back("Electron archive with " + std::to_string(count) + " files. Its JavaScript is readable as-is: extract with \"npx @electron/asar extract app.asar out\" or read files through Astro RE.");
}

// ---------- WebAssembly ----------
void parse_wasm(const Blob& b, Report& r) {
    const std::string& d = b.data; r.format = "WebAssembly"; r.kind = "module"; r.bits = 32; r.arch = "wasm32";
    static const char* names[] = {"custom", "type", "import", "function", "table", "memory", "global", "export", "start", "element", "code", "data", "datacount", "tag"};
    std::size_t p = 8; unsigned long long v = 0;
    auto name_at = [&](std::size_t& q) { unsigned long long n = 0; if (!uvarint(d, q, n) || q + n > d.size()) return std::string(); std::string s = d.substr(q, n); q += n; return s; };
    while (p < d.size()) {
        const unsigned id = (unsigned char)d[p++]; if (!uvarint(d, p, v) || p + v > d.size()) { r.limitations.push_back("Truncated section."); break; }
        const std::size_t end = p + v; Section s; s.off = p; s.size = v; s.vsize = v; s.name = id < 14 ? names[id] : "unknown";
        if (id == 0) { std::size_t q = p; s.name = "custom:" + name_at(q); }
        if (id == 10) s.exec = true;
        r.sections.push_back(s);
        if (id == 2 || id == 7) {
            std::size_t q = p; unsigned long long n = 0; uvarint(d, q, n);
            for (unsigned long long i = 0; i < n && i < 50000 && q < end; ++i) {
                Symbol sy;
                if (id == 2) { const std::string mod = name_at(q), nm = name_at(q); const unsigned kind = (unsigned char)d[q++]; sy.lib = mod; sy.name = nm; sy.imported = true; sy.kind = kind == 0 ? "function" : kind == 1 ? "table" : kind == 2 ? "memory" : "global";
                    unsigned long long t = 0; if (kind == 0) uvarint(d, q, t); else if (kind == 1) { q++; unsigned long long fl = 0; uvarint(d, q, fl); uvarint(d, q, t); if (fl & 1) uvarint(d, q, t); } else if (kind == 2) { unsigned long long fl = 0; uvarint(d, q, fl); uvarint(d, q, t); if (fl & 1) uvarint(d, q, t); } else if (kind == 3) q += 2; else break; }
                else { sy.name = name_at(q); const unsigned kind = (unsigned char)d[q++]; unsigned long long idx = 0; uvarint(d, q, idx); sy.exported = true; sy.addr = idx; sy.kind = kind == 0 ? "function" : kind == 1 ? "table" : kind == 2 ? "memory" : "global"; }
                r.symbols.push_back(sy);
            }
        }
        p = end;
    }
    r.findings.push_back("WebAssembly module: imports show what the host provides (" + std::to_string(std::count_if(r.symbols.begin(), r.symbols.end(), [](auto& s) { return s.imported; })) + "), exports show the entry points.");
}

// ---------- AI model files ----------
void parse_gguf(const Blob& b, Report& r) {
    const std::string& d = b.data; Rd rd{d}; r.format = "GGUF"; r.kind = "AI model (llama.cpp)";
    const uint32_t ver = rd.u32(4); std::size_t p = 8;
    const unsigned long long nt = ver == 1 ? rd.u32(p) : rd.u64(p); p += ver == 1 ? 4 : 8;
    const unsigned long long nkv = ver == 1 ? rd.u32(p) : rd.u64(p); p += ver == 1 ? 4 : 8;
    auto gstr = [&]() { const unsigned long long n = ver == 1 ? rd.u32(p) : rd.u64(p); p += ver == 1 ? 4 : 8; if (!rd.ok(p, n) || n > 1'000'000) throw std::runtime_error("bad string"); std::string s = d.substr(p, n); p += n; return s; };
    static const int sz[] = {1, 1, 2, 2, 4, 4, 4, 1, 0, 0, 8, 8, 8};
    Json::Object meta; std::string arch;
    std::function<std::string(uint32_t)> val = [&](uint32_t t) -> std::string {
        switch (t) { case 0: return std::to_string(rd.u8(p++)); case 1: return std::to_string((int8_t)rd.u8(p++)); case 2: { auto v = rd.u16(p); p += 2; return std::to_string(v); } case 3: { auto v = (int16_t)rd.u16(p); p += 2; return std::to_string(v); }
            case 4: { auto v = rd.u32(p); p += 4; return std::to_string(v); } case 5: { auto v = (int32_t)rd.u32(p); p += 4; return std::to_string(v); } case 6: { uint32_t u = rd.u32(p); float f; std::memcpy(&f, &u, 4); p += 4; std::ostringstream o; o << f; return o.str(); }
            case 7: return rd.u8(p++) ? "true" : "false"; case 8: return gstr(); case 10: { auto v = rd.u64(p); p += 8; return std::to_string(v); } case 11: { auto v = (long long)rd.u64(p); p += 8; return std::to_string(v); }
            case 12: { uint64_t u = rd.u64(p); double f; std::memcpy(&f, &u, 8); p += 8; std::ostringstream o; o << f; return o.str(); }
            case 9: { const uint32_t et = rd.u32(p); p += 4; const unsigned long long n = ver == 1 ? rd.u32(p) : rd.u64(p); p += ver == 1 ? 4 : 8;
                if (et == 8) { for (unsigned long long i = 0; i < n; ++i) { const unsigned long long l = ver == 1 ? rd.u32(p) : rd.u64(p); p += (ver == 1 ? 4 : 8) + l; if (p > d.size()) throw std::runtime_error("bad array"); } }
                else if (et < 13 && sz[et]) p += n * sz[et]; else throw std::runtime_error("nested array");
                return "[array of " + std::to_string(n) + "]"; }
            default: throw std::runtime_error("unknown type"); }
    };
    try {
        for (unsigned long long i = 0; i < nkv && i < 100000; ++i) { const std::string k = gstr(); const uint32_t t = rd.u32(p); p += 4; const std::string v = val(t); if (meta.size() < 300) meta[k] = jstr(v.substr(0, 500)); if (k == "general.architecture") arch = v; }
        unsigned long long params = 0; std::map<uint32_t, unsigned long long> types;
        for (unsigned long long i = 0; i < nt && i < 200000; ++i) { gstr(); const uint32_t nd = rd.u32(p); p += 4; unsigned long long c = 1; for (uint32_t k = 0; k < nd && k < 8; ++k) { c *= rd.u64(p); p += 8; } const uint32_t ty = rd.u32(p); p += 4 + 8; params += c; types[ty] += c; }
        r.extra["parameters"] = num((double)params);
        std::ostringstream o; o << std::fixed << std::setprecision(2) << params / 1e9; r.findings.push_back("GGUF v" + std::to_string(ver) + " " + arch + " model, " + std::to_string(nt) + " tensors, about " + o.str() + " billion parameters.");
    } catch (const std::exception& e) { r.limitations.push_back(std::string("GGUF metadata stopped early: ") + e.what()); }
    r.extra["metadata"] = Json(meta); r.arch = arch.empty() ? "?" : arch;
}
void parse_safetensors(const Blob& b, Report& r) {
    Rd rd{b.data}; const unsigned long long n = rd.u64(0); r.format = "safetensors"; r.kind = "AI model weights";
    Json h; try { h = JsonParser(b.data.substr(8, n)).parse(); } catch (...) { r.limitations.push_back("Header is not valid JSON."); return; }
    unsigned long long params = 0, count = 0; std::map<std::string, unsigned long long> dt;
    if (h.object_ptr()) for (const auto& [k, v] : *h.object_ptr()) {
        if (k == "__metadata__") { r.extra["metadata"] = v; continue; }
        unsigned long long c = 1; if (const Json* s = v.get("shape"); s && s->array_ptr()) for (const auto& x : *s->array_ptr()) c *= (unsigned long long)jnum(x);
        params += c; ++count; dt[get_string(v, "dtype")] += c;
    }
    Json::Object d2; for (auto& [k, v] : dt) d2[k] = num((double)v);
    r.extra["tensors"] = num((double)count); r.extra["parameters"] = num((double)params); r.extra["dtypes"] = Json(d2);
    std::ostringstream o; o << std::fixed << std::setprecision(2) << params / 1e9; r.findings.push_back(std::to_string(count) + " tensors, about " + o.str() + " billion parameters" + (b.truncated ? " in this shard" : "") + ".");
}

// ---------- SQLite: read the schema table (sqlite_schema, page 1) ----------
void parse_sqlite(const Blob& b, Report& r) {
    const std::string& d = b.data; Rd rd{d}; rd.le = false;
    r.format = "SQLite"; r.kind = "database";
    unsigned long long ps = rd.u16(16); if (ps == 1) ps = 65536;
    r.extra["page_size"] = num((double)ps); r.extra["pages"] = num(rd.u32(28)); r.extra["text_encoding"] = Json(rd.u32(56) == 1 ? "UTF-8" : rd.u32(56) == 2 ? "UTF-16le" : "UTF-16be");
    auto varint = [&](std::size_t& p) { unsigned long long v = 0; for (int i = 0; i < 9 && p < d.size(); ++i) { const unsigned char c = (unsigned char)d[p++]; if (i == 8) { v = (v << 8) | c; break; } v = (v << 7) | (c & 0x7f); if (!(c & 0x80)) break; } return v; };
    Json::Array objs; int pages = 0;
    std::function<void(unsigned long long)> page = [&](unsigned long long pg) {
        if (++pages > 2000 || pg == 0) return;
        const unsigned long long base = (pg - 1) * ps; const std::size_t h = pg == 1 ? 100 : 0;
        if (!rd.ok(base + h, 12)) return;
        const unsigned type = rd.u8(base + h); const unsigned cells = rd.u16(base + h + 3);
        const std::size_t ptrs = base + h + (type == 5 ? 12 : 8);
        for (unsigned i = 0; i < cells; ++i) {
            std::size_t c = base + rd.u16(ptrs + i * 2);
            if (type == 5) { page(rd.u32(c)); continue; }
            if (type != 13) return;
            const unsigned long long plen = varint(c); varint(c);   // payload length, rowid
            const std::size_t rec = c, local_end = std::min<std::size_t>(c + plen, base + ps);
            std::size_t hp = rec; const unsigned long long hlen = varint(hp); std::vector<unsigned long long> types;
            while (hp < rec + hlen) types.push_back(varint(hp));
            std::size_t vp = rec + hlen; std::vector<std::string> cols;
            for (auto t : types) {
                std::size_t n = t >= 13 ? (t - 13) / 2 : (t <= 4 ? t : t == 5 ? 6 : t == 6 || t == 7 ? 8 : 0);
                std::string v = vp + n <= local_end ? d.substr(vp, n) : (vp < local_end ? d.substr(vp, local_end - vp) + "…" : "");
                if (t >= 1 && t <= 6) { long long x = 0; for (std::size_t k = 0; k < n && vp + k < d.size(); ++k) x = (x << 8) | (unsigned char)d[vp + k]; v = std::to_string(x); }
                cols.push_back(v); vp += n;
            }
            if (cols.size() >= 5 && objs.size() < 1000) objs.push_back(Json(Json::Object{{"type", jstr(cols[0])}, {"name", jstr(cols[1])}, {"table", jstr(cols[2])}, {"rootpage", jstr(cols[3])}, {"sql", jstr(cols[4].substr(0, 4000))}}));
        }
    };
    page(1);
    r.extra["schema"] = Json(objs);
    const auto tables = std::count_if(objs.begin(), objs.end(), [](const Json& o) { return get_string(o, "type") == "table"; });
    r.findings.push_back(std::to_string(tables) + " tables and " + std::to_string(objs.size() - tables) + " other schema objects (indexes, views, triggers). Long CREATE statements that spill to overflow pages end with …");
}

// ---------- smaller formats ----------
void parse_java_class(const Blob& b, Report& r) {
    Rd rd{b.data}; rd.le = false; r.format = "Java class"; r.kind = "JVM bytecode"; r.lang = "Java/Kotlin (JVM)";
    const unsigned major = rd.u16(6); r.extra["class_version"] = num(major);
    if (major >= 45) r.extra["java_release"] = Json(major >= 49 ? std::to_string(major - 44) : "1." + std::to_string(major - 44));
    r.findings.push_back("Java class file (class version " + std::to_string(major) + "). Decompile with JADX, CFR or Vineflower; Astro RE can run JADX for you.");
}
void parse_dex(const Blob& b, Report& r) {
    Rd rd{b.data}; r.format = "DEX"; r.kind = "Android bytecode"; r.lang = "Java/Kotlin (DEX)"; r.os = "Android";
    r.extra["dex_version"] = jstr(b.data.substr(4, 3)); r.extra["strings"] = num(rd.u32(0x38)); r.extra["types"] = num(rd.u32(0x40));
    r.extra["methods"] = num(rd.u32(0x58)); r.extra["classes"] = num(rd.u32(0x60));
    r.findings.push_back(std::to_string(rd.u32(0x60)) + " classes and " + std::to_string(rd.u32(0x58)) + " method references. Decompile with JADX.");
}
void parse_pck(const Blob& b, Report& r) {
    Rd rd{b.data}; r.format = "Godot PCK"; r.kind = "game data archive"; r.lang = "Godot";
    const uint32_t fmt = rd.u32(4); r.extra["pack_format"] = num(fmt);
    r.extra["godot_version"] = Json(std::to_string(rd.u32(8)) + "." + std::to_string(rd.u32(12)) + "." + std::to_string(rd.u32(16)));
    std::size_t p = 20; uint32_t flags = 0;
    if (fmt >= 2) { flags = rd.u32(p); p += 4; p += 8; }   // flags, files base
    if (fmt >= 3) { p += 8; }                               // directory offset (format 3: directory may be elsewhere)
    p += 16 * 4;                                            // reserved
    if (flags & 1) { r.limitations.push_back("The PCK directory is encrypted: file names cannot be listed without the game's key."); return; }
    const uint32_t n = rd.u32(p); p += 4; Json::Array files;
    for (uint32_t i = 0; i < n && i < 100000 && rd.ok(p, 4); ++i) {
        const uint32_t l = rd.u32(p); p += 4; if (!rd.ok(p, l)) break;
        std::string name = b.data.substr(p, l); name = name.c_str(); p += l;
        const unsigned long long off = rd.u64(p), sz = rd.u64(p + 8); p += 16 + 16 + (fmt >= 2 ? 4 : 0);
        if (files.size() < 500) files.push_back(Json(Json::Object{{"path", jstr(name)}, {"offset", Json(hex(off))}, {"bytes", num((double)sz)}}));
    }
    r.extra["files"] = num(n); r.extra["listing"] = Json(files);
    if (fmt >= 3) r.limitations.push_back("Format 3 offsets are relative to the files base; listed offsets are as stored.");
}
void parse_il2cpp(const Blob& b, Report& r) {
    Rd rd{b.data}; r.format = "IL2CPP metadata"; r.kind = "Unity global-metadata.dat"; r.lang = "Unity (IL2CPP)";
    r.extra["metadata_version"] = num(rd.u32(4));
    r.findings.push_back("Unity IL2CPP metadata version " + std::to_string(rd.u32(4)) + ". Pair it with libil2cpp.so / GameAssembly.dll in Il2CppDumper or Cpp2IL to recover type and method names.");
}
void parse_script(const Blob& b, Report& r) {
    r.format = "text"; r.kind = "script or source";
    const std::string head = b.data.substr(0, 200);
    if (head.rfind("#!", 0) == 0) { r.extra["shebang"] = jstr(head.substr(0, head.find('\n'))); }
    const auto sm = b.data.rfind("sourceMappingURL=");
    if (sm != std::string::npos) { r.extra["source_map"] = jstr(b.data.substr(sm + 17, std::min<std::size_t>(300, b.data.find_first_of("\n *", sm + 17) - sm - 17))); r.findings.push_back("References a source map: if it is shipped next to the file, the original sources and names can be recovered."); }
    if (b.data.find("\"sources\"") != std::string::npos && b.data.find("\"mappings\"") != std::string::npos) {
        try { const Json j = JsonParser(b.data).parse(); r.format = "source map"; Json::Array src; if (const Json* s = j.get("sources"); s && s->array_ptr()) for (const auto& x : *s->array_ptr()) if (src.size() < 2000) src.push_back(x);
            r.extra["sources"] = Json(src); r.extra["has_sources_content"] = Json(j.get("sourcesContent") != nullptr);
            r.findings.push_back("Source map listing " + std::to_string(src.size()) + " original files" + (j.get("sourcesContent") ? " with their full original source code embedded." : ".")); } catch (...) {}
    }
    std::size_t lines = std::count(b.data.begin(), b.data.end(), '\n'); r.extra["lines"] = num((double)lines);
    if (lines < 30 && b.data.size() > 50000) r.findings.push_back("Very long lines: the code is minified. Ask Astro to pretty-print it before reading.");
}

// ---------- detect + analyse ----------
Report analyse(const Blob& b, const std::string& want_arch = "") {
    Report r; const std::string& d = b.data; Rd be{d}; be.le = false;
    auto text_like = [&]() { std::size_t bad = 0, n = std::min<std::size_t>(d.size(), 4096); for (std::size_t i = 0; i < n; ++i) { const unsigned char c = (unsigned char)d[i]; if (c < 9 || (c > 13 && c < 32)) ++bad; } return n && bad * 100 < n; };
    try {
        if (starts(d, "\x7f" "ELF")) parse_elf(b, r);
        else if (starts(d, "MZ") && d.size() > 0x40) parse_pe(b, r);
        else if (be.u32(0) == 0xcafebabe && be.u32(4) < 40) {   // fat Mach-O (a Java class has a version > 44 here)
            const uint32_t n = be.u32(4); Json::Array arches; unsigned long long pick = 0; std::string picked;
            for (uint32_t i = 0; i < n; ++i) { const std::string a = macho_cpu(be.u32(8 + i * 20)); const unsigned long long off = be.u32(8 + i * 20 + 8);
                arches.push_back(Json(Json::Object{{"arch", Json(a)}, {"offset", Json(hex(off))}, {"bytes", num(be.u32(8 + i * 20 + 12))}}));
                if (!pick || a == want_arch || (want_arch.empty() && a == "arm64")) { pick = off; picked = a; } }
            parse_macho_at(b, r, pick);
            r.extra["universal_arches"] = Json(arches);
            r.notes.push_back("Universal (fat) binary; showing the " + picked + " slice. Pass \"arch\" to pick another.");
        }
        else if (starts(d, "\xfe\xed\xfa\xce") || starts(d, "\xfe\xed\xfa\xcf") || starts(d, "\xce\xfa\xed\xfe") || starts(d, "\xcf\xfa\xed\xfe")) parse_macho_at(b, r, 0);
        else if (be.u32(0) == 0xcafebabe) parse_java_class(b, r);
        else if (starts(d, "PK\x03\x04") || starts(d, "PK\x05\x06")) parse_zip(b, r);
        else if (d.size() > 16 && Rd{d}.u32(0) == 4 && d[16] == '{') parse_asar(b, r);
        else if (starts(d, std::string_view("\0asm", 4))) parse_wasm(b, r);
        else if (starts(d, "GGUF")) parse_gguf(b, r);
        else if (d.size() > 10 && Rd{d}.u64(0) < d.size() && d[8] == '{' && d.find("\"dtype\"", 8) < 8 + Rd{d}.u64(0)) parse_safetensors(b, r);
        else if (starts(d, std::string_view("SQLite format 3\0", 16))) parse_sqlite(b, r);
        else if (starts(d, "dex\n")) parse_dex(b, r);
        else if (starts(d, "GDPC")) parse_pck(b, r);
        else if (Rd{d}.u32(0) == 0xFAB11BAF) parse_il2cpp(b, r);
        else if (starts(d, "MDMP")) { r.format = "Windows minidump"; r.kind = "crash dump"; r.findings.push_back("Windows minidump: open it in WinDbg or ask Astro to read the module list from strings."); }
        else if (starts(d, "%PDF")) { r.format = "PDF"; r.kind = "document"; }
        else if (starts(d, "\x1f\x8b")) { r.format = "gzip"; r.kind = "compressed data"; }
        else if (starts(d, "BC\xc0\xde")) { r.format = "LLVM bitcode"; r.kind = "compiler IR"; r.findings.push_back("LLVM bitcode: llvm-dis turns it into readable IR."); }
        else if (text_like()) parse_script(b, r);
        else { r.format = "unknown"; r.kind = "data"; r.unknowns.push_back("The format is not recognised. Look at the hex view and strings, and check the entropy: near 8.0 means compressed or encrypted data."); }
    } catch (const std::exception& e) { r.limitations.push_back(std::string("Parsing stopped: ") + e.what()); }
    if (r.format == "ELF" || r.format == "PE" || r.format == "Mach-O") { go_buildinfo(b, r); rust_audit(b, r); }
    // packers and protections, from section names and entropy
    for (const auto& s : r.sections) {
        const std::string n = lower(s.name);
        if (n.find("upx") != std::string::npos) r.findings.push_back("Packed with UPX (section " + s.name + "): unpack with \"upx -d\" before analysing.");
        else if (n == ".vmp0" || n == ".vmp1" || n.find("vmp") == 0) r.findings.push_back("VMProtect section " + s.name + ": code is virtualised; static disassembly will not show the original logic.");
        else if (n == ".themida" || n == ".winlice") r.findings.push_back("Themida/WinLicense section " + s.name + ".");
        else if (n == ".enigma1" || n == ".enigma2") r.findings.push_back("Enigma Protector section " + s.name + ".");
        if (s.exec && s.size > 4096) { const double e = entropy_of(d, s.off, s.size); if (e > 7.4) r.findings.push_back("Executable section " + s.name + " has entropy " + std::to_string(e).substr(0, 4) + ": likely packed or encrypted code."); }
    }
    { std::set<std::string> seen; std::erase_if(r.notes, [&](const std::string& x) { return !seen.insert(x).second; }); }
    if (b.truncated) r.limitations.push_back("Only the first " + std::to_string(kMaxFile / 1048576) + " MiB were read; facts past that point are missing.");
    return r;
}

Json section_json(const Blob& b, const Section& s) {
    Json::Object o{{"name", jstr(s.name)}, {"addr", Json(hex(s.addr))}, {"offset", Json(hex(s.off))}, {"size", num((double)s.size)}, {"vsize", num((double)s.vsize)}, {"flags", Json(s.flags)}};
    if (s.size) o["entropy"] = num(entropy_of(b.data, s.off, s.size));
    return Json(o);
}
Json symbol_json(const Symbol& s) {
    Json::Object o{{"name", jstr(s.name)}, {"kind", Json(s.kind)}, {"addr", Json(hex(s.addr))}};
    if (s.size) o["size"] = num((double)s.size);
    if (!s.lib.empty()) o["lib"] = jstr(s.lib);
    o["where"] = Json(s.imported ? "import" : s.exported ? "export" : "internal");
    return Json(o);
}
Json overview_json(const Blob& b, const Report& r) {
    Json::Object o;
    o["path"] = jstr(b.path); o["bytes"] = num((double)b.size); o["sha256"] = Json(sha256_hex(b.data));
    if (b.truncated) o["sha256_note"] = Json("hash of the part that was read");
    o["format"] = Json(r.format); o["kind"] = Json(r.kind);
    if (!r.arch.empty()) o["arch"] = Json(r.arch);
    if (r.bits) o["bits"] = num(r.bits);
    if (!r.os.empty()) o["os"] = Json(r.os);
    o["endian"] = Json(r.endian);
    if (!r.lang.empty()) o["language"] = Json(r.lang);
    if (r.has_entry) o["entry"] = Json(hex(r.entry));
    if (r.base_set) o["image_base"] = Json(hex(r.image_base));
    o["entropy"] = num(entropy(b.data.data(), b.data.size()));
    Json::Array secs, segs; for (const auto& s : r.sections) if (secs.size() < 300) secs.push_back(section_json(b, s));
    for (const auto& s : r.segments) if (segs.size() < 100) segs.push_back(section_json(b, s));
    if (!secs.empty()) o["sections"] = Json(secs);
    if (!segs.empty()) o["segments"] = Json(segs);
    Json::Array libs; for (const auto& l : r.libraries) libs.push_back(jstr(l)); if (!libs.empty()) o["libraries"] = Json(libs);
    std::size_t imp = 0, exp = 0, fn = 0; for (const auto& s : r.symbols) { imp += s.imported; exp += s.exported; fn += s.kind == "function" && !s.imported; }
    o["symbol_counts"] = Json(Json::Object{{"total", num((double)r.symbols.size())}, {"imports", num((double)imp)}, {"exports", num((double)exp)}, {"defined_functions", num((double)fn)}});
    Json::Array exs; for (const auto& s : r.symbols) if (s.exported && exs.size() < 60) exs.push_back(symbol_json(s)); if (!exs.empty()) o["exports_preview"] = Json(exs);
    if (!r.mitigations.empty()) o["mitigations"] = Json(r.mitigations);
    for (const auto& [k, v] : r.extra) o[k] = v;
    auto arr = [](const std::vector<std::string>& v) { Json::Array a; for (const auto& x : v) a.push_back(jstr(x)); return Json(a); };
    o["findings"] = arr(r.findings); o["notes"] = arr(r.notes); o["limitations"] = arr(r.limitations); o["unknowns"] = arr(r.unknowns);
    o["evidence_rule"] = Json("Facts come from the file's own structures at the offsets shown. Nothing here was executed.");
    return Json(o);
}

// ---------- optional providers: disassembly and decompilation ----------
std::string which_any(std::initializer_list<const char*> names) { for (const char* n : names) { const std::string w = tools_which(n); if (!w.empty()) return w; } return {}; }
Json providers_json() {
    Json::Object o;
    o["rizin"] = Json(which_any({"rizin"})); o["llvm-objdump"] = Json(which_any({"llvm-objdump"})); o["objdump"] = Json(which_any({"objdump"}));
    o["otool"] = Json(which_any({"otool"})); o["ghidra"] = Json(which_any({"analyzeHeadless", "analyzeHeadless.bat"})); o["jadx"] = Json(which_any({"jadx", "jadx.bat"}));
    o["java"] = Json(which_any({"java"})); o["node"] = Json(which_any({"node"}));
    o["install_hint"] = Json("Settings > Tools: rizin (Windows), JADX and Ghidra install portably; on Linux \"sudo apt install rizin binutils\", on macOS \"brew install rizin\".");
    return Json(o);
}

unsigned long long parse_addr(const std::string& s) { try { return std::stoull(s, nullptr, (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) ? 16 : 0); } catch (...) { throw std::runtime_error("Not an address: " + s); } }
// Find a symbol's address: exact, then without a leading underscore, then case-insensitive substring.
const Symbol* find_symbol(const Report& r, const std::string& name) {
    for (const auto& s : r.symbols) if (!s.imported && s.name == name) return &s;
    for (const auto& s : r.symbols) if (!s.imported && (s.name == "_" + name || "_" + s.name == name)) return &s;
    const std::string l = lower(name);
    for (const auto& s : r.symbols) if (!s.imported && s.addr && lower(s.name).find(l) != std::string::npos) return &s;
    return nullptr;
}
Json disasm(const Blob& b, const Report& r, const std::string& where, int count) {
    if (r.format != "ELF" && r.format != "PE" && r.format != "Mach-O") throw std::runtime_error("Disassembly works on ELF, PE and Mach-O files. This is " + r.format + ".");
    count = std::clamp(count, 1, 400);
    unsigned long long addr = 0; std::string label;
    if (where.empty()) { if (!r.has_entry) throw std::runtime_error("No entry point; pass an address or a symbol name."); addr = r.entry; label = "entry point"; }
    else if (std::isdigit((unsigned char)where[0])) { addr = parse_addr(where); label = hex(addr); }
    else { const Symbol* s = find_symbol(r, where); if (!s) throw std::runtime_error("No defined symbol matches \"" + where + "\". List symbols first (stripped binaries have few)."); addr = s->addr; label = s->name; }
    const unsigned long long stop = addr + (unsigned long long)count * 15;
    Json::Object o{{"address", Json(hex(addr))}, {"target", jstr(label)}};
    std::string out, used, tried;
    if (const std::string rz = which_any({"rizin"}); !rz.empty()) {
        const Captured c = run_capture({rz, "-q", "-2", "-e", "scr.color=0", "-e", "asm.bytes=false", "-c", "s " + hex(addr) + "; pd " + std::to_string(count), b.path}, fs::temp_directory_path(), {}, 60, 2'000'000);
        if (c.code == 0 && !trim(c.out).empty()) { out = c.out; used = "rizin"; } else tried += "rizin: " + clip(trim(c.err), 200) + "; ";
    }
    if (out.empty()) if (const std::string od = which_any({"llvm-objdump", "objdump"}); !od.empty()) {
        std::vector<std::string> a = {od, "-d", "--no-show-raw-insn", "--start-address=" + hex(addr), "--stop-address=" + hex(stop), "-C"};
        if (r.arch == "x86" || r.arch == "x86-64") a.push_back(od.find("llvm") != std::string::npos ? "--x86-asm-syntax=intel" : "-Mintel");
        a.push_back(b.path);
        const Captured c = run_capture(a, fs::temp_directory_path(), {}, 60, 2'000'000);
        if (c.code == 0) { std::istringstream in(c.out); std::string l; int n = 0; bool started = false;
            while (std::getline(in, l) && n < count) { if (l.find("Disassembly of") != std::string::npos) started = true; if (!started || trim(l).empty()) continue; out += l + "\n"; if (l.find(':') != std::string::npos && l.find('<') == std::string::npos) ++n; } }
        if (!out.empty()) used = fs::path(od).filename().string();
        else tried += fs::path(od).filename().string() + (c.err.find("an architecture") != std::string::npos || c.err.find("can't disassemble") != std::string::npos || c.err.find("not recognized") != std::string::npos ? " cannot disassemble " + r.arch + " (a GNU objdump built for one architecture); " : ": " + clip(trim(c.err), 200) + "; ");
    }
    if (out.empty()) {
        // No provider: show raw bytes so the agent still has evidence, and say how to get a disassembler.
        const long long off = va_to_off(r, addr);
        if (off < 0) throw std::runtime_error("Address " + hex(addr) + " is not inside any section or segment of the file.");
        o["bytes_hex"] = Json([&] {   // one line per 4-byte word on fixed-width ISAs, 16 bytes per line otherwise
            const bool fixed = r.arch == "arm64" || r.arch == "arm" || r.arch == "riscv" || r.arch == "mips" || r.arch == "powerpc" || r.arch == "powerpc64" || r.arch == "loongarch";
            const int per = fixed ? 4 : 16, total = std::min(count * (fixed ? 4 : 4), 1024); std::string h;
            for (int i = 0; i < total && off + i < (long long)b.data.size(); i += per) {
                h += hex(addr + i) + ":  ";
                for (int k = 0; k < per && off + i + k < (long long)b.data.size(); ++k) { char x[4]; std::snprintf(x, 4, "%02x ", (unsigned char)b.data[off + i + k]); h += x; }
                h += "\n";
            }
            return h; }());
        o["file_offset"] = Json(hex((unsigned long long)off));
        o["provider"] = Json("none");
        o["limitation"] = Json(std::string(tried.empty() ? "No disassembler was found." : "No installed disassembler could read this " + r.arch + " code (" + tried.substr(0, tried.size() - 2) + ").") + " Install rizin or LLVM (Settings > Tools > C / C++ compiler includes llvm-objdump on Windows; Linux: sudo apt install rizin or llvm (binutils-multiarch for other CPUs); macOS: xcode-select --install).");
        return Json(o);
    }
    o["provider"] = Json(used); o["listing"] = jstr(out.substr(0, 120000));
    return Json(o);
}

// Ghidra post-script (Java, written to the data folder at first use): decompile one function to a file.
const char* kGhidraScript = R"JAVA(import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import java.io.*;
public class AstroDecompile extends GhidraScript {
    @Override public void run() throws Exception {
        String[] a = getScriptArgs(); String want = a[0]; File out = new File(a[1]);
        Function f = null;
        if (want.startsWith("0x")) f = getFunctionContaining(toAddr(want.substring(2)));
        else for (Function g : currentProgram.getFunctionManager().getFunctions(true)) if (g.getName().equals(want)) { f = g; break; }
        if (f == null && want.equals("entry")) for (Function g : currentProgram.getFunctionManager().getFunctions(true)) if (g.getName().equals("entry") || g.getName().equals("main")) { f = g; break; }
        if (f == null) { put(out, "ERROR: no function " + want); return; }
        DecompInterface d = new DecompInterface(); d.openProgram(currentProgram);
        DecompileResults r = d.decompileFunction(f, 180, monitor);
        put(out, r.decompileCompleted() ? "// " + f.getName() + " @ " + f.getEntryPoint() + "\n" + r.getDecompiledFunction().getC() : "ERROR: " + r.getErrorMessage());
    }
    void put(File f, String s) throws Exception { try (Writer w = new OutputStreamWriter(new FileOutputStream(f), "UTF-8")) { w.write(s); } }
}
)JAVA";
fs::path re_root() { return (fs::absolute(fs::path(data_dir())) / ".astro_re").lexically_normal(); }  /* uploads, decompiler cache, Ghidra script */
fs::path re_cache_dir(const Blob& b) { const fs::path p = re_root() / "cache" / sha256_hex(b.data).substr(0, 16); std::error_code ec; fs::create_directories(p, ec); return p; }
Json decompile(const Blob& b, const Report& r, const std::string& target) {
    Json::Object o;
    const bool jvm = r.format == "DEX" || r.format == "Java class" || (r.format == "ZIP" && (r.kind.find("APK") != std::string::npos || r.kind.find("JAR") != std::string::npos || r.kind.find("AAB") != std::string::npos));
    if (jvm) {
        const std::string jx = which_any({"jadx", "jadx.bat"});
        if (jx.empty()) throw std::runtime_error("JADX is not installed. Install it from Settings > Tools > JADX (it needs Java, which Settings > Tools also installs).");
        const fs::path out = re_cache_dir(b) / "jadx"; std::error_code ec;
        if (target.empty()) {
            if (!fs::exists(out / "sources", ec)) { const Captured c = run_capture({jx, "--no-res", "--show-bad-code", "-d", out.string(), b.path}, fs::temp_directory_path(), {}, 1500, 400'000);
                if (!fs::exists(out / "sources", ec)) throw std::runtime_error("JADX failed: " + clip(c.err.empty() ? c.out : c.err, 1500)); }
            Json::Array pk; std::map<std::string, int> per;
            for (auto it = fs::recursive_directory_iterator(out / "sources", ec); it != fs::recursive_directory_iterator(); it.increment(ec))
                if (it->is_regular_file(ec)) { const std::string rel = fs::relative(it->path(), out / "sources", ec).generic_string(); per[rel.substr(0, rel.rfind('/') == std::string::npos ? 0 : rel.rfind('/'))]++; }
            for (auto& [k, v] : per) if (pk.size() < 400) pk.push_back(Json(k.empty() ? "(default package)" : k + "  (" + std::to_string(v) + " files)"));
            o["provider"] = Json("jadx"); o["output"] = jstr(out.string()); o["packages"] = Json(pk);
            o["next"] = Json("Pass \"target\": \"com.example.ClassName\" to read one class.");
            return Json(o);
        }
        std::string rel = target; std::replace(rel.begin(), rel.end(), '.', '/'); rel += ".java";
        fs::path f = out / "sources" / rel;
        if (!fs::exists(f, ec)) { run_capture({jx, "--no-res", "--show-bad-code", "--single-class", target, "-d", (out / "single").string(), b.path}, fs::temp_directory_path(), {}, 900, 400'000); f = out / "single" / "sources" / rel; }
        const auto t = read_small(f, 4'000'000);
        if (!t) throw std::runtime_error("JADX produced no source for " + target + ". Check the class name (decompile without a target to list packages).");
        o["provider"] = Json("jadx"); o["class"] = jstr(target); o["source"] = jstr(t->substr(0, 150000));
        return Json(o);
    }
    if (r.format != "ELF" && r.format != "PE" && r.format != "Mach-O") throw std::runtime_error("Decompilation works on ELF, PE, Mach-O (Ghidra) and APK, DEX, JAR, class files (JADX).");
    const std::string gh = which_any({"analyzeHeadless", "analyzeHeadless.bat"});
    if (gh.empty()) throw std::runtime_error("Ghidra is not installed. Install it from Settings > Tools > Ghidra (about 570 MB, needs Java 21, also in Settings > Tools). Disassembly works without it.");
    std::string want = trim(target);
    if (want.empty()) want = r.has_entry ? hex(r.entry) : "entry";
    else if (!std::isdigit((unsigned char)want[0])) { if (const Symbol* s = find_symbol(r, want); s && s->addr) want = hex(s->addr); }
    else want = hex(parse_addr(want));
    const fs::path cache = re_cache_dir(b), scripts = re_root() / "scripts"; std::error_code ec;
    fs::create_directories(scripts, ec);
    { std::ofstream f(scripts / "AstroDecompile.java", std::ios::binary | std::ios::trunc); f << kGhidraScript; }
    const fs::path outf = cache / ("decomp-" + sha256_hex(want).substr(0, 12) + ".c");
    if (!fs::exists(outf, ec)) {
        const bool have_proj = fs::exists(cache / "ghidra" / "astro.gpr", ec);
        fs::create_directories(cache / "ghidra", ec);
        std::vector<std::string> a = {gh, (cache / "ghidra").string(), "astro"};
        if (have_proj) { a.insert(a.end(), {"-process", fs::path(b.path).filename().string(), "-noanalysis"}); } else a.insert(a.end(), {"-import", b.path});
        a.insert(a.end(), {"-scriptPath", scripts.string(), "-postScript", "AstroDecompile.java", want, outf.string()});
        const Captured c = run_capture(a, cache, {}, 1500, 400'000);
        if (!fs::exists(outf, ec)) throw std::runtime_error("Ghidra did not finish: " + clip(c.err.empty() ? c.out : c.err, 1500));
    }
    const auto t = read_small(outf, 4'000'000);
    if (!t || t->rfind("ERROR:", 0) == 0) { fs::remove(outf, ec); throw std::runtime_error("Ghidra: " + (t ? t->substr(7, 400) : std::string("no output"))); }
    o["provider"] = Json("ghidra"); o["target"] = Json(want); o["pseudocode"] = jstr(t->substr(0, 150000));
    o["caveat"] = Json("Decompiled pseudocode is a reconstruction: names, types and some control flow are guesses. Check important claims against the disassembly.");
    return Json(o);
}

// ---------- HTTP ----------
std::string resolve(const Json& body) {
    std::string p = trim(get_string(body, "path"));
    if (p.empty()) throw std::runtime_error("Give a \"path\" to the file.");
    const std::string project = get_string(body, "project");
    if (p.rfind("upload:", 0) == 0) { const std::string id = p.substr(7); if (id.empty() || id.find_first_not_of("0123456789abcdef") != std::string::npos) throw std::runtime_error("Bad upload id."); 
        std::error_code ec; for (const auto& e : fs::directory_iterator(re_root() / "uploads" / id, ec)) return e.path().string(); throw std::runtime_error("That upload is gone; upload the file again."); }
    if (!project.empty() && fs::path(p).is_relative()) { const auto pp = project_path(project, p); if (!pp) throw std::runtime_error("The path leaves the project."); return pp->string(); }
    if (p.size() > 1 && p[0] == '~' && (p[1] == '/' || p[1] == '\\')) { const char* h = std::getenv("HOME"); if (!h) h = std::getenv("USERPROFILE"); if (h) p = std::string(h) + p.substr(1); }
    if (fs::path(p).is_relative()) throw std::runtime_error("Use an absolute path (or pick a project file).");
    return p;
}
}  // namespace re

bool handle_re_api(int fd, const HttpRequest& request) {
    if (request.path.rfind("/api/re/", 0) != 0) return false;
    if (t_remote) { send_json(fd, 403, error_json("Astro RE reads files on the computer, so it is not available from a phone.")); return true; }
    if (request.path == "/api/re/providers") { send_json(fd, 200, re::providers_json()); return true; }
    if (request.method != "POST") { send_json(fd, 405, error_json("Use POST.")); return true; }
    const auto body = parse_request_json(request, fd);
    if (!body) return true;
    const std::string op = request.path.substr(8);
    try {
        if (op == "upload") {   // chunked: {id?, name, offset, data(base64), done}
            std::string id = get_string(*body, "id");
            if (id.empty()) id = random_hex(8);
            if (id.find_first_not_of("0123456789abcdef") != std::string::npos || id.size() > 32) throw std::runtime_error("Bad upload id.");
            std::string name = fs::path(get_string(*body, "name", "file.bin")).filename().string();
            std::erase_if(name, [](unsigned char c) { return c < 32 || c == '/' || c == '\\' || c == ':'; });
            if (name.empty() || name == "." || name == "..") name = "file.bin";
            const fs::path dir = re::re_root() / "uploads" / id; std::error_code ec; fs::create_directories(dir, ec);
            const std::string bytes = base64_decode(get_string(*body, "data"));
            const auto off = (unsigned long long)json_int(*body, "offset", 0);
            if (off + bytes.size() > 2'000'000'000ull) throw std::runtime_error("Files over 2 GB are not accepted.");
            std::fstream f(dir / name, std::ios::binary | std::ios::in | std::ios::out); if (!f) { std::ofstream c(dir / name, std::ios::binary); c.close(); f.open(dir / name, std::ios::binary | std::ios::in | std::ios::out); }
            f.seekp((std::streamoff)off); f.write(bytes.data(), (std::streamsize)bytes.size());
            send_json(fd, 200, Json(Json::Object{{"id", Json(id)}, {"path", Json("upload:" + id)}, {"name", Json(name)}}));
            return true;
        }
        const std::string path = re::resolve(*body);
        const auto blob = re::load(path);
        if (op == "inspect") { const auto r = re::analyse(*blob, get_string(*body, "arch")); send_json(fd, 200, re::overview_json(*blob, r)); return true; }
        if (op == "strings") {
            const auto r = re::analyse(*blob);
            const int min = std::clamp(json_int(*body, "min", 6), 3, 200), limit = std::clamp(json_int(*body, "limit", 300), 1, 5000), skip = std::max(0, json_int(*body, "offset", 0));
            const std::string filter = re::lower(get_string(*body, "filter")); const bool interesting = json_bool(*body, "interesting", false);
            const auto all = re::extract_strings(blob->data, (std::size_t)min, 2'000'000, filter);
            Json::Array out; std::size_t matched = 0;
            for (const auto& s : all) {
                const std::string tag = re::string_tag(s.text);
                if (interesting && tag.empty()) continue;
                if (matched++ < (std::size_t)skip) continue;
                if (out.size() >= (std::size_t)limit) continue;
                Json::Object o{{"offset", Json(re::hex(s.off))}, {"text", re::jstr(s.text.substr(0, 1000))}, {"enc", Json(s.enc)}};
                if (const auto* sec = re::section_at(r, s.off)) o["section"] = re::jstr(sec->name);
                if (const long long va = re::off_to_va(r, s.off); va >= 0) o["addr"] = Json(re::hex((unsigned long long)va));
                if (!tag.empty()) o["tag"] = Json(tag);
                out.push_back(Json(o));
            }
            send_json(fd, 200, Json(Json::Object{{"total", Json((double)matched)}, {"returned", Json((double)out.size())}, {"strings", Json(out)}}));
            return true;
        }
        if (op == "symbols") {
            const auto r = re::analyse(*blob, get_string(*body, "arch"));
            const std::string filter = re::lower(get_string(*body, "filter")), where = get_string(*body, "where");
            const int limit = std::clamp(json_int(*body, "limit", 400), 1, 10000), skip = std::max(0, json_int(*body, "offset", 0));
            Json::Array out; std::size_t matched = 0;
            for (const auto& s : r.symbols) {
                if (!filter.empty() && re::lower(s.name).find(filter) == std::string::npos && re::lower(s.lib).find(filter) == std::string::npos) continue;
                if (where == "import" && !s.imported) continue;
                if (where == "export" && !s.exported) continue;
                if (where == "internal" && (s.imported || s.exported)) continue;
                if (matched++ < (std::size_t)skip || out.size() >= (std::size_t)limit) continue;
                out.push_back(re::symbol_json(s));
            }
            send_json(fd, 200, Json(Json::Object{{"total", Json((double)matched)}, {"returned", Json((double)out.size())}, {"symbols", Json(out)}}));
            return true;
        }
        if (op == "hex") {
            const auto r = re::analyse(*blob);
            unsigned long long off = 0; const std::string at = get_string(*body, "at"), addr = get_string(*body, "addr");
            if (!addr.empty()) { const long long o = re::va_to_off(r, re::parse_addr(addr)); if (o < 0) throw std::runtime_error("That address is not in the file."); off = (unsigned long long)o; }
            else if (!at.empty()) off = re::parse_addr(at);
            const int len = std::clamp(json_int(*body, "length", 512), 16, 65536);
            std::string dump;
            for (unsigned long long i = off; i < off + len && i < blob->data.size(); i += 16) {
                char line[100]; std::snprintf(line, sizeof line, "%08llx  ", i); dump += line; std::string asc;
                for (unsigned long long k = i; k < i + 16; ++k) { if (k < blob->data.size() && k < off + len) { const unsigned char c = (unsigned char)blob->data[k]; std::snprintf(line, sizeof line, "%02x ", c); dump += line; asc += (c >= 32 && c < 127) ? (char)c : '.'; } else dump += "   "; if (k == i + 7) dump += ' '; }
                dump += " |" + asc + "|\n";
            }
            Json::Object o{{"offset", Json(re::hex(off))}, {"length", Json(len)}, {"dump", Json(dump)}};
            if (const auto* sec = re::section_at(r, off)) o["section"] = re::jstr(sec->name);
            send_json(fd, 200, Json(o)); return true;
        }
        if (op == "find") {   // {"text": "..."} or {"hex": "48 8b 05"}; returns offsets (+ addresses)
            const auto r = re::analyse(*blob);
            std::string needle = get_string(*body, "text");
            if (needle.empty()) { const std::string h = get_string(*body, "hex"); std::string clean; for (char c : h) if (std::isxdigit((unsigned char)c)) clean += c;
                if (clean.size() % 2) throw std::runtime_error("Odd number of hex digits.");
                for (std::size_t i = 0; i < clean.size(); i += 2) needle += (char)std::stoi(clean.substr(i, 2), nullptr, 16); }
            if (needle.empty() || needle.size() > 4096) throw std::runtime_error("Give \"text\" or \"hex\" (up to 4096 bytes).");
            Json::Array hits; std::size_t total = 0;
            const bool utf16 = json_bool(*body, "utf16", false);
            if (utf16) { std::string w; for (char c : needle) { w += c; w += '\0'; } needle = w; }
            for (std::size_t p = blob->data.find(needle); p != std::string::npos; p = blob->data.find(needle, p + 1)) {
                ++total; if (hits.size() >= 500) continue;
                Json::Object o{{"offset", Json(re::hex(p))}};
                if (const auto* sec = re::section_at(r, p)) o["section"] = re::jstr(sec->name);
                if (const long long va = re::off_to_va(r, p); va >= 0) o["addr"] = Json(re::hex((unsigned long long)va));
                hits.push_back(Json(o));
            }
            send_json(fd, 200, Json(Json::Object{{"total", Json((double)total)}, {"hits", Json(hits)}})); return true;
        }
        if (op == "disasm") { const auto r = re::analyse(*blob, get_string(*body, "arch")); send_json(fd, 200, re::disasm(*blob, r, trim(get_string(*body, "target")), json_int(*body, "count", 60))); return true; }
        if (op == "decompile") { const auto r = re::analyse(*blob, get_string(*body, "arch")); send_json(fd, 200, re::decompile(*blob, r, trim(get_string(*body, "target")))); return true; }
        if (op == "zip") {   // list entries, or extract one entry into an upload slot so it can be inspected in turn
            std::string why; const auto ents = re::zip_entries(blob->data, why);
            if (ents.empty()) throw std::runtime_error("Not a readable ZIP archive" + (why.empty() ? std::string() : ": " + why) + ".");
            const std::string entry = get_string(*body, "entry");
            if (entry.empty()) {
                const std::string filter = re::lower(get_string(*body, "filter")); Json::Array out;
                for (const auto& z : ents) { if (!filter.empty() && re::lower(z.name).find(filter) == std::string::npos) continue; if (out.size() >= 3000) break; out.push_back(Json(Json::Object{{"name", re::jstr(z.name)}, {"bytes", Json((double)z.usize)}, {"compressed", Json((double)z.csize)}})); }
                send_json(fd, 200, Json(Json::Object{{"entries", Json((double)ents.size())}, {"listing", Json(out)}})); return true;
            }
            const auto* z = re::zip_find(ents, entry); if (!z) throw std::runtime_error("No entry named " + entry + ".");
            const auto data = re::zip_read(blob->data, *z, 512ull * 1024 * 1024); if (!data) throw std::runtime_error("Cannot extract " + entry + " (unsupported compression or too big).");
            const std::string id = random_hex(8); const fs::path dir = re::re_root() / "uploads" / id; std::error_code ec; fs::create_directories(dir, ec);
            std::string name = fs::path(entry).filename().string(); if (name.empty()) name = "entry.bin";
            { std::ofstream f(dir / name, std::ios::binary); f.write(data->data(), (std::streamsize)data->size()); }
            send_json(fd, 200, Json(Json::Object{{"path", Json("upload:" + id)}, {"name", re::jstr(name)}, {"bytes", Json((double)data->size())}})); return true;
        }
        send_json(fd, 404, error_json("Unknown Astro RE request."));
    } catch (const std::exception& e) { send_json(fd, 422, error_json(e.what())); }
    return true;
}
