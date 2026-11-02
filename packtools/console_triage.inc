// console_triage.inc - console-tester/triage: a C++ port of console-skill/console-tester/triage.py (Jake Schincariol, MIT).
// Reads PS2Recomp runtime logs and ranks what to fix next: unimplemented stubs and syscalls, missing recompiled
// functions, unhandled IOP RPCs, disc and file errors, fatal errors. Clusters by PC, names each PC's function from a
// Ghidra CSV map (Name,Start,End,Size; End exclusive), says which /console-* skill owns it; `compare` diffs two runs.
namespace packtools::console_triage {
using namespace pyrt;
using namespace packtools::console_common;

struct UsageError : std::runtime_error { using std::runtime_error::runtime_error; };

#define CT_HEX "[0-9a-fA-F]+"
struct Rx { const char* kind; const char* pat; };
const Rx RX[] = {
    {"stub", R"(Warning: Unimplemented PS2 stub called\. name=(?P<name>\S+) PC=0x(?P<pc>)" CT_HEX R"(), RA=0x(?P<ra>)" CT_HEX ")"},
    {"stub_suppressed", R"(Warning: Further calls to PS2 stub '(?P<name>[^']+)' are suppressed after (?P<n>\d+) warnings)"},
    {"syscall_todo", R"(\[Syscall TODO\] encoded=0x(?P<enc>)" CT_HEX R"() v1=0x(?P<v1>)" CT_HEX R"() v0=0x(?P<v0>)" CT_HEX R"().*? pc=0x(?P<pc>)" CT_HEX ")"},
    {"syscall", R"(Warning: Unimplemented PS2 syscall called\. PC=0x(?P<pc>)" CT_HEX R"(), RA=0x(?P<ra>)" CT_HEX R"(), Encoded=0x(?P<enc>)" CT_HEX
                R"(), v0=0x(?P<v0>)" CT_HEX R"(), v1=0x(?P<v1>)" CT_HEX R"(), Chosen=0x(?P<id>)" CT_HEX ")"},
    {"syscall_hits", R"(-> Unknown syscallId=0x(?P<id>)" CT_HEX R"() hits=(?P<hits>\d+))"},
    {"missing", R"(Error: No exact recompiled function for guest PC 0x(?P<pc>)" CT_HEX R"()(?:.*? codeRegion=(?P<code>yes|no))?)"},
    {"branch", R"(\[guest-branch:missing-target\] kind=(?P<kind>\S+) op=(?P<op>\S+) source=0x(?P<src>)" CT_HEX R"() target=0x(?P<target>)" CT_HEX
               R"() pc=0x(?P<pc>)" CT_HEX R"() ra=0x(?P<ra>)" CT_HEX R"()(?:.*? codeRegion=(?P<code>yes|no))?)"},
    {"iop", R"(\[IOP/RPC trace:unhandled\] sid=0x(?P<sid>)" CT_HEX R"() rpc=0x(?P<rpc>)" CT_HEX R"() pc=0x(?P<pc>)" CT_HEX R"() ra=0x(?P<ra>)" CT_HEX
            R"()(?:.*? loadedModules=\[(?P<mods>[^\]]*)\])?)"},
    {"cdread", R"(\[sceCdRead\] unresolved request pc=0x(?P<pc>)" CT_HEX R"() ra=0x(?P<ra>)" CT_HEX ")"},
    {"elfpart", R"(\[SifLoadElfPart\] failed path=\"(?P<path>[^\"]*)\"(?:.*? reason=(?P<reason>.*))?)"},
    {"fio", R"(^\s*(?:ps2_stub )?(?P<fn>f\w+) error: (?P<msg>.*)$)"},
    {"override", R"(\[game_overrides\] unresolved handler '(?P<name>[^']+)' for address 0x(?P<pc>)" CT_HEX ")"},
    {"table", R"(\[function-table\] cannot replace guest PC 0x(?P<pc>)" CT_HEX ")"},
    {"mpeg_off", R"(\[MPEG\] runtime built without FFmpeg)"},
    {"mpeg_err", R"(\[MPEG\] (?P<msg>(?:FFmpeg|failed|decoder|parser).*)$)"},
    {"replay_bad", R"(\[pad-replay\] (?P<msg>.*replay disabled.*|cannot open .*)$)"},
    {"fatal_exec", R"(Error during program execution: (?P<msg>.*)$)"},
    {"fatal_main", R"(\[main\] fatal exception: (?P<msg>.*)$)"},
    {"fatal_term", R"(\[terminate\] (?P<msg>.*)$)"},
    {"fatal_init", R"((?P<msg>Failed to initialize PS2 runtime.*|Failed to initialize PS2 memory.*|Failed to bind runtime core subsystems.*)$)"},
    {"fatal_elf", R"((?P<msg>Failed to load ELF file: .*|Failed to open ELF file: .*|Invalid ELF magic number|Not a MIPS executable ELF file|Unsupported ELF format.*|ELF file is too small: .*)$)"},
};
#undef CT_HEX
inline const re::Regex& rx(const char* kind) {
    for (const auto& r : RX) if (std::strcmp(r.kind, kind) == 0) { static thread_local std::map<std::string, std::string> pats; auto& p = pats[kind]; if (p.empty()) p = r.pat; return re::compile(p); }
    throw std::logic_error(kind);
}
struct Hit {   // a match and its pattern, so groups are read by name
    const re::Regex* r = nullptr; re::Match m;
    explicit operator bool() const { return static_cast<bool>(m); }
    std::string operator[](const char* name) const { return m.group(r->group_index(name)); }
};
inline Hit search(const char* kind, const std::string& line) { Hit h; h.r = &rx(kind); h.m = h.r->search(line); return h; }

struct Owner { const char* prefix; const char* owner; const char* file; };
const Owner STUB_OWNERS[] = {
    {"scePad", "/console-controller", "Pad.cpp"}, {"sceMc", "/console-saves", "MemoryCard.cpp"}, {"mc", "/console-saves", "MemoryCard.cpp"},
    {"sceSd", "/console-sound", "Audio.cpp"}, {"sceSynth", "/console-sound", "Audio.cpp"}, {"sceSS", "/console-sound", "Audio.cpp"},
    {"sceGs", "/console-graphics", "GS.cpp"}, {"sceGif", "/console-graphics", "GS.cpp"}, {"sceVif", "/console-graphics", "GS.cpp"},
    {"sceVu", "/console-graphics", "VU.cpp"}, {"sceVpu", "/console-graphics", "VU.cpp"}, {"sceIpu", "/console-graphics", "IPU.cpp"},
    {"sceMpeg", "/console-graphics", "MPEG.cpp"}, {"sceeFont", "/console-graphics", "Font.cpp"},
    {"sceCd", "/console-backend", "CD.cpp"}, {"sceDma", "/console-backend", "DMA.cpp"}, {"sceDeci", "/console-backend", "Deci2.cpp"},
    {"sceFs", "/console-backend", "FileIO.cpp"}, {"sceOpen", "/console-backend", "FileIO.cpp"}, {"sceRead", "/console-backend", "FileIO.cpp"},
    {"sceWrite", "/console-backend", "FileIO.cpp"}, {"sceClose", "/console-backend", "FileIO.cpp"}, {"sceLseek", "/console-backend", "FileIO.cpp"},
    {"sceSif", "/console-backend", "SIF.cpp"}, {"isceSif", "/console-backend", "SIF.cpp"}, {"sceRpc", "/console-backend", "RPC.cpp"},
    {"sceTty", "/console-backend", "TTY.cpp"}, {"scePrintf", "/console-backend", "TTY.cpp"},
};
inline std::pair<std::string, std::string> stub_owner(const std::string& name) {
    for (const auto& o : STUB_OWNERS) if (name.rfind(o.prefix, 0) == 0) return {o.owner, o.file};
    return {"/console-backend", ""};
}
inline int tier_of(const std::string& kind) {
    static const std::map<std::string, int> T{{"fatal", 0}, {"missing", 1}, {"stub", 2}, {"syscall", 3}, {"iop", 4}, {"io", 5}, {"mod", 6}, {"build", 7}, {"input", 8}};
    return T.at(kind);
}
inline long long hx(const std::string& t) { unsigned long long v = 0; for (char c : t) v = v * 16 + static_cast<unsigned long long>(std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : (std::tolower(static_cast<unsigned char>(c)) - 'a' + 10)); return static_cast<long long>(v); }
inline std::string h08(long long v) { return "0x" + hex08(v); }

// ---------------------------------------------------------------- Ghidra map
struct Rec { long long start, end; std::string name; };
struct FunctionMap {
    std::vector<Rec> records; std::vector<long long> starts;
    static FunctionMap load(const std::string& path) {
        if (!exists(path)) throw UsageError("no such file: " + path);
        std::FILE* f = is_dir(path) ? nullptr : open_file(path, "rb");
        if (!f) throw UsageError("cannot read " + path + ": " + OSError(is_dir(path) ? EISDIR : errno, path).what());
        std::string raw; char buf[65536]; std::size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) raw.append(buf, n);
        std::fclose(f);
        const auto rows = csv_rows(text_u(raw, true));
        if (rows.empty()) throw UsageError(path + " is not a Ghidra function map (expected the header Name,Start,End,Size)");
        std::vector<std::string> hd; for (std::size_t i = 0; i < rows[0].size() && i < 3; ++i) hd.push_back(lower(strip(rows[0][i])));
        if (hd != std::vector<std::string>{"name", "start", "end"}) throw UsageError(path + " is not a Ghidra function map (expected the header Name,Start,End,Size)");
        FunctionMap m;
        for (std::size_t k = 1; k < rows.size(); ++k) {
            const auto& row = rows[k]; const long long number = static_cast<long long>(k) + 1;
            if (row.empty() || strip(join(row, "")).empty()) continue;
            if (row.size() < 3) throw UsageError(cfmt("%s line %lld: expected Name,Start,End,Size", path.c_str(), number));
            const auto s = int16(row[1]), e = int16(row[2]);
            if (!s || !e) throw UsageError(cfmt("%s line %lld: Start and End must be hex like 0x00100230", path.c_str(), number));
            m.records.push_back({*s, *e <= *s ? *s + 4 : *e, strip(row[0])});
        }
        std::stable_sort(m.records.begin(), m.records.end(), [](const Rec& a, const Rec& b) { return a.start != b.start ? a.start < b.start : a.end < b.end; });
        for (const auto& r : m.records) m.starts.push_back(r.start);
        return m;
    }
    std::optional<std::pair<std::string, long long>> lookup(std::optional<long long> addr) const {
        if (!addr || records.empty()) return std::nullopt;
        long i = static_cast<long>(std::upper_bound(starts.begin(), starts.end(), *addr) - starts.begin()) - 1;
        const Rec* best = nullptr; int steps = 0;
        while (i >= 0 && steps < 256) {
            const Rec& r = records[static_cast<std::size_t>(i)];
            if (r.start <= *addr && *addr < r.end && (!best || (r.end - r.start) < (best->end - best->start))) best = &r;
            --i; ++steps;
        }
        if (!best) return std::nullopt;
        return std::make_pair(best->name, *addr - best->start);
    }
    Val describe(std::optional<long long> addr) const {
        const auto hit = lookup(addr);
        if (!hit) return Val::none();
        return Val::str(hit->second == 0 ? hit->first : cfmt("%s+0x%llx", hit->first.c_str(), static_cast<unsigned long long>(hit->second)));
    }
};

