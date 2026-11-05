// modder_scan.inc - `um scan`: a C++ port of universal-modder's um/scan.py (Rehan and universal-modder contributors,
// MIT). Finds installed games (Steam, Epic, Xbox; Windows, WSL, Linux, macOS) and fingerprints one: engine, runtime,
// anti-cheat, mod loaders, save folders, ranked modding routes. Read-only.
namespace packtools::modder::scan {
using namespace pyrt;
using namespace packtools::modder;

inline const std::size_t MAX_ENTRIES = 80000;
inline const int MAX_DEPTH = 6;
using Pats = std::vector<std::string>;

// ------------------------------------------------------------------ Valve KeyValues (libraryfolders.vdf, appmanifest_*.acf)
inline Val vdf(const std::string& text) {
    Val root = Val::dict(); std::vector<Val> stack; Val cur = root; std::string key; bool have_key = false;
    for (std::size_t k = 0; k < text.size(); ++k) {
        const char c = text[k];
        if (c == '"') {
            std::string s; ++k;
            for (; k < text.size() && text[k] != '"'; ++k) { if (text[k] == '\\' && k + 1 < text.size()) { s += text[k]; s += text[++k]; continue; } s += text[k]; }
            if (!have_key) { key = s; have_key = true; }
            else { cur.d->set(key, Val::str(replace_all(s, "\\\\", "\\"))); have_key = false; }
        } else if (c == '{') {
            Val n = Val::dict(); cur.d->set(have_key ? key : std::string("None"), n); stack.push_back(cur); cur = n; have_key = false;
        } else if (c == '}') {
            if (!stack.empty()) { cur = stack.back(); stack.pop_back(); }
        }
    }
    return cur;
}

// ------------------------------------------------------------------ Windows shell folders (also from WSL)
inline const std::map<std::string, std::string>& win_folders() {
    static std::map<std::string, std::string> v; static bool done = false;
    if (done) return v;
    done = true;
    if (!(is_windows() || is_wsl())) return v;
    const std::string ps = PS_UTF8 + "$f=[Environment]; @($f::GetFolderPath('UserProfile'),$f::GetFolderPath('MyDocuments'),$f::GetFolderPath('ApplicationData'),$f::GetFolderPath('LocalApplicationData')) -join '|'";
    const auto r = mk::run_proc({ps_exe(), "-NoProfile", "-Command", ps}, is_wsl() ? "/mnt/c" : "", {}, 30);
    std::vector<std::string> parts = split(strip(decode_utf8_replace(r.out)), "|");
    if (r.code != 0 || r.not_found) parts.clear();
#ifdef _WIN32
    if (parts.size() < 4) parts = {env("USERPROFILE"), env("USERPROFILE").empty() ? "" : env("USERPROFILE") + "\\Documents", env("APPDATA"), env("LOCALAPPDATA")};
#endif
    while (parts.size() < 4) parts.emplace_back("");
    const char* keys[] = {"profile", "documents", "appdata", "localappdata"};
    for (int k = 0; k < 4; ++k) if (!parts[static_cast<std::size_t>(k)].empty()) v[keys[k]] = to_posix(parts[static_cast<std::size_t>(k)]);
    return v;
}

inline std::string reg_query(const std::string& key, const std::string& value) {
    const auto r = mk::run_proc({is_wsl() ? "reg.exe" : "reg", "query", key, "/v", value}, is_wsl() ? "/mnt/c" : "", {}, 15);
    if (r.code != 0) return {};
    const auto m = re::search(value + R"(\s+REG_SZ\s+(.+))", decode_utf8_replace(r.out));
    return m ? strip(m.group(1)) : std::string();
}
inline std::string steam_registry_root() {
    if (!env("UM_STEAM_ROOT").empty()) return env("UM_STEAM_ROOT");   // tests and unusual setups
    if (is_windows()) {
        std::string p = reg_query("HKCU\\Software\\Valve\\Steam", "SteamPath");
        if (p.empty()) p = reg_query("HKLM\\SOFTWARE\\WOW6432Node\\Valve\\Steam", "InstallPath");
        return p;
    }
    if (is_wsl()) { const std::string p = reg_query("HKCU\\Software\\Valve\\Steam", "SteamPath"); return p.empty() ? p : to_posix(p); }
    return {};
}
inline std::string resolve(const std::string& p) {
    std::error_code ec; auto r = fs::weakly_canonical(fs::absolute(P(p), ec), ec); if (ec) return p;
    std::string s = u8native(r); while (s.size() > 1 && (s.back() == '/' || s.back() == '\\') && !(s.size() == 3 && s[1] == ':')) s.pop_back(); return s;
}
inline std::vector<std::string> steam_roots() {
    std::vector<std::string> c;
    const std::string reg = steam_registry_root(); if (!reg.empty()) c.push_back(reg);
    const std::string h = home_dir();
    if (env("UM_STEAM_ROOT").empty()) {
        if (is_windows()) { std::string pf = env("ProgramFiles(x86)"); if (pf.empty()) pf = "C:\\Program Files (x86)"; c.push_back(pf + "\\Steam"); c.emplace_back("C:\\Program Files\\Steam"); }
        else if (is_wsl()) { c.emplace_back("/mnt/c/Program Files (x86)/Steam"); c.emplace_back("/mnt/c/Program Files/Steam"); }
        if (is_mac()) c.push_back(path_join(h, "Library/Application Support/Steam"));
        if (!is_windows()) { c.push_back(path_join(h, ".steam/steam")); c.push_back(path_join(h, ".local/share/Steam")); c.push_back(path_join(h, ".var/app/com.valvesoftware.Steam/.local/share/Steam")); }
    }
    std::vector<std::string> out; std::set<std::string> seen;
    for (const auto& x : c) { const std::string r = resolve(x); if (isdir(path_join(r, "steamapps")) && !seen.count(r)) { seen.insert(r); out.push_back(r); } }
    return out;
}
inline Val game(const std::string& store, const Val& appid, const Val& name, const std::string& path) {
    Val g = Val::dict(); g.d->set("store", Val::str(store)); g.d->set("appid", appid); g.d->set("name", name); g.d->set("path", Val::str(path)); return g;
}
inline std::vector<std::string> glob_dir(const std::string& dir, const std::string& pat) {
    std::vector<std::string> o; std::error_code ec;
    for (fs::directory_iterator it(P(dir), ec), end; !ec && it != end; it.increment(ec)) { const std::string n = u8native(it->path().filename()); if (fnmatch(n, pat)) o.push_back(u8native(it->path())); }
    std::sort(o.begin(), o.end()); return o;
}
inline List steam_games() {
    List games; std::vector<std::string> libs;
    for (const auto& root : steam_roots()) {
        libs.push_back(root);
        const std::string lf = path_join(path_join(root, "steamapps"), "libraryfolders.vdf");
        if (isfile(lf)) {
            const Val data = vdf(decode_utf8_replace(slurp(lf)));
            const Val l = get(data, "libraryfolders");
            if (l.t == Val::DICT) for (const auto& [k, v] : l.d->items) if (v.t == Val::DICT && truthy(get(v, "path"))) libs.push_back(to_posix(get(v, "path").s));
        }
    }
    std::set<std::string> seen;
    for (const auto& lib : libs) {
        const std::string apps = path_join(lib, "steamapps");
        if (!isdir(apps) || seen.count(resolve(apps))) continue;
        seen.insert(resolve(apps));
        for (const auto& acf : glob_dir(apps, "appmanifest_*.acf")) {
            const Val st = get(vdf(decode_utf8_replace(slurp(acf))), "AppState", Val::dict());
            const std::string inst = get(st, "installdir").t == Val::STR ? get(st, "installdir").s : "";
            const std::string path = path_join(path_join(apps, "common"), inst);
            if (!inst.empty() && isdir(path)) {
                const std::string appid = get(st, "appid").t == Val::STR ? get(st, "appid").s : "";
                Val g = game("steam", get(st, "appid"), get(st, "name"), path);
                const std::string ws = path_join(path_join(path_join(apps, "workshop"), "content"), appid);
                g.d->set("workshop", isdir(ws) ? Val::str(ws) : Val());
                games.push_back(g);
            }
        }
    }
    return games;
}
inline List epic_games() {
    List out; std::string base;
    if (is_windows()) base = "C:\\ProgramData"; else if (is_wsl()) base = "/mnt/c/ProgramData";
    if (!env("UM_EPIC_ROOT").empty()) base = env("UM_EPIC_ROOT");
    if (base.empty()) return out;
    for (const auto& item : glob_dir(path_join(base, "Epic/EpicGamesLauncher/Data/Manifests"), "*.item")) {
        Val d; try { d = loads(decode_utf8_replace(slurp(item))); } catch (const PyErr&) { continue; }
        const std::string p = to_posix(get(d, "InstallLocation").t == Val::STR ? get(d, "InstallLocation").s : "");
        if (!p.empty() && isdir(p)) out.push_back(game("epic", get(d, "AppName"), get(d, "DisplayName"), p));
    }
    return out;
}
inline List xbox_games() {
    List out;
    for (const char* drive : {"c", "d", "e", "f"}) {
        std::string root;
        if (is_windows()) root = std::string(1, up_c(drive[0])) + ":/XboxGames"; else if (is_wsl()) root = std::string("/mnt/") + drive + "/XboxGames";
        if (root.empty() || !isdir(root)) continue;
        std::vector<std::string> kids; std::error_code ec;
        for (fs::directory_iterator it(P(root), ec), end; !ec && it != end; it.increment(ec)) if (it->is_directory(ec)) kids.push_back(u8native(it->path()));
        std::sort(kids.begin(), kids.end());
        for (const auto& g : kids) { const std::string c = path_join(g, "Content"); out.push_back(game("xbox", Val(), Val::str(u8native(P(g).filename())), isdir(c) ? c : g)); }
    }
    return out;
}
inline List all_games() { List a = steam_games(); for (auto& g : epic_games()) a.push_back(g); for (auto& g : xbox_games()) a.push_back(g); return a; }
inline std::string alnum_lower(const std::string& s) { std::string o; for (char c : lower(s)) if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) o += c; return o; }
inline std::string sv(const Val& v) { return v.t == Val::STR ? v.s : std::string(); }

