// console_stubs.inc - console-backend/stubs: a C++ port of console-skill/console-backend/stubs.py (Jake Schincariol,
// MIT). scan turns PS2Recomp runtime logs into a ranked list of backend fixes (stubs, syscall TODOs, missing
// functions, IOP/RPC gaps, untranslated instructions, override errors); handlers lists a checkout's real handler
// names, dispatcher numbers and files; syscall names an EE kernel syscall number.
// Each log regex runs only when its fixed text is on the line (the same matches, much faster on big logs).
namespace packtools::console_stubs {
using namespace pyrt;
using namespace packtools::console_common;

const char* const PROG = "stubs";

// ---------------------------------------------------------------- the EE kernel syscalls we are sure of
inline const std::map<long long, std::string>& builtin_syscalls() {
    static const std::map<long long, std::string> m = {
        {0x01, "ResetEE"}, {0x02, "SetGsCrt"}, {0x04, "Exit"}, {0x06, "LoadExecPS2"}, {0x07, "ExecPS2"},
        {0x10, "AddIntcHandler"}, {0x11, "RemoveIntcHandler"}, {0x12, "AddDmacHandler"}, {0x13, "RemoveDmacHandler"},
        {0x14, "EnableIntc"}, {0x15, "DisableIntc"}, {0x16, "EnableDmac"}, {0x17, "DisableDmac"},
        {0x18, "SetAlarm"}, {0x19, "ReleaseAlarm"},
        {-0x1A, "iEnableIntc"}, {-0x1B, "iDisableIntc"}, {-0x1C, "iEnableDmac"}, {-0x1D, "iDisableDmac"},
        {-0x1E, "iSetAlarm"}, {-0x1F, "iReleaseAlarm"},
        {0x20, "CreateThread"}, {0x21, "DeleteThread"}, {0x22, "StartThread"}, {0x23, "ExitThread"},
        {0x24, "ExitDeleteThread"}, {0x25, "TerminateThread"}, {-0x26, "iTerminateThread"},
        {0x27, "DisableDispatchThread"}, {0x28, "EnableDispatchThread"},
        {0x29, "ChangeThreadPriority"}, {-0x2A, "iChangeThreadPriority"},
        {0x2B, "RotateThreadReadyQueue"}, {-0x2C, "iRotateThreadReadyQueue"},
        {0x2D, "ReleaseWaitThread"}, {-0x2E, "iReleaseWaitThread"}, {0x2F, "GetThreadId"},
        {0x30, "ReferThreadStatus"}, {-0x31, "iReferThreadStatus"}, {0x32, "SleepThread"}, {0x33, "WakeupThread"},
        {-0x34, "iWakeupThread"}, {0x35, "CancelWakeupThread"}, {-0x36, "iCancelWakeupThread"},
        {0x37, "SuspendThread"}, {-0x38, "iSuspendThread"}, {0x39, "ResumeThread"}, {-0x3A, "iResumeThread"},
        {0x3C, "SetupThread"}, {0x3D, "SetupHeap"}, {0x3E, "EndOfHeap"},
        {0x40, "CreateSema"}, {0x41, "DeleteSema"}, {0x42, "SignalSema"}, {-0x43, "iSignalSema"},
        {0x44, "WaitSema"}, {0x45, "PollSema"}, {-0x46, "iPollSema"}, {0x47, "ReferSemaStatus"},
        {-0x48, "iReferSemaStatus"},
        {0x4A, "SetOsdConfigParam"}, {0x4B, "GetOsdConfigParam"},
        {0x5C, "EnableIntcHandler"}, {-0x5C, "iEnableIntcHandler"}, {0x5D, "DisableIntcHandler"},
        {-0x5D, "iDisableIntcHandler"}, {0x5E, "EnableDmacHandler"}, {-0x5E, "iEnableDmacHandler"},
        {0x5F, "DisableDmacHandler"}, {-0x5F, "iDisableDmacHandler"},
        {0x61, "EnableCache"}, {0x62, "DisableCache"}, {0x64, "FlushCache"}, {-0x68, "iFlushCache"},
        {0x70, "GsGetIMR"}, {-0x70, "iGsGetIMR"}, {0x71, "GsPutIMR"}, {-0x71, "iGsPutIMR"},
        {0x73, "SetVSyncFlag"}, {0x74, "SetSyscall"},
        {0x76, "sceSifDmaStat"}, {-0x76, "isceSifDmaStat"}, {0x77, "sceSifSetDma"}, {-0x77, "isceSifSetDma"},
        {0x78, "sceSifSetDChain"}, {-0x78, "isceSifSetDChain"}, {0x79, "sceSifSetReg"}, {0x7A, "sceSifGetReg"},
        {0x7B, "ExecOSD"}, {0x7C, "Deci2Call"}, {0x7F, "GetMemorySize"},
    };
    return m;
}

const std::string RT = "ps2xRuntime/src/lib/";
const std::string CALL_LIST = "ps2xRuntime/include/ps2_call_list.h";
const std::string DISPATCHER = RT + "Kernel/Syscalls/Dispatcher.cpp";

struct NameFile { const char* pat; const char* rel; };
// name-based guesses, used only without --repo
const NameFile STUB_PREFIX_FILES[] = {
    {R"(^sceCd)", "Kernel/Stubs/CD.cpp"}, {R"(^(sceDma|DmaAddr))", "Kernel/Stubs/DMA.cpp"},
    {R"(^sceDeci2)", "Kernel/Stubs/Deci2.cpp"},
    {R"(^(sceFs|sceOpen|sceClose|sceRead|sceWrite|sceLseek|sceIoctl|open|close|read|write|lseek|stat|fstat)$|^sceFs)", "Kernel/Stubs/FileIO.cpp"},
    {R"(^sceeFont)", "Kernel/Stubs/Font.cpp"}, {R"(^(sceGs|sceGif|sceVif1))", "Kernel/Stubs/GS.cpp"},
    {R"(^sceIpu)", "Kernel/Stubs/IPU.cpp"}, {R"(^sceMpeg)", "Kernel/Stubs/MPEG.cpp"},
    {R"(^(sceMc|mc[A-Z]))", "Kernel/Stubs/MemoryCard.cpp"}, {R"(^(scePad|Pad[A-Z]))", "Kernel/Stubs/Pad.cpp"},
    {R"(^sceRpc)", "Kernel/Stubs/RPC.cpp"}, {R"(^i?sceSif)", "Kernel/Stubs/SIF.cpp"},
    {R"(^(sceTty|scePrintf|sceResettty))", "Kernel/Stubs/TTY.cpp"}, {R"(^(sceVu0|sceVpu0))", "Kernel/Stubs/VU.cpp"},
    {R"(^(sceSd|sceSSyn|sceSynth))", "Kernel/Stubs/Audio.cpp"},
    {R"(^(ret0|ret1|reta0|.*_r|__malloc_(un)?lock|malloc_extend_top)$)", "Kernel/Stubs/Compatibility.cpp"},
    {R"(^(_?_?)(malloc|free|calloc|realloc|memalign|mem[a-z]+|str[a-z]+|v?s?n?printf|v?fprintf|puts|)"
     R"(sin|cos|tan|atan2?|sqrt|pow|exp|log|log10|floor|ceil|fabs|abs|rand|srand|f(open|close|read|write|seek|tell|flush)|)"
     R"(exit|getpid|divdi3)$)", "Kernel/Stubs/LibC.cpp"},
};
const NameFile SYSCALL_NAME_FILES[] = {
    {R"(Sema|EventFlag|Alarm)", "Kernel/Syscalls/Sync.cpp"},
    {R"(Intc|Dmac|VSync)", "Kernel/Syscalls/Interrupt.cpp"},
    {R"(Thread|Cache|^ResetEE$|^SetMemoryMode$|^Exit$)", "Kernel/Syscalls/Thread.cpp"},
    {R"(^i?sceSif)", "Kernel/Stubs/SIF.cpp"},
    {R"(^Sif)", "Kernel/Syscalls/RPC.cpp"},
    {R"(^fio)", "Kernel/Syscalls/FileIO.cpp"},
    {R"(^Deci2)", "Kernel/Syscalls/Deci2.cpp"},
    {R"(Gs|Osd|^Setup|EndOfHeap|MemorySize|SetSyscall|Exec|Copy|EntryAddress|TLB|FindAddress)", "Kernel/Syscalls/System.cpp"},
};
const std::string AUTO_NAME_RE = R"(^(?:sub|FUN|func|entry|fn|LAB)_(?:0x)?([0-9A-Fa-f]{6,8})(?:_0x[0-9A-Fa-f]+)?$)";

// ---------------------------------------------------------------- numbers
// int(text, base) for the low 64 bits (Python's ints are unbounded; only the low 32 bits of a syscall matter)
inline std::optional<std::pair<bool, uint64_t>> int_wrap(const std::string& s0, unsigned base) {
    std::string s = strip(s0); bool neg = false;
    if (!s.empty() && (s[0] == '+' || s[0] == '-')) { neg = s[0] == '-'; s = s.substr(1); }
    if (base == 16 && s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s = s.substr(2); if (!s.empty() && s[0] == '_') s = s.substr(1); }
    if (s.empty() || s[0] == '_' || s.back() == '_' || s.find("__") != std::string::npos) return std::nullopt;
    uint64_t v = 0;
    for (const auto& ch : cps(s)) {
        const uint32_t c = cp_of(ch);
        if (c == '_') continue;
        unsigned d;
        if (c < 128 && std::isxdigit(static_cast<int>(c))) d = static_cast<unsigned>(std::isdigit(static_cast<int>(c)) ? c - '0' : std::tolower(static_cast<int>(c)) - 'a' + 10);
        else if (uprop::decimal(c)) { uint32_t k = 0; while (k < c && uprop::decimal(c - k - 1)) ++k; d = k % 10; }
        else return std::nullopt;
        if (d >= base) return std::nullopt;
        v = v * base + d;
    }
    return std::make_pair(neg, v);
}
inline long long wrap_val(const std::pair<bool, uint64_t>& p) { return static_cast<long long>(p.first ? (0 - p.second) : p.second); }
inline long long hexv(const std::string& s) { const auto v = int_wrap(s, 16); return v ? wrap_val(*v) : 0; }

inline long long parse_int(const std::string& text) {
    std::string t = replace_all(strip(text), "_", "");
    const bool neg = !t.empty() && t[0] == '-';
    if (neg) t = t.substr(1);
    const bool hex = lower(t).rfind("0x", 0) == 0;
    const auto v = int_wrap(t, hex ? 16 : 10);
    if (!v) throw InputError(str_repr(text) + " is not a number (use 0x3C, 60, -0x1A or 0xFFFFFFE6)");
    const long long x = wrap_val(*v);
    return neg ? static_cast<long long>(0 - static_cast<uint64_t>(x)) : x;
}
inline long long signed_syscall(long long v) {   // 0xFFFFFFE6 -> -0x1A
    const uint32_t u = static_cast<uint32_t>(static_cast<uint64_t>(v) & 0xFFFFFFFFu);
    return u & 0x80000000u ? static_cast<long long>(u) - 0x100000000LL : static_cast<long long>(u);
}
inline std::string fmt_syscall(long long n) {
    if (n < 0) return cfmt("-0x%llX (0x%08llX)", static_cast<unsigned long long>(0 - static_cast<uint64_t>(n)), static_cast<unsigned long long>(static_cast<uint64_t>(n) & 0xFFFFFFFFu));
    return cfmt("0x%llX", static_cast<unsigned long long>(n));
}
inline std::string case_literal(long long n) {
    return n < 0 ? cfmt("static_cast<uint32_t>(-0x%llX)", static_cast<unsigned long long>(0 - static_cast<uint64_t>(n))) : cfmt("0x%llX", static_cast<unsigned long long>(n));
}

// first n code points
inline std::string head_cp(const std::string& s, std::size_t n) {
    std::size_t i = 0, k = 0;
    while (i < s.size() && k < n) { const unsigned char c = static_cast<unsigned char>(s[i]); i += c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4; ++k; }
    return s.substr(0, std::min(i, s.size()));
}
// byte offset of code point n
inline std::size_t cp_byte(const std::string& s, long n) {
    std::size_t i = 0; long k = 0;
    while (i < s.size() && k < n) { const unsigned char c = static_cast<unsigned char>(s[i]); i += c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4; ++k; }
    return std::min(i, s.size());
}
inline std::string rstrip_ws(const std::string& s) {
    auto v = cps(s); std::size_t b = v.size(); while (b > 0 && py_space(cp_of(v[b - 1]))) b--;
    std::string o; for (std::size_t k = 0; k < b; ++k) o += v[k]; return o;
}
inline bool has(const std::string& line, const char* lit) { return line.find(lit) != std::string::npos; }

// ---------------------------------------------------------------- the checkout
const std::string DEF_RE = R"(^\s*void\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(\s*uint8_t\s*\*\s*[A-Za-z_]*\s*,\s*R5900Context\s*\*)";
const std::string TODO_RE = R"re(TODO_NAMED\(\s*"([A-Za-z_][A-Za-z0-9_]*)")re";
const std::string CASE_RE = R"(case\s+(?:static_cast<\s*uint32_t\s*>\s*\(\s*)?(-?)\s*(0x[0-9A-Fa-f]+|\d+)\s*\)?\s*:)";
const std::string CALL_RE = R"((?:[A-Za-z_][A-Za-z0-9_]*::)?([A-Za-z_][A-Za-z0-9_]*)\s*\(\s*rdram\s*,\s*ctx\s*,\s*runtime\s*\)\s*;)";

inline std::set<std::string> macro_names(const std::string& text, const std::string& macro) {
    const std::string pat = R"(#define\s+)" + macro + R"(\s*\(\s*X\s*\))";
    const auto m = re::search(pat, text);
    std::set<std::string> names;
    if (!m) return names;
    for (const auto& line : split(text.substr(cp_byte(text, m.end())), "\n")) {
        const std::string body = re::sub(R"(/\*.*?\*/)", "", line);
        for (const auto& n : re::findall(R"(\bX\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\))", body)) names.insert(n);
        const std::string r = rstrip_ws(line);
        if (r.empty() || r.back() != '\\') break;
    }
    return names;
}

// signed number -> handler name, in the order first seen
struct Dispatch { std::vector<std::pair<long long, std::string>> items; std::map<long long, std::size_t> at;
    const std::string* get(long long n) const { const auto it = at.find(n); return it == at.end() ? nullptr : &items[it->second].second; }
    void setdefault(long long n, const std::string& v) { if (!at.count(n)) { at.emplace(n, items.size()); items.emplace_back(n, v); } } };
inline Dispatch parse_dispatcher(const std::string& text) {
    Dispatch o; std::vector<long long> pending;
    const re::Regex& case_re = re::compile(CASE_RE);
    const re::Regex& call_re = re::compile(CALL_RE);
    for (const auto& line : split(text, "\n")) {
        const std::string code = line.substr(0, line.find("//"));
        for (const auto& m : case_re.finditer(code)) {
            const std::string g = m.group(2);
            const bool hx = lower(g).rfind("0x", 0) == 0;
            const auto v = int_wrap(g, hx ? 16 : 10);
            const long long x = v ? wrap_val(*v) : 0;
            pending.push_back(!m.group(1).empty() ? static_cast<long long>(0 - static_cast<uint64_t>(x)) : x);
        }
        const auto c = call_re.search(code);
        if (c && !pending.empty()) { for (const long long n : pending) o.setdefault(n, c.group(1)); pending.clear(); }
        if (re::search(R"(\bdefault\s*:)", code)) pending.clear();
    }
    return o;
}

struct Knowledge {
    bool has_repo = false; std::string repo;
    std::set<std::string> syscall_names, stub_names;
    bool has_dispatch = false; Dispatch dispatch;
    std::map<std::string, std::string> defined, todo;   // handler name -> relative file