// ---------------------------------------------------------------- parsing
struct Origin { std::string label; long long number; long long index; };
struct Issue {
    std::string key, kind, title, owner; long long count = 0, min_count = 0; Origin first; std::string first_text;
    std::optional<long long> pc, ra; bool fatal = false; Val details = Val::dict(); std::string next;
    int tier() const { return fatal ? 0 : tier_of(kind); }
    long long total() const { return std::max(count, min_count); }
    Val to_dict(const FunctionMap* fmap) const {
        Val d = dict_of({{"key", Val::str(key)}, {"kind", Val::str(kind)}, {"title", Val::str(title)}, {"owner", Val::str(owner)},
            {"count", Val::integer(total())}, {"fatal", Val::boolean(fatal)}, {"tier", Val::integer(tier())},
            {"first_log", Val::str(first.label)}, {"first_line", Val::integer(first.number)}, {"first_text", Val::str(first_text)},
            {"pc", pc ? Val::str(h08(*pc)) : Val::none()}, {"ra", ra ? Val::str(h08(*ra)) : Val::none()},
            {"next", Val::str(next)}, {"details", details}});
        if (fmap) { d.d->set("function", fmap->describe(pc)); d.d->set("caller", fmap->describe(ra)); }
        return d;
    }
};

struct Triage {
    std::vector<Issue> issues; std::map<std::string, std::size_t> index; long long lines = 0; std::vector<std::string> logs;
    Issue* find(const std::string& key) { const auto it = index.find(key); return it == index.end() ? nullptr : &issues[it->second]; }
    Issue& get(const std::string& key, const std::string& kind, const std::string& title, const std::string& owner, const Origin& origin, const std::string& line) {
        if (Issue* x = find(key)) return *x;
        Issue is; is.key = key; is.kind = kind; is.title = title; is.owner = owner; is.first = origin; is.first_text = cpslice(strip(line), 0, 300);
        index[key] = issues.size(); issues.push_back(std::move(is));
        return issues.back();
    }
    void feed(const std::string& line, const Origin& origin) {
        ++lines;
        if (const auto m = search("stub", line)) {
            const std::string name = m["name"]; const auto [owner, file] = stub_owner(name);
            Issue& it = get("stub:" + name, "stub", "unimplemented stub " + name, owner, origin, line);
            ++it.count;
            if (!it.pc) { it.pc = hx(m["pc"]); it.ra = hx(m["ra"]); }
            if (!file.empty()) it.details.d->set("likely_file", Val::str("ps2xRuntime/src/lib/Kernel/Stubs/" + file));
            it.next = "implement " + name + " in the runtime (likely Kernel/Stubs/" + (file.empty() ? std::string("<the matching file>.cpp") : file) + ") or bind it by address; ret0/ret1/reta0 only to triage";
            return;
        }
        if (const auto m = search("stub_suppressed", line)) {
            const std::string name = m["name"];
            Issue& it = get("stub:" + name, "stub", "unimplemented stub " + name, stub_owner(name).first, origin, line);
            it.min_count = std::max(it.min_count, dec_int(m["n"]) + 1);
            it.details.d->set("suppressed", Val::boolean(true));
            return;
        }
        if (const auto m = search("syscall", line)) {
            const long long sid = hx(m["id"]);
            Issue& it = get(cfmt("syscall:0x%llx", sid), "syscall", cfmt("unimplemented syscall 0x%llx", sid), "/console-backend", origin, line);
            ++it.count;
            if (!it.pc) { it.pc = hx(m["pc"]); it.ra = hx(m["ra"]); }
            it.details.d->set("encoded", Val::str(cfmt("0x%llx", hx(m["enc"]))));
            it.next = cfmt("name it (/console-backend stubs.py syscall 0x%llx) and implement it in Kernel/Syscalls/ (Dispatcher.cpp routes the number)", sid);
            return;
        }
        if (const auto m = search("syscall_hits", line)) {
            if (Issue* it = find(cfmt("syscall:0x%llx", hx(m["id"])))) it->min_count = std::max(it->min_count, dec_int(m["hits"]));
            return;
        }
        if (search("syscall_todo", line)) return;   // the "Warning: Unimplemented PS2 syscall" line that follows carries the same event
        if (const auto m = search("branch", line)) {
            Issue& it = missing(hx(m["target"]), origin, line, m["code"]);
            if (it.count == 0) it.count = 1;   // the lookup line, when there is one, comes first and is counted
            it.ra = hx(m["ra"]);
            it.details.d->set("branch", Val::str(m["kind"]));
            it.details.d->set("source", Val::str(h08(hx(m["src"]))));
            return;
        }
        if (const auto m = search("missing", line)) { ++missing(hx(m["pc"]), origin, line, m["code"]).count; return; }
        if (const auto m = search("iop", line)) {
            const long long sid = hx(m["sid"]), rpc = hx(m["rpc"]);
            Issue& it = get(cfmt("iop:0x%llx:0x%llx", sid, rpc), "iop", cfmt("IOP RPC sid 0x%llx rpc 0x%llx unhandled", sid, rpc), "/console-backend", origin, line);
            ++it.count;
            if (!it.pc) { it.pc = hx(m["pc"]); it.ra = hx(m["ra"]); }
            if (!m["mods"].empty()) it.details.d->set("loaded_modules", Val::str(m["mods"]));
            it.next = cfmt("find which IRX serves sid 0x%llx (loadedModules) and answer it: run the real module in ps2xIOP or add an HLE handler", sid);
            return;
        }
        if (const auto m = search("cdread", line)) {
            const long long pc = hx(m["pc"]);
            Issue& it = get(cfmt("io:sceCdRead:0x%llx", pc), "io", "sceCdRead could not resolve a disc read", "/console-backend", origin, line);
            ++it.count; it.pc = pc; it.ra = hx(m["ra"]);
            it.next = "the read points outside the extracted files: check cdRoot (the ELF's folder) holds the whole disc, or give the runtime the disc image (cdImage)";
            return;
        }
        if (const auto m = search("elfpart", line)) {
            const std::string path = m["path"];
            Issue& it = get("io:SifLoadElfPart:" + path, "io", "module or overlay " + path + " did not load", "/console-backend", origin, line);
            ++it.count;
            if (!m["reason"].empty()) it.details.d->set("reason", Val::str(strip(m["reason"])));
            it.next = "is " + path + " in the extracted disc folder (same case)? If it is, the loader needs work";
            return;
        }
        if (const auto m = search("override", line)) {
            const std::string name = m["name"];
            Issue& it = get("mod:handler:" + name, "mod", "override binds unknown handler '" + name + "'", "/console-mod", origin, line);
            ++it.count; it.pc = hx(m["pc"]);
            it.next = "use a handler name from ps2_call_list.h, or replaceFunction with your own function";
            return;
        }
        if (const auto m = search("table", line)) {
            const long long pc = hx(m["pc"]);
            Issue& it = get(cfmt("mod:table:0x%llx", pc), "mod", "override address outside the function table", "/console-mod", origin, line);
            ++it.count; it.pc = pc;
            it.next = "the address is not a recompiled function start in this ELF build: check it in Ghidra";
            return;
        }
        if (const auto m = search("fio", line)) {
            const std::string msg = re::sub(R"(0x[0-9a-fA-F]+|\b\d+\b)", "N", strip(m["msg"]));
            Issue& it = get("io:" + m["fn"] + ":" + msg, "io", m["fn"] + " error: " + msg, "/console-backend", origin, line);
            ++it.count;
            it.next = "a guest file call failed: check the path translation and the extracted files";
            return;
        }
        if (search("mpeg_off", line)) {
            Issue& it = get("build:ffmpeg", "build", "runtime built without FFmpeg: FMV shows stub frames", "/console-anywhere", origin, line);
            ++it.count;
            it.next = "rebuild with FFmpeg available (PS2X_ENABLE_FFMPEG=ON and the libraries installed)";
            return;
        }
        if (const auto m = search("mpeg_err", line)) {
            const std::string msg = re::sub(R"(0x[0-9a-fA-F]+|-?\b\d+\b)", "N", strip(m["msg"]));
            Issue& it = get("io:mpeg:" + msg, "io", "MPEG video: " + msg, "/console-graphics", origin, line);
            ++it.count;
            it.next = "cutscene decode problem: check the FFmpeg build first (/console-anywhere doctor)";
            return;
        }
        if (search("replay_bad", line)) {
            Issue& it = get("input:replay", "input", "input replay script rejected", "/console-controller", origin, line);
            ++it.count;
            it.next = "fix the script (padmap.py check --script FILE)";
            return;
        }
        for (const char* kind : {"fatal_exec", "fatal_main", "fatal_term", "fatal_init", "fatal_elf"})
            if (const auto m = search(kind, line)) { fatal(kind, strip(m["msg"]), origin, line); return; }
    }
    Issue& missing(long long pc, const Origin& origin, const std::string& line, const std::string& code) {
        Issue& it = get(cfmt("missing:0x%llx", pc), "missing", "no recompiled function at " + h08(pc), "/console-reverse", origin, line);
        if (!it.pc) it.pc = pc;
        if (!code.empty()) it.details.d->set("code_region", Val::str(code));
        const Val cr = pyrt::get(it.details, "code_region");
        if (cr.t == Val::STR && cr.s == "no")
            it.next = h08(pc) + " is not code: something jumped through a bad pointer. Look at the caller (ra) and A/B it against PCSX2 (abdiff.py)";
        else
            it.next = "add " + h08(pc) + " to entry_points in the TOML (or define a function there in Ghidra and re-export), then re-run ps2_recomp and rebuild";
        return it;
    }
    void fatal(const std::string& kind, const std::string& msg, const Origin& origin, const std::string& line) {
        static const std::string STUB_THROW = R"(Unimplemented PS2 stub called: (?P<name>\S+))";
        if (const auto m = re::search(STUB_THROW, msg)) {
            const std::string name = m.group(1);
            Issue& it = get("stub:" + name, "stub", "unimplemented stub " + name, stub_owner(name).first, origin, line);
            it.fatal = true; it.count = std::max(it.count, 1LL);
            if (it.next.empty()) it.next = "implement " + name + " in the runtime or bind it by address";
            return;
        }
        const std::string norm = re::sub("0x[0-9a-fA-F]+", "0x?", msg);
        if (kind == "fatal_term") {   // "[terminate] unhandled exception" and its detail line are one event
            Issue& it = get("fatal:terminate", "fatal", "unhandled C++ exception ([terminate])", "/console-backend", origin, line);
            it.fatal = true;
            if (msg.rfind("unhandled", 0) == 0) ++it.count;
            else { it.count = std::max(it.count, 1LL); it.details.d->set("exception", Val::str(cpslice(msg, 0, 200))); }
            it.next = "read the exception text and the lines before it: the cause comes first";
            return;
        }
        std::string owner, nxt;
        if (kind == "fatal_elf") { owner = "/console-reverse"; nxt = "point the runner at the game's main ELF (SYSTEM.CNF BOOT2)"; }
        else if (kind == "fatal_init") { owner = "/console-anywhere"; nxt = "the host side failed to start (window, GL, memory): check the build"; }
        else { owner = "/console-backend"; nxt = "read the lines just before it in the log: the cause comes first"; }
        const std::string title = cpslice(msg, 0, 160);
        Issue& it = get("fatal:" + norm, "fatal", title.empty() ? kind : title, owner, origin, line);
        it.fatal = true; ++it.count; it.next = nxt;
    }
    void scan_file(const std::string& path) {
        const long long idx = static_cast<long long>(logs.size());
        logs.push_back(path);
        if (is_dir(path)) throw UsageError(path + " is a folder, not a log file");
        if (!exists(path)) throw UsageError("no such file: " + path);
        std::FILE* f = open_file(path, "rb");
        if (!f) throw UsageError("cannot read " + path + ": " + OSError(errno, path).what());
        std::string raw; char buf[65536]; std::size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) raw.append(buf, n);
        std::fclose(f);
        long long number = 0;
        for (const auto& line : lines_keep(text_u(raw))) feed(line, Origin{path, ++number, idx});
    }
    std::vector<const Issue*> ranked() const {
        std::vector<const Issue*> v; for (const auto& i : issues) v.push_back(&i);
        std::stable_sort(v.begin(), v.end(), [](const Issue* a, const Issue* b) {
            if (a->tier() != b->tier()) return a->tier() < b->tier();
            if (a->first.index != b->first.index) return a->first.index < b->first.index;
            if (a->first.number != b->first.number) return a->first.number < b->first.number;
            return -a->total() < -b->total();
        });
        return v;
    }
};
inline Triage scan_logs(const std::vector<std::string>& paths) { Triage t; for (const auto& p : paths) t.scan_file(p); return t; }

