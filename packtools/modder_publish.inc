// modder_publish.inc - `um publish check`: a C++ port of universal-modder's um/publish.py (Rehan and universal-modder
// contributors, MIT). Lints a mod folder before it is shared: game files copied verbatim, leaked keys, .env files,
// decompiler fingerprints, big engine archives, absolute user paths, missing README or fal credits.
namespace packtools::modder::publish {
using namespace pyrt;
using namespace packtools::modder;

struct Pat { std::string label, rx; int flags; };
inline const std::vector<Pat>& SECRET_PATTERNS() {
    static const std::vector<Pat> v = {
        {"fal key", R"(\b[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}:[0-9a-f]{32}\b)", 0},
        {"FAL_KEY assignment", R"(FAL_KEY\s*[=:]\s*['"]?[A-Za-z0-9:_\-]{20,})", 0},
        {"Anthropic key", R"(sk-ant-[A-Za-z0-9_\-]{20,})", 0},
        {"Sprixen key", R"(\bspx_live_[A-Za-z0-9_\-]{30,})", 0},
        {"Voidless key", R"(VOIDLESS_API_KEY\s*[=:]\s*['"]?void_[A-Za-z0-9_\-]{16,})", 0},
        {"OpenAI key", R"(\bsk-(?:(?:proj|svcacct|admin)-[A-Za-z0-9_\-]{32,}|[A-Za-z0-9]{32,}))", 0},
        {"GitHub token", R"(\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{60,}))", 0},
        {"AWS key id", R"(\bAKIA[0-9A-Z]{16}\b)", 0},
        {"private key", R"(-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----)", 0}};
    return v;
}
inline const std::vector<Pat>& DECOMP_PATTERNS() {
    static const std::vector<Pat> v = {
        {"Ghidra auto-name", R"(\b(?:FUN|DAT|LAB|PTR)_[0-9a-fA-F]{6,}\b)", 0},
        {"IDA auto-name", R"(\b(?:sub|loc|unk|off|dword|qword)_[0-9A-F]{5,}\b)", 0},
        {"decompiler header", R"(^\s*(?://|#|--|;)\s*(?:Decompiled with|Decompiler:|ILSpy|dnSpy|JetBrains decompiler)|^#region Assembly )", re::I | re::M}};
    return v;
}
// fast pre-filters keep the backtracking matcher off text that cannot match
inline bool may_match(const Pat& p, const std::string& t) {
    if (p.label == "fal key") return t.find(':') != std::string::npos && t.find('-') != std::string::npos;
    if (p.label == "FAL_KEY assignment") return t.find("FAL_KEY") != std::string::npos;
    if (p.label == "Anthropic key") return t.find("sk-ant-") != std::string::npos;
    if (p.label == "Sprixen key") return t.find("spx_live_") != std::string::npos;
    if (p.label == "Voidless key") return t.find("VOIDLESS_API_KEY") != std::string::npos;
    if (p.label == "OpenAI key") return t.find("sk-") != std::string::npos;
    if (p.label == "GitHub token") return t.find("gh") != std::string::npos || t.find("github_pat_") != std::string::npos;
    if (p.label == "AWS key id") return t.find("AKIA") != std::string::npos;
    if (p.label == "private key") return t.find("PRIVATE KEY-----") != std::string::npos;
    if (p.label == "Ghidra auto-name") return t.find('_') != std::string::npos && (t.find("FUN_") != std::string::npos || t.find("DAT_") != std::string::npos || t.find("LAB_") != std::string::npos || t.find("PTR_") != std::string::npos);
    if (p.label == "IDA auto-name") { for (const char* s : {"sub_", "loc_", "unk_", "off_", "dword_", "qword_"}) if (t.find(s) != std::string::npos) return true; return false; }
    if (p.label == "decompiler header") { const std::string l = lower(t); return l.find("decompile") != std::string::npos || l.find("ilspy") != std::string::npos || l.find("dnspy") != std::string::npos || t.find("#region Assembly ") != std::string::npos; }
    return true;
}
inline bool pat_search(const Pat& p, const std::string& t) { return may_match(p, t) && re::compile(p.rx, p.flags).search(t).ok; }
inline std::vector<std::string> pat_findall(const Pat& p, const std::string& t) { if (!may_match(p, t)) return {}; return re::compile(p.rx, p.flags).findall(t); }

inline const std::set<std::string> CODE_EXT = {".cs", ".c", ".cpp", ".h", ".hpp", ".py", ".lua", ".js", ".ts", ".rs", ".java", ".kt", ".gd", ".rpy", ".psc", ".gml", ".hlsl", ".glsl", ".as", ".vb", ".il"};
inline const std::set<std::string> SKIP_DIRS = {".git", ".venv", "venv", "node_modules", "__pycache__", ".pytest_cache", "obj", ".vs", ".idea"};
inline const std::set<std::string> TEXT_EXT = {".cs", ".c", ".cpp", ".h", ".hpp", ".py", ".lua", ".js", ".ts", ".json", ".toml", ".ini", ".cfg", ".txt", ".md", ".xml",
    ".yaml", ".yml", ".rs", ".java", ".kt", ".gd", ".rpy", ".psc", ".sh", ".ps1", ".bat", ".gml", ".hlsl", ".glsl", ".env"};