inline Val resolve_game(const std::string& query) {
    const std::string p = expanduser(to_posix(query));
    if (isdir(p)) {
        for (const auto& g : all_games()) if (resolve(sv(get(g, "path"))) == resolve(p)) return g;
        std::string nm = u8native(P(resolve(p)).filename()); if (nm.empty()) nm = u8native(P(p).filename());
        Val g = game("", Val(), Val::str(u8native(P(p).filename()).empty() ? nm : u8native(P(p).filename())), p); g.d->set("store", Val()); return g;
    }
    const std::string q = alnum_lower(query);
    const List games = all_games(); List hits;
    for (const auto& g : games) if (alnum_lower(sv(get(g, "name"))) == q || (get(g, "appid").t == Val::STR && get(g, "appid").s == query)) hits.push_back(g);
    if (hits.empty()) for (const auto& g : games) if (!q.empty() && alnum_lower(sv(get(g, "name")) + lower(u8native(P(sv(get(g, "path"))).filename()))).find(q) != std::string::npos) hits.push_back(g);
    if (hits.empty()) die("no installed game matches " + repr(Val::str(query)) + "; pass the install folder instead (`um scan --list` shows what was found)");
    if (hits.size() > 1) std::stable_sort(hits.begin(), hits.end(), [](const Val& a, const Val& b) { return cplen(sv(get(a, "name"))) < cplen(sv(get(b, "name"))); });
    return hits[0];
}