// ---------------------------------------------------------------- output
inline std::string where(const Val& d) {
    std::vector<std::string> parts;
    if (truthy(get(d, "pc"))) parts.push_back("pc " + get(d, "pc").s + (truthy(get(d, "function")) ? " (" + get(d, "function").s + ")" : ""));
    if (truthy(get(d, "ra"))) parts.push_back("ra " + get(d, "ra").s + (truthy(get(d, "caller")) ? " (" + get(d, "caller").s + ")" : ""));
    return parts.empty() ? "-" : join(parts, ", ");
}
inline std::string render_text(const List& dicts, const std::vector<std::string>& logs, long long lines) {
    std::vector<std::string> o{cfmt("triage: %zu log(s), %lld lines, %zu distinct issue(s)", logs.size(), lines, dicts.size())};
    if (dicts.empty()) { o.push_back("  nothing the runtime flagged. If the game still misbehaves, compare against PCSX2 (abdiff.py)."); return join(o, "\n"); }
    int rank = 0;
    for (const auto& d : dicts) {
        ++rank;
        o.push_back("");
        o.push_back(cfmt("%2d. ", rank) + get(d, "title").s + (truthy(get(d, "fatal")) ? " [FATAL]" : "") + cfmt("  x%lld  -> ", get(d, "count").i) + get(d, "owner").s);
        o.push_back("    where: " + where(d));
        o.push_back("    first: " + basename(get(d, "first_log").s) + cfmt(" line %lld: ", get(d, "first_line").i) + cpslice(get(d, "first_text").s, 0, 140));
        if (!get(d, "next").s.empty()) o.push_back("    next:  " + get(d, "next").s);
    }
    return join(o, "\n");
}
inline std::string md(const std::string& t) { return replace_all(replace_all(t, "|", "\\|"), "\n", " "); }
inline std::string render_markdown(const List& dicts) {
    std::vector<std::string> rows{"| # | issue | where | count | first seen | owner | next step |", "| --- | --- | --- | --- | --- | --- | --- |"};
    int rank = 0;
    for (const auto& d : dicts) {
        ++rank;
        rows.push_back(cfmt("| %d | ", rank) + md(get(d, "title").s) + (truthy(get(d, "fatal")) ? " (fatal)" : "") + " | " + md(where(d)) + cfmt(" | %lld | ", get(d, "count").i)
            + md(basename(get(d, "first_log").s)) + cfmt(":%lld | ", get(d, "first_line").i) + get(d, "owner").s + " | " + md(get(d, "next").s) + " |");
    }
    return join(rows, "\n");
}

