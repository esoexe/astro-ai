// console_state.inc - console-reverse/state: a C++ port of console-skill/console-reverse/state.py (Jake Schincariol, MIT).
// Keeps console/STATE.md, the project memory every /console-* skill reads first: detect, init, design, show, set,
// phase, log, strike. Never lists the generated runner folder (it only counts its .cpp files). Same text, files and
// exit codes; the templates come from the skill folder (templates/STATE.md, templates/design.md).
#if !defined(_WIN32)
#include <sys/utsname.h>
#include <unistd.h>
#endif
namespace packtools::console_state {
using namespace pyrt;
using namespace packtools::console_common;

const char* const PROG = "state";
const std::string NOT_SET = "(not set)";

struct Phase { const char* name; const char* short_; const char* done; };
const Phase PHASES[] = {
    {"PHASE_SETUP", "toolchain", "PS2Recomp builds here (ps2_recomp, ps2_analyzer, ps2EntryRunner) and both workspaces are recorded"},
    {"PHASE_EXTRACT", "disc and ELF", "the disc is extracted (console/disc), the boot ELF is found, elf.py info is recorded and console/design.md is started"},
    {"PHASE_FUNCTION_MAP", "function map", "Ghidra exported the function map (or ps2_analyzer wrote a TOML) and the TOML is in console/"},
    {"PHASE_RECOMPILE", "recompile", "ps2_recomp finished, its report is read (unhandled instructions, warnings, errors) and the generated files are in the runtime"},
    {"PHASE_BUILD_RUN", "build and boot", "ps2EntryRunner builds with exit code 0 and the run gets past boot (first file reads or first frames)"},
    {"PHASE_IO_MENUS", "I/O and menus", "the game reads its files, reaches its menus and takes input; temporary ret0/ret1 stubs are replaced by real code"},
};
constexpr int NPHASES = 6;

// key -> label in templates/STATE.md ("- Label: value")
const std::vector<std::pair<std::string, std::string>> FIELDS = {
    {"title", "Title"}, {"serial", "Serial"}, {"region", "Region"}, {"elf_name", "Boot ELF name"},
    {"workspace", "Game workspace"}, {"console_dir", "Console folder"}, {"disc_image", "Disc image"},
    {"disc_dir", "Extracted disc (cdRoot)"}, {"elf_path", "Boot ELF path"},
    {"ps2recomp", "PS2Recomp checkout"}, {"toml", "Recompiler config (TOML)"},
    {"ghidra_csv", "Ghidra function map (CSV)"}, {"recomp_output", "Generated C++ (TOML output)"},
    {"host", "Host"}, {"build_dir", "Build dir"}, {"generator", "Generator"}, {"compiler", "C++ compiler"},
    {"build_type", "Build type"}, {"runner_exe", "Runner executable"},
    {"runner_cpp_count", "Runner .cpp count (count only, never list)"}, {"phase", "Phase"},
};
inline std::string label_of(const std::string& key) { for (const auto& [k, l] : FIELDS) if (k == key) return l; return key; }
inline std::vector<std::string> settable() { std::vector<std::string> v; for (const auto& [k, l] : FIELDS) if (k != "phase") v.push_back(k); v.push_back("runner_cmd"); return v; }

inline std::optional<std::string> region_of(const std::string& pre) {
    static const std::map<std::string, std::string> R{{"SLUS", "USA"}, {"SCUS", "USA"}, {"SLES", "Europe"}, {"SCES", "Europe"},
        {"SLPS", "Japan"}, {"SLPM", "Japan"}, {"SCPS", "Japan"}, {"SLKA", "Korea"}, {"SCKA", "Korea"}, {"SCAJ", "Asia"}};
    const auto it = R.find(pre); if (it == R.end()) return std::nullopt; return it->second;
}

const char* const GITIGNORE = "# Written by console-reverse state.py. Game data, and anything derived from it, stays on this machine.\n"
    "# Only your own patches, overrides, notes and art belong in a repository.\n"
    "disc/\noutput/\ntextures/\naudio/\nlevels/\nsaves/\nruns/\ndist/\n"
    "*.iso\n*.bin\n*.img\n*.mdf\n*.nrg\n*.chd\n*.cso\n*.elf\n*.irx\nSL??_*\nSC??_*\n";

const char* const DISC_IMAGE_EXT[] = {".iso", ".bin", ".img", ".mdf", ".nrg", ".chd", ".cso"};
inline bool skip_dir(const std::string& e) {
    static const std::set<std::string> S{"runner", "node_modules", ".git", "out", "build", "build64", "outvita", "__pycache__"};
    return S.count(e) > 0;
}

// ---------------------------------------------------------------- small helpers
inline std::string template_path(const char* name) { return abspath(skill_file(std::string("templates/") + name)); }
inline std::string cwd() { std::error_code ec; return u8(std::filesystem::current_path(ec)); }
inline std::string now_stamp() { const std::string t = now_iso(); return t.substr(0, 10) + " " + t.substr(11, 5); }   // %Y-%m-%d %H:%M
inline std::string newlines_u(std::string s) {   // text mode's universal newlines: \r\n and \r read as \n
    std::string o; o.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) { if (s[i] == '\r') { o += '\n'; if (i + 1 < s.size() && s[i + 1] == '\n') ++i; } else o += s[i]; }
    return o;
}
inline std::optional<std::string> read_text(const std::string& p) {   // open(p, encoding="utf-8", errors="replace").read()
    std::FILE* f = open_file(p, "rb"); if (!f) return std::nullopt;
    std::string d; char buf[65536]; std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) d.append(buf, n);
    std::fclose(f);
    return newlines_u(decode_utf8_replace(d));
}
inline void write_text(const std::string& p, const std::string& text) {   // newline="\n"
    std::FILE* f = open_file(p, "wb"); if (!f) throw OSError(errno, p);
    std::fwrite(text.data(), 1, text.size(), f); std::fclose(f);
}
inline std::string join_ws(const std::string& s) { return join(split_ws(s), " "); }   // " ".join(s.split())
inline long long cplen(const std::string& s) { return static_cast<long long>(cps(s).size()); }
inline std::string cs(const std::string& s, long a, long b) { return cpslice(s, a, b); }
inline std::string cs(const std::string& s, long a) { return cpslice(s, a); }
inline std::string rstrip_nl(std::string s) { while (!s.empty() && s.back() == '\n') s.pop_back(); return s; }
inline long long int_of(const std::string& digits) {   // int() of \d+ text (any Unicode decimal digits)
    long long v = 0;
    for (const auto& ch : cps(digits)) {
        uint32_t c = cp_of(ch), k = 0;
        while (k < c && uprop::decimal(c - k - 1)) ++k;
        v = std::min<long long>(v * 10 + static_cast<long long>(k % 10), 1000000000000000LL);   // capped: only 0..5 matter
    }
    return v;
}

