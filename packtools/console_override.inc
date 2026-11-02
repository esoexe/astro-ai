// console_override.inc - console-mod/override: a C++ port of console-skill/console-mod/override.py (Jake Schincariol, MIT).
// `new` writes console/patches/<slug>.cpp: a PS2_REGISTER_GAME_OVERRIDE module keyed by the ELF's file name, entry
// point and CRC-32, with --bind (route a game function to a runtime handler), --replace (your C++ instead) and --wrap
// (the game's function, then yours). `keys` prints the three keys. The template is templates/override.cpp from the
// skill folder; render_module and ensure_cmake_include are shared with console-mod/pnach.
namespace packtools::console_override {
using namespace pyrt;
using namespace packtools::console_common;

const char* const PROG = "override";
const char* const PLACEHOLDERS[] = {"MOD_NAME", "ELF_NAME", "ENTRY", "CRC32", "NAMESPACE", "NOTES", "FUNCTIONS", "APPLY_BODY"};
const char* const GENERIC_HANDLERS[] = {"ret0", "ret1", "reta0"};
inline bool cpp_keyword(const std::string& w) {
    static const std::set<std::string> K = [] {
        std::set<std::string> s;
        for (const auto& x : split(std::string("alignas alignof and asm auto bitand bitor bool break case catch char class compl concept "
            "const consteval constexpr constinit const_cast continue co_await co_return co_yield decltype default delete do "
            "double dynamic_cast else enum explicit export extern false float for friend goto if inline int long mutable "
            "namespace new noexcept not nullptr operator or private protected public register reinterpret_cast requires "
            "return short signed sizeof static static_assert static_cast struct switch template this thread_local throw true "
            "try typedef typeid typename union unsigned using virtual void volatile wchar_t while xor main "
            "applyOverride rdram ctx runtime"), " ")) if (!x.empty()) s.insert(x);
        return s;
    }();
    return K.count(w) > 0;
}

inline std::string hex32(long long v) { return cfmt("0x%08llX", static_cast<unsigned long long>(v) & 0xFFFFFFFFULL); }
inline std::string c_string(const std::string& s) { return replace_all(replace_all(replace_all(replace_all(s, "\\", "\\\\"), "\"", "\\\""), "\n", " "), "\r", " "); }
inline std::string slugify(const std::string& text) {
    std::string s = re::sub("[^A-Za-z0-9]+", "_", text);
    std::size_t a = 0, b = s.size();
    while (a < b && s[a] == '_') ++a;
    while (b > a && s[b - 1] == '_') --b;
    s = s.substr(a, b - a);
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (s.empty()) s = "mod";
    if (std::isdigit(static_cast<unsigned char>(s[0]))) s = "m_" + s;
    return s;
}
inline std::string read_all(const std::string& path) {
    std::FILE* f = open_file(path, "rb");
    if (!f) throw OSError(is_dir(path) ? EISDIR : errno, path);
    std::string raw; char buf[65536]; std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) raw.append(buf, n);
    std::fclose(f);
    return raw;
}
inline void write_all(const std::string& path, const std::string& text) {
    std::FILE* f = open_file(path, "wb");
    if (!f) throw OSError(errno, path);
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
}