inline const std::set<std::string> ARCHIVE_EXT = {".pak", ".utoc", ".ucas", ".bsa", ".ba2", ".vpk", ".rpf", ".pck", ".assets", ".bundle", ".sga", ".big", ".wad", ".bdt", ".archive"};
inline std::string suffix_lower(const std::string& rel) {   // Path.suffix.lower()
    const std::string n = rel.substr(rel.rfind('/') == std::string::npos ? 0 : rel.rfind('/') + 1);
    const auto d = n.rfind('.'); if (d == std::string::npos || d == 0 || d + 1 == n.size()) return {};
    return lower(n.substr(d));
}
inline std::string name_of(const std::string& rel) { return rel.substr(rel.rfind('/') == std::string::npos ? 0 : rel.rfind('/') + 1); }
inline std::string stem(const std::string& rel) { const std::string n = name_of(rel); const auto d = n.rfind('.'); return d == std::string::npos || d == 0 ? n : n.substr(0, d); }
inline std::string text_of(const std::string& path, bool sig = false) {
    std::string b = slurp(path); if (sig && b.size() >= 3 && b.compare(0, 3, "\xEF\xBB\xBF") == 0) b = b.substr(3); return decode_utf8_replace(b);
}

// names a loader would install this mod under, from the mod's own metadata only (lower-cased)
inline std::set<std::string> own_names(const std::string& root, const std::vector<std::string>& files) {
    std::set<std::string> names;
    for (const auto& rel : files) {
        const std::string n = lower(name_of(rel)), full = path_join(root, rel);
        if (ends(n, ".csproj")) {
            const auto m = re::search(R"(<AssemblyName>\s*([^<]+?)\s*</AssemblyName>)", text_of(full, true));
            names.insert(lower(m && m.group(1).find("$(") == std::string::npos ? m.group(1) : stem(rel)));
        } else if (n == "info.json" || n == "manifest.json") {
            Val d; try { d = loads(text_of(full, true)); } catch (const PyErr&) { continue; }
            for (const char* k : n == "info.json" ? std::vector<const char*>{"Id", "AssemblyName"} : std::vector<const char*>{"name"}) {
                const Val v = d.t == Val::DICT ? get(d, k) : Val();
                if (v.t == Val::STR && !strip(v.s).empty()) names.insert(lower(re::sub(R"(\.dll$)", "", strip(v.s), 0, re::I)));
            }
        }
    }
    return names;
}
// (game-relative path, mod name) when a file is in a loader's mods folder: Mods/<name>/..., BepInEx/plugins/<name>/...,
// or a plugin DLL directly in BepInEx/plugins/ (named by its stem)
inline std::optional<std::pair<std::string, std::string>> loader_folder(const std::vector<std::string>& rel) {
    std::vector<std::string> low; for (const auto& x : rel) low.push_back(lower(x));
    for (std::size_t i = 0; i + 1 < low.size(); ++i) {
        const bool bep = low[i] == "plugins" && i > 0 && low[i - 1] == "bepinex";
        if (i + 2 < low.size() && (low[i] == "mods" || bep)) return std::make_pair(join(std::vector<std::string>(rel.begin(), rel.begin() + static_cast<long>(i + 2)), "/") + "/", low[i + 1]);
        if (bep && i + 2 == low.size() && ends(low.back(), ".dll")) return std::make_pair(join(rel, "/"), low.back().substr(0, low.back().size() - 4));
    }
    return std::nullopt;
}