// shutil.which(name)
inline std::optional<std::string> which(const std::string& cmd) {
    auto runnable = [](const std::string& p) {
        if (!exists(p) || is_dir(p)) return false;
#if defined(_WIN32)
        return true;
#else
        return ::access(p.c_str(), X_OK) == 0;
#endif
    };
#if defined(_WIN32)
    const char* pe = std::getenv("PATHEXT");
    std::vector<std::string> exts; for (const auto& e : split(pe && *pe ? pe : ".COM;.EXE;.BAT;.CMD;.VBS;.JS;.WS;.MSC", ";")) if (!e.empty()) exts.push_back(e);
    std::vector<std::string> files;
    bool has_ext = false; for (const auto& e : exts) if (lower(cmd).size() >= e.size() && lower(cmd).compare(lower(cmd).size() - e.size(), e.size(), lower(e)) == 0) has_ext = true;
    if (has_ext) files.push_back(cmd);
    for (const auto& e : exts) files.push_back(cmd + e);
    if (cmd.find('/') != std::string::npos || cmd.find('\\') != std::string::npos) { for (const auto& f : files) if (runnable(f)) return f; return std::nullopt; }
    const char* pv = std::getenv("PATH");
    std::vector<std::string> dirs;
    if (!std::getenv("NoDefaultCurrentDirectoryInExePath")) dirs.push_back(".");
    for (const auto& d : split(pv ? pv : "", ";")) dirs.push_back(d);
    std::set<std::string> seen;
    for (const auto& d : dirs) {
        if (!seen.insert(lower(d)).second) continue;
        for (const auto& f : files) { const std::string p = d.empty() ? f : (d.back() == '\\' || d.back() == '/' ? d + f : d + "\\" + f); if (runnable(p)) return p; }
    }
    return std::nullopt;
#else
    if (cmd.find('/') != std::string::npos) { if (runnable(cmd)) return cmd; return std::nullopt; }
    const char* pv = std::getenv("PATH");
    const std::string path = pv ? pv : "/bin:/usr/bin";
    if (path.empty()) return std::nullopt;
    std::set<std::string> seen;
    for (const auto& d : split(path, ":")) {
        if (!seen.insert(d).second) continue;
        const std::string p = path_join(d, cmd);
        if (runnable(p)) return p;
    }
    return std::nullopt;
#endif
}

inline std::string host_name() {   // "%s %s" % (platform.system() or "unknown", platform.machine() or "")
#if defined(_WIN32)
    const char* m = std::getenv("PROCESSOR_ARCHITEW6432"); if (!m || !*m) m = std::getenv("PROCESSOR_ARCHITECTURE");
    return std::string("Windows ") + (m ? m : "");
#else
    struct utsname u {};
    if (::uname(&u) != 0) return "unknown ";
    return std::string(*u.sysname ? u.sysname : "unknown") + " " + u.machine;
#endif
}

// ---------------------------------------------------------------- locating files
inline std::string find_state(const std::string& workspace) {
    if (!workspace.empty()) return path_join(path_join(abspath(workspace), "console"), "STATE.md");
    std::string cur = cwd();
    while (true) {
        const std::string cand = path_join(path_join(cur, "console"), "STATE.md");
        if (is_file(cand)) return cand;
        if (basename(cur) == "console" && is_file(path_join(cur, "STATE.md"))) return path_join(cur, "STATE.md");
        const std::string parent = dirname(cur);
        if (parent == cur) return path_join(path_join(cwd(), "console"), "STATE.md");
        cur = parent;
    }
}
inline std::string read_state(const std::string& path) {
    if (!is_file(path)) throw InputError("no state file at " + path + ": run `state.py init` first");
    auto t = read_text(path);
    if (!t) throw OSError(errno, path);
    return *t;
}
inline void write_state(const std::string& path, const std::string& text) {
    const std::string tmp = path + ".tmp";
    write_text(tmp, text);
    replace_file(tmp, path);
}
inline std::optional<std::string> find_ci(const std::string& dir, const std::string& name) {
    std::vector<std::string> names;
    try { names = listdir(dir); } catch (const std::exception&) { return std::nullopt; }
    const std::string low = lower(name);
    for (const auto& n : names) if (lower(n) == low) return path_join(dir, n);
    return std::nullopt;
}

