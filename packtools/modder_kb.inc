// modder_kb.inc - `um kb`: a C++ port of universal-modder's um/kb.py (Rehan and universal-modder contributors, MIT).
// The knowledge base of field notes on how games were modded: search prior notes (a local clone, else a copy synced
// from GitHub), show one, scaffold a new note, check notes against the schema, regenerate INDEX.md + index.json, and
// open a pull request with a note (a dry run unless --yes; ask your human first).
namespace packtools::modder::kb {
using namespace pyrt;
using namespace packtools::modder;

inline std::string REPO() { const std::string r = env("UM_KB_REPO"); return r.empty() ? "rehan-remade/universal-modder" : r; }
inline std::string BRANCH() { const std::string b = env("UM_KB_BRANCH"); return b.empty() ? "main" : b; }
inline const std::vector<std::string> ROUTES = {"data", "asset-only", "loader-api", "managed-patch", "native-hook", "reimplementation", "decomp-recomp", "passthrough", "emulator", "other"};
inline const std::vector<std::string> STATUSES = {"idea", "in-progress", "working", "released", "abandoned"};
inline const std::vector<std::string> PLATFORMS = {"windows", "linux", "macos", "proton", "emulator", "console", "other"};
inline const std::vector<std::string> GAME_KEYS = {"kind", "title", "game", "engine", "route", "status", "date", "agents"};
inline const std::vector<std::string> TECH_KEYS = {"kind", "title", "tags", "date", "agents"};
inline const std::vector<std::string> LIST_KEYS = {"games_also", "tools", "agents", "humans", "links", "tags"};
inline const std::vector<std::string> GAME_SECTIONS = {"setup", "route", "verification", "gotchas"};
inline const int MAX_BLOCK_WARN = 60, MAX_BLOCK_FAIL = 150, MAX_NOTE_KB = 120;
inline const double MAX_MEDIA_MB = 1.5;
inline bool in_list(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

// ------------------------------------------------------------------ where the notes are
// knowledge/ of the cwd's repo (a clone or fork), or the Library pack's bundled copy when one is installed
inline std::string local_root() {
    std::error_code ec; fs::path d = fs::current_path(ec);
    while (!ec) {
        if (isfile(u8native(d / "knowledge" / "TEMPLATE.md"))) return u8native(d / "knowledge");
        if (!d.has_parent_path() || d.parent_path() == d) break;
        d = d.parent_path();
    }
    return {};
}
inline std::string cache_root() { return path_join(path_join(path_join(data_dir(), "kb"), replace_all(REPO(), "/", "__")), "knowledge"); }
inline double now_s() { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); }

// mirror knowledge/ from GitHub into the cache (git tree API + raw files; no auth needed)
inline std::string sync(bool quiet = false) {
    const std::string api = "https://api.github.com/repos/" + REPO() + "/git/trees/" + BRANCH() + "?recursive=1";
    const HttpResp tr = http_get_um(api, {"User-Agent: universal-modder", "Accept: application/vnd.github+json"}, 60);
    if (tr.status != 200) throw OSError(5, "GitHub tree API: " + (tr.error.empty() ? "HTTP " + std::to_string(tr.status) : tr.error));
    const Val tree = loads(tr.body);
    std::vector<std::string> paths;
    for (const auto& t : iter(get(tree, "tree", Val::list()))) {
        const std::string p = get(t, "path").t == Val::STR ? get(t, "path").s : "";
        if (get(t, "type").t == Val::STR && get(t, "type").s == "blob" && starts(p, "knowledge/") && (ends(p, ".md") || ends(p, ".json"))) paths.push_back(p);
    }
    const std::string root = cache_root(), tmp = path_join(u8native(P(root).parent_path()), "knowledge.tmp");
    std::error_code ec; fs::remove_all(P(tmp), ec);
    for (const auto& p : paths) {
        const std::string url = "https://raw.githubusercontent.com/" + REPO() + "/" + BRANCH() + "/" + p;
        const HttpResp r = http_get_um(url, {"User-Agent: universal-modder"}, 60);
        if (r.status != 200) throw OSError(5, url + ": " + (r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error));
        spit(path_join(tmp, p.substr(10)), r.body);
    }
    fs::remove_all(P(root), ec); fs::rename(P(tmp), P(root), ec); if (ec) throw OSError(ec.value(), root);
    spit(path_join(root, ".synced"), float_repr(now_s()));
    if (!quiet) print("synced " + std::to_string(paths.size()) + " files from github.com/" + REPO() + " -> " + root);
    return root;
}
inline std::string resolve_root(const std::string& explicit_root = "", bool remote = false) {
    if (!explicit_root.empty()) return explicit_root;
    if (!env("UM_KB").empty()) return env("UM_KB");
    const std::string loc = remote ? "" : local_root();
    if (!loc.empty()) return loc;
    const std::string root = cache_root(), stamp = path_join(root, ".synced");
    bool stale = !pexists(stamp);
    if (!stale) { const std::string t = strip(slurp(stamp)); stale = now_s() - (t.empty() ? 0.0 : std::strtod(t.c_str(), nullptr)) > 86400; }
    if (stale) {
        try { return sync(true); }
        catch (const std::exception& e) { if (!pexists(root)) die("no local knowledge/ and GitHub sync failed (" + std::string(e.what()) + "); clone https://github.com/" + REPO()); }
    }
    return root;
}

// ------------------------------------------------------------------ notes
struct Note { std::string path, rel; Val meta; std::string body; };
inline std::pair<Val, std::string> parse_text(const std::string& text) {
    const auto m = re::compile(R"(^---\s*\n(.*?)\n---\s*\n?(.*)$)", re::S).match(text);
    if (!m) return {Val::dict(), text};
    Val meta;
    try { meta = yaml_load(m.group(1)); }
    catch (const YamlError& e) { Val d = Val::dict(); d.d->set("_yaml_error", Val::str(e.what())); return {d, m.group(2)}; }
    catch (const std::exception& e) { Val d = Val::dict(); d.d->set("_yaml_error", Val::str(e.what())); return {d, m.group(2)}; }
    return {meta.t == Val::DICT ? meta : Val::dict(), m.group(2)};
}
inline std::pair<Val, std::string> parse(const std::string& path) { return parse_text(decode_utf8_replace(slurp(path))); }
inline std::vector<Note> notes(const std::string& root) {
    std::vector<Note> out;
    for (const auto& rel : rglob_files(root)) {
        if (!ends(rel, ".md")) continue;
        if (rel == "README.md" || rel == "INDEX.md" || rel == "TEMPLATE.md" || starts(rel, "engines/")) continue;
        auto [meta, body] = parse(path_join(root, rel));
        out.push_back({path_join(root, rel), rel, meta, body});
    }
    return out;
}
inline std::string slug(const std::string& s) {
    std::string o; bool dash = false;
    for (char c : lower(s)) { if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) { o += c; dash = false; } else if (!dash) { o += '-'; dash = true; } }
    std::size_t a = 0; while (a < o.size() && o[a] == '-') ++a; std::size_t b = o.size(); while (b > a && o[b - 1] == '-') --b;
    o = o.substr(a, b - a); if (o.size() > 60) o = o.substr(0, 60);
    return o.empty() ? "note" : o;
}
// a list field as text, whatever YAML made of it
inline std::vector<std::string> strs(const Val& v) {
    if (v.is_none() || (v.t == Val::STR && !is_date(v) && v.s.empty())) return {};
    std::vector<std::string> o;
    if (v.t == Val::LIST) for (const auto& x : *v.l) o.push_back(x.t == Val::DICT ? repr(x) : ystr(x)); else o.push_back(v.t == Val::DICT ? repr(v) : ystr(v));
    return o;
}
inline std::string mstr(const Val& meta, const std::string& k) { const Val v = get(meta, k); return v.t == Val::DICT ? repr(v) : ystr(v); }   // str(meta.get(k, ""))
inline std::string mstr_or_empty(const Val& meta, const std::string& k) { return has(meta, k) ? mstr(meta, k) : std::string(); }