// ------------------------------------------------------------------ the file index: lower-cased relative paths (bounded walk)
struct Index {
    std::string root; std::vector<std::string> files; std::set<std::string> fileset, dirs; bool truncated = false;
    explicit Index(const std::string& r) : root(r) {
        // breadth of os.walk (top-down); directory entries sorted so reports are stable
        std::vector<std::pair<std::string, std::string>> stack{{r, ""}};
        while (!stack.empty() && !truncated) {
            auto [dir, rel] = stack.back(); stack.pop_back();
            const int depth = rel.empty() ? 0 : static_cast<int>(std::count(rel.begin(), rel.end(), '/')) + 1;
            std::vector<std::string> subdirs, fl; std::error_code ec;
            for (fs::directory_iterator it(P(dir), fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
                std::error_code e2; const std::string n = u8native(it->path().filename());
                if (it->is_directory(e2) && !it->is_symlink(e2)) subdirs.push_back(n); else if (it->is_directory(e2)) subdirs.push_back(n); else fl.push_back(n);
            }
            std::sort(subdirs.begin(), subdirs.end()); std::sort(fl.begin(), fl.end());
            if (depth >= MAX_DEPTH) subdirs.clear();
            std::vector<std::string> keep;
            for (const auto& d : subdirs) { const std::string l = lower(d); if (l != "__pycache__" && l != ".git" && l != "shadercache") keep.push_back(d); }
            for (const auto& d : keep) dirs.insert(lower(rel.empty() ? d : rel + "/" + d));
            for (const auto& f : fl) { const std::string x = lower(rel.empty() ? f : rel + "/" + f); files.push_back(x); fileset.insert(x); }
            if (files.size() > MAX_ENTRIES) { truncated = true; break; }
            for (auto it = keep.rbegin(); it != keep.rend(); ++it) stack.emplace_back(path_join(dir, *it), rel.empty() ? *it : rel + "/" + *it);
        }
    }
    std::vector<std::string> find(const Pats& pats) const { std::vector<std::string> o; for (const auto& f : files) for (const auto& p : pats) if (fnmatch(f, p)) { o.push_back(f); break; } return o; }
    bool has(const Pats& pats) const { for (const auto& p : pats) for (const auto& f : files) if (fnmatch(f, p)) return true; return false; }
    bool has_dir(const Pats& pats) const { for (const auto& p : pats) for (const auto& d : dirs) if (fnmatch(d, p)) return true; return false; }
    bool in(const std::string& rel) const { return fileset.count(rel) > 0; }
    std::string path(const std::string& rel_lower) const {   // case-insensitive rel path -> real path
        std::string cur = root;
        for (const auto& part : split(rel_lower, "/")) {
            std::string hit; std::error_code ec;
            for (fs::directory_iterator it(P(cur), ec), end; !ec && it != end; it.increment(ec)) if (lower(u8native(it->path().filename())) == part) { hit = u8native(it->path()); break; }
            if (hit.empty()) return path_join(root, rel_lower);
            cur = hit;
        }
        return cur;
    }
};
inline std::vector<std::string> take(const std::vector<std::string>& v, std::size_t n) { return std::vector<std::string>(v.begin(), v.begin() + static_cast<long>(std::min(n, v.size()))); }
inline std::vector<std::string> cat(std::vector<std::string> a, const std::vector<std::string>& b) { a.insert(a.end(), b.begin(), b.end()); return a; }

// ------------------------------------------------------------------ binary sniffing
inline uint32_t rd32(const std::string& b, std::size_t at) { return uint32_t(uint8_t(b[at])) | uint32_t(uint8_t(b[at + 1])) << 8 | uint32_t(uint8_t(b[at + 2])) << 16 | uint32_t(uint8_t(b[at + 3])) << 24; }
inline uint16_t rd16(const std::string& b, std::size_t at) { return static_cast<uint16_t>(uint8_t(b[at]) | uint8_t(b[at + 1]) << 8); }
inline uint64_t rd64(const std::string& b, std::size_t at) { return uint64_t(rd32(b, at)) | uint64_t(rd32(b, at + 4)) << 32; }
struct PeInfo { bool ok = false; std::string arch; bool managed = false; };
inline PeInfo pe_info(const std::string& path) {
    PeInfo r; const std::string head = head_bytes(path, 4096);
    if (head.size() < 0x40 || head.substr(0, 2) != "MZ") return r;
    const uint32_t off = rd32(head, 0x3C);
    if (static_cast<std::size_t>(off) + 0x100 > head.size() || head.substr(off, 4) != std::string("PE\0\0", 4)) return r;
    const uint16_t machine = rd16(head, off + 4); const std::size_t opt = off + 24; const uint16_t magic = rd16(head, opt);
    const std::size_t dd = opt + (magic == 0x10B ? 96 : 112);
    const uint32_t num_dirs = rd32(head, opt + (magic == 0x10B ? 92 : 108));
    uint32_t clr = 0; if (num_dirs > 14 && dd + 15 * 8 <= head.size()) clr = rd32(head, dd + 14 * 8);
    char hx[16]; std::snprintf(hx, sizeof hx, "0x%x", machine);
    r.ok = true; r.arch = machine == 0x14C ? "x86" : machine == 0x8664 ? "x64" : machine == 0xAA64 ? "arm64" : hx; r.managed = clr != 0; return r;
}
inline bool dig(char c) { return c >= '0' && c <= '9'; }
// Unreal's FEngineVersion branch: ++UE4+Release-4.27 (ASCII) or the same in UTF-16-LE, read in bounded chunks
inline std::string unreal_version(const std::string& path, double budget_s = 8.0) {
    std::ifstream f(P(path), std::ios::binary); if (!f) return {};
    const auto t0 = std::chrono::steady_clock::now(); std::string tail; std::size_t read = 0;
    const std::string a = "++UE"; const std::string w("+\0+\0U\0E\0", 8);
    while (read < (400u << 20) && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < budget_s) {
        std::string chunk(16u << 20, '\0'); f.read(chunk.data(), static_cast<std::streamsize>(chunk.size())); chunk.resize(static_cast<std::size_t>(f.gcount()));
        if (chunk.empty()) break;
        read += chunk.size(); const std::string buf = tail + chunk;
        // ASCII form first (the regex list order), then UTF-16
        for (std::size_t k = buf.find(a); k != std::string::npos; k = buf.find(a, k + 1)) {
            std::size_t j = k + 4; if (j >= buf.size() || (buf[j] != '4' && buf[j] != '5')) continue; ++j;
            const std::string rel = "+Release-"; if (buf.compare(j, rel.size(), rel) != 0) continue; j += rel.size();
            if (j + 2 >= buf.size() || !dig(buf[j]) || buf[j + 1] != '.' || !dig(buf[j + 2])) continue;
            std::size_t e = j + 2; while (e < buf.size() && dig(buf[e])) ++e;
            return buf.substr(k + 2, e - k - 2);
        }
        for (std::size_t k = buf.find(w); k != std::string::npos; k = buf.find(w, k + 1)) {
            std::size_t j = k + 8; if (j + 1 >= buf.size() || (buf[j] != '4' && buf[j] != '5') || buf[j + 1] != '\0') continue; j += 2;
            const std::string rel("+\0R\0e\0l\0e\0a\0s\0e\0-\0", 18); if (buf.compare(j, rel.size(), rel) != 0) continue; j += rel.size();
            if (j + 5 >= buf.size() || !dig(buf[j]) || buf[j + 1] != '\0' || buf[j + 2] != '.' || buf[j + 3] != '\0' || !dig(buf[j + 4]) || buf[j + 5] != '\0') continue;
            std::size_t e = j + 6; if (e + 1 < buf.size() && dig(buf[e]) && buf[e + 1] == '\0') e += 2;
            std::string o; for (std::size_t q = k; q < e; ++q) if (buf[q] != '\0') o += buf[q];
            return o.substr(2);
        }
        tail = buf.size() > 256 ? buf.substr(buf.size() - 256) : buf;
    }
    return {};
}
// (?:20\d\d|6000|[45])\.\d+\.\d+[abfpx]\d+ in the first 64 KB of a Unity data file
inline std::string unity_version_in(const std::string& head) {
    auto at = [&](std::size_t k) -> std::size_t {   // end of a match starting at k, or 0
        std::size_t j = k;
        if (head.compare(j, 2, "20") == 0 && j + 3 < head.size() && dig(head[j + 2]) && dig(head[j + 3])) j += 4;
        else if (head.compare(j, 4, "6000") == 0) j += 4;
        else if (j < head.size() && (head[j] == '4' || head[j] == '5')) j += 1;
        else return 0;
        auto alt = [&](std::size_t start) -> std::size_t {
            std::size_t q = start; if (q >= head.size() || head[q] != '.') return 0; ++q;
            const std::size_t d1 = q; while (q < head.size() && dig(head[q])) ++q; if (q == d1) return 0;
            if (q >= head.size() || head[q] != '.') return 0; ++q;
            const std::size_t d2 = q; while (q < head.size() && dig(head[q])) ++q; if (q == d2) return 0;
            // backtracking: \d+ before the letter is greedy and the letter must follow
            if (q >= head.size() || !std::strchr("abfpx", head[q]) || head[q] == '\0') return 0; ++q;
            const std::size_t d3 = q; while (q < head.size() && dig(head[q])) ++q; if (q == d3) return 0;
            return q;
        };
        std::size_t e = alt(j);
        if (!e && head.compare(k, 2, "20") == 0) e = 0;
        return e;
    };
    for (std::size_t k = 0; k < head.size(); ++k) { const std::size_t e = at(k); if (e) return head.substr(k, e - k); }
    return {};
}
inline std::string unity_version(const Index& ix, const std::string& data_dir) {
    for (const char* name : {"globalgamemanagers", "data.unity3d", "maindata", "resources.assets"}) {
        const std::string rel = data_dir + "/" + name;
        if (ix.in(rel)) { const std::string head = head_bytes(ix.path(rel), 1 << 16); if (head.empty() && !isfile(ix.path(rel))) continue; const std::string v = unity_version_in(head); if (!v.empty()) return v; }
    }
    return {};
}
inline std::string godot_pck(const std::string& path) {
    std::ifstream f(P(path), std::ios::binary); if (!f) return {};
    std::string head(20, '\0'); f.read(head.data(), 20); head.resize(static_cast<std::size_t>(f.gcount()));
    auto fmtv = [](const std::string& h, const std::string& tail) { return std::to_string(rd32(h, 8)) + "." + std::to_string(rd32(h, 12)) + "." + std::to_string(rd32(h, 16)) + tail; };
    if (head.size() >= 20 && head.substr(0, 4) == "GDPC") return fmtv(head, " (pack format " + std::to_string(rd32(head, 4)) + ")");
    f.clear(); f.seekg(0, std::ios::end); const long long size = static_cast<long long>(f.tellg());
    if (size < 12) return {};
    f.seekg(size - 12); std::string tail(12, '\0'); f.read(tail.data(), 12);
    if (f.gcount() == 12 && tail.substr(8) == "GDPC") {
        const uint64_t sz = rd64(tail, 0); if (static_cast<long long>(sz) + 12 > size) return {};
        f.seekg(size - 12 - static_cast<long long>(sz)); std::string h2(20, '\0'); f.read(h2.data(), 20);
        if (f.gcount() == 20 && h2.substr(0, 4) == "GDPC") return fmtv(h2, " (embedded in exe)");
    }
    return {};
}

// ------------------------------------------------------------------ knowledge tables
struct Named { std::string name; Pats pats; };
inline const std::vector<Named>& ANTI_CHEAT() {
    static const std::vector<Named> v = {
        {"EasyAntiCheat", {"easyanticheat/*", "*easyanticheat*", "start_protected_game.exe", "eac_launcher.exe"}},
        {"BattlEye", {"battleye/*", "*beservice*", "*_be.exe"}},
        {"EA Javelin anticheat", {"eaanticheat*", "*/eaanticheat*"}},
        {"nProtect GameGuard", {"gameguard/*", "*gamemon*.des"}},
        {"XIGNCODE3", {"xigncode/*", "*x3.xem"}},
        {"Tencent ACE", {"*ace-base*", "*sgguard*", "*/ace/*.sys"}},
        {"mhyprot", {"*mhypbase.dll", "*mhyprot*"}},
        {"PunkBuster", {"pb/pbsvc*", "pb/pbcl*"}},
        {"Ricochet", {"*randgrid.sys"}}};
    return v;
}
inline const std::vector<Named>& LOADERS() {
    static const std::vector<Named> v = {
        {"BepInEx", {"bepinex/core/*", "doorstop_config.ini", ".doorstop_version"}},
        {"MelonLoader", {"melonloader/*"}},
        {"UE4SS", {"*ue4ss.dll", "*ue4ss-settings.ini", "*/ue4ss/*"}},
        {"tModLoader", {"tmodloader.dll"}},
        {"SMAPI", {"stardewmodapi*", "smapi-internal/*"}},
        {"Everest (Celeste)", {"celeste.mod.mm.dll", "everest*"}},
        {"ModEngine2", {"*modengine2_launcher.exe", "*config_eldenring.toml"}},
        {"REFramework", {"reframework/*"}},
        {"Script Extender (Bethesda)", {"skse64_loader.exe", "skse_loader.exe", "f4se_loader.exe", "nvse_loader.exe", "sfse_loader.exe", "obse64_loader.exe", "obse_loader.exe"}},
        {"ScriptHookV", {"scripthookv.dll", "scripthookrdr2.dll"}},
        {"ASI loader", {"*.asi"}},
        {"Cyber Engine Tweaks", {"bin/x64/plugins/cyber_engine_tweaks/*"}},
        {"RED4ext", {"red4ext/*"}},
        {"REDmod", {"tools/redmod/*"}},
        {"ReShade", {"*reshade.ini", "*reshade*.log"}},
        {"Fabric/Forge profile", {"*fabric-loader*", "*neoforge*", "*forge-*.jar"}},
        {"Steamodded (Balatro)", {"*steamodded*"}},
        {"Hollow Knight Modding API", {"*modding api*", "*/managed/mods/*"}},
        {"RE_Kenshi (Kenshi OGRE plugin)", {"re_kenshi.dll", "kenshilib.dll"}},
        {"Kenshi Mod Manager", {"kenshimodtool.exe"}},
        {"DFHack", {"dfhooks*.dll", "libdfhooks*.so"}}};
    return v;
}
inline bool in_skip_dir(const std::string& rel) {
    static const std::set<std::string> SKIP = {"redist", "commonredist", "vcredist", "directx", "installer", "installers", "support"};
    auto parts = split(rel, "/"); if (!parts.empty()) parts.pop_back();
    for (auto p : parts) { std::size_t k = 0; while (k < p.size() && p[k] == '_') ++k; if (SKIP.count(p.substr(k))) return true; }
    return false;
}
inline const std::vector<std::string> MOD_DIRS = {"mods", "mod", "addons", "plugins", "custom", "workshop", "usermods", "~mods", "content/paks/~mods", "data/scripts", "bepinex/plugins"};
inline const std::string BANNERLORD_ROUTE = "official module system: Modules/<id>/SubModule.xml + ModuleData XML/XSLT and C# MBSubModuleBase; optional Bannerlord.Harmony for managed patches; inspect the installed TaleWorlds DLLs";
inline const std::vector<std::string> BANNERLORD_BIN_DIRS = {"win64_shipping_client", "gaming.desktop.x64_shipping_client", "win64_shipping_weditor"};
struct Known { std::string route, playbook; };
inline const std::vector<std::pair<std::string, Known>>& KNOWN() {
    static const std::vector<std::pair<std::string, Known>> v = {
        {"mount & blade ii: bannerlord", {BANNERLORD_ROUTE, "bannerlord.md"}},
        {"mount & blade ii bannerlord", {BANNERLORD_ROUTE, "bannerlord.md"}},
        {"terraria", {"tModLoader (free Steam app 1281930): C# ModItem/ModNPC/ModProjectile/ModSystem, hot reload from ModSources", "dotnet-xna.md"}},
        {"stardew valley", {"SMAPI + Content Patcher (JSON content packs) or C# SMAPI mods with Harmony", "dotnet-xna.md"}},
        {"celeste", {"Everest + Olympus; C# code mods with MonoMod hooks, maps with L\xC3\xB6nn/Ahorn", "dotnet-xna.md"}},
        {"rimworld", {"XML Defs + C# with Harmony; drop mods in Mods/ (or Workshop)", "unity.md"}},
        {"age of empires ii: definitive edition", {"data mods (.dat via genieutils-py), UI/graphics mods, scenarios (AoE2ScenarioParser), SLD sprites", "genie-aoe2.md"}},
        {"the elder scrolls v: skyrim special edition", {"ESP/ESL plugins (xEdit, Creation Kit), Papyrus, SKSE + CommonLibSSE-NG native plugins, MO2", "bethesda.md"}},
        {"fallout 4", {"ESP plugins (FO4Edit, Creation Kit), Papyrus, F4SE plugins, MO2", "bethesda.md"}},
        {"starfield", {"Creation Kit plugins, SFSE", "bethesda.md"}},
        {"elden ring", {"ModEngine2 (offline, EAC disabled) + Smithbox/DSMapStudio param/map edits; never online", "big-frameworks.md"}},
        {"grand theft auto v", {"Story mode only: ScriptHookV + ASI loader, OpenIV/CodeWalker; BattlEye guards GTA Online - never mod online", "big-frameworks.md"}},
        {"grand theft auto v enhanced", {"Story mode only: ScriptHookV (enhanced build) + ASI loader; BattlEye guards GTA Online - never mod online", "big-frameworks.md"}},
        {"cyberpunk 2077", {"REDmod / Cyber Engine Tweaks (Lua) / RED4ext / ArchiveXL, WolvenKit for assets", "big-frameworks.md"}},
        {"baldur's gate 3", {"Script Extender (Lua) + LSLib/Multitool for .pak, official mod.io toolkit", "big-frameworks.md"}},
        {"valheim", {"BepInEx 5 + Jotunn, HarmonyX patches", "unity.md"}},
        {"lethal company", {"BepInEx 5 + HarmonyX (Thunderstore)", "unity.md"}},
        {"risk of rain 2", {"BepInEx 5 + R2API (Thunderstore)", "unity.md"}},
        {"hollow knight", {"Hollow Knight Modding API (Lumafly installer), C# mods", "unity.md"}},
        {"slay the spire", {"ModTheSpire + BaseMod (Java, SpirePatch)", "misc-engines.md"}},
        {"slay the spire 2", {"the game's own mod loader: C# .dll + Godot .pck + .json manifest in mods/; BaseLib (NuGet Alchyr.Sts2.BaseLib) for cards, relics and characters", "godot.md"}},
        {"balatro", {"Steamodded + lovely (Lua injection into the L\xC3\x96VE game)", "misc-engines.md"}},
        {"kenshi", {"FCS .mod data mods + asset overrides in mods/<name>/ mirroring data/ (load order in the launcher or KMM); code via RE_Kenshi + KenshiLib C++ plugins (VS2010 v100 x64, GPLv3, listed in the mod's RE_Kenshi.json) - the game exe is native, no CLR", "native.md"}},
        {"factorio", {"official Lua modding API (mods/ folder, data.lua + control.lua)", "misc-engines.md"}},
        {"mindustry", {"the built-in mod loader, no patching: mod.hjson/mod.json + content/<type>/*.hjson|json (blocks, units, items...), sprites/ (sprites-override/ replaces vanilla), scripts/main.js; Java mods are jars from Anuken/MindustryJavaModTemplate. Mods go in %APPDATA%/Mindustry/mods, ~/.local/share/Mindustry/mods, or <install>/saves/mods on Steam", "misc-engines.md"}},
        {"dwarf fortress", {"raws, no exe patching (v50+): mods/<name>/ with info.txt (ID, NUMERIC_VERSION) + objects/ and graphics/ raws, in the user folder (Bay 12 Games/Dwarf Fortress under %APPDATA% or ~/.local/share; the install in portable mode) or the Workshop. Mods are picked and ordered at world creation and copied to data/installed_mods (bump NUMERIC_VERSION after edits); graphics replacements declare [REQUIRES_ID_AFTER_ME:vanilla_creatures_graphics]. DFHack (Lua) for scripting", "misc-engines.md"}},
        {"counter-strike 2", {"Workshop maps / Source 2 tools; local -insecure only. VAC: never inject on official servers", "source.md"}},
        {"portal 2", {"VScript (Squirrel) + Puzzle Maker/Hammer, Workshop", "source.md"}},
        {"half-life 2", {"Source SDK 2013 mods (C++), maps with Hammer", "source.md"}},
        {"doom", {"WAD/PK3 mods with a source port (GZDoom/UZDoom)", "misc-engines.md"}},
        {"the binding of isaac: rebirth", {"Lua mod in <install>/mods/<name>/ (main.lua, content/*.xml, resources/ overrides), IsaacDocs API; EnableMods=1 + EnableDebugConsole=1 in options.ini; vanilla assets via tools/ResourceExtractor", "isaac.md"}},
        {"call of duty: world at war", {"dual route: zero-linker data override in mods/<name>/*.iwd (GSC/CSC, IWI textures, weapons) + mod.ff via linker_pc.exe for binary assets; T4M (d3d9.dll) raises asset limits; solo or a co-op lobby you host, never public servers", "iw-engine.md"}},
        {"call of duty 4: modern warfare", {"data overrides in mods/<name>/*.iwd + mod.ff via linker_pc.exe; single-player, or a multiplayer server you run (CoD4X)", "iw-engine.md"}},
        {"minecraft", {"Fabric (Mixin) or NeoForge", "minecraft.md"}}};
    return v;
}
inline const Known* known_of(const std::string& k) { for (const auto& [a, b] : KNOWN()) if (a == k) return &b; return nullptr; }
inline const std::vector<std::pair<std::string, Pats>>& KNOWN_FILES() {
    static const std::vector<std::pair<std::string, Pats>> v = {
        {"call of duty: world at war", {"codwaw.exe", "codwawmp.exe"}},
        {"call of duty 4: modern warfare", {"iw3sp.exe", "iw3mp.exe"}},
        {"dwarf fortress", {"data/vanilla/*/info.txt", "dwarf fortress.exe", "dwarfort"}},
        {"mindustry", {"mindustry.exe", "mindustry.json", "mindustry.jar", "mindustry.app/contents/resources/desktop.jar"}},
        {"the binding of isaac: rebirth", {"isaac-ng.exe"}}};
    return v;
}
inline const std::map<std::string, std::vector<std::string>>& KNOWN_SAVES() {
    static const std::map<std::string, std::vector<std::string>> v = {
        {"call of duty: world at war", {"{localappdata}/Activision/CoDWaW/players"}},
        {"age of empires ii: definitive edition", {"{profile}/Games/Age of Empires 2 DE"}},
        {"terraria", {"{documents}/My Games/Terraria"}},
        {"tmodloader", {"{documents}/My Games/Terraria/tModLoader"}},
        {"stardew valley", {"{appdata}/StardewValley"}},
        {"the elder scrolls v: skyrim special edition", {"{documents}/My Games/Skyrim Special Edition"}},
        {"elden ring", {"{appdata}/EldenRing"}},
        {"grand theft auto v", {"{documents}/Rockstar Games/GTA V"}},
        {"grand theft auto v legacy", {"{documents}/Rockstar Games/GTA V"}},
        {"cyberpunk 2077", {"{profile}/Saved Games/CD Projekt Red/Cyberpunk 2077"}},
        {"counter-strike 2", {}}};
    return v;
}
inline const std::vector<std::string> ONLINE_ONLY = {"valorant", "league of legends", "fortnite", "apex legends", "pubg", "pubg battlegrounds", "rainbow six siege",
    "rainbow six siege x", "tom clancy s rainbow six siege", "tom clancy s rainbow six siege x", "call of duty", "call of duty hq", "call of duty warzone", "destiny 2",
    "genshin impact", "escape from tarkov", "battlefield 2042", "battlefield 6", "overwatch", "overwatch 2", "counter strike 2", "dota 2", "marvel rivals", "the finals",
    "rust", "dead by daylight", "naraka bladepoint", "warframe", "deadlock"};
// 'Tom Clancy's Rainbow Six® Siege' -> 'tom clancy s rainbow six siege'
inline std::string plain_name(const std::string& name) {
    std::string l = replace_all(replace_all(lower(name), "\xC2\xAE", ""), "\xE2\x84\xA2", ""), o; bool sp = true;
    for (char c : l) { if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) { o += c; sp = false; } else if (!sp) { o += ' '; sp = true; } }
    while (!o.empty() && o.back() == ' ') o.pop_back();
    return o;
}
inline std::string online_only(const std::string& name) { const std::string n = plain_name(name); for (const auto& x : ONLINE_ONLY) if (x == n) return n; return {}; }