// ---------------------------------------------------------------- detection
struct Cnf { std::string boot2, elf_name; std::optional<std::string> serial, region; };
inline std::optional<Cnf> parse_cnf(const std::string& path) {
    std::FILE* f = open_file(path, "rb"); if (!f) return std::nullopt;
    std::string raw(65536, '\0'); raw.resize(std::fread(raw.data(), 1, raw.size(), f)); std::fclose(f);
    std::string boot2;
    for (const auto& line : splitlines(latin1(raw))) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (upper(strip(line.substr(0, eq))) == "BOOT2") boot2 = strip(line.substr(eq + 1));
    }
    if (boot2.empty()) return std::nullopt;
    std::string leaf = re::split(R"([\\/:])", boot2).back();
    leaf = re::sub(R"(;\d+$)", "", leaf);
    Cnf c; c.boot2 = boot2; c.elf_name = leaf;
    if (const auto m = re::match(R"(^([A-Za-z]{4})_(\d{3})\.(\d{2})$)", leaf)) {
        c.serial = upper(m.group(1)) + "-" + m.group(2) + m.group(3);
        c.region = region_of(cs(*c.serial, 0, 4));
    }
    return c;
}
inline std::vector<std::string> sorted_listdir(const std::string& d) { auto v = listdir(d); std::sort(v.begin(), v.end()); return v; }
inline std::vector<std::string> shallow_dirs(const std::string& base) {
    std::vector<std::string> o;
    try {
        for (const auto& e : sorted_listdir(base)) {
            const std::string p = path_join(base, e);
            if (is_dir(p) && e.rfind(".", 0) != 0 && !skip_dir(e)) o.push_back(p);
        }
    } catch (const std::exception&) {}
    return o;
}
inline std::map<std::string, std::string> read_cmake_cache(const std::string& path) {
    std::map<std::string, std::string> vals;
    const auto t = read_text(path); if (!t) return vals;
    static const std::set<std::string> WANT{"CMAKE_GENERATOR", "CMAKE_CXX_COMPILER", "CMAKE_BUILD_TYPE", "CMAKE_CONFIGURATION_TYPES", "PS2X_DEFAULT_BOOT_ELF"};
    for (const auto& line : split(*t, "\n")) {
        const auto m = re::match(R"(^([A-Za-z_][A-Za-z0-9_]*):[A-Z]+=(.*)$)", line);
        if (m && WANT.count(m.group(1))) vals[m.group(1)] = m.group(2);
    }
    return vals;
}
inline std::vector<std::pair<std::string, std::string>> read_toml_paths(const std::string& path) {   // output / ghidra_output basic strings
    std::vector<std::pair<std::string, std::string>> keys;
    const auto t = read_text(path); if (!t) return keys;
    auto lines = split(*t, "\n");
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string line = lines[i] + (i + 1 < lines.size() ? "\n" : "");
        const auto m = re::match(R"re(^\s*(output|ghidra_output)\s*=\s*"((?:[^"\\]|\\.)*)")re", line);
        if (!m) continue;
        bool have = false; for (const auto& kv : keys) if (kv.first == m.group(1)) have = true;
        if (!have) keys.emplace_back(m.group(1), re::sub(R"(\\(.))", R"(\1)", m.group(2)));
    }
    return keys;
}
inline Val key_of(const std::vector<std::pair<std::string, std::string>>& kv, const std::string& k) { for (const auto& [a, b] : kv) if (a == k) return Val::str(b); return Val::none(); }
inline bool is_ps2recomp(const std::string& p) { return !p.empty() && is_file(path_join(path_join(p, "ps2xRuntime"), "CMakeLists.txt")) && is_dir(path_join(p, "ps2xRecomp")); }
inline Val count_cpp(const std::string& folder) {   // count, never list (runner/ can hold 30,000+ files)
    std::error_code ec; std::filesystem::directory_iterator it(fspath(folder), ec);
    if (ec) return Val::none();
    long long n = 0;
    for (; it != std::filesystem::directory_iterator(); it.increment(ec)) {
        if (ec) return Val::none();
        const std::string name = u8(it->path().filename());
        if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".cpp") == 0) ++n;
    }
    return Val::integer(n);
}
inline Val find_binary(const std::string& build, const std::string& sub, const std::string& name) {
    const std::string base = path_join(build, sub);
    std::vector<std::string> c{path_join(base, name), path_join(base, name + ".exe")};
    for (const char* cfg : {"Debug", "Release", "RelWithDebInfo", "MinSizeRel"}) { c.push_back(path_join(path_join(base, cfg), name + ".exe")); c.push_back(path_join(path_join(base, cfg), name)); }
    for (const auto& x : c) if (is_file(x)) return Val::str(x);
    return Val::none();
}
inline bool ends_with_ci(const std::string& s, const char* suf) { const std::string l = lower(s); const std::size_t n = std::strlen(suf); return l.size() >= n && l.compare(l.size() - n, n, suf) == 0; }