// ---------------------------------------------------------------- the override keys and the executable segments
struct ElfKeys {
    std::string name, data; long long entry = 0, crc32 = 0, size = 0;
    std::vector<std::pair<long long, long long>> exec_ranges;            // [vaddr, vaddr + filesz)
    std::vector<std::tuple<long long, long long, long long>> exec_segments;   // vaddr, offset, filesz
    explicit ElfKeys(const std::string& path) {
        if (!is_file(path)) throw InputError("ELF not found: " + path);
        data = read_all(path);
        name = basename(path);
        if (data.size() < 52 || data.compare(0, 4, "\x7f" "ELF") != 0) throw InputError(name + " is not an ELF file");
        if (data[4] != 1 || data[5] != 1) throw InputError(name + " is not a 32-bit little-endian ELF (PS2 EE ELFs are)");
        const uint32_t machine = u16le(data, 18);
        if (machine != 8) throw InputError(cfmt("%s is not a MIPS ELF (machine %u, the PS2 EE is 8)", name.c_str(), machine));
        entry = u32le(data, 24);
        const long long phoff = u32le(data, 28), phentsize = u16le(data, 42), phnum = u16le(data, 44);
        const long long len = static_cast<long long>(data.size());
        if (phnum && (phentsize < 32 || phoff + phnum * phentsize > len)) throw InputError(name + " is truncated or has a bad program header table");
        for (long long i = 0; i < phnum; ++i) {
            const std::size_t at = static_cast<std::size_t>(phoff + i * phentsize);
            const long long p_type = u32le(data, at), off = u32le(data, at + 4), vaddr = u32le(data, at + 8), filesz = u32le(data, at + 16), flags = u32le(data, at + 24);
            if (p_type == 1 && (flags & 1) && filesz) {
                if (off + filesz > len) throw InputError(cfmt("%s is truncated: segment %lld runs past the end of the file", name.c_str(), i));
                exec_ranges.emplace_back(vaddr, vaddr + filesz);
                exec_segments.emplace_back(vaddr, off, filesz);
            }
        }
        crc32 = zip::crc32(data);
        size = len;
    }
    bool in_code(long long addr) const { for (const auto& [a, b] : exec_ranges) if (a <= addr && addr < b) return true; return false; }
    // jal and j instructions that reach `target` directly (executable segments)
    std::pair<std::vector<long long>, std::vector<long long>> call_sites(long long target) const {
        std::vector<long long> jal, j;
        const uint32_t index = static_cast<uint32_t>((target >> 2) & 0x03FFFFFF);
        for (const auto& [base, off, size] : exec_segments) {
            if (((base + 4) & 0xF0000000LL) != (target & 0xF0000000LL)) continue;
            for (const auto& [op, list] : {std::pair<uint32_t, std::vector<long long>*>{3, &jal}, {2, &j}}) {
                const uint32_t w = (op << 26) | index;
                const char needle[4] = {static_cast<char>(w & 0xFF), static_cast<char>((w >> 8) & 0xFF), static_cast<char>((w >> 16) & 0xFF), static_cast<char>(w >> 24)};
                const std::size_t end = static_cast<std::size_t>(off + size);
                for (std::size_t i = static_cast<std::size_t>(off); i + 4 <= end; ++i) {
                    if (std::memcmp(data.data() + i, needle, 4) != 0) continue;
                    if ((static_cast<long long>(i) - off) % 4 == 0) list->push_back(base + static_cast<long long>(i) - off);
                }
            }
        }
        return {jal, j};
    }
};

// start addresses from a Ghidra CSV (Name,Start,End,Size)
inline std::set<long long> function_starts(const std::string& path) {
    if (!is_file(path)) throw InputError("Ghidra map not found: " + path);
    std::set<long long> starts;
    for (const auto& line : lines_keep(text_u(read_all(path)))) {
        std::vector<std::string> parts; for (const auto& p : split(line, ",")) parts.push_back(strip(p));
        if (parts.size() >= 2 && lower(parts[1]).rfind("0x", 0) == 0) if (const auto v = int16(parts[1])) starts.insert(*v);
    }
    return starts;
}

// handler names from ps2xRuntime/include/ps2_call_list.h in a PS2Recomp checkout
inline std::set<std::string> repo_handlers(const std::string& repo) {
    const std::string path = path_join(path_join(path_join(repo, "ps2xRuntime"), "include"), "ps2_call_list.h");
    if (!is_file(path)) throw InputError("no ps2xRuntime/include/ps2_call_list.h under " + repo + " (is that a PS2Recomp checkout?)");
    const std::string text = text_u(read_all(path));
    std::set<std::string> names;
    static const std::string DEF = R"(#define\s+)", XN = R"(\bX\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\))";
    const auto blocks = re::split(DEF, text);
    for (std::size_t i = 1; i < blocks.size(); ++i) {
        const std::string& block = blocks[i];
        if (block.rfind("PS2_SYSCALL_LIST", 0) == 0 || block.rfind("PS2_STUB_LIST", 0) == 0)
            for (const auto& m : re::compile(XN).finditer(block)) names.insert(m.group(1));
    }
    if (names.empty()) throw InputError("found no X(Name) entries in " + path);
    return names;
}
// same alias rule as ps2_runtime_calls.h: exact, or with/without one leading underscore
inline std::optional<std::string> resolve_handler(const std::string& name, const std::set<std::string>& names) {
    if (names.count(name)) return name;
    if (!name.empty() && name[0] == '_' && names.count(name.substr(1))) return name.substr(1);
    if (names.count("_" + name)) return "_" + name;
    return std::nullopt;
}