inline std::optional<FunctionMap> load_map(const Val& a) { if (S(a, "map").empty() && get(a, "map").is_none()) return std::nullopt; if (S(a, "map").empty()) return std::nullopt; return FunctionMap::load(S(a, "map")); }

inline int cmd_scan(const Val& a) {
    const auto fmap = load_map(a);
    std::vector<std::string> paths; for (const auto& v : *get(a, "logs").l) paths.push_back(v.s);
    const Triage t = scan_logs(paths);
    List dicts; for (const Issue* i : t.ranked()) dicts.push_back(i->to_dict(fmap ? &*fmap : nullptr));
    const long long top = get(a, "top").i;
    if (top) {   // dicts[:top], negative counts from the end
        const long long nn = static_cast<long long>(dicts.size());
        const long long keep = top > 0 ? std::min(top, nn) : std::max(0LL, nn + top);
        dicts.resize(static_cast<std::size_t>(keep));
    }
    if (B(a, "json")) out(dumps2(dict_of({{"logs", str_list(t.logs)}, {"lines", Val::integer(t.lines)}, {"issues", Val::list(dicts)}})));
    else if (B(a, "markdown")) out(render_markdown(dicts));
    else out(render_text(dicts, t.logs, t.lines));
    return t.issues.empty() ? 0 : 1;
}