inline Val detect(const std::string& workspace, const std::string& ps2recomp) {
    const std::string ws = abspath(workspace.empty() ? cwd() : workspace);
    const std::string console = path_join(ws, "console");
    Val r = dict_of({{"workspace", Val::str(ws)}, {"console_dir", Val::str(console)}, {"state_exists", Val::boolean(is_file(path_join(console, "STATE.md")))},
        {"host", Val::str(host_name())}, {"disc_dir", Val::none()}, {"elf_name", Val::none()}, {"elf_path", Val::none()}, {"serial", Val::none()}, {"region", Val::none()},
        {"disc_images", Val::list({})}, {"tomls", Val::list({})}, {"ghidra_csvs", Val::list({})},
        {"ps2recomp", Val::none()}, {"build_dirs", Val::list({})}, {"build_dir", Val::none()}, {"generator", Val::none()}, {"compiler", Val::none()},
        {"build_type", Val::none()}, {"runner_exe", Val::none()}, {"ps2_recomp", Val::none()}, {"ps2_analyzer", Val::none()},
        {"runner_cpp_count", Val::none()}, {"tools", Val::dict()}, {"notes", Val::list({})}});
    auto L = [&](const char* k) -> List& { return *get(r, k).l; };
    auto note = [&](const std::string& s) { L("notes").push_back(Val::str(s)); };
    // extracted disc: SYSTEM.CNF in console/disc, the workspace, console/, or one level down
    std::vector<std::string> cands{path_join(console, "disc"), ws, console};
    for (const auto& d : shallow_dirs(ws)) cands.push_back(d);
    for (const auto& d : shallow_dirs(console)) cands.push_back(d);
    std::set<std::string> seen;
    for (const auto& d : cands) {
        if (seen.count(d) || !is_dir(d)) continue;
        seen.insert(d);
        const auto cnf = find_ci(d, "SYSTEM.CNF");
        if (cnf && is_file(*cnf)) {
            if (const auto info = parse_cnf(*cnf)) {
                r.d->set("disc_dir", Val::str(d)); r.d->set("elf_name", Val::str(info->elf_name));
                r.d->set("serial", info->serial ? Val::str(*info->serial) : Val::none()); r.d->set("region", info->region ? Val::str(*info->region) : Val::none());
                const auto elf = find_ci(d, info->elf_name);
                if (elf && is_file(*elf)) r.d->set("elf_path", Val::str(*elf));
                break;
            }
        }
    }
    for (const auto& d : {ws, console}) {
        std::vector<std::string> names;
        try { names = sorted_listdir(d); } catch (const std::exception&) { continue; }
        for (const auto& e : names) {
            const std::string p = path_join(d, e);
            if (!is_file(p)) continue;
            bool img = false; for (const char* x : DISC_IMAGE_EXT) if (ends_with_ci(e, x)) img = true;
            if (img) L("disc_images").push_back(Val::str(p));
            else if (d == console && ends_with_ci(e, ".toml")) L("tomls").push_back(Val::str(p));
            else if (d == console && ends_with_ci(e, ".csv")) {
                if (const auto t = read_text(p)) {
                    const std::size_t nl = t->find('\n');
                    const std::string first = nl == std::string::npos ? *t : t->substr(0, nl + 1);
                    if (lower(strip(replace_all(first, " ", ""))) == "name,start,end,size") L("ghidra_csvs").push_back(Val::str(p));
                }
            }
        }
    }
    r.d->set("recomp_output", Val::none());
    if (!L("tomls").empty()) {
        const auto keys = read_toml_paths(L("tomls")[0].s);
        r.d->set("recomp_output", key_of(keys, "output"));
        const Val g = key_of(keys, "ghidra_output");
        if (L("ghidra_csvs").empty() && truthy(g)) L("ghidra_csvs").push_back(g);
    }
    // PS2Recomp checkout
    std::vector<std::string> guesses;
    if (!ps2recomp.empty()) guesses = {ps2recomp};   // the user named it: check that one only
    else {
        for (const auto& base : {ws, dirname(ws)}) for (const char* n : {"PS2Recomp", "ps2recomp", "PS2RECOMP"}) guesses.push_back(path_join(base, n));
        guesses.push_back(ws);
    }
    for (const auto& g : guesses) if (!g.empty() && is_ps2recomp(g)) { r.d->set("ps2recomp", Val::str(abspath(g))); break; }
    if (!ps2recomp.empty() && get(r, "ps2recomp").is_none()) note(ps2recomp + " is not a PS2Recomp checkout (no ps2xRuntime/CMakeLists.txt)");
    if (!get(r, "ps2recomp").is_none()) {
        const std::string repo = get(r, "ps2recomp").s;
        std::vector<std::string> builds;
        for (const char* rel : {"out/build", "build", "build64", "out", "outvita"}) {
            std::string p = repo; for (const auto& part : split(rel, "/")) p = path_join(p, part);
            if (is_file(path_join(p, "CMakeCache.txt"))) builds.push_back(p);
            if (std::string(rel) == "out/build" && is_dir(p))
                for (const auto& sub : shallow_dirs(p)) if (is_file(path_join(sub, "CMakeCache.txt"))) builds.push_back(sub);
        }
        r.d->set("build_dirs", str_list(builds));
        if (!builds.empty()) {
            const std::string b = builds[0];
            const auto cache = read_cmake_cache(path_join(b, "CMakeCache.txt"));
            auto cg = [&](const char* k) { const auto it = cache.find(k); return it == cache.end() ? Val::none() : Val::str(it->second); };
            r.d->set("build_dir", Val::str(b)); r.d->set("generator", cg("CMAKE_GENERATOR")); r.d->set("compiler", cg("CMAKE_CXX_COMPILER"));
            const Val bt = cg("CMAKE_BUILD_TYPE"), ct = cg("CMAKE_CONFIGURATION_TYPES");
            r.d->set("build_type", truthy(bt) ? bt : truthy(ct) ? Val::str("multi-config (" + ct.s + ")") : Val::none());
            r.d->set("runner_exe", find_binary(b, "ps2xRuntime", "ps2EntryRunner"));
            r.d->set("ps2_recomp", find_binary(b, "ps2xRecomp", "ps2_recomp"));
            r.d->set("ps2_analyzer", find_binary(b, "ps2xAnalyzer", "ps2_analyzer"));
            if (builds.size() > 1) note("more than one build dir: " + join(builds, ", ") + ". Ask which one is real, never delete one.");
        } else {
            note("no configured build dir yet (no CMakeCache.txt): Phase 0 configures one");
        }
        r.d->set("runner_cpp_count", count_cpp(path_join(path_join(path_join(repo, "ps2xRuntime"), "src"), "runner")));
    }
    for (const char* tool : {"cmake", "ninja", "git", "c++", "clang++", "g++", "clang-cl", "cl", "pkg-config", "xcrun"}) {
        const auto w = which(tool); get(r, "tools").d->set(tool, w ? Val::str(*w) : Val::none());
    }
#if defined(_WIN32)
    { const char* vs = std::getenv("VSINSTALLDIR"); if ((!vs || !*vs) && !which("cl")) note("Windows: no Visual Studio developer environment (VSINSTALLDIR unset, cl not on PATH)"); }
#endif
    return r;
}
inline std::optional<std::string> runner_cmd_for(const Val& d) {
    if (truthy(get(d, "runner_exe")) && truthy(get(d, "elf_path"))) return "\"" + get(d, "runner_exe").s + "\" \"" + get(d, "elf_path").s + "\"";
    return std::nullopt;
}