inline int check(const std::string& mod, const std::string& game) {
    const std::string root = to_posix(mod);
    if (!isdir(root)) die("not a folder: " + root);
    std::vector<std::string> fails, warns;
    const std::vector<std::string> files = rglob_files(root, SKIP_DIRS);
    if (!game.empty()) {
        const std::string g = to_posix(game);
        const fs::path gr = fs::weakly_canonical(fs::absolute(P(g))), mine = fs::weakly_canonical(fs::absolute(P(root)));
        std::string inside;   // a mod inside the install (Dwarf Fortress mods/<name>) is not game content
        { const auto rel = mine.lexically_relative(gr); const std::string r = u8p(rel); if (mine != gr && !r.empty() && r != "." && !starts(r, "..")) inside = r; }
        std::map<long long, std::vector<std::string>> by_size;   // size -> game-relative paths
        for (const auto& rel : rglob_files(g)) {
            if (!inside.empty() && (rel == inside || starts(rel, inside + "/"))) continue;
            const long long sz = fsize(path_join(g, rel)); if (sz >= 0) by_size[sz].push_back(rel);
        }
        const auto own = own_names(root, files); std::set<std::string> skipped;
        for (auto& [sz, ps] : by_size) {
            std::vector<std::string> keep;
            for (const auto& p : ps) { const auto hit = loader_folder(split(p, "/")); if (hit && own.count(hit->second)) skipped.insert(hit->first); else keep.push_back(p); }
            ps = keep;
        }
        if (!skipped.empty()) print("note: not compared, this mod's own installed copy: " + join(std::vector<std::string>(skipped.begin(), skipped.end()), ", "));
        for (const auto& f : files) {
            const long long sz = fsize(path_join(root, f));
            if (sz < 64 || !by_size.count(sz)) continue;
            const std::string h = sha1_file(path_join(root, f));
            for (const auto& gp : by_size[sz]) if (sha1_file(path_join(g, gp)) == h) { fails.push_back("game file copied verbatim: " + f + "  (== " + gp + ")"); break; }
        }
    }
    for (const auto& rel : files) {
        const std::string name = name_of(rel), suf = suffix_lower(rel), full = path_join(root, rel);
        if (name == ".env" || ends(name, ".env")) fails.push_back("env file (secrets?): " + rel);
        const long long sz = fsize(full);
        if (ARCHIVE_EXT.count(suf) && sz > (5LL << 20)) warns.push_back("large engine archive (" + std::to_string(sz >> 20) + " MB): " + rel + " - make sure it holds only your own assets");
        if (TEXT_EXT.count(suf) || name == ".env") {
            const std::string txt = text_of(full);
            for (const auto& p : SECRET_PATTERNS()) if (pat_search(p, txt)) fails.push_back(p.label + " in " + rel);
            if (CODE_EXT.count(suf)) for (const auto& p : DECOMP_PATTERNS()) { const auto m = pat_findall(p, txt); if (!m.empty()) warns.push_back(p.label + " x" + std::to_string(m.size()) + " in " + rel + " (e.g. " + repr(Val::str(strip(m[0]))) + ")"); }
            if ((txt.find(":\\Users\\") != std::string::npos || txt.find("/home/") != std::string::npos || txt.find("/Users/") != std::string::npos) &&
                re::search(R"([A-Z]:\\Users\\[^\\\s"']+|/home/[a-z_][a-z0-9_-]*/|/Users/[A-Za-z]+/)", txt).ok)
                warns.push_back("absolute user path in " + rel);
        }
    }
    bool readme = false; for (const auto& f : files) if (starts(lower(name_of(f)), "readme")) readme = true;
    if (!readme) warns.emplace_back("no README (install steps, requirements, credits)");
    bool manifest = false; for (const auto& f : files) if (name_of(f) == "fal_manifest.jsonl") manifest = true;
    if (manifest) {
        bool credit = false;
        for (const auto& f : files) { const std::string l = lower(name_of(f)); if ((starts(l, "readme") || starts(l, "credits")) && lower(text_of(path_join(root, f))).find("fal") != std::string::npos) credit = true; }
        if (!credit) warns.emplace_back("fal-generated assets (fal_manifest.jsonl) but no credit line in README/CREDITS");
    }
    for (const auto& x : fails) print("FAIL  " + x);
    for (const auto& x : warns) print("WARN  " + x);
    print(std::string(!fails.empty() ? "FAIL" : !warns.empty() ? "WARN" : "PASS") + ": " + std::to_string(files.size()) + " files, " + std::to_string(fails.size()) + " failures, " + std::to_string(warns.size()) + " warnings");
    return fails.empty() ? 0 : 1;
}

inline const char* DOC =
    "Check a mod folder before you share it: no game files, no decompiled code, no secrets, credits present.\n\n"
    "    um publish check ./MyMod --game \"C:\\\\Program Files (x86)\\\\Steam\\\\steamapps\\\\common\\\\Terraria\"\n\n"
    "FAIL  files byte-identical to files in the game install (redistributing game files), leaked API keys\n"
    "      (FAL_KEY, Anthropic, GitHub, AWS...), .env files. This mod's own installed copy (Mods/<Id>/,\n"
    "      BepInEx/plugins/<name>/, named by Info.json, manifest.json or a .csproj) isn't compared; other mods are.\n"
    "WARN  decompiler fingerprints in source (FUN_xxxx / DAT_xxxx / sub_XXXX, \"// Decompiled with\", ILSpy/dnSpy\n"
    "      headers) - reimplement or reference instead of shipping decompiled code; big engine archives\n"
    "      (.pak/.bsa/.ba2/.vpk/.rpf/.utoc...) that may carry original assets; absolute user paths; no README /\n"
    "      credits; fal-generated assets listed in fal_manifest.jsonl without an attribution line\n"
    "Modelled on IW4L's publish-check. It is a lint, not legal advice: when in doubt ship a patch/converter that\n"
    "runs on the user's own install (\"bring your own game files\") instead of the files themselves.\n";
inline void reg(Parser& sub) {
    Parser& p = sub.sub("publish", DOC); p.help_line = "lint a mod folder before sharing (game files, decompiled code, secrets)"; p.raw_description = true;
    p.subcommands("cmd", false); p.sub_metavar = "<cmd>";
    Parser& q = p.sub("check"); q.help_line = "check a mod folder";
    q.pos("mod"); q.opt({"--game"}).help("game install folder, to catch copied game files");
}
inline int main_(const Val& a) { return check(S(a, "mod"), S(a, "game")); }
}  // namespace packtools::modder::publish