struct Engine { std::string label, playbook, route; };
inline const std::vector<std::pair<std::string, Engine>>& ENGINES() {
    static const std::vector<std::pair<std::string, Engine>> v = {
        {"bannerlord", {"Bannerlord (TaleWorlds native engine + managed modules)", "bannerlord.md", BANNERLORD_ROUTE}},
        {"unity-mono", {"Unity (Mono)", "unity.md", "BepInEx 5 (or MelonLoader) plugin in C# with HarmonyX patches; read Managed/Assembly-CSharp.dll with ILSpy; assets via AssetRipper/UABEA"}},
        {"unity-il2cpp", {"Unity (IL2CPP)", "unity.md", "BepInEx 6 (IL2CPP) or MelonLoader; recover types with Cpp2IL / Il2CppDumper, patch through Il2CppInterop"}},
        {"unreal", {"Unreal Engine", "unreal.md", "UE4SS (Lua/C++ hooks, live property viewer) and/or pak mods in Content/Paks/~mods (FModel to browse, retoc/repak to pack)"}},
        {"godot", {"Godot", "godot.md", "recover the project with GDRE Tools; patch scripts/scenes via a PCK overlay or Godot Mod Loader"}},
        {"gamemaker", {"GameMaker", "misc-engines.md", "UndertaleModTool: decompile/edit GML, sprites and rooms in data.win"}},
        {"rpgmaker-mvmz", {"RPG Maker MV/MZ (NW.js)", "misc-engines.md", "JS plugins in js/plugins + js/plugins.js; data/*.json for database edits"}},
        {"rpgmaker-rgss", {"RPG Maker XP/VX/VX Ace (RGSS Ruby)", "misc-engines.md", "extract the RGSS archive, edit Ruby in Scripts.rvdata2"}},
        {"renpy", {"Ren'Py", "misc-engines.md", "unrpa/unrpyc to read; add .rpy files in game/"}},
        {"xna-fna", {"XNA/FNA/MonoGame (.NET)", "dotnet-xna.md", "the game's loader if any (tModLoader, SMAPI, Everest), else Harmony/MonoMod patches; read the exe with ILSpy"}},
        {"dotnet", {".NET application", "dotnet-xna.md", "Harmony/MonoMod patches via a loader (BepInEx supports .NET games too); read with ILSpy"}},
        {"source", {"Source (1)", "source.md", "custom/ + addons, VScript where supported, Source SDK mods; SourceMod only on servers you run"}},
        {"source2", {"Source 2", "source.md", "Workshop tools / addons, VScript/Panorama; decompile assets with Source 2 Viewer (VRF)"}},
        {"creation", {"Bethesda Creation/Gamebryo", "bethesda.md", "ESP/ESL plugins (xEdit, Creation Kit), Papyrus scripts, script-extender plugins, MO2 profiles"}},
        {"genie", {"Genie (Age of Empires DE)", "genie-aoe2.md", "data mods (genieutils-py), graphics/UI mods, scenarios; SLD sprites"}},
        {"clausewitz", {"Paradox Clausewitz/Jomini", "misc-engines.md", "script mods (plain text) in Documents/Paradox Interactive/<game>/mod"}},
        {"re-engine", {"Capcom RE Engine", "big-frameworks.md", "REFramework (Lua scripts, in-game UI) + Fluffy Mod Manager for pak/natives replacements"}},
        {"mt-framework", {"Capcom MT Framework", "big-frameworks.md", ".arc archives under native*/ (nativePC, nativeDX10, ...): ARCtool to unpack/repack, Fluffy Mod Manager to install; DeadRisingEx for Dead Rising; native code via DLL hooks"}},
        {"fromsoft", {"FromSoftware (Dantelion)", "big-frameworks.md", "ModEngine2 + Smithbox/DSMapStudio, param edits; offline only"}},
        {"rage", {"Rockstar RAGE", "big-frameworks.md", "story mode: ScriptHookV + ASI loader, OpenIV/CodeWalker for assets; never online"}},
        {"hedgehog", {"Hedgehog Engine (Sonic Team)", "big-frameworks.md", "Hedge Mod Manager + its HE1/HE2 mod loaders (Generations, Lost World, Forces, Frontiers, Shadow Generations) or Unleashed Recompiled's built-in loader: mod.ini folders of loose file replacements"}},
        {"blackrock", {"Black Rock Studio engine (Split/Second)", "big-frameworks.md", "no community tools: .ark packs, .model/.textures assets and plaintext .params config to reverse yourself; native code via DLL hooks"}},
        {"redengine", {"CD Projekt REDengine", "big-frameworks.md", "REDmod, Cyber Engine Tweaks, RED4ext, WolvenKit (Cyberpunk); REDkit (Witcher 3)"}},
        {"idtech", {"id Tech / Doom family", "misc-engines.md", "WAD/PK3/PK4 mods, source ports"}},
        {"cryengine", {"CryEngine", "native.md", "pak (zip) overrides, Lua/XML where exposed; native hooks otherwise"}},
        {"frostbite", {"Frostbite", "native.md", "Frosty Tool Suite for supported titles, offline only; most titles have kernel anti-cheat"}},
        {"electron", {"Electron / NW.js / HTML5", "misc-engines.md", "extract resources/app.asar (or package.nw), patch JS, open devtools"}},
        {"love2d", {"L\xC3\x96VE (Lua)", "misc-engines.md", "the .love/exe is a zip of Lua; patch or inject with lovely"}},
        {"zengin", {"ZenGin (Gothic 1/2)", "zengin.md", "Daedalus script mods first (MDK scripts -> .DAT in a .mod volume; Ikarus/LeGo, Ninja); Union plugin SDK (x86 C++ DLL in system/autorun, gothic-api headers) for engine code; GD3D11 renderer; assets in Data/*.vdf via ZenKit. Union/GD3D11 need G1 1.08k_mod or G2 NotR 2.6.0.0-rev2: check the exe build first (on Steam use the Workshop beta, don't swap the exe), see zengin.md"}},
        {"java", {"Java", "misc-engines.md", "decompile jars (Vineflower/CFR), patch with a mod loader or bytecode (Mixin/ASM)"}},
        {"defold", {"Defold", "misc-engines.md", "unpack game.arcd; Lua scripts"}},
        {"cocos", {"Cocos2d-x", "native.md", "Lua/JS scripts if bundled; else native hooks"}},
        {"haxe", {"Haxe/OpenFL/HaxeFlixel", "misc-engines.md", "assets/ overrides; hscript mod loaders where present (Polymod)"}},
        {"iw", {"IW engine (Call of Duty)", "iw-engine.md", "data overrides in mods/<name>/*.iwd (GSC/CSC scripts, IWI textures, weapons) + mod.ff via the Mod Tools' linker_pc.exe; um fastfile reads rawfiles out of your own .ff"}},
        {"native", {"Unknown native engine", "native.md", "proxy-DLL loader + function hooks (MinHook/SafetyHook), memory reading; Ghidra/IDA + x64dbg + Cheat Engine"}}};
    return v;
}
inline const Engine& engine(const std::string& k) { for (const auto& [a, b] : ENGINES()) if (a == k) return b; return ENGINES().back().second; }
inline bool is_engine(const std::string& k) { for (const auto& [a, b] : ENGINES()) if (a == k) return true; return false; }
inline std::vector<std::string> engine_keys_sorted() { std::vector<std::string> v; for (const auto& [a, b] : ENGINES()) v.push_back(a); std::sort(v.begin(), v.end()); return v; }