// ---------------------------------------------------------------- state edits (positions are code points, like str)
inline std::string set_field(const std::string& text, const std::string& key, const std::string& value) {
    if (key == "runner_cmd") {
        static const std::string P = "(## Active runner command\\n```\\n)(.*?)(\\n```)";
        const re::Regex& pat = re::compile(P, re::S);
        if (!pat.search(text)) throw InputError("STATE.md has no 'Active runner command' block");
        return pat.sub_fn([&](const re::Match& m) { return m.group(1) + value + m.group(3); }, text, 1);
    }
    const std::string label = label_of(key);
    const std::string P = "^- " + re::escape(label) + ": .*$";
    const re::Regex& pat = re::compile(P, re::M);
    if (!pat.search(text)) throw InputError("STATE.md has no '- " + label + ":' line");
    return pat.sub_fn([&](const re::Match&) { return "- " + label + ": " + value; }, text, 1);
}
inline Val get_fields(const std::string& text) {
    Val o = Val::dict();
    for (const auto& [key, label] : FIELDS) {
        const auto m = re::search("^- " + re::escape(label) + ": (.*)$", text, re::M);
        o.d->set(key, m ? Val::str(strip(m.group(1))) : Val::none());
    }
    const auto m = re::search("## Active runner command\\n```\\n(.*?)\\n```", text, re::S);
    o.d->set("runner_cmd", m ? Val::str(m.group(1)) : Val::none());
    return o;
}
struct Bounds { long start, body, end; };
inline std::optional<Bounds> section_bounds(const std::string& text, const std::string& name) {
    const auto m = re::search("^## " + re::escape(name) + "[^\\n]*\\n", text, re::M | re::I);
    if (!m) return std::nullopt;
    const auto nxt = re::search("^## ", cs(text, m.end()), re::M);
    const long end = nxt ? m.end() + nxt.start() : static_cast<long>(cplen(text));
    return Bounds{m.start(), m.end(), end};
}
inline std::string append_to_section(std::string text, const std::string& name, const std::string& line) {
    auto b = section_bounds(text, name);
    if (!b) { text = rstrip_nl(text) + "\n\n## " + name + "\n"; b = section_bounds(text, name); }
    std::string body = rstrip_nl(cs(text, b->body, b->end));
    body = (body.empty() ? "" : body + "\n") + line + "\n";
    const std::string tail = cs(text, b->end);
    if (!tail.empty()) body += "\n";
    return cs(text, 0, b->body) + body + tail;
}
inline std::string add_log(const std::string& text, const std::string& message) {
    return append_to_section(text, "Session log", "- " + now_stamp() + "  " + join_ws(message));
}
const char* const STRIKE_RE = R"(^- \[(?P<key>[^\]\n]+)\] (?P<n>\d+)/3 strikes?; last: (?P<note>.*)$)";

inline int parse_phase(const std::string& value) {
    std::string v = upper(strip(value));
    if (!v.empty() && all_cps(v, uprop::digit)) {
        if (!all_cps(v, uprop::decimal)) throw PyErr("ValueError", "invalid literal for int() with base 10: " + str_repr(v));
        const long long i = int_of(v);
        if (i >= 0 && i < NPHASES) return static_cast<int>(i);
    }
    if (v.rfind("PHASE_", 0) != 0) v = "PHASE_" + v;
    for (int i = 0; i < NPHASES; ++i) if (v == PHASES[i].name) return i;
    std::vector<std::string> names; for (const auto& p : PHASES) names.emplace_back(p.name);
    throw InputError("unknown phase " + str_repr(value) + ": use one of " + join(names, ", ") + " (or 0..5)");
}
inline std::string phase_label(int i) { return cfmt("%s (%d of 5: %s)", PHASES[i].name, i, PHASES[i].short_); }