inline int cmd_compare(const Val& a) {
    const auto fmap = load_map(a);
    const FunctionMap* fm = fmap ? &*fmap : nullptr;
    const Triage old = scan_logs({S(a, "old")}), neu = scan_logs({S(a, "new")});
    List fixed, still, added;
    Triage& nref = const_cast<Triage&>(neu);
    Triage& oref = const_cast<Triage&>(old);
    for (const Issue* it : old.ranked()) {
        if (const Issue* n = nref.find(it->key)) { Val d = n->to_dict(fm); d.d->set("old_count", Val::integer(it->total())); still.push_back(d); }
        else fixed.push_back(it->to_dict(fm));
    }
    for (const Issue* it : neu.ranked()) if (!oref.find(it->key)) added.push_back(it->to_dict(fm));
    if (B(a, "json")) { out(dumps2(dict_of({{"fixed", Val::list(fixed)}, {"still", Val::list(still)}, {"new", Val::list(added)}}))); }
    else if (B(a, "markdown")) {
        for (const auto& [label, l] : std::vector<std::pair<std::string, const List*>>{{"new", &added}, {"still", &still}, {"fixed", &fixed}}) {
            out(cfmt("### %s (%zu)\n", label.c_str(), l->size()));
            if (!l->empty()) out(render_markdown(*l));
            out("");
        }
    } else {
        out("compare: " + S(a, "old") + " -> " + S(a, "new"));
        out(cfmt("  fixed %zu, still there %zu, new %zu", fixed.size(), still.size(), added.size()));
        for (const auto& d : fixed) out("  FIXED  " + get(d, "title").s);
        for (const auto& d : still) out("  STILL  " + get(d, "title").s + cfmt("  (x%lld -> x%lld)", get(d, "old_count").i, get(d, "count").i));
        for (const auto& d : added) out("  NEW    " + get(d, "title").s + (truthy(get(d, "fatal")) ? " [FATAL]" : "") + "  -> " + get(d, "owner").s + "  [" + where(d) + "]");
        if (!added.empty()) out("  New issues appeared: the change may have broken something, or the game got further and met the next blocker. Read the first NEW line.");
    }
    return added.empty() ? 0 : 1;
}