// ------------------------------------------------------------------ detection
struct Hit { std::string key; int score; std::vector<std::string> ev; Val det; };
inline bool tool_exe(const std::string& stem) {
    return re::search(R"(construction.?set|\bfcs\b|mod.?tool|mod.?manager|\bkmm\b|launcher|editor|setup|installer)", stem).ok;
}
inline std::string stem_of(const std::string& rel) { std::string n = rel.substr(rel.rfind('/') == std::string::npos ? 0 : rel.rfind('/') + 1); const auto d = n.rfind('.'); return d == std::string::npos || d == 0 ? n : n.substr(0, d); }
inline Val jlist(const std::vector<std::string>& v) { Val l = Val::list(); for (const auto& s : v) l.l->push_back(Val::str(s)); return l; }

inline std::pair<std::vector<Hit>, Val> detect(const Index& ix) {
    std::vector<Hit> hits; Val facts = Val::dict();
    auto add = [&](const std::string& key, int score, std::vector<std::string> ev, Val det = Val::dict()) { hits.push_back({key, score, std::move(ev), det.t == Val::DICT ? det : Val::dict()}); };
    auto D = [](std::initializer_list<std::pair<const char*, Val>> kv) { Val d = Val::dict(); for (const auto& [k, v] : kv) d.d->set(k, v); return d; };

    // Bannerlord: its own module system; a managed launcher alone suggests the wrong generic .NET route
    { Pats pats; for (const auto& d : BANNERLORD_BIN_DIRS) pats.push_back("bin/" + d + "/taleworlds.mountandblade.dll");
      const auto dlls = ix.find(pats);
      if (!dlls.empty() && ix.has({"modules/native/submodule.xml"})) {
          std::string dll = dlls[0]; std::size_t best = 99;
          for (const auto& f : dlls) { const std::string d = split(f, "/")[1]; const auto it = std::find(BANNERLORD_BIN_DIRS.begin(), BANNERLORD_BIN_DIRS.end(), d); const std::size_t k = static_cast<std::size_t>(it - BANNERLORD_BIN_DIRS.begin()); if (k < best) { best = k; dll = f; } }
          const std::string vf = dll.substr(0, dll.rfind('/')) + "/version.xml";
          Val det = D({{"product", Val::str("Mount and Blade II Bannerlord")}});
          if (ix.in(vf)) {
              try {
                  std::string data = slurp(ix.path(vf));
                  if (data.size() >= 2 && ((uint8_t(data[0]) == 0xFF && uint8_t(data[1]) == 0xFE) || (uint8_t(data[0]) == 0xFE && uint8_t(data[1]) == 0xFF))) {
                      const bool le = uint8_t(data[0]) == 0xFF; std::string u;
                      for (std::size_t k = 2; k + 1 < data.size(); k += 2) { const uint32_t c = le ? (uint8_t(data[k]) | uint8_t(data[k + 1]) << 8) : (uint8_t(data[k]) << 8 | uint8_t(data[k + 1])); u += utf8(c); }
                      data = re::sub(R"(^<\?xml[^>]*\?>)", "", u);
                  }
                  const auto root = pyrt::xml::fromstring(data);
                  std::string version = root->get("Value");
                  if (version.empty()) for (const auto& k : root->kids) if (k->tag == "Singleplayer") { version = k->get("Value"); if (version.empty()) version = k->get("value"); break; }
                  version = strip(version);
                  if (!version.empty()) { det.d->set("version", Val::str(version)); det.d->set("version_file", Val::str(vf)); }
              } catch (const std::exception&) {}
          }
          add("bannerlord", 100, {dll, "modules/native/submodule.xml"}, det);
      } }

    // Unity
    { std::vector<std::string> data_dirs; for (const auto& f : ix.find({"*_data/globalgamemanagers", "*_data/data.unity3d", "*_data/maindata"})) { const std::string d = f.substr(0, f.rfind('/') == std::string::npos ? 0 : f.rfind('/')); if (std::find(data_dirs.begin(), data_dirs.end(), d) == data_dirs.end()) data_dirs.push_back(d); }
      if (ix.has({"*unityplayer.dll", "*unityplayer.so", "*unityplayer.dylib"}) || !data_dirs.empty()) {
          const std::string dd = data_dirs.empty() ? "" : data_dirs[0];
          const bool il2cpp = ix.has({"*gameassembly.dll", "*gameassembly.so", "*gameassembly.dylib", "*il2cpp_data/metadata/global-metadata.dat"});
          const std::string ver = dd.empty() ? "" : unity_version(ix, dd);
          Val det = D({{"data_dir", Val::str(dd)}, {"version", ver.empty() ? Val() : Val::str(ver)}});
          const std::string info = dd.empty() ? "" : dd + "/app.info";
          if (!info.empty() && ix.in(info)) { auto ls = splitlines(decode_utf8_replace(slurp(ix.path(info)))); ls.emplace_back(""); ls.emplace_back(""); det.d->set("company", Val::str(ls[0])); det.d->set("product", Val::str(ls[1])); }
          if (il2cpp) add("unity-il2cpp", 100, {"UnityPlayer + GameAssembly / global-metadata.dat"}, det);
          else add("unity-mono", 100, {ix.has({"*_data/managed/assembly-csharp.dll"}) ? "UnityPlayer + Managed/Assembly-CSharp.dll" : "UnityPlayer"}, det);
      } }

    // Unreal
    { const auto shipping = ix.find({"*/binaries/win64/*-win64-shipping.exe", "*/binaries/win64/*-wingdk-shipping.exe", "*/binaries/linux/*-linux-shipping"});
      const auto paks = ix.find({"*/content/paks/*.pak", "*/content/paks/*.utoc"});
      if (!shipping.empty() || !paks.empty() || ix.has_dir({"engine/binaries/thirdparty"})) {
          Val det = Val::dict();
          if (!shipping.empty()) {
              det.d->set("project", Val::str(split(shipping[0], "/")[0])); det.d->set("exe", Val::str(shipping[0]));
              const std::string v = unreal_version(ix.path(shipping[0])); if (!v.empty()) det.d->set("engine_version", Val::str(v));
          }
          det.d->set("iostore", Val::boolean(ix.has({"*.utoc"}))); det.d->set("paks", Val::integer(static_cast<long long>(paks.size())));
          add("unreal", shipping.empty() ? 70 : 100, {shipping.empty() ? "Content/Paks" : "*-Win64-Shipping.exe"}, det);
      } }

    // Godot
    { bool found = false; const auto pcks = ix.find({"*.pck"});
      for (std::size_t k = 0; k < pcks.size() && k < 3; ++k) { const std::string v = godot_pck(ix.path(pcks[k])); if (!v.empty()) { add("godot", 100, {pcks[k]}, D({{"version", Val::str(v)}})); found = true; break; } }
      if (!found) { const auto ex = take(ix.find({"*.exe", "*.x86_64"}), 6); for (const auto& e : ex) { const std::string v = godot_pck(ix.path(e)); if (!v.empty()) { add("godot", 100, {e + " (embedded pck)"}, D({{"version", Val::str(v)}})); break; } } } }

    // GameMaker
    for (const char* name : {"data.win", "game.unx", "game.ios", "game.droid", "assets/game.unx"}) {
        if (!ix.in(name)) continue;
        Val det = Val::dict(); const std::string head = head_bytes(ix.path(name), 64);
        if (head.size() > 17 && head.substr(0, 4) == "FORM" && head.substr(8, 4) == "GEN8") det.d->set("bytecode_version", Val::integer(uint8_t(head[17])));
        add("gamemaker", 100, {name}, det); break;
    }
    // RPG Maker
    if (ix.has({"www/js/rpg_core.js", "js/rpg_core.js"})) add("rpgmaker-mvmz", 100, {"rpg_core.js (MV)"}, D({{"version", Val::str("MV")}}));
    else if (ix.has({"js/rmmz_core.js", "www/js/rmmz_core.js"})) add("rpgmaker-mvmz", 100, {"rmmz_core.js (MZ)"}, D({{"version", Val::str("MZ")}}));
    if (ix.has({"game.rgssad", "game.rgss2a", "game.rgss3a", "data/scripts.rxdata", "data/scripts.rvdata", "data/scripts.rvdata2"})) add("rpgmaker-rgss", 100, take(ix.find({"game.rgss*", "data/scripts.r*"}), 2));
    // Ren'Py
    if (ix.has_dir({"renpy"}) && ix.has_dir({"game"})) add("renpy", 100, {"renpy/ + game/"}, D({{"archives", Val::integer(static_cast<long long>(ix.find({"game/*.rpa"}).size()))}}));
    // ZenGin
    if (ix.has({"system/gothic.exe", "system/gothic1.exe", "system/gothic2.exe"}) || (ix.has({"system/vdfs32g.dll", "system/vdfs32.dll"}) && ix.has({"data/*.vdf"})))
        add("zengin", 95, cat(take(ix.find({"system/gothic*.exe"}), 1), take(ix.find({"data/*.vdf"}), 1)));

    // XNA / FNA / MonoGame / .NET
    { const auto xna = ix.find({"fna.dll", "monogame.framework.dll", "microsoft.xna.framework*.dll", "*/fna.dll"});
      std::vector<std::string> exes;
      for (const auto& f : ix.files) { if (!ends(f, ".exe")) continue; const std::string base = f.substr(f.rfind('/') == std::string::npos ? 0 : f.rfind('/') + 1); if (starts(base, "unins") || in_skip_dir(f)) continue; exes.push_back(f); }
      std::stable_sort(exes.begin(), exes.end(), [](const std::string& a, const std::string& b) { const auto ca = std::count(a.begin(), a.end(), '/'), cb = std::count(b.begin(), b.end(), '/'); return ca != cb ? ca < cb : a < b; });
      exes = take(exes, 12);
      std::vector<std::string> managed; std::map<std::string, long long> sizes;
      for (const auto& e : exes) {
          const PeInfo info = pe_info(ix.path(e));
          if (info.ok) {
              if (!has(facts, "executables")) facts.d->set("executables", Val::dict());
              facts.d->get("executables")->d->set(e, D({{"arch", Val::str(info.arch)}, {"managed", Val::boolean(info.managed)}}));
              if (info.managed) managed.push_back(e);
          }
          const long long sz = fsize(ix.path(e)); if (sz >= 0) sizes[e] = sz;
      }
      auto sz_of = [&](const std::string& e) { const auto it = sizes.find(e); return it == sizes.end() ? 0LL : it->second; };
      if (!xna.empty() || ix.has({"content/*.xnb"})) add("xna-fna", 95, cat(take(xna.empty() ? std::vector<std::string>{"Content/*.xnb"} : xna, 2), take(managed, 1)));
      else if (!managed.empty() || !ix.find({"*.runtimeconfig.json"}).empty()) {
          long long biggest = 0; for (const auto& m : managed) biggest = std::max(biggest, sz_of(m));
          std::vector<std::string> big_native;
          for (const auto& e : exes) if (std::find(managed.begin(), managed.end(), e) == managed.end() && !tool_exe(stem_of(e)) && sz_of(e) > biggest) big_native.push_back(e);
          std::stable_sort(big_native.begin(), big_native.end(), [&](const std::string& a, const std::string& b) { return sz_of(a) > sz_of(b); });
          bool apphost = false;
          if (!big_native.empty()) { const std::string b = big_native[0].substr(0, big_native[0].size() - 4); apphost = ix.in(b + ".runtimeconfig.json") || ix.in(b + ".deps.json"); }
          bool all_tools = !managed.empty(); for (const auto& m : managed) if (!tool_exe(stem_of(m))) all_tools = false;
          if (!managed.empty() && !big_native.empty() && !apphost && all_tools) {
              const auto ogre = take(ix.find({"*ogremain*.dll"}), 2);
              add("native", 65, cat(take(big_native, 1), ogre), D({{"note", Val::str("managed exe(s) look like tools (" + join(managed, ", ") + "); game binary is native")}}));
          } else add("dotnet", 70, take(!managed.empty() ? managed : ix.find({"*.runtimeconfig.json"}), 2));
      } }
    // OGRE: a secondary signal
    { bool have_native = false; for (const auto& h : hits) if (h.key == "native") have_native = true;
      if (!have_native && ix.has({"*ogremain*.dll"})) { const auto plug = ix.find({"plugins.cfg", "plugins_*.cfg"}); Val det = Val::dict(); if (!plug.empty()) det.d->set("plugin_cfg", Val::str(plug[0])); add("native", 55, cat(take(ix.find({"*ogremain*.dll"}), 2), take(plug, 1)), det); } }

    // Source / Source 2
    if (ix.has({"*/gameinfo.gi"}) || ix.has({"game/bin/win64/engine2.dll"})) { auto e = take(ix.find({"*/gameinfo.gi"}), 2); add("source2", 100, e.empty() ? std::vector<std::string>{"game/bin/win64/engine2.dll"} : e); }
    else if (ix.has({"*/gameinfo.txt"}) && (ix.has({"*_dir.vpk"}) || ix.has({"bin/engine.dll", "bin/x64/engine.dll"}))) add("source", 100, take(ix.find({"*/gameinfo.txt"}), 2));
    if (ix.has({"data/*.esm"}) && ix.has({"data/*.bsa", "data/*.ba2"})) add("creation", 100, take(ix.find({"data/*.esm"}), 3));
    if (ix.has({"resources/_common/dat/*.dat"}) && ix.has({"aoe2de_s.exe", "aoede_s.exe", "aoe*_s.exe"})) add("genie", 100, take(ix.find({"resources/_common/dat/*.dat"}), 1));
    if (ix.has_dir({"common"}) && ix.has_dir({"events"}) && ix.has({"launcher-settings.json", "*/launcher-settings.json"})) add("clausewitz", 90, {"common/ + events/ + launcher-settings.json"});
    if (ix.has({"re_chunk_000.pak", "re_chunk_000.pak.*"})) add("re-engine", 100, {"re_chunk_000.pak"});
    if (ix.has({"native*/*.arc"})) add("mt-framework", 95, take(ix.find({"native*/*.arc"}), 2));
    if (ix.has({"eldenring.exe", "game/eldenring.exe", "darksoulsiii.exe", "game/darksoulsiii.exe", "sekiro.exe", "armoredcore6.exe", "game/armoredcore6.exe", "nightreign.exe", "game/nightreign.exe"}) || ix.has({"*/regulation.bin", "regulation.bin"}))
        add("fromsoft", 95, take(ix.find({"*eldenring.exe", "*darksoulsiii.exe", "*sekiro.exe", "*armoredcore6.exe", "*nightreign.exe", "*regulation.bin"}), 2));
    { const auto he = ix.find({"unleashedrecomp.exe", "unleashedrecomp", "sonicgenerations.exe", "slw.exe", "*sonic forces.exe", "sonicfrontiers.exe", "sonic_x_shadow_generations.exe"});
      if (!he.empty() || (ix.has({"*.arl"}) && ix.has({"*.ar", "*.ar.??"}))) add("hedgehog", 95, cat(take(he, 1), take(ix.find({"*.arl"}), 1))); }
    if (ix.has({"*.shaderinst"}) && ix.has({"*.model"})) add("blackrock", 95, cat(take(ix.find({"*.shaderinst"}), 1), take(ix.find({"*.model"}), 1)));
    if (ix.has({"gta5.exe", "gta5_enhanced.exe", "playgtav.exe", "rdr2.exe", "gtaiv.exe"}) || (ix.has({"*.rpf"}) && ix.has({"*.exe"}))) add("rage", 95, take(ix.find({"gta5*.exe", "playgtav.exe", "rdr2.exe", "*.rpf"}), 2));
    if (ix.has({"archive/pc/content/*.archive"}) || ix.has({"bin/x64/witcher3.exe", "content/content0/*.bundle"})) add("redengine", 100, take(ix.find({"archive/pc/content/*.archive", "bin/x64/witcher3.exe"}), 1));
    if (ix.has({"*.wad", "*.pk3", "base/*.pk4", "id1/pak0.pak", "base/*.resources"})) add("idtech", 70, take(ix.find({"*.wad", "*.pk3", "base/*.pk4", "id1/pak0.pak", "base/*.resources"}), 2));
    if (ix.has({"*crysystem.dll"})) add("cryengine", 95, take(ix.find({"*crysystem.dll"}), 1));
    if (ix.has({"data/cas.cat", "*layout.toc"})) add("frostbite", 90, take(ix.find({"data/cas.cat", "*layout.toc"}), 1));
    if (ix.has({"resources/app.asar", "resources/app/package.json", "package.nw", "nw.dll", "*/nw.dll"}))
        add("electron", 90, take(ix.find({"resources/app.asar", "resources/app/package.json", "package.nw", "nw.dll"}), 2), D({{"construct", Val::boolean(ix.has({"*c3runtime.js", "*c2runtime.js"}))}, {"phaser", Val::boolean(ix.has({"*phaser*.js"}))}}));
    else if (ix.has({"index.html"}) && ix.has({"*.js"}) && hits.empty()) add("electron", 50, {"index.html + js"});
    if (ix.has({"love.dll", "*.love", "lovec.exe"})) add("love2d", 95, take(ix.find({"love.dll", "*.love"}), 1));
    { std::vector<std::string> jars; for (const auto& j : ix.find({"*.jar"})) if (std::count(j.begin(), j.end(), '/') <= 1 && !in_skip_dir(j)) jars.push_back(j);
      if (!jars.empty() && (ix.has_dir({"jre", "jre/*", "jdk*", "java*"}) || jars.size() <= 5)) add("java", 60, take(jars, 2)); }
    if (ix.has({"game.dmanifest", "game.arcd"})) add("defold", 100, {"game.arcd"});
    if (ix.has({"*libcocos2d*.dll", "*cocos2d*.dll"})) add("cocos", 90, take(ix.find({"*libcocos2d*.dll", "*cocos2d*.dll"}), 1));
    if (ix.has({"lime.ndll", "*lime.hdll"})) add("haxe", 80, take(ix.find({"lime.ndll", "*lime.hdll"}), 1));
    if (ix.has({"zone/*/*.ff", "zone/*.ff"}) || ix.has({"main/*.iwd"}) || ix.has({"iw3sp.exe", "iw3mp.exe", "codwaw.exe", "codwawmp.exe", "iw4sp.exe", "iw4mp.exe"}))
        add("iw", 95, take(ix.find({"zone/*/*.ff", "zone/*.ff", "main/*.iwd", "*iw*.exe", "*codwaw*.exe"}), 3));
    if (hits.empty()) add("native", 10, {"no known engine signature"});
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.score > b.score; });
    return {hits, facts};
}

