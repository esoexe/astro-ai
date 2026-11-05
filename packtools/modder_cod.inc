// modder_cod.inc - `um weapon` and `um fastfile`: C++ ports of universal-modder's um/weapon.py and um/fastfile.py
// (Rehan and universal-modder contributors, MIT). Call of Duty 4 / World at War weapon files and unsigned PC zones.
namespace packtools::modder {
// latin-1 text (one byte = one character) <-> UTF-8, the way Python's read_text("latin-1") and print() meet
inline std::string latin1_to_utf8(const std::string& s) {
    std::string o; o.reserve(s.size());
    for (unsigned char c : s) { if (c < 0x80) o += static_cast<char>(c); else { o += static_cast<char>(0xC0 | (c >> 6)); o += static_cast<char>(0x80 | (c & 0x3F)); } }
    return o;
}
inline std::string utf8_to_latin1(const std::string& s) {   // UnicodeEncodeError for characters past U+00FF
    std::string o; o.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) { o += static_cast<char>(c); continue; }
        if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            const unsigned v = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
            if (v <= 0xFF) { o += static_cast<char>(v); ++i; continue; }
        }
        throw PyErr("UnicodeEncodeError", "'latin-1' codec can't encode a character: only U+0000..U+00FF fit in a weapon file");
    }
    return o;
}
inline long long floordiv(long long a, long long b) { const long long q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }
// str(Path(s)) on POSIX: no empty or "." parts, no trailing slash ("" -> ".")
inline std::string path_str(const std::string& s) {
    if (s.empty()) return ".";
    const bool abs = s[0] == '/'; const bool two = abs && s.size() > 1 && s[1] == '/' && (s.size() < 3 || s[2] != '/');
    std::vector<std::string> out; for (const auto& c : split(s, "/")) if (!c.empty() && c != ".") out.push_back(c);
    const std::string r = (two ? "//" : abs ? "/" : "") + join(out, "/");
    return r.empty() ? "." : r;
}
inline bool py_space_latin1(unsigned char c) { return c == ' ' || (c >= 0x09 && c <= 0x0D) || (c >= 0x1C && c <= 0x1F) || c == 0x85 || c == 0xA0; }
inline std::string strip_latin1(const std::string& s) {   // str.strip() on latin-1 decoded text
    std::size_t a = 0, b = s.size();
    while (a < b && py_space_latin1(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && py_space_latin1(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}
}  // namespace packtools::modder

namespace packtools::modder::weapon {
using namespace pyrt;
using namespace packtools::modder;

inline const char* DOC =
    "Call of Duty weapon file inspector, editor, and generator.\n\n"
    "    um weapon get raw/weapons/sp/colt\n"
    "    um weapon get raw/weapons/sp/colt damage\n"
    "    um weapon set raw/weapons/sp/colt damage=500 clipSize=16 -o mods/MyMod/weapons/sp/colt_modded\n"
    "    um weapon clone raw/weapons/sp/colt mods/MyMod/weapons/sp/colt_upgraded --damage 1000 --clip 16\n"
    "    um weapon diff raw/weapons/sp/colt raw/weapons/sp/colt_dirty_harry\n\n"
    "Inspects, parses, and edits Call of Duty: World at War and CoD4 weapon files.\n"
    "Handles the engine's backslash-delimited dictionary format (WEAPONFILE\\\\key\\\\value\\\\...),\n"
    "preserving all 300+ properties including viewmodel animations, recoil kick, and\n"
    "ballistic damage falloff curves.\n\n"
    "set and clone write a new file (-o / the destination): point it at your mod's copy, never\n"
    "at the Mod Tools' raw/ folder or the game's files. set --in-place edits the file itself.\n"
    "Weapon files are Treyarch's and Infinity Ward's data: keep copies of raw/ files out of any\n"
    "repo, and ship your changes as a diff or a build step that runs set/clone on the player's\n"
    "own raw/ files.\n";

// properties keep latin-1 bytes; ordered map: (key, value) in file order, later duplicates update in place
struct Weapon { std::string header; std::vector<std::pair<std::string, std::string>> props; std::vector<std::string> order; };
inline std::string* find(Weapon& w, const std::string& k) { for (auto& [a, v] : w.props) if (a == k) return &v; return nullptr; }
inline const std::string* find(const Weapon& w, const std::string& k) { for (const auto& [a, v] : w.props) if (a == k) return &v; return nullptr; }
inline void put(Weapon& w, const std::string& k, const std::string& v) { if (std::string* p = find(w, k)) *p = v; else w.props.emplace_back(k, v); }

inline Weapon parse_weapon(const std::string& path) {
    if (!isfile(path)) die("weapon file not found: " + path);
    const std::string text = strip_latin1(slurp(path));
    const std::vector<std::string> tokens = split(text, "\\");
    Weapon w; w.header = tokens.empty() ? "WEAPONFILE" : tokens[0];
    for (std::size_t i = 1; i + 1 < tokens.size(); i += 2) { put(w, tokens[i], tokens[i + 1]); w.order.push_back(tokens[i]); }
    return w;
}
inline void write_weapon(const std::string& path, const Weapon& w) {
    std::vector<std::string> parts{w.header}; std::set<std::string> seen;
    for (const auto& k : w.order) if (const std::string* v = find(w, k)) { parts.push_back(k); parts.push_back(*v); seen.insert(k); }
    for (const auto& [k, v] : w.props) if (!seen.count(k)) { parts.push_back(k); parts.push_back(v); }
    spit(path, join(parts, "\\") + "\\");
}
inline void get_weapon(const std::string& path, const std::string& key) {
    const Weapon w = parse_weapon(path); const std::string name = name_of_path(path);
    if (!key.empty()) {
        const std::string k = utf8_to_latin1(key);
        if (const std::string* v = find(w, k)) print(key + " = " + latin1_to_utf8(*v));
        else die("key '" + key + "' not found in " + name);
        return;
    }
    print("Weapon: " + name + " (" + std::to_string(w.props.size()) + " properties)");
    for (const char* h : {"displayName", "weaponClass", "weaponType", "damage", "minDamage", "clipSize", "maxAmmo", "fireTime", "reloadTime",
                          "moveSpeedScale", "projectileModel", "explosionRadius", "explosionInnerDamage"})
        if (const std::string* v = find(w, h); v && !v->empty()) print("  " + ljust(h, 22) + " = " + latin1_to_utf8(*v));
}
inline void set_weapon(const std::string& path, const std::vector<std::string>& updates, const std::string& out, bool in_place) {
    if (out.empty() && !in_place) die("pass -o <file> (your mod's copy) or --in-place; set never overwrites its input by default");
    Weapon w = parse_weapon(path);
    const std::string dest = !out.empty() ? out : path;
    for (const auto& u : updates) {
        const auto eq = u.find('=');
        if (eq == std::string::npos) die("invalid property format: '" + u + "' (expected key=value)");
        const std::string k = utf8_to_latin1(strip(u.substr(0, eq))), v = utf8_to_latin1(strip(u.substr(eq + 1)));
        put(w, k, v);
        if (std::find(w.order.begin(), w.order.end(), k) == w.order.end()) w.order.push_back(k);
    }
    write_weapon(dest, w);
    print("Updated " + std::to_string(updates.size()) + " properties -> " + dest);
}
inline void clone_weapon(const std::string& src, const std::string& dst, const Val& damage, const Val& clip, const Val& ammo, bool explosive) {
    Weapon w = parse_weapon(src);
    auto I = [](long long v) { return std::to_string(v); };
    if (!damage.is_none()) { put(w, "damage", I(damage.i)); put(w, "minDamage", I(std::max(10LL, floordiv(damage.i, 3)))); }
    if (!clip.is_none()) put(w, "clipSize", I(clip.i));
    if (!ammo.is_none()) { put(w, "maxAmmo", I(ammo.i)); put(w, "startAmmo", I(ammo.i)); }
    if (explosive) {
        put(w, "weaponType", "projectile"); put(w, "weaponClass", "grenade"); put(w, "impactType", "rifle_grenade");
        put(w, "projectileModel", "projectile_usa_m9a1_riflegrenade"); put(w, "projectileSpeed", "2400"); put(w, "explosionRadius", "220");
        put(w, "explosionInnerDamage", I(!damage.is_none() && damage.i != 0 ? damage.i : 250)); put(w, "explosionOuterDamage", "75");
        put(w, "projImpactExplode", "1"); put(w, "projTrailEffect", "weapon/grenade/fx_trail_rifle_grenade");
    }
    write_weapon(dst, w);
    print("Cloned " + name_of_path(src) + " -> " + dst + " (" + std::to_string(w.props.size()) + " properties)");
}
inline void diff_weapons(const std::string& f1, const std::string& f2) {
    const Weapon a = parse_weapon(f1), b = parse_weapon(f2);
    std::set<std::string> keys; for (const auto& kv : a.props) keys.insert(kv.first); for (const auto& kv : b.props) keys.insert(kv.first);
    // sorted() on str: latin-1 code points sort like the bytes
    std::vector<std::array<std::string, 3>> diffs;
    for (const auto& k : keys) {
        const std::string* x = find(a, k); const std::string* y = find(b, k);
        const std::string v1 = x ? *x : "<absent>", v2 = y ? *y : "<absent>";
        if (v1 != v2) diffs.push_back({k, v1, v2});
    }
    print("Comparing " + name_of_path(f1) + " vs " + name_of_path(f2) + " (" + std::to_string(diffs.size()) + " differences):");
    for (std::size_t i = 0; i < diffs.size() && i < 40; ++i)
        print("  " + ljust(latin1_to_utf8(diffs[i][0]), 24) + " | " + ljust(latin1_to_utf8(diffs[i][1]), 24) + " -> " + ljust(latin1_to_utf8(diffs[i][2]), 24));
    if (diffs.size() > 40) print("  ... and " + std::to_string(diffs.size() - 40) + " more differences.");
}

inline void reg(Parser& sub) {
    Parser& p = sub.sub("weapon", DOC); p.help_line = "inspect, edit, and clone Call of Duty weapon files"; p.raw_description = true;
    p.subcommands("cmd", false); p.sub_metavar = "<cmd>";
    { Parser& q = p.sub("get"); q.help_line = "view weapon stats or query a property"; q.pos("file").help("path to weapon file"); q.pos("key").nargs("?").help("optional specific property name"); }
    { Parser& q = p.sub("set"); q.help_line = "modify weapon properties and save to file"; q.pos("file").help("path to weapon file");
      q.pos("updates").nargs("+").help("key=value pairs (e.g. damage=500 clipSize=30)"); q.opt({"-o", "--out"}).help("output file path (your mod's copy)");
      q.opt({"--in-place"}).store_true().help("overwrite the input file instead"); }
    { Parser& q = p.sub("clone"); q.help_line = "clone a weapon with stats or explosive ammo presets"; q.pos("src").help("source weapon file"); q.pos("dst").help("destination weapon file");
      q.opt({"--damage"}).integer().help("override damage"); q.opt({"--clip"}).integer().help("override clip size"); q.opt({"--ammo"}).integer().help("override max reserve ammo");
      q.opt({"--explosive"}).store_true().help("turn weapon into an explosive projectile launcher"); }
    { Parser& q = p.sub("diff"); q.help_line = "compare differences between two weapon files"; q.pos("file1").help("first weapon file"); q.pos("file2").help("second weapon file"); }
}
inline int main_(const Val& a) {
    const std::string c = S(a, "cmd");
    if (c == "get") get_weapon(S(a, "file"), S(a, "key"));
    else if (c == "set") { std::vector<std::string> u; for (const auto& v : *get(a, "updates").l) u.push_back(v.s); set_weapon(S(a, "file"), u, S(a, "out"), truthy(get(a, "in_place"))); }
    else if (c == "clone") clone_weapon(S(a, "src"), S(a, "dst"), get(a, "damage"), get(a, "clip"), get(a, "ammo"), truthy(get(a, "explosive")));
    else if (c == "diff") diff_weapons(S(a, "file1"), S(a, "file2"));
    return 0;
}
}  // namespace packtools::modder::weapon

namespace packtools::modder::fastfile {
using namespace pyrt;
using namespace packtools::modder;

inline const char* DOC =
    "Call of Duty FastFile (.ff) inspector and script/rawfile extractor.\n\n"
    "    um fastfile list zone/english/patch.ff\n"
    "    um fastfile dump zone/english/patch.ff -o extracted/\n"
    "    um fastfile dump zone/english/nazi_zombie_factory.ff -o extracted/\n\n"
    "Decompresses retail Call of Duty: World at War (T4) and Call of Duty 4 (IW 3.0)\n"
    "FastFiles and extracts all embedded RawFiles (GSC scripts, CSC client scripts,\n"
    "menu files, CSV string/sound tables, arena files, and configs). In these titles,\n"
    "GSC/CSC gameplay scripts are stored as 100% human-readable plain text source.\n\n"
    "Read-only: it reads unsigned PC zones (IWffu100) and never writes, repacks or signs\n"
    "one. Signed zones (IWff0100) are refused. RawFiles are found by scanning the zone\n"
    "for their serialized layout, not by a full zone parser, so treat a missing file as\n"
    "\"not found by the scan\". The dump is the game's own code from your install: keep it\n"
    "outside any repo and never publish it.\n";

inline std::string bytes_lit(const std::string& b) {   // repr(bytes)
    const bool dq = b.find('\'') != std::string::npos && b.find('"') == std::string::npos;
    std::string o = dq ? "b\"" : "b'";
    for (unsigned char c : b) {
        if (c == '\\') o += "\\\\"; else if (c == '\t') o += "\\t"; else if (c == '\n') o += "\\n"; else if (c == '\r') o += "\\r";
        else if (!dq && c == '\'') o += "\\'";
        else if (c < 0x20 || c >= 0x7F) { char h[8]; std::snprintf(h, sizeof h, "\\x%02x", c); o += h; }
        else o += static_cast<char>(c);
    }
    return o + (dq ? "\"" : "'");
}
inline std::string group3(long long n) { return commas(std::to_string(n)); }
inline std::string stem_of(const std::string& path) { return u8native(P(path).stem()); }

inline std::pair<uint32_t, std::string> decompress_fastfile(const std::string& path) {
    if (!isfile(path)) die("FastFile not found: " + path);
    const std::string data = slurp(path), name = name_of_path(path);
    const std::string magic = data.substr(0, 8);
    if (magic == "IWff0100") die(name + " is a signed fastfile (IWff0100). um fastfile reads unsigned PC zones (IWffu100) only and never touches signatures.");
    if (magic != "IWffu100") die("unrecognized FastFile header magic: " + bytes_lit(magic) + " in " + name);
    if (data.size() < 12) throw PyErr("struct.error", "unpack requires a buffer of 4 bytes");
    uint32_t version = 0; for (int k = 3; k >= 0; --k) version = (version << 8) | static_cast<unsigned char>(data[8 + static_cast<std::size_t>(k)]);
    if (version != 387 && version != 5)
        eprint("WARNING: FastFile version " + std::to_string(version) + " detected in " + name + ".\n"
               "  This extractor targets World at War PC (v387) and Call of Duty 4 PC (v5).\n"
               "  Other titles lay out zones and RawFiles differently (MW2 zlib-compresses them,\n"
               "  Black Ops II compiles GSC to bytecode), so the scan may miss or misread files.");
    std::string out;
    try { out = zip::zlib_decompress(data.substr(12)); }
    catch (const PyErr& e) { die("failed to decompress " + name + ": " + e.what()); }
    return {version, out};
}
// RAWFILE_PATTERN: \xff{4}(.{4})\xff{4}([a-zA-Z0-9_/\\.-]+(?:\.[a-zA-Z0-9_]+))\x00, scanned like re.finditer
inline std::vector<std::pair<std::string, std::string>> extract_rawfiles(const std::string& d) {
    std::vector<std::pair<std::string, std::string>> files;   // dict: insertion order, later duplicates replace the value
    auto name_c = [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '/' || c == '\\' || c == '.' || c == '-'; };
    auto ext_c = [](unsigned char c) { return std::isalnum(c) || c == '_'; };
    auto ff4 = [&](std::size_t at) { return at + 4 <= d.size() && d.compare(at, 4, "\xff\xff\xff\xff", 4) == 0; };
    std::size_t i = 0;
    while (i + 12 < d.size()) {
        if (!ff4(i) || !ff4(i + 8)) { ++i; continue; }
        const std::size_t ns = i + 12; std::size_t e = ns;
        while (e < d.size() && name_c(static_cast<unsigned char>(d[e]))) ++e;
        bool ok = e < d.size() && d[e] == '\0';
        if (ok) {
            const std::size_t dot = d.rfind('.', e - 1);
            ok = dot != std::string::npos && dot > ns && dot >= ns && dot + 1 < e;
            for (std::size_t k = dot + 1; ok && k < e; ++k) if (!ext_c(static_cast<unsigned char>(d[k]))) ok = false;
        }
        if (!ok) { ++i; continue; }
        uint32_t length = 0; for (int k = 3; k >= 0; --k) length = (length << 8) | static_cast<unsigned char>(d[i + 4 + static_cast<std::size_t>(k)]);
        const std::string name = replace_all(d.substr(ns, e - ns), "\\", "/");
        const std::size_t start = e + 1;
        if (length > 0 && length <= d.size() - start) {
            const std::string payload = d.substr(start, length);
            bool hit = false; for (auto& kv : files) if (kv.first == name) { kv.second = payload; hit = true; }
            if (!hit) files.emplace_back(name, payload);
        }
        i = start;
    }
    return files;
}
inline void list_fastfile(const std::string& path) {
    const auto [version, data] = decompress_fastfile(path);
    auto files = extract_rawfiles(data);
    print("FastFile: " + name_of_path(path) + " (version " + std::to_string(version) + ", decompressed " + group3(static_cast<long long>(data.size())) + " bytes)");
    if (files.empty()) { print("  no RawFiles (.gsc, .csc, .menu, .csv, .cfg) found in this FastFile."); return; }
    print("  found " + std::to_string(files.size()) + " RawFiles:");
    std::sort(files.begin(), files.end());
    long long total = 0;
    for (const auto& [n, p] : files) { total += static_cast<long long>(p.size()); print("    " + rjust(group3(static_cast<long long>(p.size())), 8) + " B  " + n); }
    print("  total raw content: " + group3(total) + " bytes across " + std::to_string(files.size()) + " files.");
}
inline std::string normpath_posix(const std::string& p) {   // os.path.normpath on POSIX
    if (p.empty()) return ".";
    const int lead = p[0] == '/' ? (p.size() > 1 && p[1] == '/' && (p.size() < 3 || p[2] != '/') ? 2 : 1) : 0;
    std::vector<std::string> out;
    for (const auto& c : split(p, "/")) {
        if (c.empty() || c == ".") continue;
        if (c != ".." || (!lead && out.empty()) || (!out.empty() && out.back() == "..")) out.push_back(c);
        else if (!out.empty()) out.pop_back();
    }
    std::string s = std::string(static_cast<std::size_t>(lead), '/') + join(out, "/");
    return s.empty() ? "." : s;
}
inline void dump_fastfile(const std::string& path, const std::string& out_dir) {
    const auto [version, data] = decompress_fastfile(path);
    const auto files = extract_rawfiles(data);
    const std::string target = !out_dir.empty() ? path_str(out_dir) : stem_of(path) + "_extracted";
    makedirs(target);
    print("Extracting " + std::to_string(files.size()) + " files from " + name_of_path(path) + " (version " + std::to_string(version) + ") -> " + target + "...");
    long long count = 0, total = 0;
    for (const auto& [name, payload] : files) {
        std::string clean = normpath_posix(name);
        std::size_t k = 0; while (k < clean.size() && (clean[k] == '/' || clean[k] == '\\')) ++k; clean = clean.substr(k);
        if (starts(clean, "..")) continue;
        // a name with a backslash was already turned into '/', so the folders line up on every OS
        spit(path_join(target, clean), payload);
        ++count; total += static_cast<long long>(payload.size());
    }
    print("Successfully extracted " + std::to_string(count) + " files (" + group3(total) + " bytes) into " + target + "/");
}

inline void reg(Parser& sub) {
    Parser& p = sub.sub("fastfile", DOC); p.help_line = "inspect and extract Call of Duty FastFiles (.ff)"; p.raw_description = true;
    p.subcommands("cmd", false); p.sub_metavar = "<cmd>";
    { Parser& q = p.sub("list"); q.help_line = "list GSCs, CSCs, menus, and raw assets inside a .ff file"; q.pos("file").help("path to .ff FastFile"); }
    { Parser& q = p.sub("dump"); q.help_line = "extract all embedded scripts and rawfiles from a .ff file to disk"; q.pos("file").help("path to .ff FastFile");
      q.opt({"-o", "--out"}).help("destination directory (defaults to <name>_extracted/)"); }
}
inline int main_(const Val& a) {
    const std::string c = S(a, "cmd");
    if (c == "list") list_fastfile(S(a, "file"));
    else if (c == "dump") dump_fastfile(S(a, "file"), S(a, "out"));
    return 0;
}
}  // namespace packtools::modder::fastfile