// ---------------------------------------------------------------- rendering (also used by pnach override)
inline std::string render_module(const std::string& mod_name, const std::string& elf_name, long long entry, long long crc32, const std::string& slug,
                                 const std::string& functions, const std::string& apply_body, const std::vector<std::string>& notes) {
    const std::string tpl = skill_data("templates/override.cpp");
    if (!is_file(tpl)) throw InputError("templates/override.cpp is missing (reinstall the console-mod skill)");
    std::string text = text_u(read_all(tpl));
    std::vector<std::string> nl; for (const auto& n : notes) nl.push_back(n.empty() ? "//" : "// " + n);
    const std::map<std::string, std::string> values{
        {"MOD_NAME", c_string(mod_name)}, {"ELF_NAME", c_string(elf_name)},
        {"ENTRY", cfmt("0x%08llXu", static_cast<unsigned long long>(entry))}, {"CRC32", cfmt("0x%08llXu", static_cast<unsigned long long>(crc32))},
        {"NAMESPACE", "console_mod_" + slug}, {"NOTES", join(nl, "\n")}, {"FUNCTIONS", functions}, {"APPLY_BODY", apply_body}};
    for (const char* k : PLACEHOLDERS) text = replace_all(text, std::string("@") + k + "@", values.at(k));
    std::set<std::string> left;
    static const std::string FIELD = "@[A-Z][A-Z0-9_]*@";
    for (const auto& m : re::compile(FIELD).finditer(text)) left.insert(m.group(0));
    if (!left.empty()) throw InputError("template still has unfilled fields: " + join(std::vector<std::string>(left.begin(), left.end()), ", "));
    return text;
}
// put console_mods.cmake next to the overrides; install advice when newly written
inline std::optional<std::string> ensure_cmake_include(const std::string& folder) {
    const std::string dst = path_join(folder, "console_mods.cmake");
    if (exists(dst)) return std::nullopt;
    const std::string tpl = skill_data("templates/console_mods.cmake");
    if (!is_file(tpl)) throw InputError("templates/console_mods.cmake is missing (reinstall the console-mod skill)");
    const std::string text = text_u(read_all(tpl));
    makedirs(folder);
    write_all(dst, text);
    return "wrote " + dst + "; add once at the end of ps2xRuntime/CMakeLists.txt: include(\"" + replace_all(dst, "\\", "/") + "\" OPTIONAL)";
}
// refuse the generated runner folder and anything that is not a .cpp
inline void guard_out(const std::string& path) {
    const std::string norm = replace_all(abspath(path), "\\", "/");
    if ((norm + "/").find("/ps2xRuntime/src/runner/") != std::string::npos) throw InputError("refusing to write into ps2xRuntime/src/runner/ (generated code): use console/patches/");
    if (norm.size() < 4 || norm.compare(norm.size() - 4, 4, ".cpp") != 0) throw InputError("the output must be a .cpp file: " + path);
}
inline std::string today() { const std::time_t t = std::time(nullptr); std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[16]; std::strftime(b, sizeof b, "%Y-%m-%d", &tm); return b; }

// ---------------------------------------------------------------- new
inline std::vector<std::pair<long long, std::string>> pairs(const Val& values, const std::string& flag) {
    std::vector<std::pair<long long, std::string>> out;
    if (values.t != Val::LIST) return out;
    for (const auto& v : *values.l) {
        const auto eqp = v.s.find('=');
        if (eqp == std::string::npos) throw InputError(flag + " needs 0xADDR=name, got " + str_repr(v.s));
        const std::string a = strip(v.s.substr(0, eqp)), n = strip(v.s.substr(eqp + 1));
        const auto addr = int16(a);
        if (!addr) throw InputError(flag + " address is not hex: " + str_repr(a));
        if (*addr < 0 || *addr > 0xFFFFFFFFLL) throw InputError(flag + " address out of range: " + str_repr(a));
        if (*addr & 3) throw InputError(flag + " address " + hex32(*addr) + " is not 4-byte aligned (functions start on a word)");
        static const std::string ID = "[A-Za-z_][A-Za-z0-9_]*";
        if (!re::fullmatch(ID, n)) throw InputError(flag + " name " + str_repr(n) + " is not a C++ identifier");
        out.emplace_back(*addr, n);
    }
    return out;
}