inline std::string fmt_tpl(std::string t, const std::map<std::string, std::string>& wf) { for (const auto& [k, v] : wf) t = replace_all(t, "{" + k + "}", v); return t; }
inline List save_hints(const std::string& name, const Val& det) {
    const auto& wf = win_folders();
    auto W = [&](const char* k) { const auto it = wf.find(k); return it == wf.end() ? std::string() : it->second; };
    const std::string prof = W("profile"), docs = W("documents"), appdata = W("appdata"), local = W("localappdata");
    const std::string base = re::sub("[:\xC2\xAE\xE2\x84\xA2]", "", name);
    std::vector<std::string> names;
    auto addn = [&](const std::string& n) { if (!n.empty() && std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n); };
    addn(name); addn(base); addn(split(base, " - ")[0]); addn(re::sub(R"(\s*(definitive edition|legacy|enhanced|remastered)$)", "", base, 0, re::I));
    addn(sv(get(det, "product"))); addn(sv(get(det, "project")));
    std::vector<std::string> c;
    { const auto it = KNOWN_SAVES().find(lower(name));
      if (it != KNOWN_SAVES().end()) for (const auto& t : it->second) { bool ok = true; for (const auto& k : re::findall(R"(\{(\w+)\})", t)) if (!wf.count(k)) ok = false; if (ok) c.push_back(fmt_tpl(t, wf)); } }
    if (truthy(get(det, "company")) && truthy(get(det, "product")) && !prof.empty()) c.push_back(path_join(path_join(path_join(prof, "AppData/LocalLow"), sv(get(det, "company"))), sv(get(det, "product"))));
    for (const auto& n : names) {
        std::vector<std::string> ds; if (!docs.empty()) ds.push_back(docs); if (!prof.empty()) { const std::string pd = path_join(prof, "Documents"); if (std::find(ds.begin(), ds.end(), pd) == ds.end()) ds.push_back(pd); }
        for (const auto& d : ds) { c.push_back(path_join(path_join(d, "My Games"), n)); c.push_back(path_join(d, n)); }
        if (!prof.empty()) { c.push_back(path_join(path_join(prof, "Saved Games"), n)); c.push_back(path_join(path_join(prof, "Games"), n)); }
        if (!appdata.empty()) c.push_back(path_join(appdata, n));
        if (!local.empty()) { c.push_back(path_join(local, n)); c.push_back(path_join(path_join(local, n), "Saved")); }
    }
    if (is_mac()) for (const auto& n : names) c.push_back(path_join(path_join(home_dir(), "Library/Application Support"), n));
    if (!is_windows() && !is_wsl()) { for (const auto& n : names) c.push_back(path_join(path_join(home_dir(), ".local/share"), n)); for (const auto& n : names) c.push_back(path_join(path_join(home_dir(), ".config"), n)); }
    List out; std::set<std::string> seen;
    for (const auto& x : c) if (isdir(x) && !seen.count(x)) { seen.insert(x); out.push_back(Val::str(x)); }
    return out;
}