// ---------------------------------------------------------------- commands
inline int cmd_detect(const Val& a) {
    const Val d = detect(S(a, "workspace"), S(a, "ps2recomp"));
    if (B(a, "json")) { out(dumps2(d)); return 0; }
    auto show = [](const std::string& label, const Val& v) {
        const bool empty = v.is_none() || (v.t == Val::STR && v.s.empty()) || (v.t == Val::LIST && v.l->empty());
        std::string s = label; while (cplen(s) < 22) s += ' ';
        out(s + " " + (empty ? std::string("-") : pystr(v)));
    };
    auto sv = [](const std::string& s) { return Val::str(s); };
    auto joined = [&](const char* k) { std::vector<std::string> v; for (const auto& x : *get(d, k).l) v.push_back(x.s); return sv(join(v, ", ")); };
    show("Game workspace", get(d, "workspace"));
    show("State file", sv(path_join(get(d, "console_dir").s, "STATE.md") + (truthy(get(d, "state_exists")) ? " (exists)" : " (not yet)")));
    show("Host", get(d, "host"));
    show("Extracted disc", get(d, "disc_dir"));
    show("Boot ELF", truthy(get(d, "elf_path")) ? get(d, "elf_path") : get(d, "elf_name"));
    show("Serial", truthy(get(d, "serial")) ? sv(get(d, "serial").s + " (" + (get(d, "region").is_none() ? std::string("None") : get(d, "region").s) + ")") : Val::none());
    show("Disc images", joined("disc_images"));
    show("TOML configs", joined("tomls"));
    show("Ghidra maps", joined("ghidra_csvs"));
    show("Generated C++", get(d, "recomp_output"));
    show("PS2Recomp", get(d, "ps2recomp"));
    show("Build dir", get(d, "build_dir"));
    show("Generator", get(d, "generator"));
    show("C++ compiler", get(d, "compiler"));
    show("Build type", get(d, "build_type"));
    show("ps2_recomp", get(d, "ps2_recomp"));
    show("ps2_analyzer", get(d, "ps2_analyzer"));
    show("ps2EntryRunner", get(d, "runner_exe"));
    show("Runner .cpp files", get(d, "runner_cpp_count"));
    std::vector<std::string> tools;
    for (const auto& [k, v] : get(d, "tools").d->items) if (truthy(v)) tools.push_back(k);
    std::sort(tools.begin(), tools.end());
    show("Tools on PATH", sv(join(tools, ", ")));
    for (const auto& n : *get(d, "notes").l) out("Note: " + n.s);
    std::vector<std::string> missing;
    for (const char* k : {"disc_dir", "elf_path", "ps2recomp", "build_dir"}) if (!truthy(get(d, k))) missing.emplace_back(k);
    if (!missing.empty()) out("Not found: " + join(missing, ", ") + ". Ask the user once, then record it with `state.py set`.");
    return 0;
}

inline int cmd_init(const Val& a) {
    const std::string wsarg = S(a, "workspace");
    const std::string path = !wsarg.empty() ? find_state(wsarg) : path_join(path_join(cwd(), "console"), "STATE.md");
    if (exists(path)) { std::fflush(stdout); std::fprintf(stderr, "%s: %s already exists; read it (`state.py show`) instead of starting over\n", PROG, path.c_str()); return 1; }
    const std::string ws = dirname(dirname(path));
    const Val d = detect(ws, S(a, "ps2recomp"));
    const std::string tpath = template_path("STATE.md");
    const auto tmpl = read_text(tpath);
    if (!tmpl) throw InputError("the template " + tpath + " is missing: reinstall the skill");
    std::string text = *tmpl;
    std::string serial = !S(a, "serial").empty() ? S(a, "serial") : (truthy(get(d, "serial")) ? get(d, "serial").s : std::string());
    if (!serial.empty()) {
        const auto m = re::match(R"(^([A-Za-z]{4})[-_ ]?(\d{3})\.?(\d{2})$)", strip(serial));
        if (!m) throw InputError(str_repr(serial) + " is not a PS2 serial (expected like SLUS-20312)");
        serial = upper(m.group(1)) + "-" + m.group(2) + m.group(3);
    }
    auto ov = [&](const char* k) { const Val v = get(d, k); return truthy(v) ? pystr(v) : NOT_SET; };
    auto first = [&](const char* k) { const auto& l = *get(d, k).l; return l.empty() ? NOT_SET : l[0].s; };
    const auto rc = runner_cmd_for(d);
    const std::vector<std::pair<std::string, std::string>> vals = {
        {"TITLE", !S(a, "game").empty() ? S(a, "game") : NOT_SET},
        {"SERIAL", !serial.empty() ? serial : NOT_SET},
        {"REGION", !serial.empty() ? region_of(cs(serial, 0, 4)).value_or("unknown prefix") : NOT_SET},
        {"ELF_NAME", truthy(get(d, "elf_name")) ? get(d, "elf_name").s : !serial.empty() ? cs(serial, 0, 4) + "_" + cs(serial, 5, 8) + "." + cs(serial, 8) : NOT_SET},
        {"WORKSPACE", ws},
        {"CONSOLE_DIR", path_join(ws, "console")},
        {"DISC_IMAGE", first("disc_images")},
        {"DISC_DIR", ov("disc_dir")},
        {"ELF_PATH", ov("elf_path")},
        {"PS2RECOMP", ov("ps2recomp")},
        {"TOML", first("tomls")},
        {"GHIDRA_CSV", first("ghidra_csvs")},
        {"RECOMP_OUTPUT", ov("recomp_output")},
        {"HOST", get(d, "host").s},
        {"BUILD_DIR", ov("build_dir")},
        {"GENERATOR", ov("generator")},
        {"COMPILER", ov("compiler")},
        {"BUILD_TYPE", ov("build_type")},
        {"RUNNER_EXE", ov("runner_exe")},
        {"RUNNER_CPP_COUNT", get(d, "runner_cpp_count").is_none() ? NOT_SET : pystr(get(d, "runner_cpp_count"))},
        {"PHASE", phase_label(0)},
        {"RUNNER_CMD", rc ? *rc : std::string("(not set: record it with `state.py set runner_cmd \"...\"`)")},
    };
    for (const auto& [k, v] : vals) text = replace_all(text, "{{" + k + "}}", v);
    auto left = re::findall(R"(\{\{[A-Z_]+\}\})", text);
    if (!left.empty()) {
        std::sort(left.begin(), left.end()); left.erase(std::unique(left.begin(), left.end()), left.end());
        throw InputError("template placeholders left unfilled: " + join(left, ", "));
    }
    text = add_log(text, "state file created by state.py init");
    makedirs(dirname(path));
    write_state(path, text);
    const std::string gi = path_join(dirname(path), ".gitignore");
    bool wrote_gi = false;
    if (!exists(gi)) { write_text(gi, GITIGNORE); wrote_gi = true; }
    std::vector<std::string> unknown;
    for (const auto& [k, v] : vals) if (v == NOT_SET) unknown.push_back(lower(k));
    if (B(a, "json")) {
        out(dumps2(dict_of({{"state", Val::str(path)}, {"gitignore_written", Val::boolean(wrote_gi)}, {"unknown", str_list(unknown)}})));
    } else {
        out("Created " + path);
        if (wrote_gi) out("Created " + gi + " (keeps disc/, output/ and other game-derived folders out of git)");
        if (!unknown.empty()) {
            out("Still unknown: " + join(unknown, ", "));
            out("Ask the user once, then record each with `state.py set <key> <value>`.");
        }
    }
    return 0;
}