inline int cmd_new(const Val& a) {
    const ElfKeys elf(S(a, "elf"));
    const auto binds = pairs(get(a, "bind"), "--bind"), repl = pairs(get(a, "replace"), "--replace"), wraps = pairs(get(a, "wrap"), "--wrap");
    if (binds.empty() && repl.empty() && wraps.empty()) throw InputError("give at least one --bind, --replace or --wrap (see --help)");
    const std::string mod = S(a, "name");
    std::vector<std::string> warnings;
    const std::string repo = get(a, "repo").is_none() ? "" : S(a, "repo");
    std::optional<std::set<std::string>> names;
    if (!repo.empty()) names = repo_handlers(repo);
    std::vector<std::pair<long long, std::string>> resolved;
    for (auto [addr, h] : binds) {
        if (names) {
            const auto r = resolve_handler(h, *names);
            if (!r) throw InputError("--bind handler " + str_repr(h) + " is not in PS2_SYSCALL_LIST or PS2_STUB_LIST of " + repo);
            h = *r;
        } else if (std::find(std::begin(GENERIC_HANDLERS), std::end(GENERIC_HANDLERS), h) == std::end(GENERIC_HANDLERS)) {
            warnings.push_back("handler " + str_repr(h) + " not checked: pass --repo <PS2Recomp> to verify it exists");
        }
        resolved.emplace_back(addr, h);
    }
    std::set<std::string> seen_fn;
    std::vector<std::pair<long long, std::string>> rw(repl); rw.insert(rw.end(), wraps.begin(), wraps.end());
    for (const auto& [addr, fn] : rw) {
        (void)addr;
        if (cpp_keyword(fn)) throw InputError(str_repr(fn) + " is a reserved name, pick another function name");
        if (seen_fn.count(fn)) throw InputError("function name " + str_repr(fn) + " is used twice");
        seen_fn.insert(fn);
    }
    std::vector<long long> addrs; for (const auto& x : resolved) addrs.push_back(x.first); for (const auto& x : rw) addrs.push_back(x.first);
    std::set<long long> dup;
    for (const long long x : addrs) if (std::count(addrs.begin(), addrs.end(), x) > 1) dup.insert(x);
    if (!dup.empty()) { std::vector<std::string> d; for (const long long x : dup) d.push_back(hex32(x)); throw InputError("address used twice: " + join(d, ", ")); }
    std::optional<std::set<long long>> starts;
    if (!get(a, "map").is_none() && !S(a, "map").empty()) starts = function_starts(S(a, "map"));
    std::vector<std::string> reach;
    for (const long long addr : addrs) {
        if (!elf.exec_ranges.empty() && !elf.in_code(addr)) warnings.push_back(hex32(addr) + " is not inside an executable segment of " + elf.name);
        if (starts && !starts->count(addr)) warnings.push_back(hex32(addr) + " is not a function start in the Ghidra map");
        const auto [jal, j] = elf.call_sites(addr);
        reach.push_back(cfmt("  %s: reached by %zu jal and %zu j instruction(s) in the ELF", hex32(addr).c_str(), jal.size(), j.size()));
        if (!j.empty()) {
            std::vector<std::string> js; for (std::size_t k = 0; k < j.size() && k < 4; ++k) js.push_back(hex32(j[k]));
            warnings.push_back(hex32(addr) + cfmt(" is the target of %zu j tail jump(s) (", j.size()) + join(js, ", ")
                + "): PS2Recomp emits those as direct C++ calls that skip the function table, so your function does not run on those paths"
                + (jal.empty() ? "; nothing reaches it with jal: pick the caller one level up" : ""));
        }
    }
    std::vector<std::string> fn, apply;
    for (const auto& [addr, h] : resolved) {
        const std::string x = hex32(addr);
        apply.insert(apply.end(), {
            "    // --bind " + x + "=" + h + ": the game's function now runs the runtime's own " + h + " handler.",
            "    // (Same effect as stubs = [\"" + h + "@" + x + "\"] in the TOML, without re-running ps2_recomp.)",
            "    ps2_game_overrides::bindAddressHandler(runtime, " + x + "u, \"" + h + "\");", ""});
    }
    for (const auto& [addr, name] : repl) {
        const std::string x = hex32(addr);
        fn.insert(fn.end(), {
            "// --replace " + x + "=" + name + ": runs INSTEAD of the game's function at " + x + ".",
            "void " + name + "(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)", "{", "    (void)rdram;", "    (void)runtime;",
            "    // Arguments: read what the game's function reads (check it in Ghidra).",
            "    const uint32_t a0 = getRegU32(ctx, 4); // $a0, first argument",
            "    const uint32_t a1 = getRegU32(ctx, 5); // $a1, second argument", "    (void)a0;", "    (void)a1;", "",
            "    // TODO: the new behaviour. Guest memory: Ps2FastRead32(rdram, addr), Ps2FastWrite32(rdram, addr, value).", "",
            "    setReturnU32(ctx, 0u);        // the value the caller sees in $v0 (setReturnS32 for signed)",
            "    ctx->pc = getRegU32(ctx, 31); // return to the caller ($ra)", "}", ""});
        apply.insert(apply.end(), {
            "    // --replace " + x + "=" + name, "    if (!runtime.replaceFunction(" + x + "u, &" + name + "))", "    {",
            "        std::cerr << \"[console-mod] " + c_string(mod) + ": cannot replace " + x + " (outside the recompiled function table)\" << std::endl;",
            "    }", ""});
    }
    for (const auto& [addr, name] : wraps) {
        const std::string x = hex32(addr), orig = "g_original_" + name;
        fn.insert(fn.end(), {
            "PS2Runtime::RecompiledFunction " + orig + " = nullptr;", "",
            "// --wrap " + x + "=" + name + ": runs the game's own function, then lets you change what it did.",
            "void " + name + "(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)", "{",
            "    // Read arguments BEFORE the call: the game's code reuses these registers.",
            "    const uint32_t a0 = getRegU32(ctx, 4);", "    (void)a0;", "    const uint32_t returnAddress = getRegU32(ctx, 31);", "",
            "    " + orig + "(rdram, ctx, runtime); // the game's function: sets $v0 and ctx->pc", "",
            "    if (ctx->pc != returnAddress)", "    {",
            "        return; // it paused mid-way for the EE scheduler and finishes later without coming back here", "    }",
            "    // TODO: adjust the result. Example, double what the game returned:", "    // setReturnU32(ctx, getRegU32(ctx, 2) * 2u);", "}", ""});
        apply.insert(apply.end(), {
            "    // --wrap " + x + "=" + name, "    if (runtime.hasFunction(" + x + "u))", "    {",
            "        PS2Runtime::RecompiledFunction current = runtime.lookupFunction(" + x + "u);", "        if (current != &" + name + ")", "        {",
            "            " + orig + " = current; // keep the game's function (safe if loadELF runs twice)", "        }",
            "        runtime.replaceFunction(" + x + "u, &" + name + ");", "    }", "    else", "    {",
            "        std::cerr << \"[console-mod] " + c_string(mod) + ": no recompiled function at " + x + " to wrap\" << std::endl;", "    }", ""});
    }
    while (!apply.empty() && apply.back().empty()) apply.pop_back();
    std::vector<std::string> notes{"Mod: " + mod, "Generated by console-mod override.py new on " + today() + " for " + elf.name + cfmt(" (%lld bytes).", elf.size)};
    for (const auto& [addr, h] : resolved) notes.push_back("  bind    " + hex32(addr) + " -> runtime handler " + h);
    for (const auto& [addr, name] : repl) notes.push_back("  replace " + hex32(addr) + " -> " + name + "()");
    for (const auto& [addr, name] : wraps) notes.push_back("  wrap    " + hex32(addr) + " -> " + name + "() around the game's function");
    notes.push_back("Replacing a function affects calls that go through the runtime's function table (jal,");
    notes.push_back("jalr and register jumps). A j tail jump to a known function is emitted as a direct C++");
    notes.push_back("call and skips it: if a target is reached only by j, hook the caller one level up.");
    notes.insert(notes.end(), reach.begin(), reach.end());
    const std::string slug = slugify(mod);
    const std::string text = render_module(mod, elf.name, elf.entry, elf.crc32, slug, join(fn, "\n"), join(apply, "\n"), notes);

    const std::string outp = !get(a, "out").is_none() && !S(a, "out").empty() ? S(a, "out") : path_join(path_join("console", "patches"), slug + ".cpp");
    guard_out(outp);
    if (exists(outp) && !B(a, "force")) throw InputError(outp + " exists: pass --force to overwrite it (it may hold your own code)");
    makedirs(dirname(abspath(outp)));
    write_all(outp, text);
    const auto cm = ensure_cmake_include(dirname(abspath(outp)));
    out("wrote " + outp + "  (keys: " + elf.name + ", entry " + hex32(elf.entry) + ", CRC-32 " + hex32(elf.crc32) + ")");
    for (const auto& w : warnings) out("  warning: " + w);
    out("  install: " + (cm ? *cm : std::string("console_mods.cmake already next to it")));
    out("  (it adds the .cpp files to the ps2EntryRunner executable itself: inside the static ps2_runtime");
    out("  library the linker would drop the self-registration)");
    out("  then: cmake --build out/build --config Debug  (incremental, never clean)");
    return warnings.empty() ? 0 : 1;
}