inline Val host_info() {
    std::string machine, os_name, version; bool translated = false;
#ifdef _WIN32
    SYSTEM_INFO si{}; GetNativeSystemInfo(&si);
    machine = si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "amd64" : si.wProcessorArchitecture == 12 ? "arm64" : si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL ? "x86" : "unknown";
    os_name = "windows";
    { using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW); RTL_OSVERSIONINFOW vi{}; vi.dwOSVersionInfoSize = sizeof vi;
      HMODULE nt = GetModuleHandleW(L"ntdll.dll"); auto fn = nt ? reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(nt, "RtlGetVersion"))) : nullptr;
      if (fn && fn(&vi) == 0) version = std::to_string(vi.dwMajorVersion) + "." + std::to_string(vi.dwMinorVersion) + "." + std::to_string(vi.dwBuildNumber); }
#else
    struct utsname u{}; uname(&u); machine = lower(u.machine);
    if (is_mac()) {
        os_name = "macos";
        const auto r = mk::run_proc({"sw_vers", "-productVersion"}, "", {}, 5); version = strip(decode_utf8_replace(r.out));
        if (machine == "x86_64") { const auto t = mk::run_proc({"sysctl", "-n", "sysctl.proc_translated"}, "", {}, 5); if (strip(decode_utf8_replace(t.out)) == "1") translated = true; }
    } else {
        os_name = "linux";
        for (const char* f : {"/etc/os-release", "/usr/lib/os-release"}) {
            for (const auto& ln : splitlines(slurp(f))) if (starts(ln, "PRETTY_NAME=")) { std::string v = ln.substr(12); if (v.size() >= 2 && (v[0] == '"' || v[0] == '\'')) v = v.substr(1, v.size() - 2); version = v; }
            if (!version.empty()) break;
        }
        if (version.empty()) version = u.release;
    }
#endif
    static const std::map<std::string, std::string> A = {{"x86_64", "x64"}, {"amd64", "x64"}, {"x86", "x86"}, {"i386", "x86"}, {"i686", "x86"}, {"arm64", "arm64"}, {"aarch64", "arm64"}};
    std::string arch = A.count(machine) ? A.at(machine) : machine;
    if (translated) arch = "arm64";
    // tests: UM_FAKE_HOST=os,version,arch
    if (!env("UM_FAKE_HOST").empty()) { const auto p = split(env("UM_FAKE_HOST"), ","); if (p.size() >= 3) { os_name = p[0]; version = p[1]; arch = p[2]; } }
    Val h = Val::dict(); h.d->set("os", Val::str(os_name)); h.d->set("os_version", Val::str(version.empty() ? "unknown" : version)); h.d->set("arch", Val::str(arch));
    h.d->set("wsl", Val::boolean(is_wsl())); h.d->set("translated", Val::boolean(translated));
    return h;
}
inline std::string host_line(const Val& h) {
    const std::string os = sv(get(h, "os")), ver = sv(get(h, "os_version")); std::string base;
    if (truthy(get(h, "wsl"))) base = "Windows via WSL (" + ver + ")";
    else if (os == "linux" && !(ver.size() && dig(ver[0]))) base = ver;
    else base = (os == "macos" ? "macOS" : os == "windows" ? "Windows" : os == "linux" ? "Linux" : os) + " " + ver;
    return base + ", " + sv(get(h, "arch")) + (truthy(get(h, "translated")) ? " (Python runs under Rosetta)" : "");
}