// ------------------------------------------------------------------ search
// a term that must start a word ("rust" finds "Rust" and "rusty", not "trust"); one starting with punctuation matches anywhere
struct Term { std::string t; bool word; };
inline bool alnum_byte(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }
inline bool term_alnum_start(const std::string& t) { if (t.empty()) return false; const unsigned char c = static_cast<unsigned char>(t[0]); return std::isalnum(c) || c >= 0x80; }
inline std::size_t term_count(const Term& t, const std::string& text, std::size_t cap = SIZE_MAX) {
    std::size_t n = 0;
    for (std::size_t k = text.find(t.t); k != std::string::npos && n < cap;) {
        if (!t.word || k == 0 || !alnum_byte(static_cast<unsigned char>(text[k - 1]))) { ++n; k = text.find(t.t, k + t.t.size()); }
        else k = text.find(t.t, k + 1);
    }
    return n;
}
inline bool term_in(const Term& t, const std::string& text) { return term_count(t, text, 1) > 0; }
inline std::vector<std::vector<Term>> groups_of(const std::vector<std::string>& args) {
    std::vector<std::vector<Term>> out;
    for (const auto& a0 : args) {
        const std::string a = join(split_ws(lower(a0)), " ");
        if (a.empty()) continue;
        std::vector<Term> g{{a, term_alnum_start(a)}};
        if (a.find(' ') != std::string::npos) { std::vector<std::string> seen; for (const auto& w : split(a, " ")) if (std::find(seen.begin(), seen.end(), w) == seen.end()) { seen.push_back(w); g.push_back({w, term_alnum_start(w)}); } }
        out.push_back(g);
    }
    return out;
}
inline List search(const std::string& root, const std::vector<std::string>& terms_in, const std::string& game, const std::string& engine, const std::string& route, long long limit) {
    const auto groups = groups_of(terms_in);
    std::vector<Term> terms; for (const auto& g : groups) for (const auto& t : g) { bool dup = false; for (const auto& x : terms) if (x.t == t.t && x.word == t.word) dup = true; if (!dup) terms.push_back(t); }
    struct Res { long long score; Val v; };
    std::vector<Res> res;
    for (const auto& n : notes(root)) {
        const Val& meta = n.meta;
        if (!game.empty()) { std::string hay = slug(mstr_or_empty(meta, "game")) + " "; std::vector<std::string> also; for (const auto& g : strs(get(meta, "games_also"))) also.push_back(slug(g)); hay += join(also, " "); if (hay.find(slug(game)) == std::string::npos) continue; }
        if (!engine.empty() && lower(mstr_or_empty(meta, "engine")) != lower(engine)) continue;
        if (!route.empty() && lower(mstr_or_empty(meta, "route")) != lower(route)) continue;
        std::vector<std::pair<int, std::string>> fields;
        fields.emplace_back(5, mstr_or_empty(meta, "title"));
        { std::vector<std::string> f{mstr_or_empty(meta, "game")}; for (const auto& g : strs(get(meta, "games_also"))) f.push_back(g); fields.emplace_back(4, join(f, " ")); }
        { std::vector<std::string> f{mstr_or_empty(meta, "engine"), mstr_or_empty(meta, "route")}; for (const auto& g : strs(get(meta, "tags"))) f.push_back(g); for (const auto& g : strs(get(meta, "tools"))) f.push_back(g); fields.emplace_back(3, join(f, " ")); }
        const std::string low = lower(n.body);
        std::string text = low; for (const auto& [w, f] : fields) text += "\n" + lower(f);
        if (!groups.empty()) {
            bool any = false;
            for (const auto& g : groups) { if (term_in(g[0], text)) { any = true; break; } if (g.size() > 1) { bool all = true; for (std::size_t k = 1; k < g.size(); ++k) if (!term_in(g[k], text)) { all = false; break; } if (all) { any = true; break; } } }
            if (!any) continue;
        }
        long long score = 0;
        for (const auto& t : terms) { for (const auto& [w, f] : fields) if (term_in(t, lower(f))) score += w; score += static_cast<long long>(std::min<std::size_t>(5, term_count(t, low))); }
        if (!terms.empty() && score == 0) continue;
        std::vector<std::pair<int, std::string>> counted;
        for (const auto& ln0 : splitlines(n.body)) { const std::string ln = strip(ln0); if (ln.empty()) continue; int c = 0; const std::string l = lower(ln); for (const auto& t : terms) if (term_in(t, l)) ++c; if (c) counted.emplace_back(c, ln); }
        std::stable_sort(counted.begin(), counted.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        Val hits = Val::list(); for (std::size_t k = 0; k < counted.size() && k < 3; ++k) hits.l->push_back(Val::str(counted[k].second));
        Val r = Val::dict();
        r.d->set("score", Val::integer(score)); r.d->set("path", Val::str(n.rel));
        for (const char* k : {"title", "game", "engine", "route", "status"}) { Val v = get(meta, k); if (is_date(v)) v = Val::str(v.s); r.d->set(k, v); }
        r.d->set("hits", hits);
        res.push_back({score, r});
    }
    std::stable_sort(res.begin(), res.end(), [](const Res& a, const Res& b) { return a.score > b.score; });
    List out; for (std::size_t k = 0; k < res.size() && static_cast<long long>(k) < std::max(0LL, limit); ++k) out.push_back(res[k].v);
    if (limit < 0) { out.clear(); const long long keep = static_cast<long long>(res.size()) + limit; for (long long k = 0; k < keep; ++k) out.push_back(res[static_cast<std::size_t>(k)].v); }
    return out;
}

// ------------------------------------------------------------------ check
inline std::string resolve_abs(const fs::path& p) { std::error_code ec; return u8native(fs::weakly_canonical(fs::absolute(p, ec), ec)); }
inline std::pair<std::vector<std::string>, std::vector<std::string>> check_note(const std::string& path, const std::string& root = "") {
    std::vector<std::string> fails, warns;
    const std::string text = decode_utf8_replace(slurp(path));
    auto [meta, body] = parse_text(text);
    if (meta.d->items.empty()) return {{path + ": no YAML front matter (start the file with --- ... --- ; see knowledge/TEMPLATE.md)"}, {}};
    if (has(meta, "_yaml_error")) return {{path + ": front matter is not valid YAML: " + get(meta, "_yaml_error").s}, {}};
    const Val kindv = get(meta, "kind", Val::str("game"));
    const std::string kind = ystr(kindv);
    auto empty_val = [](const Val& v) { return v.is_none() || (v.t == Val::STR && !is_date(v) && v.s.empty()) || (v.t == Val::LIST && v.l->empty()); };
    for (const auto& k : (kindv.t == Val::STR && kind == "game") ? GAME_KEYS : TECH_KEYS) if (empty_val(get(meta, k))) fails.push_back("missing front-matter key: " + k);
    if (!(kindv.t == Val::STR && (kind == "game" || kind == "technique"))) fails.push_back("kind must be game or technique, not " + yrepr(kindv));
    if (kindv.t == Val::STR && kind == "game") {
        const Val rt = get(meta, "route"), pf = get(meta, "platform"), en = get(meta, "engine");
        if (truthy(rt) && !(rt.t == Val::STR && in_list(ROUTES, rt.s))) fails.push_back("route " + yrepr(rt) + " not one of: " + join(ROUTES, ", "));
        if (truthy(pf) && !(pf.t == Val::STR && in_list(PLATFORMS, pf.s))) warns.push_back("platform " + yrepr(pf) + " not one of: " + join(PLATFORMS, ", "));
        if (truthy(en) && !(en.t == Val::STR && (scan::is_engine(en.s) || en.s == "unknown"))) warns.push_back("engine " + yrepr(en) + " isn't one of um scan's keys (" + join(scan::engine_keys_sorted(), ", ") + ", unknown)");
        std::vector<std::string> heads; for (const auto& h : re::findall(R"(^##\s+(.+)$)", body, re::M)) heads.push_back(lower(h));
        for (const auto& s : GAME_SECTIONS) { bool ok = false; for (const auto& h : heads) if (starts(h, s)) ok = true; if (!ok) fails.push_back("missing section: ## " + std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])))) + s.substr(1) + "..."); }
        if (join(heads, " ").find("gotchas") != std::string::npos) {
            // numbered items count as a list item ("1. ...") or a numbered subheading ("### 1. ..."), anywhere after ## Gotchas
            bool numbered = false; bool in = false;
            for (const auto& ln : splitlines(body)) {
                if (!in) { if (re::match(R"(##\s+Gotchas)", ln, re::I)) in = true; continue; }
                if (re::match(R"(\s*1\.|\s*#{2,6}\s+1\.)", ln)) { numbered = true; break; }
            }
            if (!numbered) warns.emplace_back("Gotchas section has no numbered items");
        }
    }
    { const Val st = get(meta, "status"); if (truthy(st) && !(st.t == Val::STR && !is_date(st) && in_list(STATUSES, st.s))) fails.push_back("status " + yrepr(st) + " not one of: " + join(STATUSES, ", ")); }
    { const Val d = get(meta, "date"); if (truthy(d) && !is_date(d) && !re::match(R"(^\d{4}-\d{2}-\d{2}$)", ystr(d)).ok) fails.push_back("date must be YYYY-MM-DD, got " + yrepr(d)); }
    for (const auto& k : LIST_KEYS) {
        const Val v = get(meta, k);
        if (!v.is_none() && v.t != Val::LIST) fails.push_back(k + " must be a list, e.g. " + k + ": [\"a\", \"b\"]");
        if (v.t == Val::LIST) for (const auto& x : *v.l) {
            if (!(x.t == Val::STR && !is_date(x))) fails.push_back(k + ": item " + yrepr(x) + " is YAML " + ytype(x) + ", not text; quote it, e.g. [\"Cataclysm: Dark Days Ahead\"]");
            else { bool bad = false; for (const auto& pr : std::vector<std::pair<char, char>>{{'(', ')'}, {'[', ']'}, {'{', '}'}}) if (std::count(x.s.begin(), x.s.end(), pr.first) != std::count(x.s.begin(), x.s.end(), pr.second)) bad = true;
                   if (bad) fails.push_back(k + ": item " + repr(Val::str(x.s)) + " has unbalanced brackets (split on a comma?); quote items that contain commas"); }
        }
    }
    const std::string fname = name_of_path(path);
    if (get(meta, "title").t == Val::STR && get(meta, "title").s == "Homing missiles and a tactical nuke in Terraria" && fname.find("TEMPLATE") == std::string::npos) fails.emplace_back("title is still the template's example title");
    { std::vector<std::string> left; for (const char* ph : {"FILL IN", "Two to four sentences:", "Numbered; each one symptom", "What you saw. **Cause:** what it really was"}) if (text.find(ph) != std::string::npos) left.push_back(repr(Val::str(ph)));
      if (!left.empty() && fname.find("TEMPLATE") == std::string::npos) fails.push_back("unfilled template text: " + join(left, ", ")); }
    for (const auto& p : publish::SECRET_PATTERNS()) if (publish::pat_search(p, text)) fails.push_back(p.label + " in the note - remove it");
    for (const auto& b : re::findall(R"(```[^\n]*\n(.*?)```)", body, re::S)) {
        const long n = static_cast<long>(std::count(b.begin(), b.end(), '\n'));
        if (n > MAX_BLOCK_FAIL) fails.push_back("a " + std::to_string(n) + "-line code block: link to your repo instead of pasting code (limit " + std::to_string(MAX_BLOCK_FAIL) + ")");
        else if (n > MAX_BLOCK_WARN) warns.push_back("a " + std::to_string(n) + "-line code block: keep snippets short (under " + std::to_string(MAX_BLOCK_WARN) + " lines)");
        for (const auto& p : publish::DECOMP_PATTERNS()) if (publish::pat_search(p, b)) warns.push_back(p.label + " in a code block: describe decompiled logic, don't paste it");
    }
    if ((text.find(":\\Users\\") != std::string::npos || text.find("/home/") != std::string::npos || text.find("/Users/") != std::string::npos) &&
        re::search(R"([A-Z]:\\Users\\(?!<)[^\\\s`"']+|/home/(?!<)[a-z_][a-z0-9_-]*/|/Users/(?!<)[A-Za-z]+/)", text).ok)
        warns.emplace_back("absolute user path (use <you>, %USERPROFILE% or ~ instead)");
    if (static_cast<long long>(text.size()) > MAX_NOTE_KB * 1024LL) warns.push_back("note is " + std::to_string(text.size() / 1024) + " KB; split it or trim (limit " + std::to_string(MAX_NOTE_KB) + " KB)");
    const fs::path parent = P(path).parent_path();
    for (const auto& m : re::compile(R"(\]\(([^)\s#]+\.md)(?:#[^)\s]*)?\))").finditer(body)) {
        const std::string target = m.group(1);
        if (!re::match(R"([a-z][a-z0-9+.-]*:|/)", target, re::I).ok && !pexists(resolve_abs(parent / P(target))))
            warns.push_back("link to a note that does not exist: " + target + " (links are relative to this note's folder; if the file lives in another repo, link to it on GitHub instead)");
    }
    std::string kroot = root;
    if (kroot.empty()) { fs::path q = fs::absolute(P(path)); while (q.has_parent_path() && q.parent_path() != q) { q = q.parent_path(); if (u8native(q.filename()) == "knowledge") { kroot = u8native(q); break; } } }
    if (!kroot.empty()) for (const auto& m : re::compile(R"(`(?:knowledge/)?((?:games|techniques)/[a-z0-9_./-]+\.md)`)").finditer(body))
        if (!pexists(path_join(kroot, m.group(1)))) warns.push_back("reference to a note that does not exist: " + m.group(1));
    for (const auto& m : re::compile(R"(!\[[^\]]*\]\(([^)\s]+)\))").finditer(body)) {
        const std::string src = m.group(1); if (starts(src, "http")) continue;
        const std::string img = resolve_abs(parent / P(src));
        if (!pexists(img)) fails.push_back("image not found: " + src);
        else if (static_cast<double>(fsize(img)) > MAX_MEDIA_MB * 1048576.0) fails.push_back("image " + src + " is " + fmt_f(static_cast<double>(fsize(img)) / 1048576.0, 1) + " MB (limit " + float_repr(MAX_MEDIA_MB) + " MB; link videos instead)");
    }
    return {fails, warns};
}

// ------------------------------------------------------------------ index
inline std::pair<std::string, List> build_index(const std::string& root) {
    List rows;
    for (const auto& n : notes(root)) {
        Val r = Val::dict(); r.d->set("path", Val::str(n.rel));
        for (const auto& [k, v] : n.meta.d->items) r.d->set(k, is_date(v) ? Val::str(v.s) : v);
        rows.push_back(r);
    }
    auto gs = [](const Val& r, const char* k) { const Val* v = r.d->get(k); return v ? (v->t == Val::DICT ? repr(*v) : ystr(*v)) : std::string(); };
    List games, techs;
    for (const auto& r : rows) { const Val* k = r.d->get("kind"); const std::string kind = k ? ystr(*k) : "game"; if (kind == "game" && (!k || k->t == Val::STR)) games.push_back(r); else if (k && k->t == Val::STR && kind == "technique") techs.push_back(r); }
    std::stable_sort(games.begin(), games.end(), [&](const Val& a, const Val& b) { const std::string ga = lower(gs(a, "game")), gb = lower(gs(b, "game")); return ga != gb ? ga < gb : gs(a, "date") < gs(b, "date"); });
    std::stable_sort(techs.begin(), techs.end(), [&](const Val& a, const Val& b) { return lower(gs(a, "title")) < lower(gs(b, "title")); });
    auto esc = [](const Val* v) { std::string s = (!v || !truthy(*v)) ? std::string() : (v->t == Val::DICT || v->t == Val::LIST) ? repr(*v) : ystr(*v); return replace_all(s, "|", "\\|"); };
    auto escs = [](const std::string& s) { return replace_all(s, "|", "\\|"); };
    std::vector<std::string> lines = {"# Knowledge base index", "",
        "_Generated by `um kb index` from the notes' front matter; don't edit by hand. Machine-readable: `index.json`._", "",
        "## Games (" + std::to_string(games.size()) + " notes)", "",
        "| Game | Note | Engine | Route | Status | Agents | Date |", "|---|---|---|---|---|---|---|"};
    for (const auto& r : games) {
        const auto also = strs(r.d->at("games_also"));
        std::string g = esc(r.d->get("game"));
        if (!also.empty()) { std::vector<std::string> e; for (const auto& x : also) e.push_back(escs(x)); g += " + " + join(e, ", "); }
        lines.push_back("| " + g + " | [" + esc(r.d->get("title")) + "](" + r.d->get("path")->s + ") | " + esc(r.d->get("engine")) + " | " + esc(r.d->get("route")) + " | " +
                        esc(r.d->get("status")) + " | " + escs(join(strs(r.d->at("agents")), ", ")) + " | " + esc(r.d->get("date")) + " |");
    }
    lines.emplace_back(""); lines.push_back("## Techniques (" + std::to_string(techs.size()) + " notes)"); lines.emplace_back("");
    for (const auto& r : techs) lines.push_back("- [" + esc(r.d->get("title")) + "](" + r.d->get("path")->s + ") \xC2\xB7 " + escs(join(strs(r.d->at("tags")), ", ")));
    lines.insert(lines.end(), {"", "## Engine playbooks", "",
        "Per-engine routes and tools live with the skills: [skills/mod-any-game/references/engines/](../skills/mod-any-game/references/engines/).", ""});
    return {join(lines, "\n"), rows};
}
inline std::string index_json(const List& rows) { DumpOpts o; o.indent = 1; return dumps(Val::list(rows), o) + "\n"; }

// ------------------------------------------------------------------ new / pr
inline std::string new_note(const std::string& root, const std::string& game, const std::string& title, const std::string& kind, const std::string& from_scan,
                            const std::string& engine, const std::string& route, const std::string& agent, const std::string& out) {
    auto [meta0, body] = parse(path_join(root, "TEMPLATE.md"));
    Val meta = Val::dict(); for (const auto& [k, v] : meta0.d->items) meta.d->set(k, v);
    std::string ag = agent; if (ag.empty()) ag = env("UM_AGENT"); if (ag.empty()) ag = "FILL IN: agent (model)";
    meta.d->set("kind", Val::str(kind)); meta.d->set("title", Val::str(title)); meta.d->set("date", Val::str(today_iso())); meta.d->set("agents", Val::list({Val::str(ag)}));
    meta.d->set("humans", Val::list()); meta.d->set("links", Val::list()); meta.d->set("tags", Val::list()); meta.d->set("status", Val::str("in-progress"));
    if (kind == "game") {
        meta.d->set("game", Val::str(game.empty() ? "FILL IN" : game)); meta.d->set("games_also", Val::list()); meta.d->set("engine", Val::str(engine.empty() ? "unknown" : engine));
        meta.d->set("route", Val::str(route.empty() ? "other" : route)); meta.d->set("tools", Val::list()); meta.d->set("game_version", Val::str("FILL IN: exact build"));
        meta.d->set("platform", Val::str("windows")); meta.d->set("anti_cheat", Val::str("FILL IN"));
        if (!from_scan.empty()) {
            const Val s = scan::scan(from_scan);
            std::vector<std::string> ac; for (const auto& x : *get(s, "anti_cheat").l) ac.push_back(x.s);
            meta.d->set("game", game.empty() ? get(s, "name") : Val::str(game)); meta.d->set("engine", get(get(s, "engine"), "key"));
            meta.d->set("anti_cheat", Val::str(ac.empty() ? "none found by um scan" : join(ac, ", ")));
            meta.d->set("game_version", Val::str(strip("FILL IN (" + (truthy(get(s, "store")) ? pystr(get(s, "store")) : std::string("store?")) + " " + (truthy(get(s, "appid")) ? pystr(get(s, "appid")) : std::string()) + ")")));
            meta.d->set("tools", get(s, "mod_loaders_installed"));
        }
    } else {
        Val m2 = Val::dict(); for (const auto& [k, v] : meta.d->items) if (!in_list({"game", "games_also", "game_version", "platform", "engine", "route", "tools", "anti_cheat"}, k)) m2.d->set(k, v); meta = m2;
    }
    body = re::sub(R"(^# .*$)", "# " + replace_all(title, "\\", "\\\\"), body, 1, re::M);
    if (kind == "technique") body = "# " + title + "\n\n> What this technique is for, in two sentences.\n\n## When to use it\n\n## How\n\n## Gotchas\n1. **Symptom.** ... **Cause:** ... **Fix:** ...\n\n## Seen in\nLinks to game notes that used it.\n";
    const std::string front = yaml_dump(meta);
    const std::string base = kind == "game" ? path_join(path_join(root, "games"), slug(game.empty() && has(meta, "game") ? ystr(get(meta, "game")) : (game.empty() ? "misc" : game))) : path_join(root, "techniques");
    const std::string path = !out.empty() ? out : path_join(base, slug(title) + ".md");
    if (pexists(path)) die(path + " exists");
    spit(path, "---\n" + front + "---\n" + body);
    return path;
}
// --head for `gh pr create`: "<fork owner>:<branch>" for a PR from a fork
inline std::string pr_head(const std::string& branch, const std::string& fork_url) {
    const auto m = re::search(R"(github\.com[:/]+([^/]+)/)", fork_url);
    return m ? m.group(1) + ":" + branch : branch;
}
inline void open_pr(const std::string& path, bool yes) {
    const std::string root = local_root();
    if (root.empty()) die("run this inside a clone of the repo (or your fork)");
    const std::string repo = u8native(P(root).parent_path());
    auto [meta, body] = parse(path);
    { const auto [fails, w] = check_note(path, root); if (!fails.empty()) die("fix these first:\n  " + join(fails, "\n  ")); }
    const std::string g = mstr_or_empty(meta, "game");
    std::string branch = "kb/" + slug(g.empty() ? "technique" : g) + "-" + slug(has(meta, "title") ? mstr(meta, "title") : "note"); if (branch.size() > 80) branch = branch.substr(0, 80);
    const std::string title = "kb: " + mstr(meta, "title") + (truthy(get(meta, "game")) ? " (" + mstr(meta, "game") + ")" : "");
    const std::string pr_body = "Field note: **" + mstr(meta, "title") + "**\n\n- game: " + (has(meta, "game") ? mstr(meta, "game") : "-") + "\n- engine / route: " + (has(meta, "engine") ? mstr(meta, "engine") : "-") + " / " +
        (has(meta, "route") ? mstr(meta, "route") : "-") + "\n- status: " + mstr(meta, "status") + "\n- agents: " + join(strs(get(meta, "agents")), ", ") + "\n\n" +
        "Checked with `um kb check`; index regenerated with `um kb index`.\n\n- [x] no game files, decompiled code dumps or secrets\n- [x] versions and verification written down\n";
    std::vector<std::string> media; { const std::string md = path_join(u8native(P(path).parent_path()), "media"); if (isdir(md)) for (const auto& f : rglob_files(md)) if (f.find('/') == std::string::npos) media.push_back(path_join(md, f)); }
    bool can_push = false;
    if (!mk::which("gh").empty()) { const auto r = mk::run_proc({"gh", "api", "repos/" + REPO(), "--jq", ".permissions.push"}); can_push = strip(decode_utf8_replace(r.out)) == "true"; }
    const std::string remote = can_push ? "origin" : "fork";
    std::vector<std::vector<std::string>> cmds = {{"git", "checkout", "-b", branch}, {"um", "kb", "index"}};
    { std::vector<std::string> add = {"git", "add", path, path_join(root, "INDEX.md"), path_join(root, "index.json")}; add.insert(add.end(), media.begin(), media.end()); cmds.push_back(add); }
    cmds.push_back({"git", "commit", "-m", title});
    if (!can_push) cmds.push_back({"gh", "repo", "fork", "--remote", "--remote-name", "fork"});
    cmds.push_back({"git", "push", "-u", remote, branch});
    cmds.push_back({"gh", "pr", "create", "--repo", REPO(), "--head", branch, "--title", title, "--body", pr_body});
    if (!yes) {
        print("dry run (add --yes after your human agrees):");
        for (const auto& c : cmds) { const std::string all = join(c, " "); if (all.size() < 200) print("  " + all); else { std::vector<std::string> h(c.begin(), c.begin() + static_cast<long>(std::min<std::size_t>(6, c.size()))); h.emplace_back("..."); print("  " + join(h, " ")); } }
        return;
    }
    if (mk::which("gh").empty()) die("needs the GitHub CLI (gh) logged in; or push a branch and open the PR on github.com");
    const auto [idx, rows] = build_index(root);
    spit(path_join(root, "INDEX.md"), idx); spit(path_join(root, "index.json"), index_json(rows));
    for (auto c : cmds) {
        if (c.size() >= 3 && c[0] == "um") continue;
        if (c.size() >= 3 && c[0] == "gh" && c[1] == "repo" && c[2] == "fork" && mk::run_proc({"git", "remote", "get-url", "fork"}, repo).code == 0) continue;
        if (c.size() >= 3 && c[0] == "gh" && c[1] == "pr" && c[2] == "create" && remote == "fork") {
            const std::string url = strip(decode_utf8_replace(mk::run_proc({"git", "remote", "get-url", "fork"}, repo).out));
            for (std::size_t k = 0; k + 1 < c.size(); ++k) if (c[k] == "--head") { c[k + 1] = pr_head(branch, url); break; }
        }
        const auto r = mk::run_proc(c, repo);
        if (r.code != 0) { std::string t = strip(decode_utf8_replace(!r.err.empty() ? r.err : r.out)); if (t.size() > 800) t = t.substr(t.size() - 800); std::vector<std::string> h(c.begin(), c.begin() + static_cast<long>(std::min<std::size_t>(4, c.size()))); die(join(h, " ") + " failed: " + t); }
        if (c[0] == "gh" && c[1] == "pr") print(strip(decode_utf8_replace(r.out)));
    }
}

// ------------------------------------------------------------------ CLI
inline const char* DOC =
    "The knowledge base: field notes on how games were actually modded, written by agents for agents.\n\n"
    "    um kb search \"unreal pak mod\"               # prior art before you start (local repo, or synced from GitHub)\n"
    "    um kb search terraria --route loader-api    # filter by game / engine / route\n"
    "    um kb show games/terraria/fal-arsenal.md\n"
    "    um kb new --game \"Hades II\" --title \"A new boon god\" --from-scan \"hades\"   # scaffold a note, pre-filled\n"
    "    um kb check knowledge/games/hades-ii/new-boon-god.md                       # validate before a PR\n"
    "    um kb index                                  # regenerate knowledge/INDEX.md + index.json\n"
    "    um kb sync                                   # refresh the cached copy from GitHub\n"
    "    um kb pr knowledge/games/hades-ii/new-boon-god.md --yes                     # branch, commit, push to your fork, open a PR\n\n"
    "Every note is Markdown with YAML front matter (knowledge/TEMPLATE.md). `check` enforces the schema, the\n"
    "required sections, no secrets, no pasted decompiled code, no huge files. Contributions go through pull\n"
    "requests: that's how one agent's hard-won gotchas reach the next one. Ask your human before `um kb pr --yes`.\n";
inline void reg(Parser& sub) {
    Parser& p = sub.sub("kb", DOC); p.help_line = "knowledge base of how games were modded: search, write, check, PR"; p.raw_description = true;
    p.subcommands("cmd", false); p.sub_metavar = "<cmd>";
    { Parser& q = p.sub("search"); q.help_line = "find prior notes (local clone, else the GitHub copy)";
      q.pos("terms").nargs("*"); q.opt({"--game"}); q.opt({"--engine"}); q.opt({"--route"}).choices(ROUTES);
      q.opt({"--limit"}).integer().def(10LL); q.opt({"--json"}).store_true(); q.opt({"--remote"}).store_true().help("search the synced GitHub copy even inside a clone"); q.opt({"--root"}); }
    { Parser& q = p.sub("show"); q.help_line = "print a note"; q.pos("note"); q.opt({"--remote"}).store_true(); q.opt({"--root"}); }
    { Parser& q = p.sub("new"); q.help_line = "scaffold a note from the template";
      q.opt({"--title"}).required(); q.opt({"--game"}); q.opt({"--kind"}).def("game").choices({"game", "technique"});
      q.opt({"--from-scan"}).help("pre-fill engine / anti-cheat / loaders from `um scan <game>`"); q.opt({"--engine"}); q.opt({"--route"}).choices(ROUTES);
      q.opt({"--agent"}).help("e.g. \"Codex (gpt-6)\"; default $UM_AGENT"); q.opt({"--out"}); q.opt({"--root"}); }
    { Parser& q = p.sub("check"); q.help_line = "validate notes (all, or the given paths)"; q.pos("paths").nargs("*");
      q.opt({"--index"}).store_true().help("also fail if INDEX.md is stale"); q.opt({"--root"}); }
    { Parser& q = p.sub("index"); q.help_line = "regenerate knowledge/INDEX.md and index.json"; q.opt({"--root"}); }
    { Parser& q = p.sub("sync"); q.help_line = "refresh the cached GitHub copy"; }
    { Parser& q = p.sub("pr"); q.help_line = "open a pull request with a note (dry run unless --yes)"; q.pos("note"); q.opt({"--yes"}).store_true().help("really branch, push and open the PR"); }
}
inline std::vector<std::string> strlist(const Val& v) { std::vector<std::string> o; if (v.t == Val::LIST) for (const auto& x : *v.l) o.push_back(pystr(x)); return o; }
inline int main_(const Val& a) {
    const std::string c = S(a, "cmd");
    if (c == "sync") { try { sync(); } catch (const OSError& e) { die(std::string("sync failed: ") + e.what()); } return 0; }
    if (c == "search") {
        const std::string root = resolve_root(S(a, "root"), truthy(get(a, "remote")));
        const List res = search(root, strlist(get(a, "terms")), S(a, "game"), S(a, "engine"), S(a, "route"), get(a, "limit").i);
        if (truthy(get(a, "json"))) { DumpOpts o; o.indent = 1; print(dumps(Val::list(res), o)); return 0; }
        if (res.empty()) print("nothing in " + root + " matches; you may be first - write it up afterwards (`um kb new`)");
        for (const auto& r : res) {
            std::vector<std::string> f; for (const char* k : {"game", "engine", "route", "status"}) if (truthy(get(r, k))) f.push_back(pystr(get(r, k)));
            print(get(r, "path").s + "\n   " + pystr(get(r, "title")) + "  [" + (f.empty() ? std::string("technique") : join(f, " | ")) + "]");
            for (const auto& h : *get(r, "hits").l) { std::string t = h.s; std::size_t k = 0; while (k < t.size() && (t[k] == '>' || t[k] == ' ')) ++k; print("     | " + cpslice(t.substr(k), 0, 160)); }
        }
        return 0;
    }
    if (c == "show") {
        const std::string root = resolve_root(S(a, "root"), truthy(get(a, "remote")));
        std::string p = path_join(root, S(a, "note"));
        if (!pexists(p)) {
            std::string hit; for (const auto& rel : rglob_files(root)) if (ends(rel, ".md") && path_join(root, rel).find(S(a, "note")) != std::string::npos) { hit = path_join(root, rel); break; }
            if (hit.empty()) die("no note " + repr(Val::str(S(a, "note"))) + " in " + root);
            p = hit;
        }
        print(decode_utf8_replace(slurp(p)));
        return 0;
    }
    if (c == "new") {
        const std::string root = !S(a, "root").empty() ? S(a, "root") : local_root();
        if (root.empty()) die("run inside a clone of the repo (knowledge/ is written there), or pass --root");
        const std::string p = new_note(root, S(a, "game"), S(a, "title"), S(a, "kind"), S(a, "from_scan"), S(a, "engine"), S(a, "route"), S(a, "agent"), S(a, "out"));
        print(p); print("next: fill it in (Gotchas matter most), `um kb check " + p + "`, `um kb index`, then a PR (`um kb pr ...`)");
        return 0;
    }
    if (c == "check") {
        const std::string root = !S(a, "root").empty() ? S(a, "root") : local_root();
        std::vector<std::string> paths = strlist(get(a, "paths"));
        if (paths.empty() && !root.empty()) for (const auto& n : notes(root)) paths.push_back(n.path);
        int bad = 0;
        for (const auto& p : paths) {
            const auto [fails, warns] = check_note(p, root);
            for (const auto& f : fails) print("FAIL " + p + ": " + f);
            for (const auto& w : warns) print("WARN " + p + ": " + w);
            bad += !fails.empty();
        }
        if (!root.empty() && truthy(get(a, "index")) && strlist(get(a, "paths")).empty()) {
            const auto [idx, rows] = build_index(root);
            if (!pexists(path_join(root, "INDEX.md")) || decode_utf8_replace(slurp(path_join(root, "INDEX.md"))) != idx) { print("FAIL knowledge/INDEX.md is out of date: run `um kb index`"); ++bad; }
        }
        print(std::string(bad ? "FAIL" : "PASS") + ": " + std::to_string(paths.size()) + " notes checked");
        return bad ? 1 : 0;
    }
    if (c == "index") {
        const std::string root = !S(a, "root").empty() ? S(a, "root") : local_root();
        if (root.empty()) die("no local knowledge/ folder");
        const auto [idx, rows] = build_index(root);
        spit(path_join(root, "INDEX.md"), idx); spit(path_join(root, "index.json"), index_json(rows));
        print(path_join(root, "INDEX.md") + ": " + std::to_string(rows.size()) + " notes");
        return 0;
    }
    if (c == "pr") { open_pr(S(a, "note"), truthy(get(a, "yes"))); return 0; }
    return 1;
}
}  // namespace packtools::modder::kb