    explicit Knowledge(const std::string& r, bool given) {
        if (!given) return;
        repo = abspath(r); has_repo = true;
        load();
    }
    std::string read(const std::string& rel) const {
        std::string path = repo; for (const auto& part : split(rel, "/")) path = path_join(path, part);
        try { return decode_utf8_replace(read_text(path)); }
        catch (const PyErr&) { throw InputError(repo + " is not a PS2Recomp checkout (missing " + rel + ")"); }
    }
    void walk(const std::string& dir, const std::string& rel) {
        std::error_code ec; std::vector<std::string> files, dirs;
        for (std::filesystem::directory_iterator it(fspath(dir), ec), end; !ec && it != end; it.increment(ec)) {
            const std::string n = u8(it->path().filename());
            std::error_code e2; const bool link = it->is_symlink(e2); const bool d = it->is_directory(e2);
            if (d) { if (!link) dirs.push_back(n); }
            else files.push_back(n);
        }
        std::sort(files.begin(), files.end()); std::sort(dirs.begin(), dirs.end());
        const re::Regex& def_re = re::compile(DEF_RE, re::M);
        const re::Regex& todo_re = re::compile(TODO_RE);
        for (const auto& f : files) {
            if (f.size() < 4 || f.compare(f.size() - 4, 4, ".cpp") != 0) continue;
            std::string src;
            try { src = decode_utf8_replace(read_text(path_join(dir, f))); } catch (const PyErr&) { continue; }
            const std::string r = rel + "/" + f;
            for (const auto& m : def_re.finditer(src)) defined.emplace(m.group(1), r);
            for (const auto& m : todo_re.finditer(src)) todo.emplace(m.group(1), r);
        }
        for (const auto& d : dirs) walk(path_join(dir, d), rel + "/" + d);
    }
    void load() {
        const std::string text = read(CALL_LIST);
        syscall_names = macro_names(text, "PS2_SYSCALL_LIST");
        stub_names = macro_names(text, "PS2_STUB_LIST");
        if (syscall_names.empty() && stub_names.empty()) throw InputError(CALL_LIST + " has no PS2_SYSCALL_LIST / PS2_STUB_LIST");
        dispatch = parse_dispatcher(read(DISPATCHER)); has_dispatch = true;
        std::string kernel = repo; for (const auto& part : split(RT + "Kernel", "/")) kernel = path_join(kernel, part);
        if (is_dir(kernel)) walk(kernel, "Kernel");
    }
    // the runtime's own alias rule: exact, or with/without one leading underscore -> (kind, handler)
    std::pair<std::string, std::string> resolve(const std::string& name) const {
        if (!has_repo || name.empty()) return {"", ""};
        for (const auto& [kind, names] : {std::make_pair(std::string("syscall"), &syscall_names), std::make_pair(std::string("stub"), &stub_names)}) {
            if (names->count(name)) return {kind, name};
            if (name[0] == '_' && names->count(name.substr(1))) return {kind, name.substr(1)};
            if (names->count("_" + name)) return {kind, "_" + name};
        }
        return {"", ""};
    }
    // relative file under ps2xRuntime/src/lib/ ("" when unknown), exact?
    std::pair<std::string, bool> file_for(const std::string& name, bool syscall = false) const {
        if (has_repo && defined.count(name)) return {defined.at(name), true};
        if (syscall) { for (const auto& nf : SYSCALL_NAME_FILES) if (re::search(nf.pat, name)) return {nf.rel, false}; }
        else { for (const auto& nf : STUB_PREFIX_FILES) if (re::search(nf.pat, name)) return {nf.rel, false}; }
        return {"", false};
    }
    // (built-in name, dispatcher name); "" = None
    std::pair<std::string, std::string> syscall_name(long long n) const {
        const std::string* r = has_dispatch ? dispatch.get(n) : nullptr;
        const auto& b = builtin_syscalls(); const auto it = b.find(n);
        return {it == b.end() ? "" : it->second, r ? *r : ""};
    }
};

// ---------------------------------------------------------------- log parsing
#define H_ "([0-9A-Fa-f]+)"
const std::string STUB_RE = R"(Warning: Unimplemented PS2 stub called\. name=(\S*) PC=0x)" H_ R"(, RA=0x)" H_;
const std::string SUPPRESS_RE = R"(Warning: Further calls to PS2 stub '([^']*)' are suppressed after (\d+) warnings)";
const std::string SYSCALL_TODO_RE = R"(\[Syscall TODO\] encoded=0x)" H_ R"( v1=0x)" H_ R"( v0=0x)" H_ R"(.*? pc=0x)" H_;
const std::string SYSCALL_WARN_RE = R"(Warning: Unimplemented PS2 syscall called\. PC=0x)" H_ R"(, RA=0x)" H_ R"(, Encoded=0x)" H_ R"(, v0=0x)" H_ R"(, v1=0x)" H_ R"(, Chosen=0x)" H_;
const std::string UNKNOWN_HITS_RE = R"(-> Unknown syscallId=0x)" H_ R"( hits=(\d+))";
const std::string MISSING_RE = R"(Error: No exact recompiled function for guest PC 0x)" H_ R"( tableBase=0x)" H_ R"( tableEnd=0x)" H_ R"( codeRegion=(yes|no))";
const std::string BRANCH_RE = R"(\[guest-branch:missing-target\] kind=(\S+) op=(\S+) source=0x)" H_ R"( target=0x)" H_ R"( pc=0x)" H_ R"( ra=0x)" H_;
const std::string CODE_REGION_RE = R"(codeRegion=(yes|no))";
const std::string RPC_RE = R"(\[IOP/RPC trace:unhandled\] sid=0x)" H_ R"( rpc=0x)" H_ R"( pc=0x)" H_ R"( ra=0x)" H_;
const std::string RPC_MODS_RE = R"(loadedModules=\[([^\]]*)\])";
const std::string IOPLOG_RE = R"(\[ps2xIOP:(error|warning)\] (.*))";
const std::string SIFMOD_RE = R"re(\[SIF module\] (\S+) id=(-?\d+) ref=(\d+) path="([^"]*)")re";
const std::string OVR_UNRESOLVED_RE = R"(\[game_overrides\] unresolved handler '([^']*)' for address 0x)" H_;
const std::string OVR_TABLE_RE = R"(\[function-table\] cannot replace guest PC 0x)" H_;
const std::string OVR_APPLY_RE = R"(\[game_overrides\] applying '([^']*)')";
const std::string FATAL_RE = R"((?:\[main\] fatal exception: |Error during program execution: |\[terminate\] std::exception: |\[terminate\] std::system_error .*?message=)(.*))";
const std::string UNHANDLED_RE = R"((Unhandled [^\n]*?) at 0x)" H_ R"( raw=0x([0-9A-Fa-f]{8}))";
const std::string ELF_LOADED_RE = R"(ELF file loaded successfully\. Entry point: 0x)" H_;
const std::string CD_SEARCH_FAIL_RE = R"(sceCdSearchFile failed: (.*?) \((?:normalized path is empty, )?root: ([^,)]*))";
const std::string CD_LBN_RE = R"(sceCdRead unresolved LBN 0x)" H_ R"( sectors=(\d+))";
const std::string CD_REQ_RE = R"(\[sceCdRead\] unresolved request pc=0x)" H_ R"( ra=0x)" H_;
const std::string FIO_RE = R"(\b(fio[A-Za-z]+) (error|warning): (.*))";
#undef H_

struct Group {
    std::string kind; bool key_int = false; long long key_i = 0; std::string key_s;
    long long rank = 0; std::string first; bool has_pc = false, has_ra = false; long long pc = 0, ra = 0; std::string example;
    std::map<std::string, long long> counts;
    // extra
    bool has_suppressed = false; long long suppressed_after = 0;
    bool has_code_region = false; std::string code_region;
    bool has_table = false; long long table_lo = 0, table_hi = 0;
    bool has_sid = false; long long sid = 0;
    bool has_path = false; std::string path, root;
    bool has_lbn = false; bool has_fio = false;
    bool has_handler = false; std::string handler;
    bool has_what = false; std::string what, raw;
    bool ended_run = false;
};
struct Module { std::string op; long long id; std::string path; };
struct Milestones { bool argv_boot = false; bool has_entry = false; long long elf_entry = 0; std::vector<std::string> overrides_applied; std::vector<Module> modules; bool has_end = false; std::string ended_with; };

struct Scan {
    std::vector<Group> groups; std::map<std::string, std::size_t> index;
    long long order = 0, lines = 0; std::vector<std::string> files; Milestones ms;

    static std::string gkey(const std::string& kind, bool is_int, long long i, const std::string& s) { return kind + '\x01' + (is_int ? "i" + std::to_string(i) : "s" + s); }
    Group& group(const std::string& kind, bool is_int, long long ki, const std::string& ks, const std::string& where, const std::string& line_text, const long long* pc = nullptr, const long long* ra = nullptr) {
        const std::string k = gkey(kind, is_int, ki, ks);
        auto it = index.find(k);
        if (it == index.end()) {
            Group g; g.kind = kind; g.key_int = is_int; g.key_i = ki; g.key_s = ks; g.rank = ++order; g.first = where;
            g.example = head_cp(strip(line_text), 240);
            it = index.emplace(k, groups.size()).first; groups.push_back(std::move(g));
        }
        Group& g = groups[it->second];
        if (!g.has_pc && pc) { g.has_pc = true; g.pc = *pc; }
        if (!g.has_ra && ra) { g.has_ra = true; g.ra = *ra; }
        return g;
    }
    Group& gs(const std::string& kind, const std::string& key, const std::string& where, const std::string& line, const long long* pc = nullptr, const long long* ra = nullptr) { return group(kind, false, 0, key, where, line, pc, ra); }
    Group& gi(const std::string& kind, long long key, const std::string& where, const std::string& line, const long long* pc = nullptr, const long long* ra = nullptr) { return group(kind, true, key, "", where, line, pc, ra); }
    static void bump(Group& g, const std::string& source, long long n = 1) { g.counts[source] += n; }

    void feed(const std::string& path, long long line_no, const std::string& line) {
        const std::string where = basename(path) + ":" + std::to_string(line_no);
        bool hit = false;
        if (has(line, "Warning: Unimplemented PS2 stub called. name="))
            for (const auto& m : re::compile(STUB_RE).finditer(line)) {
                const std::string name = m.group(1).empty() ? "(unnamed)" : m.group(1);
                const long long pc = hexv(m.group(2)), ra = hexv(m.group(3));
                bump(gs("stub", name, where, line, &pc, &ra), "warn"); hit = true;
            }
        if (has(line, "Warning: Further calls to PS2 stub '"))
            for (const auto& m : re::compile(SUPPRESS_RE).finditer(line)) {
                Group& g = gs("stub", m.group(1).empty() ? "(unnamed)" : m.group(1), where, line);
                const auto v = int_wrap(m.group(2), 10); g.has_suppressed = true; g.suppressed_after = v ? wrap_val(*v) : 0; hit = true;
            }
        if (has(line, "Warning: Unimplemented PS2 syscall called. PC=0x"))
            for (const auto& m : re::compile(SYSCALL_WARN_RE).finditer(line)) {
                const long long n = signed_syscall(hexv(m.group(6))), pc = hexv(m.group(1)), ra = hexv(m.group(2));
                bump(gi("syscall", n, where, line, &pc, &ra), "warn"); hit = true;
            }
        if (!hit) {
            if (has(line, "[Syscall TODO] encoded=0x"))
                for (const auto& m : re::compile(SYSCALL_TODO_RE).finditer(line)) {
                    const long long enc = hexv(m.group(1)), v1 = hexv(m.group(2)), pc = hexv(m.group(4));
                    bump(gi("syscall", signed_syscall(enc ? enc : v1), where, line, &pc), "todo"); hit = true;
                }
            if (has(line, "-> Unknown syscallId=0x"))
                for (const auto& m : re::compile(UNKNOWN_HITS_RE).finditer(line)) {
                    Group& g = gi("syscall", signed_syscall(hexv(m.group(1))), where, line);
                    const auto v = int_wrap(m.group(2), 10); const long long h = v ? wrap_val(*v) : 0;
                    const auto it = g.counts.find("hits"); g.counts["hits"] = std::max(it == g.counts.end() ? 0LL : it->second, h); hit = true;
                }
        }
        if (has(line, "Error: No exact recompiled function for guest PC 0x"))
            for (const auto& m : re::compile(MISSING_RE).finditer(line)) {
                const long long pc = hexv(m.group(1));
                Group& g = gi("missing", pc, where, line, &pc);
                g.has_code_region = true; g.code_region = m.group(4);
                g.has_table = true; g.table_lo = hexv(m.group(2)); g.table_hi = hexv(m.group(3));
                bump(g, "dispatch"); hit = true;
            }
        if (has(line, "[guest-branch:missing-target] kind="))
            for (const auto& m : re::compile(BRANCH_RE).finditer(line)) {
                const long long target = hexv(m.group(4)), ra = hexv(m.group(6));
                Group& g = gi("missing", target, where, line, &target, &ra);
                const auto cr = re::search(CODE_REGION_RE, line);
                if (cr && !g.has_code_region) { g.has_code_region = true; g.code_region = cr.group(1); }
                bump(g, "branch"); hit = true;
            }
        if (has(line, "[IOP/RPC trace:unhandled] sid=0x"))
            for (const auto& m : re::compile(RPC_RE).finditer(line)) {
                const long long sid = hexv(m.group(1)), rpc = hexv(m.group(2)), pc = hexv(m.group(3)), ra = hexv(m.group(4));
                Group& g = gs("iop", cfmt("rpc sid=0x%llX rpc=0x%llX", static_cast<unsigned long long>(sid), static_cast<unsigned long long>(rpc)), where, line, &pc, &ra);
                g.has_sid = true; g.sid = sid;
                bump(g, "rpc"); hit = true;
            }
        if (has(line, "[ps2xIOP:")) {
            const auto m = re::search(IOPLOG_RE, line);
            if (m) {
                std::string msg = re::sub(R"(0x[0-9A-Fa-f]+)", "0x?", strip(m.group(2)));
                msg = head_cp(re::sub(R"(\b\d+\b)", "#", msg), 160);
                bump(gs("iop", "ps2xIOP " + m.group(1) + ": " + msg, where, line), "log"); hit = true;
            }
        }
        if (has(line, "sceCdSearchFile failed: ")) {
            const auto m = re::search(CD_SEARCH_FAIL_RE, line);
            if (m) {
                Group& g = gs("io", "disc file not found: " + head_cp(strip(m.group(1)), 160), where, line);
                g.has_path = true; g.path = strip(m.group(1)); g.root = strip(m.group(2));
                bump(g, "search"); hit = true;
            }
        }
        if (has(line, "sceCdRead unresolved LBN 0x")) {
            const auto m = re::search(CD_LBN_RE, line);
            if (m) { Group& g = gs("io", "raw sector reads (no CD image)", where, line); g.has_lbn = true; bump(g, "lbn"); hit = true; }
        }
        if (has(line, "[sceCdRead] unresolved request pc=0x")) {
            const auto m = re::search(CD_REQ_RE, line);
            if (m) { const long long pc = hexv(m.group(1)), ra = hexv(m.group(2)); bump(gs("io", "sceCdRead request not understood", where, line, &pc, &ra), "req"); hit = true; }
        }
        if (has(line, "fio")) {
            const auto m = re::search(FIO_RE, line);
            if (m) {
                const std::string msg = head_cp(re::sub(R"(\b\d+\b)", "#", strip(m.group(3))), 160);
                Group& g = gs("io", m.group(1) + " " + m.group(2) + ": " + msg, where, line);
                g.has_fio = true; bump(g, "fio"); hit = true;
            }
        }
        if (has(line, "[game_overrides] unresolved handler '"))
            for (const auto& m : re::compile(OVR_UNRESOLVED_RE).finditer(line)) {
                const long long pc = hexv(m.group(2));
                Group& g = gs("override", "unresolved handler '" + m.group(1) + "'", where, line, &pc);
                g.has_handler = true; g.handler = m.group(1); bump(g, "log"); hit = true;
            }
        if (has(line, "[function-table] cannot replace guest PC 0x"))
            for (const auto& m : re::compile(OVR_TABLE_RE).finditer(line)) {
                const long long addr = hexv(m.group(1));
                bump(gs("override", cfmt("outside function table 0x%08llX", static_cast<unsigned long long>(addr)), where, line, &addr), "log"); hit = true;
            }
        if (has(line, "[main] fatal exception: ") || has(line, "Error during program execution: ") || has(line, "[terminate] std::exception: ") || has(line, "[terminate] std::system_error ")) {
            const auto m = re::search(FATAL_RE, line);
            if (m) {
                const std::string msg = strip(m.group(1));
                ms.has_end = true; ms.ended_with = head_cp(msg, 240);
                const auto u = re::search(UNHANDLED_RE, msg);
                const auto stub_end = re::match(R"(Unimplemented PS2 stub called: (\S+))", msg);
                if (u) {
                    const long long addr = hexv(u.group(2));
                    Group& g = gi("opcode", addr, where, line, &addr);
                    g.has_what = true; g.what = u.group(1); g.raw = upper(u.group(3));
                    bump(g, "fatal");
                } else if (stub_end && index.count(gkey("stub", false, 0, stub_end.group(1)))) {
                    groups[index[gkey("stub", false, 0, stub_end.group(1))]].ended_run = true;
                } else {
                    const std::string key = head_cp(re::sub(R"(0x[0-9A-Fa-f]+)", "0x?", msg), 160);
                    bump(gs("fatal", key, where, line), "fatal");
                }
                hit = true;
            }
        }
        if (!hit) {
            if (has(line, "Using argv boot path")) ms.argv_boot = true;
            if (has(line, "ELF file loaded successfully. Entry point: 0x")) {
                const auto m = re::search(ELF_LOADED_RE, line);
                if (m) { ms.has_entry = true; ms.elf_entry = hexv(m.group(1)); }
            }
            if (has(line, "[game_overrides] applying '"))
                for (const auto& m : re::compile(OVR_APPLY_RE).finditer(line))
                    if (std::find(ms.overrides_applied.begin(), ms.overrides_applied.end(), m.group(1)) == ms.overrides_applied.end()) ms.overrides_applied.push_back(m.group(1));
            if (has(line, "[SIF module] "))
                for (const auto& m : re::compile(SIFMOD_RE).finditer(line))
                    if (ms.modules.size() < 200) { const auto v = int_wrap(m.group(2), 10); ms.modules.push_back({m.group(1), v ? wrap_val(*v) : 0, m.group(4)}); }
        }
    }

    void read(const std::string& path) {
        if (is_dir(path)) throw InputError(path + " is a folder; pass log files");
        errno = 0;
        std::FILE* f = open_file(path, "rb");
        if (!f) throw InputError("cannot open " + path + ": " + strerror_of(errno ? errno : ENOENT));
        files.push_back(abspath(path));
        std::string pend; char buf[1 << 16]; std::size_t n; long long i = 0;
        const auto one = [&](const std::string& raw) {
            ++lines; ++i;
            std::string line = decode_utf8_replace(raw);
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            line = head_cp(line, 20000);
            feed(path, i, line);
        };
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
            pend.append(buf, n);
            std::size_t a = 0;
            for (std::size_t k; (k = pend.find('\n', a)) != std::string::npos; a = k + 1) one(pend.substr(a, k + 1 - a));
            pend.erase(0, a);
        }
        std::fclose(f);
        if (!pend.empty()) one(pend);
    }
};

// ---------------------------------------------------------------- advice
inline Val hexs(bool has_v, long long v) { return has_v ? Val::str(cfmt("0x%08llX", static_cast<unsigned long long>(v))) : Val::none(); }
struct Advice { std::string tool, where, how; };

inline Advice advise(const Group& g, const Knowledge& kb) {
    const std::string& kind = g.kind;
    const std::string note_guess = kb.has_repo ? "" : " (a guess from the name: pass --repo to confirm)";
    if (kind == "stub") {
        const std::string& name = g.key_s;
        const auto aut = re::match(AUTO_NAME_RE, name);
        const auto [rkind, handler] = kb.resolve(name);
        if (!handler.empty()) {
            const auto ff = kb.file_for(handler);
            const std::string where = !ff.first.empty() ? RT + ff.first : RT + "Kernel/";
            if (kb.todo.count(handler)) {
                const std::string how = "`" + handler + "` exists as a runtime handler but is still a TODO in " + where + ". Implement it there: read "
                    "arguments with getRegU32(ctx, 4..7), return with setReturnU32/setReturnS32. Until then a "
                    "temporary \"ret0@0x<start>\" or \"ret1@0x<start>\" binding in the TOML can show how much "
                    "it matters (record it in STATE.md as TEMPORARY).";
                return {"Runtime C++", where, how};
            }
            const std::string how = "`" + handler + "` has a working runtime handler in " + where + ", so this call came from a `skip` entry (skipped "
                "functions always log as unimplemented) or from an older build. Take it out of `skip` "
                "(bind it in `stubs` instead) and re-run ps2_recomp, or just rebuild.";
            return {"TOML", "console/<serial>.toml", how};
        }
        if (aut) {
            const std::string a = cfmt("0x%08llX", static_cast<unsigned long long>(hexv(aut.group(1))));
            const std::string how = "A skipped or unbound function (the name carries its start, " + a + "). If Ghidra shows it is an "
                "SDK call the runtime implements, bind it in [general].stubs: \"<Handler>@" + a + "\" (check "
                "names with `stubs.py handlers --repo`). If it is the game's own code, take it out of skip/"
                "stubs and re-run ps2_recomp. To classify it first, a temporary \"ret0@" + a + "\" for one run.";
            return {"TOML", "console/<serial>.toml [general].stubs", how};
        }
        const auto ff = kb.file_for(name);
        const std::string& rel = ff.first;
        std::string hint;
        if (kb.has_repo)
            hint = "No runtime handler is named `" + name + "`. Find the matching one (`stubs.py handlers --repo ... "
                "--grep <part>`) and bind it: \"<Handler>@0x<start>\". Or implement the body in a game "
                "override with runtime.replaceFunction(0x<start>, fn), which needs no header change. Adding "
                "a new name to PS2_STUB_LIST edits ps2_call_list.h: every generated file rebuilds, ask first.";
        else
            hint = "`" + name + "` may be a runtime handler that is still a TODO" + (!rel.empty() ? " in " + RT + rel : std::string()) +
                ", or a library name the runtime does not know. Pass --repo to tell which. Bind with \"<Handler>@0x<start>\" or implement it.";
        return {"TOML or runtime C++", !rel.empty() ? RT + rel + note_guess : std::string("console/<serial>.toml"), hint};
    }
    if (kind == "syscall") {
        const long long n = g.key_i;
        const auto [builtin, repo_name] = kb.syscall_name(n);
        const std::string name = !repo_name.empty() ? repo_name : builtin;
        if (!repo_name.empty())
            return {"Rebuild", DISPATCHER, "This checkout already dispatches " + fmt_syscall(n) + " to `" + repo_name + "`: the log came from an older build. Rebuild "
                    "(incremental) and run again."};
        if (!name.empty()) {
            const auto ff = kb.file_for(name, true);
            const std::string& rel = ff.first;
            const bool defined = kb.has_repo && (kb.defined.count(name) || !kb.resolve(name).second.empty());
            std::string how;
            if (defined) {
                const auto it = kb.defined.find(name);
                how = "Syscall " + fmt_syscall(n) + " is " + name + " and the handler exists (" + RT + (it != kb.defined.end() ? it->second : (!rel.empty() ? rel : std::string("Kernel/"))) +
                      "), but Dispatcher.cpp has no case for it. Add `case " + case_literal(n) + ":` calling it.";
            } else
                how = "Syscall " + fmt_syscall(n) + " is " + name + " (standard EE kernel table). Implement it in " +
                      (!rel.empty() ? RT + rel + note_guess : RT + "Kernel/Syscalls/") + " and add `case " + case_literal(n) + ":` in "
                      "Dispatcher.cpp. The runtime returns 0 for unknown syscalls and carries on, so fix this "
                      "before chasing later symptoms.";
            return {"Runtime C++", DISPATCHER, how};
        }
        return {"Runtime C++", DISPATCHER,
                "Unknown syscall " + fmt_syscall(n) + ". Identify it first: in Ghidra, the value loaded into v1 before the syscall at "
                "PC; ps2tek and the ps2sdk kernel headers list the numbers. Then implement it and add `case " + case_literal(n) + ":`. "
                "The runtime returns 0 meanwhile."};
    }
    if (kind == "missing") {
        const long long pc = g.key_i;
        const auto H8 = [](long long v) { return cfmt("0x%08llX", static_cast<unsigned long long>(v)); };
        if (g.has_table && !(g.table_lo <= pc && pc < g.table_hi))
            return {"Investigate", "the caller (RA, trace=)",
                    H8(pc) + " is outside the generated function table [" + H8(g.table_lo) + ", " + H8(g.table_hi) + "): a bad pointer or a second "
                    "program. Look at the caller, not the target."};
        if (g.has_code_region && g.code_region == "yes")
            return {"Recompiler", "console/<serial>.toml [general].entry_points",
                    "A function start inside the ELF's code that the recompiler did not emit. Add \"" + H8(pc) + "\" to "
                    "entry_points (or define the function in Ghidra and re-export), then re-run ps2_recomp. Every "
                    "re-run rebuilds the runner: batch several entry points per run."};
        return {"Investigate", "the caller (RA, trace=)",
                H8(pc) + " is outside the loaded ELF's code: an overlay or second program loaded at runtime (it needs "
                "its own recompile), or a wild pointer. Decompile the caller at RA-8 before anything else."};
    }
    if (kind == "opcode")
        return {"Game override", "console/patches/ (override module)",
                "The recompiler could not translate " + (g.has_what ? g.what : std::string("an instruction")) + " (word 0x" + (g.has_what ? g.raw : std::string("?")) + ") at " +
                cfmt("0x%08llX", static_cast<unsigned long long>(g.key_i)) + " and emitted a throw. Replace the "
                "function with a game override, or patch the instruction in [[patches.instructions]] if an "
                "equivalent exists. Record it under Known upstream issues and offer the user a minimal upstream "
                "report (address and word only)."};
    if (kind == "iop") {
        if (g.has_sid)
            return {"IOP", "ps2xIOP/ and the extracted disc's modules",
                    cfmt("No loaded IRX module and no generic service answers SID 0x%llX. Check the game's IRX files are ", static_cast<unsigned long long>(g.sid)) +
                    "in the extracted disc folder and loaded (see the [SIF module] lines). Generic HLE exists only "
                    "for MCSERV, LIBSD and DBCMAN (ps2xIOP/src/modules/). If the module did load, it may need IOP "
                    "imports or hardware the emulator lacks (ps2xIOP/src/emulator/)."};
        return {"IOP", "ps2xIOP/src/emulator/",
                "Read the message: module loads, imports and IOP services live in ps2xIOP. A missing IRX file is "
                "an extraction or path-case problem, not a code problem."};
    }
    if (kind == "io") {
        if (g.has_path)
            return {"Disc files", "the extracted disc folder (cdRoot)",
                    "The game asked for " + g.path + " and it is not under " + (!g.root.empty() ? g.root : std::string("the ELF's folder")) + ". Check the file is on the disc (`iso.py ls`), "
                    "that the extraction finished (`iso.py info` says complete), and that the run command passes "
                    "the ELF inside the extracted folder: cdrom0: is that ELF's folder."};
        if (g.has_lbn)
            return {"Game override", "console/patches/ (override module)",
                    "The game reads disc sectors by number (its own file table), not by name, and no CD image is "
                    "configured, so the reads return nothing. Point the runtime at the user's own disc image from "
                    "the game override: `PS2Runtime::IoPaths p = PS2Runtime::getIoPaths(); p.cdImage = "
                    "\"/path/to/your.iso\"; PS2Runtime::setIoPaths(p);`. It must be a plain 2048-byte-sector "
                    "image (`iso.py plain` converts a raw .bin). Keep that path local: never share the image."};
        if (g.has_fio)
            return {"Runtime C++", RT + "Kernel/Syscalls/FileIO.cpp",
                    "A file call failed inside the runtime. Paths are resolved by ps2_vfs.cpp against cdRoot, "
                    "host and mc0 as written (case-sensitive on Linux). Check the path in the message first."};
        return {"Runtime C++", RT + "Kernel/Stubs/CD.cpp",
                "sceCdRead got arguments the runtime could not map to a file or sector. Read sceCdRead in CD.cpp, "
                "decompile the caller (RA-8) in Ghidra, and compare the arguments with PCSX2."};
    }
    if (kind == "override") {
        if (g.has_handler)
            return {"Game override", "your override module",
                    "bindAddressHandler got a name that is not in PS2_SYSCALL_LIST or PS2_STUB_LIST: '" + g.handler + "'. Check "
                    "the spelling with `stubs.py handlers --repo`."};
        return {"Game override", "your override module",
                "replaceFunction refused an address outside the generated function table. Use the function's "
                "start address in this ELF build."};
    }
    return {"Investigate", "the lines before it",
            "The run ended with an exception. Read the lines just before it; the cause is usually the last "
            "stub, syscall or missing function logged."};
}

inline std::string label(const Group& g) {
    if (g.kind == "syscall") return "syscall " + fmt_syscall(g.key_i);
    if (g.kind == "missing") return cfmt("missing function 0x%08llX", static_cast<unsigned long long>(g.key_i));
    if (g.kind == "opcode") return cfmt("untranslated instruction at 0x%08llX", static_cast<unsigned long long>(g.key_i));
    return g.key_int ? std::to_string(g.key_i) : g.key_s;
}

// ---------------------------------------------------------------- commands
inline Val build_report(const std::vector<std::string>& paths, const Knowledge& kb) {
    Scan scan;
    for (const auto& p : paths) scan.read(p);
    List rows;
    for (const auto& g : scan.groups) {   // already in rank order
        long long calls = 1;
        if (!g.counts.empty()) { calls = LLONG_MIN; for (const auto& kv : g.counts) calls = std::max(calls, kv.second); }
        const Advice a = advise(g, kb);
        Val row = dict_of({{"rank", Val::integer(static_cast<long long>(rows.size()) + 1)}, {"kind", Val::str(g.kind)}, {"what", Val::str(label(g))},
                           {"calls", Val::integer(calls)}, {"first", Val::str(g.first)}, {"first_pc", hexs(g.has_pc, g.pc)}, {"first_ra", hexs(g.has_ra, g.ra)},
                           {"fix_tool", Val::str(a.tool)}, {"where", Val::str(a.where)}, {"how", Val::str(a.how)}, {"example", Val::str(g.example)}});
        if (g.kind == "syscall") {
            const auto [builtin, repo_name] = kb.syscall_name(g.key_i);
            row.d->set("syscall", dict_of({{"number", Val::integer(g.key_i)}, {"builtin_name", builtin.empty() ? Val::none() : Val::str(builtin)},
                                           {"dispatcher", repo_name.empty() ? Val::none() : Val::str(repo_name)}}));
        }
        if (g.kind == "stub") {
            if (g.has_pc && g.has_ra && g.pc == g.ra)
                row.d->set("how", Val::str(a.how + cfmt(" The logged PC equals RA because the generated wrapper sets pc = ra before the "
                                                         "handler runs: the call site is RA-8 (0x%08llX).", static_cast<unsigned long long>((g.ra - 8) & 0xFFFFFFFFLL))));
            row.d->set("ended_run", Val::boolean(g.ended_run));
            if (g.has_suppressed) {
                row.d->set("suppressed_after", Val::integer(g.suppressed_after));
                row.d->set("calls", Val::integer(std::max(calls, g.suppressed_after + 1)));
            }
        }
        if (g.kind == "missing") row.d->set("code_region", g.has_code_region ? Val::str(g.code_region) : Val::none());
        rows.push_back(row);
    }
    const Milestones& m = scan.ms;
    List mods; for (const auto& x : m.modules) mods.push_back(dict_of({{"op", Val::str(x.op)}, {"id", Val::integer(x.id)}, {"path", Val::str(x.path)}}));
    const Val ms = dict_of({{"argv_boot", Val::boolean(m.argv_boot)}, {"elf_entry", hexs(m.has_entry, m.elf_entry)},
                            {"overrides_applied", str_list(m.overrides_applied)}, {"modules", Val::list(std::move(mods))},
                            {"ended_with", m.has_end ? Val::str(m.ended_with) : Val::none()}});
    return dict_of({{"logs", str_list(scan.files)}, {"lines", Val::integer(scan.lines)}, {"repo", kb.has_repo ? Val::str(kb.repo) : Val::none()},
                    {"issues", Val::list(std::move(rows))}, {"milestones", ms}});
}

inline std::string md_cell(const std::string& s) { return replace_all(replace_all(s, "|", "\\|"), "\n", " "); }
inline std::string or_dash(const Val& v) { return v.is_none() || (v.t == Val::STR && v.s.empty()) ? "-" : v.s; }

inline void print_markdown(const Val& rep) {
    out("| # | Kind | What | Calls | First PC | First RA | Fix tool | Where | Status |");
    out("| --- | --- | --- | --- | --- | --- | --- | --- | --- |");
    for (const auto& r : get(rep, "issues").l ? *get(rep, "issues").l : List{})
        out(cfmt("| %lld | ", get(r, "rank").i) + S(r, "kind") + " | " + md_cell(S(r, "what")) + cfmt(" | %lld | ", get(r, "calls").i) + or_dash(get(r, "first_pc")) + " | " +
            or_dash(get(r, "first_ra")) + " | " + md_cell(S(r, "fix_tool")) + " | " + md_cell(S(r, "where")) + " | open |");
}

inline void print_text(const Val& rep, long long limit) {
    const Val ms = get(rep, "milestones");
    const List& logs = *get(rep, "logs").l;
    const List& issues = *get(rep, "issues").l;
    out(cfmt("Read %lld lines from %zu log(s)", get(rep, "lines").i, logs.size()) +
        (!get(rep, "repo").is_none() ? "; handler names from " + S(rep, "repo") : std::string("; no --repo: built-in table and name guesses")));
    std::vector<std::string> boot;
    if (B(ms, "argv_boot")) boot.push_back("argv boot path");
    if (!get(ms, "elf_entry").is_none()) boot.push_back("ELF loaded, entry " + S(ms, "elf_entry"));
    if (!get(ms, "overrides_applied").l->empty()) { std::vector<std::string> o; for (const auto& x : *get(ms, "overrides_applied").l) o.push_back(x.s); boot.push_back("overrides applied: " + join(o, ", ")); }
    if (!get(ms, "modules").l->empty()) boot.push_back(cfmt("%zu IOP module loads", get(ms, "modules").l->size()));
    out("Boot: " + (boot.empty() ? std::string("no boot milestones seen") : join(boot, "; ")));
    if (!get(ms, "ended_with").is_none() && !S(ms, "ended_with").empty()) out("Run ended with: " + S(ms, "ended_with"));
    if (issues.empty()) { out("No stubs, syscalls, missing functions or IOP gaps in these logs."); return; }
    out(cfmt("%zu issue(s), in the order they first appeared (fix the first one first):", issues.size()));
    // issues[:limit] with Python slicing
    const long long n = static_cast<long long>(issues.size());
    long long stop = limit < 0 ? std::max(0LL, n + limit) : std::min(limit, n);
    for (long long k = 0; k < stop; ++k) {
        const Val& r = issues[static_cast<std::size_t>(k)];
        out("");
        const Val* er = r.d->get("ended_run");
        const std::string flag = er && truthy(*er) ? "  [ended the run]" : "";
        const long long calls = get(r, "calls").i;
        out(cfmt("%lld. ", get(r, "rank").i) + S(r, "kind") + ": " + S(r, "what") + cfmt("  (%lld call", calls) + (calls == 1 ? "" : "s") + ", first at " + S(r, "first") + ")" + flag);
        if (!get(r, "first_pc").is_none() || !get(r, "first_ra").is_none()) out("   PC " + or_dash(get(r, "first_pc")) + "  RA " + or_dash(get(r, "first_ra")));
        out("   Fix tool: " + S(r, "fix_tool") + "  ->  " + S(r, "where"));
        out("   " + S(r, "how"));
    }
    if (n > limit) out(cfmt("\n... %lld more (use --limit or --json)", n - limit));
}

inline int cmd_scan(const Val& a) {
    const Knowledge kb(S(a, "repo"), !get(a, "repo").is_none());
    std::vector<std::string> logs; for (const auto& v : *get(a, "logs").l) logs.push_back(v.s);
    const Val rep = build_report(logs, kb);
    if (B(a, "json")) out(dumps2(rep));
    else if (B(a, "markdown")) print_markdown(rep);
    else print_text(rep, get(a, "limit").i);
    return get(rep, "issues").l->empty() ? 0 : 1;
}

inline bool lower_less(const std::string& x, const std::string& y) { const std::string a = lower(x), b = lower(y); return a != b ? a < b : x < y; }
inline int cmd_handlers(const Val& a) {
    const Knowledge kb(S(a, "repo"), true);
    std::vector<std::string> order; std::map<std::string, std::vector<long long>> by_name;
    for (const auto& [n, name] : kb.dispatch.items) { if (!by_name.count(name)) order.push_back(name); by_name[name].push_back(n); }
    const auto numbers = [&](const std::string& name) {
        std::vector<long long> v = by_name.count(name) ? by_name.at(name) : std::vector<long long>{};
        std::stable_sort(v.begin(), v.end(), [](long long x, long long y) { return std::llabs(x) < std::llabs(y); });
        List l; for (const long long n : v) l.push_back(Val::str(fmt_syscall(n))); return l;
    };
    const auto file_of = [&](const std::string& name) { const auto it = kb.defined.find(name); return it == kb.defined.end() ? Val::none() : Val::str(RT + it->second); };
    List rows;
    const auto add = [&](const std::string& name, const std::string& kind) {
        rows.push_back(dict_of({{"name", Val::str(name)}, {"kind", Val::str(kind)}, {"numbers", Val::list(numbers(name))}, {"file", file_of(name)},
                                {"todo", Val::boolean(kb.todo.count(name) > 0)}}));
    };
    for (const auto& [kind, names] : {std::make_pair(std::string("syscall"), &kb.syscall_names), std::make_pair(std::string("stub"), &kb.stub_names)}) {
        std::vector<std::string> v(names->begin(), names->end()); std::stable_sort(v.begin(), v.end(), lower_less);
        for (const auto& name : v) add(name, kind);
    }
    std::vector<std::string> extra;
    for (const auto& name : order) if (!kb.syscall_names.count(name) && !kb.stub_names.count(name)) extra.push_back(name);
    std::stable_sort(extra.begin(), extra.end(), lower_less);
    for (const auto& name : extra) add(name, "dispatch-only");
    const std::string grep = S(a, "grep");
    const bool has_grep = !get(a, "grep").is_none() && !grep.empty();
    List keep;
    for (const auto& r : rows) {
        if (has_grep && lower(S(r, "name")).find(lower(grep)) == std::string::npos) continue;
        if (S(a, "kind") != "all" && S(r, "kind") != S(a, "kind")) continue;
        if (B(a, "todo") && !B(r, "todo")) continue;
        keep.push_back(r);
    }
    if (B(a, "json")) { out(dumps2(Val::list(keep))); return 0; }
    for (const auto& r : keep) {
        std::vector<std::string> nums; for (const auto& x : *get(r, "numbers").l) nums.push_back(x.s);
        const std::string ns = nums.empty() ? "-" : join(nums, ", ");
        out(cfmt("%-34s %-13s %-28s ", S(r, "name").c_str(), S(r, "kind").c_str(), ns.c_str()) + (get(r, "file").is_none() ? std::string("-") : S(r, "file")) + (B(r, "todo") ? "  (TODO)" : ""));
    }
    out(cfmt("%zu handler(s)", keep.size()) + (has_grep ? " matching " + str_repr(grep) : std::string()));
    return 0;
}

inline int cmd_syscall(const Val& a) {
    const long long n = signed_syscall(parse_int(S(a, "number")));
    const Knowledge kb(S(a, "repo"), !get(a, "repo").is_none());
    const auto [builtin, repo_name] = kb.syscall_name(n);
    const Val o = dict_of({{"number", Val::integer(n)}, {"display", Val::str(fmt_syscall(n))}, {"builtin_name", builtin.empty() ? Val::none() : Val::str(builtin)},
                           {"dispatcher", kb.has_repo && !repo_name.empty() ? Val::str(repo_name) : Val::none()}, {"repo", kb.has_repo ? Val::str(kb.repo) : Val::none()}});
    if (B(a, "json")) out(dumps2(o));
    else {
        out("Syscall      " + fmt_syscall(n));
        out("Standard     " + (!builtin.empty() ? builtin + " (built-in EE kernel table)" : std::string("not in the built-in table")));
        if (kb.has_repo) out("Runtime      " + (!repo_name.empty() ? "dispatched to " + repo_name + " in Dispatcher.cpp" : std::string("no case in Dispatcher.cpp: the runtime logs [Syscall TODO] and returns 0")));
        else out("Runtime      pass --repo <PS2Recomp> to check Dispatcher.cpp");
    }
    return (!builtin.empty() || !repo_name.empty()) ? 0 : 1;
}

inline int main(const std::vector<std::string>& argv0) {
    std::vector<std::string> argv = argv0;
    // let a negative syscall number through argparse ("syscall -0x1A"): move it after "--"
    if (!argv.empty() && argv[0] == "syscall" && std::find(argv.begin(), argv.end(), "--") == argv.end()) {
        std::vector<std::size_t> neg;
        for (std::size_t k = 1; k < argv.size(); ++k) if (re::match(R"(^-(0[xX])?[0-9A-Fa-f]+$)", argv[k])) neg.push_back(k);
        if (neg.size() == 1) {
            const std::string v = argv[neg[0]];
            std::vector<std::string> rest; for (std::size_t k = 1; k < argv.size(); ++k) if (k != neg[0]) rest.push_back(argv[k]);
            argv = {"syscall"}; argv.insert(argv.end(), rest.begin(), rest.end()); argv.push_back("--"); argv.push_back(v);
        }
    }
    Parser p(PROG, "Turn PS2Recomp runtime logs into ranked backend fixes; list the runtime's handlers and syscalls.");
    p.subcommands("cmd", true); p.sub_metavar = "COMMAND";
    { Parser& s = p.sub("scan"); s.help_line = "parse runtime logs and suggest the fix tool for each issue";
      s.pos("logs").nargs("+").help("runtime log files (stdout+stderr of ps2EntryRunner)");
      s.opt({"--repo"}).metavar("PATH").help("PS2Recomp checkout: real handler names and syscall numbers");
      s.opt({"--json"}).store_true().help("machine-readable output");
      s.opt({"--markdown"}).store_true().help("a table to paste into console/STATE.md");
      s.opt({"--limit"}).integer().def(Val::integer(25)).help("issues to print in text mode (default 25)"); }
    { Parser& s = p.sub("handlers"); s.help_line = "list runtime handlers, their syscall numbers and files";
      s.opt({"--repo"}).metavar("PATH").required().help("PS2Recomp checkout");
      s.opt({"--kind"}).choices({"all", "syscall", "stub", "dispatch-only"}).def("all");
      s.opt({"--grep"}).metavar("TEXT").help("only names containing TEXT (case-insensitive)");
      s.opt({"--todo"}).store_true().help("only handlers that are still TODO_NAMED stubs");
      s.opt({"--json"}).store_true().help("machine-readable output"); }
    { Parser& s = p.sub("syscall"); s.help_line = "name an EE kernel syscall number (0x3C, 60, -0x1A, 0xFFFFFFE6)";
      s.pos("number").help("the syscall number");
      s.opt({"--repo"}).metavar("PATH").help("PS2Recomp checkout: also check Dispatcher.cpp");
      s.opt({"--json"}).store_true().help("machine-readable output"); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    return guard(PROG, [&]() -> int {
        if (cmd == "scan") return cmd_scan(a);
        if (cmd == "handlers") return cmd_handlers(a);
        return cmd_syscall(a);
    });
}
PACKTOOL("console-backend/stubs", main);
}  // namespace packtools::console_stubs