inline Val scan(const std::string& query) {
    const Val g = resolve_game(query);
    const std::string root = sv(get(g, "path"));
    const Index ix(root);
    auto [hits, facts] = detect(ix);
    const Hit& top = hits[0];
    const Engine& E = engine(top.key);
    auto present = [&](const Pats& pats) { Pats dirs; for (const auto& p : pats) if (ends(p, "/*")) { std::string d = p; while (!d.empty() && (d.back() == '*' || d.back() == '/')) d.pop_back(); dirs.push_back(d); } return ix.has(pats) || ix.has_dir(dirs); };
    std::vector<std::string> anti, loaders, moddirs;
    for (const auto& n : ANTI_CHEAT()) if (present(n.pats)) anti.push_back(n.name);
    for (const auto& n : LOADERS()) if (present(n.pats)) loaders.push_back(n.name);
    for (const auto& d : MOD_DIRS) if (ix.dirs.count(d)) moddirs.push_back(d);
    if (top.key == "bannerlord" && ix.dirs.count("modules")) moddirs.emplace_back("modules");
    std::string name = sv(get(g, "name")); if (name.empty()) name = u8native(P(root).filename());
    const std::string lname = lower(name);
    const Known* known = nullptr;
    { std::vector<std::pair<std::string, Known>> ks(KNOWN().begin(), KNOWN().end());
      std::stable_sort(ks.begin(), ks.end(), [](const auto& a, const auto& b) { return cplen(a.first) > cplen(b.first); });
      for (const auto& [k, v] : ks) if (k == lname || (lname.find(k) != std::string::npos && cplen(k) > 5)) { known = known_of(k); break; }
      if (!known) for (const auto& [k, pats] : KNOWN_FILES()) if (ix.has(pats)) { known = known_of(k); break; } }
    const std::string online = online_only(name);
    List routes;
    auto R = [](const std::string& r, const std::string& pb, const std::string& why) { Val d = Val::dict(); d.d->set("route", Val::str(r)); d.d->set("playbook", Val::str(pb)); d.d->set("why", Val::str(why)); return d; };
    if (known) routes.push_back(R(known->route, known->playbook, "known game"));
    if (!known || known->playbook != E.playbook || top.key == "native") routes.push_back(R(E.route, E.playbook, "engine: " + E.label));
    if (top.key != "native" && top.key != "bannerlord") routes.push_back(R(engine("native").route, "native.md", "fallback when no loader reaches what you need"));
    List warnings; const Val host = host_info();
    if (ix.files.size() < 5) warnings.push_back(Val::str("the install folder is (nearly) empty: the game is not fully installed, or lives elsewhere"));
    if (truthy(get(facts, "executables")) && sv(get(host, "os")) != "windows" && !truthy(get(host, "wsl")))
        warnings.push_back(Val::str("the exes are Windows (PE) builds but this host is " + host_line(host) + ": not native, so it needs a compatibility layer (unless the folder also holds a build for this OS); read skills/game-recon/references/compatibility.md"));
    if (!anti.empty()) warnings.push_back(Val::str("anti-cheat present (" + join(anti, ", ") + "): offline/single-player only, never inject into online play; many titles need the anti-cheat disabled via an official offline launch option - do not bypass it"));
    if (!online.empty()) warnings.push_back(Val::str("'" + online + "' is an online competitive game: modding its client breaks the ToS and gets accounts banned - stop, or use official tools only (Workshop/creative modes)"));
    if (std::find(loaders.begin(), loaders.end(), "ScriptHookV") != loaders.end() && top.key.find("rage") != std::string::npos) warnings.push_back(Val::str("ScriptHookV only works in story mode; launch GTA offline"));
    Val eng = Val::dict(); eng.d->set("key", Val::str(top.key)); eng.d->set("label", Val::str(E.label)); eng.d->set("confidence", Val::integer(top.score)); eng.d->set("evidence", jlist(top.ev));
    for (const auto& [k, v] : top.det.d->items) if (!v.is_none() && !(v.t == Val::STR && v.s.empty())) eng.d->set(k, v);
    List others; for (std::size_t k = 1; k < hits.size() && k < 4; ++k) { Val o = Val::dict(); o.d->set("key", Val::str(hits[k].key)); o.d->set("evidence", jlist(hits[k].ev)); others.push_back(o); }
    Val r = Val::dict();
    r.d->set("name", Val::str(name)); r.d->set("store", get(g, "store")); r.d->set("appid", get(g, "appid")); r.d->set("path", Val::str(root));
    r.d->set("engine", eng); r.d->set("other_engine_signals", Val::list(others)); r.d->set("anti_cheat", jlist(anti)); r.d->set("mod_loaders_installed", jlist(loaders));
    r.d->set("mod_folders", jlist(moddirs)); r.d->set("workshop", get(g, "workshop")); r.d->set("saves", Val::list(save_hints(name, top.det)));
    r.d->set("executables", truthy(get(facts, "executables")) ? get(facts, "executables") : Val::dict()); r.d->set("routes", Val::list(routes)); r.d->set("warnings", Val::list(warnings));
    r.d->set("playbook", Val::str("skills/mod-any-game/references/engines/" + sv(get(routes[0], "playbook"))));
    r.d->set("files_indexed", Val::integer(static_cast<long long>(ix.files.size()))); r.d->set("index_truncated", Val::boolean(ix.truncated)); r.d->set("host", host);
    return r;
}

inline std::string format_report(const Val& r) {
    const Val e = get(r, "engine");
    std::vector<std::string> lines;
    lines.push_back(sv(get(r, "name")) + (truthy(get(r, "store")) ? "  (" + pystr(get(r, "store")) + " " + pystr(get(r, "appid")) + ")" : ""));
    lines.push_back("  path:      " + sv(get(r, "path")));
    if (truthy(get(r, "host"))) lines.push_back("  host:      " + host_line(get(r, "host")));
    std::vector<std::string> ev; for (const auto& x : *get(e, "evidence").l) ev.push_back(pystr(x));
    lines.push_back("  engine:    " + sv(get(e, "label")) + "  [" + pystr(get(e, "confidence")) + "%]  evidence: " + join(ev, ", "));
    std::vector<std::string> extra;
    for (const auto& [k, v] : e.d->items) if (k != "key" && k != "label" && k != "confidence" && k != "evidence") extra.push_back(k + "=" + (v.t == Val::BOOL ? (v.b ? "True" : "False") : pystr(v)));
    if (!extra.empty()) lines.push_back("             " + join(extra, ", "));
    const Val others = get(r, "other_engine_signals");
    if (others.size()) { std::vector<std::string> o; for (const auto& x : *others.l) { const Val ev2 = get(x, "evidence"); o.push_back(sv(get(x, "key")) + " (" + (ev2.size() ? pystr((*ev2.l)[0]) : "") + ")"); } lines.push_back("  also:      " + join(o, "; ")); }
    const Val ex = get(r, "executables");
    if (ex.size()) { std::vector<std::string> o; std::size_t n = 0; for (const auto& [k, v] : ex.d->items) { if (n++ >= 5) break; o.push_back(k + " [" + sv(get(v, "arch")) + (truthy(get(v, "managed")) ? ", .NET" : "") + "]"); } lines.push_back("  exes:      " + join(o, ", ")); }
    auto lj = [&](const char* k) { std::vector<std::string> o; for (const auto& x : *get(r, k).l) o.push_back(pystr(x)); return join(o, ", "); };
    lines.push_back("  anti-cheat: " + (lj("anti_cheat").empty() ? std::string("none found") : lj("anti_cheat")));
    lines.push_back("  loaders:   " + (lj("mod_loaders_installed").empty() ? std::string("none installed") : lj("mod_loaders_installed")));
    if (get(r, "mod_folders").size()) lines.push_back("  mod dirs:  " + lj("mod_folders"));
    if (truthy(get(r, "workshop"))) lines.push_back("  workshop:  " + pystr(get(r, "workshop")));
    if (get(r, "saves").size()) { std::vector<std::string> o; for (const auto& x : *get(r, "saves").l) o.push_back(pystr(x)); lines.push_back("  saves:     " + join(o, "\n             ")); }
    lines.emplace_back("  routes:");
    long n = 0; for (const auto& rt : *get(r, "routes").l) lines.push_back("    " + std::to_string(++n) + ". " + sv(get(rt, "route")) + "  (" + sv(get(rt, "why")) + "; read references/engines/" + sv(get(rt, "playbook")) + ")");
    for (const auto& w : *get(r, "warnings").l) lines.push_back("  WARNING:   " + pystr(w));
    if (truthy(get(r, "index_truncated"))) lines.emplace_back("  note:      file index truncated (huge install); pass a subfolder for detail");
    return join(lines, "\n");
}

inline const char* DOC =
    "Find installed games and fingerprint one: engine, scripting runtime, anti-cheat, mod loaders, saves.\n\n"
    "    um scan --list                 # Steam / Epic / Xbox installs on this machine (Windows, WSL, Linux, macOS)\n"
    "    um scan terraria               # fuzzy name or a path; prints the report\n"
    "    um scan \"C:\\\\Games\\\\Foo\" --json  # machine-readable, for agents\n\n"
    "The report starts with `host:`, the machine `um` runs on: OS and version, CPU (named like the exes' `[x64]`,\n"
    "`[x86]`, `[arm64]`) and WSL. Compare them to know whether the game is native here. It warns when the exes are\n"
    "Windows builds and the host is neither Windows nor WSL.\n\n"
    "The report ends with ranked modding routes and the playbook to read next\n"
    "(skills/mod-any-game/references/engines/*.md). Everything here is read-only.\n";

inline void reg(Parser& sub) {
    Parser& p = sub.sub("scan", DOC); p.help_line = "find installed games; fingerprint engine, anti-cheat, loaders, saves, routes"; p.raw_description = true;
    p.pos("game").nargs("?").help("name (fuzzy) or install folder");
    p.opt({"--list"}).store_true().help("list installed games (Steam, Epic, Xbox)");
    p.opt({"--json"}).store_true();
}
inline int main_(const Val& a) {
    if (truthy(get(a, "list"))) {
        List games = all_games();
        if (truthy(get(a, "json"))) { emit_json(Val::list(games)); return 0; }
        if (games.empty()) { print("no Steam/Epic/Xbox installs found; pass a game folder to `um scan <path>`"); return 0; }
        std::stable_sort(games.begin(), games.end(), [](const Val& x, const Val& y) { return lower(sv(get(x, "name"))) < lower(sv(get(y, "name"))); });
        for (const auto& g : games) print(ljust(sv(get(g, "store")), 6) + " " + rjust(truthy(get(g, "appid")) ? pystr(get(g, "appid")) : "", 10) + "  " + pystr(get(g, "name")) + "  ->  " + sv(get(g, "path")));
        return 0;
    }
    if (S(a, "game").empty()) die("give a game name or folder, or --list");
    const Val r = scan(S(a, "game"));
    print(truthy(get(a, "json")) ? jdump(r) : format_report(r));
    return 0;
}
}  // namespace packtools::modder::scan