inline int cmd_keys(const Val& a) {
    const ElfKeys elf(S(a, "elf"));
    out("PS2_REGISTER_GAME_OVERRIDE(\"your mod\", \"" + c_string(elf.name) + cfmt("\", 0x%08llXu, 0x%08llXu, &applyOverride)", static_cast<unsigned long long>(elf.entry), static_cast<unsigned long long>(elf.crc32)));
    out("  file name " + elf.name + ", entry " + hex32(elf.entry) + ", CRC-32 " + hex32(elf.crc32) + " (standard CRC-32 of the whole ELF, not the PCSX2 CRC)");
    return 0;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p(PROG, "Scaffold a PS2Recomp game override (.cpp) with the real API: bind game functions to runtime handlers, replace them, or wrap them.");
    p.epilog = "Exit codes: 0 ok, 1 written with warnings, 2 bad input.";
    p.subcommands("cmd", false);
    { Parser& s = p.sub("new"); s.help_line = "write a new override module";
      s.opt({"--name"}).required().help("the mod name, e.g. \"Gold rifle\" (also names the file)");
      s.opt({"--elf"}).required().help("the game's main ELF: gives the file name, entry and CRC-32 keys");
      s.opt({"--bind"}).append().metavar("0xADDR=handler").help("route the game function at ADDR to a runtime handler (repeatable)");
      s.opt({"--replace"}).append().metavar("0xADDR=fn_name").help("replace the game function at ADDR with a C++ function you fill in (repeatable)");
      s.opt({"--wrap"}).append().metavar("0xADDR=fn_name").help("run the game function at ADDR, then your code (repeatable)");
      s.opt({"--map"}).help("Ghidra CSV (Name,Start,End,Size) to check addresses are function starts");
      s.opt({"--repo"}).help("PS2Recomp checkout, to check --bind handler names against ps2_call_list.h");
      s.opt({"--out"}).help("output .cpp (default console/patches/<slug>.cpp)");
      s.opt({"--force"}).store_true().help("overwrite the output if it exists"); }
    { Parser& s = p.sub("keys"); s.help_line = "print the override keys (file name, entry, CRC-32) for an ELF";
      s.pos("elf").help("the game's main ELF"); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { flush(); const std::string t = ap::help_text(ap::norm(p, p.prog)); std::fwrite(t.data(), 1, t.size(), stdout); return 2; }
    return guard(std::string(PROG) + ": error", [&]() -> int { return cmd == "new" ? cmd_new(a) : cmd_keys(a); });
}
PACKTOOL("console-mod/override", main);
}  // namespace packtools::console_override