inline int cmd_lookup(const Val& a) {
    const FunctionMap fmap = FunctionMap::load(S(a, "map"));
    List rows; bool all = true;
    for (const auto& v : *get(a, "addrs").l) {
        const auto addr = int16(v.s);
        if (!addr) throw UsageError(str_repr(v.s) + " is not a hex address");
        const Val fn = fmap.describe(*addr);
        if (!truthy(fn)) all = false;
        rows.push_back(dict_of({{"addr", Val::str(h08(*addr))}, {"function", fn}}));
    }
    if (B(a, "json")) out(dumps2(Val::list(rows)));
    else for (const auto& r : rows) out(get(r, "addr").s + "  " + (truthy(get(r, "function")) ? get(r, "function").s : std::string("(not in the map)")));
    return all ? 0 : 1;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("triage", "Rank what to fix next from PS2Recomp runtime logs, and diff two runs.");
    p.epilog = "Typical: triage.py scan console/runs/latest.log --map console/functions.csv --markdown";
    p.subcommands("cmd", false); p.sub_metavar = "command";
    { Parser& s = p.sub("scan"); s.help_line = "parse one or more logs, cluster by PC, rank, name the owner";
      s.pos("logs").nargs("+").help("runtime log files (run.py writes console/runs/*.log)");
      s.opt({"--map"}).help("Ghidra CSV map (Name,Start,End,Size) to name each PC");
      s.opt({"--top"}).integer().def(0).help("show only the first N issues");
      s.opt({"--markdown"}).store_true().help("a table to paste into console/STATE.md");
      s.opt({"--json"}).store_true();
      s.exclusive({"markdown", "json"}); }
    { Parser& s = p.sub("compare"); s.help_line = "what got fixed, what is still there, what is new";
      s.pos("old"); s.pos("new"); s.opt({"--map"});
      s.opt({"--markdown"}).store_true(); s.opt({"--json"}).store_true();
      s.exclusive({"markdown", "json"}); }
    { Parser& s = p.sub("lookup"); s.help_line = "name the function each address is in (Ghidra CSV map)";
      s.pos("addrs").nargs("+").help("hex addresses, like 0x00123456");
      s.opt({"--map"}).required(); s.opt({"--json"}).store_true(); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { flush(); const std::string t = ap::help_text(ap::norm(p, p.prog)); std::fwrite(t.data(), 1, t.size(), stdout); return 2; }
    try {
        if (cmd == "scan") return cmd_scan(a);
        if (cmd == "compare") return cmd_compare(a);
        return cmd_lookup(a);
    } catch (const UsageError& e) {
        flush(); std::fprintf(stderr, "error: %s\n", e.what());
        return 2;
    }
}
PACKTOOL("console-tester/triage", main);
}  // namespace packtools::console_triage