inline int cmd_design(const Val& a) {
    const std::string state_path = find_state(S(a, "workspace"));
    const std::string text = read_state(state_path);
    const Val fields = get_fields(text);
    const std::string path = path_join(dirname(state_path), "design.md");
    if (exists(path)) { std::fflush(stdout); std::fprintf(stderr, "%s: %s already exists; edit it instead\n", PROG, path.c_str()); return 1; }
    const std::string tpath = template_path("design.md");
    const auto tmpl = read_text(tpath);
    if (!tmpl) throw OSError(ENOENT, tpath);
    auto fv = [&](const char* k) { const Val v = get(fields, k); return truthy(v) ? v.s : NOT_SET; };
    std::string body = replace_all(*tmpl, "{{TITLE}}", fv("title"));
    body = replace_all(body, "{{SERIAL}}", fv("serial"));
    write_text(path, body);
    write_state(state_path, add_log(text, "design.md created"));
    out("Created " + path);
    return 0;
}

inline Val strikes_of(const std::string& text) {
    List l;
    static const std::string P = STRIKE_RE;
    for (const auto& m : re::compile(P, re::M).finditer(text))
        l.push_back(dict_of({{"key", Val::str(m.group(1))}, {"strikes", Val::integer(int_of(m.group(2)))}, {"last", Val::str(m.group(3))}}));
    return Val::list(l);
}

inline int cmd_show(const Val& a) {
    const std::string path = find_state(S(a, "workspace"));
    const std::string text = read_state(path);
    if (B(a, "json")) {
        Val fields = get_fields(text);
        fields.d->set("state", Val::str(path));
        fields.d->set("strikes", strikes_of(text));
        out(dumps2(fields));
        return 0;
    }
    auto write = [](const std::string& s) { std::fwrite(s.data(), 1, s.size(), stdout); };
    if (!get(a, "section").is_none()) {
        const std::string sec = S(a, "section");
        const auto b = section_bounds(text, sec);
        if (!b) throw InputError("no section " + str_repr(sec) + " in " + path);
        write(rstrip_nl(cs(text, b->start, b->end)) + "\n");
        return 0;
    }
    const auto b = section_bounds(text, "Session log");
    if (b && !B(a, "all")) {
        std::vector<std::string> body;
        for (const auto& l : split(rstrip_nl(cs(text, b->body, b->end)), "\n")) if (!strip(l).empty()) body.push_back(l);
        const long long n = get(a, "log_lines").i;
        std::vector<std::string> keep;
        if (n > 0) keep.assign(body.size() > static_cast<std::size_t>(n) ? body.end() - n : body.begin(), body.end());
        const std::string head = cs(text, 0, b->body);
        const std::string note = body.size() > keep.size() ? cfmt("(%zu earlier log lines hidden; --all shows them)\n", body.size() - keep.size()) : "";
        write(head + note + join(keep, "\n") + (keep.empty() ? "" : "\n") + cs(text, b->end));
    } else {
        write(text);
    }
    return 0;
}

inline int cmd_set(const Val& a) {
    const std::string key = replace_all(lower(S(a, "key")), "-", "_");
    const auto keys = settable();
    if (std::find(keys.begin(), keys.end(), key) == keys.end()) throw InputError("unknown field " + str_repr(S(a, "key")) + "; fields: " + join(keys, ", "));
    const std::string path = find_state(S(a, "workspace"));
    std::string text = read_state(path);
    const std::string value = key != "runner_cmd" ? join_ws(S(a, "value")) : strip(S(a, "value"));
    if (value.empty()) throw InputError("empty value for " + key);
    text = set_field(text, key, value);
    if (key == "runner_cmd") text = add_log(text, "active runner command set");
    write_state(path, text);
    out(key + " = " + value);
    return 0;
}

inline int cmd_phase(const Val& a) {
    const int i = parse_phase(S(a, "phase"));
    const std::string path = find_state(S(a, "workspace"));
    std::string text = read_state(path);
    const Val old = get(get_fields(text), "phase");
    text = set_field(text, "phase", phase_label(i));
    text = add_log(text, std::string("phase -> ") + PHASES[i].name);
    write_state(path, text);
    out("Phase: " + phase_label(i) + " (was " + (truthy(old) ? old.s : std::string("unset")) + ")");
    out(std::string("Done when: ") + PHASES[i].done + ".");
    return 0;
}

inline int cmd_log(const Val& a) {
    std::vector<std::string> parts; for (const auto& v : *get(a, "message").l) parts.push_back(v.s);
    const std::string msg = join(parts, " ");
    if (strip(msg).empty()) throw InputError("empty log line");
    const std::string path = find_state(S(a, "workspace"));
    const std::string text = read_state(path);
    write_state(path, add_log(text, msg));
    out("Logged: " + join_ws(msg));
    return 0;
}

inline int cmd_strike(const Val& a) {
    const std::string path = find_state(S(a, "workspace"));
    std::string text = read_state(path);
    static const std::string P = STRIKE_RE;
    const re::Regex& rx = re::compile(P, re::M);
    if (B(a, "list")) {
        const auto ms = rx.finditer(text);
        if (ms.empty()) out("No open strikes.");
        bool stop = false;
        for (const auto& m : ms) { const long long n = int_of(m.group(2)); out(cfmt("[%s] %lld/3  last: %s", m.group(1).c_str(), n, m.group(3).c_str())); if (n >= 3) stop = true; }
        return stop ? 1 : 0;
    }
    if (S(a, "key").empty()) throw InputError("name the problem: state.py strike \"PC 0x00123456 hang\" \"what you tried\"");
    const std::string key = join_ws(S(a, "key"));
    if (key.find(']') != std::string::npos || key.find('[') != std::string::npos) throw InputError("the problem name cannot contain [ or ]");
    std::optional<re::Match> current;
    for (const auto& m : rx.finditer(text)) if (m.group(1) == key) { current = m; break; }
    if (B(a, "clear")) {
        if (!current) { out("No strikes recorded for [" + key + "]."); return 0; }
        text = cs(text, 0, current->start()) + cs(text, current->end() + 1);
        text = add_log(text, "circuit breaker cleared for [" + key + "]: " + (!S(a, "note").empty() ? S(a, "note") : std::string("fixed")));
        write_state(path, text);
        out("Cleared [" + key + "].");
        return 0;
    }
    const long long n = (current ? int_of(current->group(2)) : 0) + 1;
    const std::string note = join_ws(!S(a, "note").empty() ? S(a, "note") : std::string("no note"));
    const std::string line = cfmt("- [%s] %lld/3 strike%s; last: %s (%s)", key.c_str(), n, n == 1 ? "" : "s", note.c_str(), now_stamp().c_str());
    if (current) text = cs(text, 0, current->start()) + line + cs(text, current->end());
    else text = append_to_section(text, "Circuit breaker", line);
    text = add_log(text, cfmt("strike %lld/3 on [%s]: %s", n, key.c_str(), note.c_str()));
    write_state(path, text);
    if (n >= 3) {
        out(cfmt("STRIKE %lld of 3 on [%s]: STOP. Do not guess again.", n, key.c_str()));
        out("1. Re-read STATE.md and reference/guardrails.md (/console-reverse).");
        out("2. Load the reference for this subsystem, read the original MIPS in Ghidra.");
        out("3. A/B against PCSX2 if it is available: the first divergence is the bug.");
        out("4. Still stuck: write the user one specific question with what you know and tried.");
        return 1;
    }
    if (n == 2) out("Strike 2 of 3 on [" + key + "]. Before the next attempt, load the reference for this subsystem and re-read the original code. No third guess without new information.");
    else out("Strike 1 of 3 on [" + key + "].");
    return 0;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p(PROG, "Keep console/STATE.md: the project memory every /console-* skill reads first.");
    p.subcommands("cmd", true); p.sub_metavar = "COMMAND";
    const auto common = [](Parser& s) { s.opt({"--workspace"}).metavar("PATH").help("game workspace that holds console/ (default: search up from here)"); };
    { Parser& s = p.sub("detect"); s.help_line = "report what can be found without asking"; common(s);
      s.opt({"--ps2recomp"}).metavar("PATH").help("the PS2Recomp checkout, if you know it");
      s.opt({"--json"}).store_true().help("machine-readable output"); }
    { Parser& s = p.sub("init"); s.help_line = "create console/STATE.md (refuses to overwrite)"; common(s);
      s.opt({"--game"}).help("the game's title as it shipped");
      s.opt({"--serial"}).help("disc serial, e.g. SLUS-20312");
      s.opt({"--ps2recomp"}).metavar("PATH").help("the PS2Recomp checkout");
      s.opt({"--json"}).store_true().help("machine-readable output"); }
    { Parser& s = p.sub("design"); s.help_line = "create console/design.md from the template"; common(s); }
    { Parser& s = p.sub("show"); s.help_line = "print the state (session log trimmed)"; common(s);
      s.opt({"--section"}).help("print one section, e.g. 'Crashes' or 'Active runner command'");
      s.opt({"--log-lines"}).integer().def(15).help("session log lines to show (default 15)");
      s.opt({"--all"}).store_true().help("print the whole file");
      s.opt({"--json"}).store_true().help("fields, phase, runner command and strikes as JSON"); }
    { Parser& s = p.sub("set"); s.help_line = "record one field: " + join(settable(), ", "); common(s);
      s.pos("key").help("field name, e.g. runner_cmd, build_dir, title");
      s.pos("value").help("the value (quote it)"); }
    { Parser& s = p.sub("phase"); s.help_line = "move to a phase and log it"; common(s);
      s.pos("phase").help("PHASE_SETUP, PHASE_EXTRACT, PHASE_FUNCTION_MAP, PHASE_RECOMPILE, PHASE_BUILD_RUN, PHASE_IO_MENUS, or 0..5"); }
    { Parser& s = p.sub("log"); s.help_line = "append a dated line to the session log"; common(s);
      s.pos("message").nargs("+").help("what happened, in one line"); }
    { Parser& s = p.sub("strike"); s.help_line = "circuit breaker: count a failed fix (stop at 3)"; common(s);
      s.pos("key").nargs("?").help("the problem, e.g. \"PC 0x00123456 hang\"");
      s.pos("note").nargs("?").help("what was tried this time");
      s.opt({"--clear"}).store_true().help("the problem is fixed: remove its line");
      s.opt({"--list"}).store_true().help("show open problems and their strikes"); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    return guard(PROG, [&]() -> int {
        if (cmd == "detect") return cmd_detect(a);
        if (cmd == "init") return cmd_init(a);
        if (cmd == "design") return cmd_design(a);
        if (cmd == "show") return cmd_show(a);
        if (cmd == "set") return cmd_set(a);
        if (cmd == "phase") return cmd_phase(a);
        if (cmd == "log") return cmd_log(a);
        return cmd_strike(a);
    });
}
PACKTOOL("console-reverse/state", main);
}  // namespace packtools::console_state
