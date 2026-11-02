// replica-skill helpers (Jake Schincariol, MIT), ported to C++: same output, same exit codes as the Python originals.
//   replica-design/contrast       WCAG 2 contrast of every text colour in a token file against its backgrounds
//   replica-brand/sweep           find the original app's name, domains and colours left in the clone's codebase
//   replica-launch/listing        lint an App Store / Google Play listing against field limits and rejection traps
//   replica-diff/parity           feature-parity score from the feature matrix CSV (imgdiff in social_replica_img.inc)
//   replica-entrepreneur/reviews  rank what the original's users hate and ask for, quoting their exact words
// Group 5. Python's dynamic edges are kept: a JSON value of the wrong type fails the way Python fails (uncaught, rc 1),
// and only the exceptions each script catches become its "<tool>: message" exit 2.
#include <array>
#include <set>
#include <ctime>
#include <cmath>

namespace packtools::rp {
using namespace pyrt;
namespace pm = packtools::master;

inline bool os_or_value(const PyErr& e) {   // except (OSError, ValueError)
    return e.type == "OSError" || e.type == "ValueError" || e.type == "UnicodeDecodeError" || e.type == "json.decoder.JSONDecodeError";
}
inline std::size_t py_len(const Val& v) {
    if (v.t == Val::LIST) return v.l->size();
    if (v.t == Val::DICT) return v.d->items.size();
    if (v.t == Val::STR) return cplen(v.s);
    throw PyErr("TypeError", std::string("object of type '") + type_name(v) + "' has no len()");
}
inline Val py_index(const Val& v, std::size_t i) {   // v[i], i small and non-negative
    if (v.t == Val::LIST) { if (i >= v.l->size()) throw PyErr("IndexError", "list index out of range"); return (*v.l)[i]; }
    if (v.t == Val::STR) { const auto c = cps(v.s); if (i >= c.size()) throw PyErr("IndexError", "string index out of range"); return Val::str(c[i]); }
    if (v.t == Val::DICT) throw PyErr("KeyError", std::to_string(i));
    throw PyErr("TypeError", std::string("'") + type_name(v) + "' object is not subscriptable");
}
inline const std::string& need_str(const Val& v, const char* method) {   // v.strip() / v.split() on a non-str
    if (v.t != Val::STR) throw PyErr("AttributeError", std::string("'") + type_name(v) + "' object has no attribute '" + method + "'");
    return v.s;
}
inline std::vector<std::string> split_on(const std::string& s, char sep) {   // str.split(sep)
    std::vector<std::string> o; std::string cur;
    for (char c : s) { if (c == sep) { o.push_back(cur); cur.clear(); } else cur += c; }
    o.push_back(cur); return o;
}
inline std::string join(const std::vector<std::string>& v, const std::string& sep) {
    std::string o; for (std::size_t k = 0; k < v.size(); ++k) { if (k) o += sep; o += v[k]; } return o;
}
inline Val sdict(std::initializer_list<std::pair<const char*, Val>> kv) { Val d = Val::dict(); for (const auto& [k, v] : kv) d.d->set(k, v); return d; }
inline Val deep(const Val& v) { return loads(dumps(v)); }
inline std::string i2s(long long v) { return std::to_string(v); }
inline std::string pct5(double x, int nd) { return rjust(fmt_f(x, nd), 5); }
inline void print_json(const Val& v) { DumpOpts o; o.indent = 2; print(dumps(v, o)); }
}  // namespace packtools::rp

// ---------------------------------------------------------------------------------------------- replica-design/contrast
namespace packtools::rp_contrast {
using namespace pyrt;
using namespace packtools::rp;

inline const std::string HEX = R"(^#?([0-9a-fA-F]{3}|[0-9a-fA-F]{6})$)";
struct Need { double aa, aaa; };
inline std::optional<Need> needs(const Val& size) {
    if (size.t != Val::STR) return std::nullopt;
    if (size.s == "normal") return Need{4.5, 7.0};
    if (size.s == "large") return Need{3.0, 4.5};
    if (size.s == "ui") return Need{3.0, 3.0};
    return std::nullopt;
}
struct OMap {   // a dict of str -> str that keeps insertion order
    std::vector<std::pair<std::string, std::string>> items;
    const std::string* get(const std::string& k) const { for (const auto& [a, b] : items) if (a == k) return &b; return nullptr; }
    void set(const std::string& k, const std::string& v) { for (auto& [a, b] : items) if (a == k) { b = v; return; } items.emplace_back(k, v); }
};
inline std::array<int, 3> parse_hex(const std::string& value) {
    const re::Match m = re::match(HEX, strip(value));
    if (!m) throw PyErr("ValueError", "not a hex colour: " + str_repr(value));
    std::string h = m.group(1);
    if (h.size() == 3) { std::string o; for (char c : h) { o += c; o += c; } h = o; }
    return {std::stoi(h.substr(0, 2), nullptr, 16), std::stoi(h.substr(2, 2), nullptr, 16), std::stoi(h.substr(4, 2), nullptr, 16)};
}
inline double luminance(const std::array<int, 3>& rgb) {
    const auto chan = [](int c0) { const double c = c0 / 255.0; return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
    const double r = chan(rgb[0]), g = chan(rgb[1]), b = chan(rgb[2]);
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}
inline double ratio(const std::string& fg, const std::string& bg) {
    const double la = luminance(parse_hex(fg)), lb = luminance(parse_hex(bg));
    const double hi = std::max(la, lb), lo = std::min(la, lb);
    return (hi + 0.05) / (lo + 0.05);
}
inline void flatten(const Val& obj, const std::string& prefix, OMap& out) {   // {"color": {"text": {"muted": "#..."}}} -> {"text-muted": "#..."}
    if (obj.t == Val::DICT) {
        const Val* v = obj.d->get("value");
        if (v && v->t == Val::STR) { out.set(prefix, v->s); return; }
        v = obj.d->get("$value");
        if (v && v->t == Val::STR) { out.set(prefix, v->s); return; }
        for (const auto& [k, x] : obj.d->items) {
            if (!k.empty() && (k[0] == '$' || k[0] == '_')) continue;
            flatten(x, prefix.empty() ? k : prefix + "-" + k, out);
        }
    } else if (obj.t == Val::STR && re::match(HEX, strip(obj.s))) {
        out.set(prefix, strip(obj.s));
    }
}
inline OMap colours(const Val& tokens) {
    const Dict& d = pm::as_dict(tokens);
    const Val src = d.has("color") ? *d.get("color") : d.has("colors") ? *d.get("colors") : d.has("colour") ? *d.get("colour") : Val::dict();
    OMap f; flatten(src, "", f);
    OMap o; for (const auto& [k, v] : f.items) if (re::match(HEX, v)) o.set(k, v);
    return o;
}
inline Val default_pairs(const OMap& cols) {
    std::vector<std::string> fg, bg;
    for (const auto& [k, v] : cols.items) {
        if (re::search(R"((^|-)(text|fg|on)(-|$))", k)) fg.push_back(k);
        if (re::search(R"((^|-)(bg|background|surface)(-|$))", k)) bg.push_back(k);
    }
    Val o = Val::list();
    for (const auto& f : fg) for (const auto& b : bg) o.l->push_back(Val::list({Val::str(f), Val::str(b)}));
    return o;
}
inline bool in_cols(const OMap& cols, const Val& k) { pm::need_hashable(k); return k.t == Val::STR && cols.get(k.s); }
inline Val check(const OMap& cols, const Val& pairs) {
    Val rows = Val::list();
    for (const Val& p : iter(pairs)) {
        if (py_len(p) < 2) continue;
        const Val fg = py_index(p, 0), bg = py_index(p, 1);
        Val size = py_len(p) > 2 ? py_index(p, 2) : Val::str("normal");
        pm::need_hashable(size);
        if (!needs(size)) size = Val::str("normal");
        if (!in_cols(cols, fg) || !in_cols(cols, bg)) {
            rows.l->push_back(sdict({{"fg", fg}, {"bg", bg}, {"size", size}, {"error", Val::str("unknown token")}}));
            continue;
        }
        const std::string& fh = *cols.get(fg.s); const std::string& bh = *cols.get(bg.s);
        const double r = ratio(fh, bh); const Need n = *needs(size);
        rows.l->push_back(sdict({{"fg", fg}, {"bg", bg}, {"size", size}, {"ratio", Val::flt(round_n(r, 2))},
                                 {"aa", Val::boolean(r >= n.aa)}, {"aaa", Val::boolean(r >= n.aaa)},
                                 {"fg_hex", Val::str(fh)}, {"bg_hex", Val::str(bh)}}));
    }
    return rows;
}
inline bool failing(const Val& r) { return has(r, "error") || !truthy(get(r, "aa")); }
inline std::string render(const Val& rows) {
    std::vector<std::string> out; long long fails = 0;
    for (const Val& r : *rows.l) {
        if (failing(r)) ++fails;
        if (has(r, "error")) {
            out.push_back("  ??    " + ljust(pystr(get(r, "fg")), 24) + " on " + ljust(pystr(get(r, "bg")), 20) + " " + pystr(get(r, "error")));
            continue;
        }
        const std::string size = get(r, "size").s;
        std::string grade;
        if (size == "ui") grade = truthy(get(r, "aa")) ? "PASS" : "FAIL";
        else grade = truthy(get(r, "aaa")) ? "AAA" : (truthy(get(r, "aa")) ? "AA" : "FAIL");
        out.push_back("  " + ljust(grade, 5) + " " + pct5(get(r, "ratio").num(), 2) + ":1  " + ljust(pystr(get(r, "fg")), 24) + " on " +
                      ljust(pystr(get(r, "bg")), 20) + " " + (size == "normal" ? std::string() : size));
    }
    out.push_back("");
    out.push_back("  " + i2s(static_cast<long long>(rows.l->size())) + " pairs, " + i2s(fails) + " failing AA");
    return join(out, "\n");
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("contrast", "Contrast check for replica-design tokens. Standard library only.");
    p.pos("args").nargs("+").help("tokens.json, or two hex colours");
    p.opt({"--json"}).store_true();
    const Val a = parse_args(p, argv);
    std::vector<std::string> args; for (const Val& x : *get(a, "args").l) args.push_back(x.s);
    OMap cols; Val pairs;
    try {
        if (args.size() == 2 && re::match(HEX, args[0]) && re::match(HEX, args[1])) {
            cols.set("fg", args[0]); cols.set("bg", args[1]);
            pairs = Val::list({Val::list({Val::str("fg"), Val::str("bg")})});
        } else {
            const Val tokens = loads(read_text_utf8(args[0]));
            cols = colours(tokens);
            const Val pv = get(tokens, "pairs");
            pairs = truthy(pv) ? pv : default_pairs(cols);
        }
    } catch (const PyErr& e) {
        if (!os_or_value(e)) throw;
        eprint(std::string("contrast: ") + e.what()); return 2;
    }
    if (!truthy(pairs)) { eprint("contrast: no pairs to check. Add a \"pairs\" list to the token file."); return 2; }
    const Val rows = check(cols, pairs);
    if (B(a, "json")) print_json(rows); else print(render(rows));
    for (const Val& r : *rows.l) if (failing(r)) return 1;
    return 0;
}
PACKTOOL("replica-design/contrast", main);
}  // namespace packtools::rp_contrast

// ---------------------------------------------------------------------------------------------- replica-brand/sweep
namespace packtools::rp_sweep {
using namespace pyrt;
using namespace packtools::rp;
namespace fs = std::filesystem;

inline const std::set<std::string> SKIP_DIRS = {".git", "node_modules", ".next", ".nuxt", ".svelte-kit", "dist", "build", "out", ".vercel", ".turbo",
                                                ".cache", "coverage", "__pycache__", ".venv", "venv", "Pods", ".expo", "DerivedData", ".gradle"};
inline const std::set<std::string> SKIP_FILES = {"package-lock.json", "yarn.lock", "pnpm-lock.yaml", "bun.lockb", "Podfile.lock", "Cargo.lock", "poetry.lock"};
inline const std::set<std::string> BINARY_EXT = {".png", ".jpg", ".jpeg", ".gif", ".webp", ".ico", ".icns", ".pdf", ".zip", ".gz", ".woff", ".woff2", ".ttf",
                                                 ".otf", ".eot", ".mp4", ".mov", ".mp3", ".wav", ".avif", ".heic", ".psd", ".sketch", ".fig", ".jar", ".so", ".dylib"};
inline const std::string HEX = R"(#(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{3})(?![0-9a-zA-Z]))";
#ifdef _WIN32
inline const char SEP = '\\';
#else
inline const char SEP = '/';
#endif

inline std::string norm_hex(const std::string& h0) {
    std::string h = lower(h0); std::size_t k = 0;
    while (k < h.size() && h[k] == '#') ++k;
    h = h.substr(k);
    const auto c = cps(h);
    if (c.size() == 3) { h.clear(); for (const auto& x : c) h += x + x; }
    return "#" + h;
}
struct Pat { std::string kind, label; const re::Regex* rx; };
inline std::vector<Pat> build_patterns(const List& avoid, const List& domains) {
    std::vector<Pat> pats;
    for (const Val& n : avoid) {
        const std::string name = strip(need_str(n, "strip"));
        if (name.empty()) continue;
        std::vector<std::string> parts;   // "Acuity Scheduling" also matches AcuityScheduling and acuity-scheduling
        for (const auto& x : re::split(R"([\s_-]+)", name)) if (!x.empty()) parts.push_back(re::escape(x));
        const std::string pat = join(parts, R"([\s_-]?)");
        pats.push_back({"name", name, &re::compile(pat, re::I)});
    }
    for (const Val& dv : domains) {
        const std::string d = lower(strip(need_str(dv, "strip")));
        if (!d.empty()) { const std::string pat = re::escape(d); pats.push_back({"domain", d, &re::compile(pat, re::I)}); }
    }
    return pats;
}
inline std::string splitext_ext(const std::string& name) {   // os.path.splitext(name)[1]: leading dots are not an extension
    const std::size_t dot = name.rfind('.');
    if (dot == std::string::npos) return "";
    for (std::size_t k = 0; k < dot; ++k) if (name[k] != '.') return name.substr(dot);
    return "";
}
inline bool is_binary(const fs::path& path, const std::string& name) {
    if (BINARY_EXT.count(lower(splitext_ext(name)))) return true;
#ifdef _WIN32
    std::FILE* f = _wfopen(path.c_str(), L"rb");
#else
    std::FILE* f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) return true;
    char buf[4096]; const std::size_t n = std::fread(buf, 1, sizeof buf, f); std::fclose(f);
    return std::memchr(buf, 0, n) != nullptr;
}
inline std::string u8(const fs::path& p) { const auto s = p.u8string(); return std::string(s.begin(), s.end()); }

struct Sweep {
    std::vector<Pat> pats; std::set<std::string> cols; bool include_replica = false; Val hits = Val::list();
    std::size_t max_size = 2000000;
    void hit(const std::string& file, long long line, const std::string& kind, const std::string& match, const std::string& text) {
        hits.l->push_back(sdict({{"file", Val::str(file)}, {"line", Val::integer(line)}, {"kind", Val::str(kind)}, {"match", Val::str(match)}, {"text", Val::str(text)}}));
    }
    static std::string rel_join(const std::string& rel_dir, const std::string& name) { return rel_dir == "." ? name : rel_dir + SEP + name; }
    void walk(const fs::path& dirpath, const std::string& rel_dir) {
        std::error_code ec;
        fs::directory_iterator it(dirpath, ec);
        if (ec) return;   // os.walk ignores directories it cannot list
        std::vector<std::string> dirnames, filenames;
        for (; it != fs::directory_iterator(); it.increment(ec)) {
            if (ec) return;
            std::error_code e2;
            const bool isdir = it->is_directory(e2);
            (isdir && !e2 ? dirnames : filenames).push_back(u8(it->path().filename()));
        }
        std::vector<std::string> keep;
        for (const auto& d : dirnames) {
            if (SKIP_DIRS.count(d)) continue;
            if (d == "replica" && rel_dir == "." && !include_replica) continue;
            keep.push_back(d);
        }
        std::sort(keep.begin(), keep.end());
        for (const auto& d : keep)
            for (const auto& p : pats)
                if (p.kind == "name" && p.rx->search(d)) hit(rel_join(rel_dir, d) + "/", 0, "path", p.label, "folder name");
        std::sort(filenames.begin(), filenames.end());
        for (const auto& fn : filenames) {
            if (SKIP_FILES.count(fn)) continue;
            const fs::path path = dirpath / fspath(fn);
            const std::string rel = rel_join(rel_dir, fn);
            for (const auto& p : pats)
                if (p.kind == "name" && p.rx->search(fn)) hit(rel, 0, "path", p.label, "file name");
            std::error_code se;
            const auto size = fs::file_size(path, se);
            if (se || size > max_size || is_binary(path, fn)) continue;
            std::string text;
            try { text = read_text_utf8(u8(path), true); } catch (const OSError&) { continue; }
            std::vector<std::string> lines;
            for (std::size_t s = 0; s < text.size();) {
                const std::size_t e = text.find('\n', s);
                if (e == std::string::npos) { lines.push_back(text.substr(s)); break; }
                lines.push_back(text.substr(s, e - s + 1)); s = e + 1;
            }
            for (std::size_t i = 0; i < lines.size(); ++i) {
                const std::string& line = lines[i];
                const long long ln = static_cast<long long>(i) + 1;
                for (const auto& p : pats)
                    if (p.rx->search(line)) hit(rel, ln, p.kind, p.label, cpslice(strip(line), 0, 160));
                if (!cols.empty())
                    for (const auto& m : re::compile(HEX).finditer(line)) {
                        const std::string nh = norm_hex(m.group(0));
                        if (cols.count(nh)) hit(rel, ln, "color", nh, cpslice(strip(line), 0, 160));
                    }
            }
        }
        for (const auto& d : keep) {
            const fs::path sub = dirpath / fspath(d);
            std::error_code le;
            if (fs::is_symlink(sub, le)) continue;   // followlinks=False
            walk(sub, rel_join(rel_dir, d));
        }
    }
};
inline std::string render(const Val& hits) {
    if (hits.l->empty()) return "Clean. Nothing of the original's name, domain or colours found.";
    std::vector<std::string> out; std::map<std::string, long long> by_kind;
    for (const Val& h : *hits.l) {
        const std::string kind = get(h, "kind").s;
        by_kind[kind] += 1;
        const long long line = get(h, "line").i;
        const std::string loc = line ? get(h, "file").s + ":" + i2s(line) : get(h, "file").s;
        out.push_back(ljust(kind, 7) + " " + ljust(cpslice(get(h, "match").s, 0, 18), 18) + " " + loc + "  " + get(h, "text").s);
    }
    out.push_back("");
    std::vector<std::string> kv; for (const auto& [k, n] : by_kind) kv.push_back(k + " " + i2s(n));
    out.push_back(i2s(static_cast<long long>(hits.l->size())) + " hits (" + join(kv, ", ") + "). Replace every one before you launch.");
    return join(out, "\n");
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("sweep", "Rebrand sweep for replica-brand and replica-deploy. Standard library only.");
    p.pos("root").nargs("?").def(".");
    p.opt({"--avoid"}).def("").help("comma-separated names");
    p.opt({"--domains"}).def("").help("comma-separated domains");
    p.opt({"--colors"}).def("").help("comma-separated hex colours");
    p.opt({"--config"}).help("JSON with avoid / domains / colors lists");
    p.opt({"--include-replica"}).store_true();
    p.opt({"--json"}).store_true();
    const Val a = parse_args(p, argv);
    const auto cli = [&](const char* k) { List o; for (const auto& x : split_on(S(a, k), ',')) if (!strip(x).empty()) o.push_back(Val::str(x)); return o; };
    List avoid = cli("avoid"), domains = cli("domains"), colors = cli("colors");
    if (!S(a, "config").empty()) {
        Val cfg;
        try { cfg = loads(read_text_utf8(S(a, "config"))); }
        catch (const PyErr& e) { if (!os_or_value(e)) throw; eprint(std::string("sweep: ") + e.what()); return 2; }
        const Dict& d = pm::as_dict(cfg);
        for (const Val& x : pm::iterate(d.at("avoid", Val::list()))) avoid.push_back(x);
        for (const Val& x : pm::iterate(d.at("domains", Val::list()))) domains.push_back(x);
        for (const Val& x : pm::iterate(d.at("colors", Val::list()))) colors.push_back(x);
    }
    if (avoid.empty() && domains.empty() && colors.empty()) { eprint("sweep: nothing to look for. Pass --avoid with the original app's name."); return 2; }
    Sweep sw;
    sw.pats = build_patterns(avoid, domains);
    for (const Val& c : colors) { const std::string& s = need_str(c, "strip"); if (re::fullmatch(HEX, strip(s))) sw.cols.insert(norm_hex(s)); }
    sw.include_replica = B(a, "include_replica");
    std::error_code ec;
    const fs::path root = fs::absolute(fspath(S(a, "root")), ec).lexically_normal();
    sw.walk(root, ".");
    if (B(a, "json")) print_json(sw.hits); else print(render(sw.hits));
    return sw.hits.l->empty() ? 0 : 1;
}
PACKTOOL("replica-brand/sweep", main);
}  // namespace packtools::rp_sweep

// ---------------------------------------------------------------------------------------------- replica-launch/listing
namespace packtools::rp_listing {
using namespace pyrt;
using namespace packtools::rp;

struct Store { const char* key; const char* title; std::vector<std::pair<std::string, long long>> limits; };
inline const std::vector<Store> LIMITS = {
    {"app_store", "App Store", {{"name", 30}, {"subtitle", 30}, {"promotional_text", 170}, {"description", 4000}, {"keywords", 100}, {"whats_new", 4000}}},
    {"google_play", "Google Play", {{"title", 30}, {"short_description", 80}, {"full_description", 4000}}},
};
inline const std::set<std::string> SHORT_FIELDS = {"name", "subtitle", "title", "short_description"};
inline const std::string CLAIMS = R"((#\s?1\b|\bnumber one\b|\bno\.? ?1\b|\bbest\b|\btop[- ]rated\b|\b(app|game) of the year\b)"
                                  R"(|\bfree\b|\bdiscount\b|\bsale\b|\b\d+% off\b|\bnew!?$))";
inline const std::set<std::string> STOP = {"a", "an", "and", "app", "for", "the", "to", "of", "with", "your", "you", "in", "on", "&"};

inline std::string text_of(const Val& v) {   // unicodedata.normalize("NFC", text or "")
    if (!truthy(v)) return "";
    if (v.t != Val::STR) throw PyErr("TypeError", std::string("normalize() argument 2 must be str, not ") + type_name(v));
    return v.s;
}
inline long long length(const Val& v) {   // characters as a person counts them (an emoji built from parts is one)
    const std::string text = uni::normalize("NFC", text_of(v));
    long long count = 0; bool prev_joiner = false;
    for (uint32_t ch : to_u32(text)) {
        const std::string cat = uni::category(ch);
        if (ch == 0x200D) { prev_joiner = true; continue; }
        if (cat == "Mn" || cat == "Me" || ch == 0xFE0F || ch == 0xFE0E || (ch >= 0x1F3FB && ch <= 0x1F3FF)) continue;
        if (prev_joiner) { prev_joiner = false; continue; }
        ++count;
    }
    return count;
}
inline bool has_emoji(const std::string& text) {
    for (uint32_t ch : to_u32(text)) if (std::string(uni::category(ch)) == "So" || ch >= 0x1F000) return true;
    return false;
}
inline std::vector<std::string> words(const std::string& text) {
    std::vector<std::string> o;
    for (const auto& w : re::findall(R"([a-z0-9']+)", lower(text))) if (!STOP.count(w)) o.push_back(w);
    return o;
}
inline std::string str_or_empty(const Val& v) { return truthy(v) ? v.s : std::string(); }   // (text or "") once length() has vetted it

inline Val lint(const Val& listing, const std::optional<std::vector<std::string>>& cli_avoid) {
    Val issues = Val::list();
    const auto add = [&](const std::string& level, const std::string& store, const std::string& field, const std::string& msg) {
        issues.l->push_back(sdict({{"level", Val::str(level)}, {"store", Val::str(store)}, {"field", Val::str(field)}, {"message", Val::str(msg)}}));
    };
    List src;
    if (cli_avoid && !cli_avoid->empty()) { for (const auto& s : *cli_avoid) src.push_back(Val::str(s)); }
    else { const Val la = pm::as_dict(listing).at("avoid"); if (truthy(la)) src = iter(la); }
    std::vector<std::string> avoid;
    for (const Val& x : src) { const std::string s = strip(need_str(x, "strip")); if (!s.empty()) avoid.push_back(s); }

    bool found_store = false;
    for (const auto& st : LIMITS) {
        const Val data = pm::as_dict(listing).at(st.key);
        if (!truthy(data)) continue;
        found_store = true;
        for (const auto& [field, limit] : st.limits) {
            const Val value = pm::as_dict(data).at(field, Val::str(""));
            const long long n = length(value);
            if (!truthy(value)) {
                const bool err = field == "name" || field == "title" || field == "description" || field == "full_description";
                add(err ? "error" : "warn", st.key, field, "empty");
                continue;
            }
            const std::string& v = value.s;
            if (n > limit) add("error", st.key, field, i2s(n) + " characters, limit is " + i2s(limit) + " (" + i2s(n - limit) + " over)");
            for (const auto& name : avoid) {
                const std::string pat = R"((?<!\w))" + re::escape(name) + R"((?!\w))";
                if (re::search(pat, v, re::I))
                    add("error", st.key, field, "mentions '" + name + "'. Another app's name or trademark in your metadata gets the listing rejected "
                        "and invites a takedown. Describe what yours does instead.");
            }
            if (SHORT_FIELDS.count(field)) {
                const re::Match m = re::search(CLAIMS, v, re::I);
                if (m) add("warn", st.key, field, "'" + strip(m.group(0)) + "' reads as a ranking or price claim. Google Play bans these in the title "
                           "and short description, and Apple rejects claims you cannot back up.");
                if (has_emoji(v)) add("warn", st.key, field, "emoji in a short field. Play rejects them in titles; App Store reviewers often do too.");
                const auto caps = re::findall(R"(\b[A-Z]{4,}\b)", v);
                if (!caps.empty()) add("warn", st.key, field, "all-caps word(s): " + join(caps, ", "));
            }
        }
        if (std::string(st.key) == "app_store" && truthy(pm::as_dict(data).at("keywords"))) {
            const std::string kw_raw = pm::as_dict(data).at("keywords").s;
            std::vector<std::string> parts; for (const auto& k : split_on(kw_raw, ',')) parts.push_back(lower(strip(k)));
            if (re::search(R"(,\s)", kw_raw)) add("warn", st.key, "keywords", "spaces after commas waste characters. Use word1,word2,word3");
            std::set<std::string> dupes;
            for (const auto& k : parts) if (!k.empty() && std::count(parts.begin(), parts.end(), k) > 1) dupes.insert(k);
            if (!dupes.empty()) add("warn", st.key, "keywords", "repeated: " + join(std::vector<std::string>(dupes.begin(), dupes.end()), ", "));
            std::set<std::string> in_name;
            for (const auto& w : words(str_or_empty(pm::as_dict(data).at("name", Val::str(""))))) in_name.insert(w);
            for (const auto& w : words(str_or_empty(pm::as_dict(data).at("subtitle", Val::str(""))))) in_name.insert(w);
            std::set<std::string> wasted;
            for (const auto& k : parts) for (const auto& w : words(k)) if (in_name.count(w)) wasted.insert(w);
            if (!wasted.empty()) add("warn", st.key, "keywords", "already in the name or subtitle, so these characters are wasted: " +
                                     join(std::vector<std::string>(wasted.begin(), wasted.end()), ", "));
            for (const auto& k : parts) if (k.empty()) { add("warn", st.key, "keywords", "empty entry (double comma or trailing comma)"); break; }
        }
    }
    if (!found_store) add("error", "-", "-", "no app_store or google_play section found");
    return issues;
}
inline std::string render(const Val& listing, const Val& issues) {
    std::vector<std::string> out;
    for (const auto& st : LIMITS) {
        const Val data = pm::as_dict(listing).at(st.key);
        if (!truthy(data)) continue;
        out.push_back(st.title);
        for (const auto& [field, limit] : st.limits) {
            const long long n = length(pm::as_dict(data).at(field, Val::str("")));
            const std::string mark = n > limit ? "OVER" : (n ? "ok" : "--");
            out.push_back("  " + ljust(field, 18) + " " + rjust(i2s(n), 5) + " / " + ljust(i2s(limit), 5) + " " + mark);
        }
        out.push_back("");
    }
    if (issues.l->empty()) out.push_back("No issues.");
    long long errs = 0;
    for (const Val& i : *issues.l) {
        if (get(i, "level").s == "error") ++errs;
        out.push_back(ljust(upper(get(i, "level").s), 5) + " " + get(i, "store").s + "." + get(i, "field").s + ": " + get(i, "message").s);
    }
    out.push_back("");
    out.push_back(i2s(errs) + " errors, " + i2s(static_cast<long long>(issues.l->size()) - errs) + " warnings");
    return join(out, "\n");
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("listing", "Store listing linter for replica-launch. Standard library only.");
    p.pos("listing").help("listing.json");
    p.opt({"--avoid"}).help("comma-separated names that must not appear (the original app, its company)");
    p.opt({"--json"}).store_true();
    const Val a = parse_args(p, argv);
    Val listing;
    try { listing = loads(read_text_utf8(S(a, "listing"))); }
    catch (const PyErr& e) { if (!os_or_value(e)) throw; eprint(std::string("listing: ") + e.what()); return 2; }
    std::optional<std::vector<std::string>> avoid;
    if (!S(a, "avoid").empty()) avoid = split_on(S(a, "avoid"), ',');
    const Val issues = lint(listing, avoid);
    if (B(a, "json")) print_json(issues); else print(render(listing, issues));
    for (const Val& i : *issues.l) if (get(i, "level").s == "error") return 1;
    return 0;
}
PACKTOOL("replica-launch/listing", main);
}  // namespace packtools::rp_listing

// ---------------------------------------------------------------------------------------------- replica-diff/parity
namespace packtools::rp_parity {
using namespace pyrt;
using namespace packtools::rp;

inline std::optional<long long> weight_of(const std::string& p) {
    if (p == "must" || p == "p0") return 3;
    if (p == "should" || p == "p1") return 2;
    if (p == "could" || p == "p2") return 1;
    return std::nullopt;
}
inline std::optional<double> credit_of(const std::string& c) {
    if (c == "yes" || c == "done") return 1.0;
    if (c == "partial") return 0.5;
    if (c == "no" || c.empty() || c == "todo") return 0.0;
    return std::nullopt;
}
inline std::string priority_name(const Val& w) {   // PRIORITY_NAME.get(w, "could")
    if (w.t == Val::INT || w.t == Val::FLOAT || w.t == Val::BOOL) {
        const double x = w.num();
        if (x == 3) return "must";
        if (x == 2) return "should";
        if (x == 1) return "could";
    }
    return "could";
}
struct MatrixError : PyErr { explicit MatrixError(const std::string& m) : PyErr("MatrixError", m) {} };
inline std::string sget(const Val& row, const std::string& k, const std::string& dflt = "") {
    const Val* v = row.d->get(k); if (!v) return dflt; return v->t == Val::STR ? v->s : pystr(*v);
}

inline std::vector<Val> load(const std::string& path) {
    const std::string text = read_text_utf8(path, false, true, true);
    if (csv_rows(text).empty()) throw MatrixError(path + " is empty");
    const CsvDict cd = csv_dicts(text);
    std::vector<std::string> fields; for (const auto& f : cd.fields) fields.push_back(lower(strip(f)));
    for (const char* need : {"feature", "priority", "clone"})
        if (std::find(fields.begin(), fields.end(), need) == fields.end())
            throw MatrixError(path + " has no '" + need + "' column. Columns needed: feature, area, priority, original, clone, notes");
    std::vector<Val> rows; long long i = 1;
    for (const auto& raw : cd.rows) {
        ++i;
        Val row = Val::dict();
        for (const auto& [k, v] : raw) {
            const std::string key = k ? lower(strip(*k)) : std::string();
            std::string val;
            if (v.t == Val::STR) val = strip(v.s);
            else if (truthy(v)) val = strip(need_str(v, "strip"));
            row.d->set(key, Val::str(val));
        }
        if (!truthy(row.d->at("feature"))) continue;
        row.d->set("line", Val::integer(i));
        rows.push_back(row);
    }
    return rows;
}
inline Val brief(const Val& r) {
    const Val* w = r.d->get("weight");
    const Val wv = w ? *w : Val::integer(weight_of(lower(sget(r, "priority"))).value_or(1));
    const std::string clone = sget(r, "clone");
    return sdict({{"feature", Val::str(sget(r, "feature"))}, {"area", Val::str(sget(r, "area"))}, {"priority", Val::str(priority_name(wv))},
                  {"clone", Val::str(clone.empty() ? "no" : clone)}, {"notes", Val::str(sget(r, "notes"))}});
}
inline Val score(const std::vector<Val>& rows) {
    std::vector<Val> counted, skipped, extras; std::vector<std::string> problems;
    for (const Val& row : rows) {
        std::string original = lower(sget(row, "original", "yes")); if (original.empty()) original = "yes";
        const std::string clone = lower(sget(row, "clone")), prio = lower(sget(row, "priority"));
        if (original == "no" || original == "n" || original == "false") { extras.push_back(row); continue; }
        if (clone == "skip") { skipped.push_back(row); continue; }
        const std::string line = i2s(row.d->at("line").i);
        if (!weight_of(prio)) problems.push_back("line " + line + ": priority '" + sget(row, "priority") + "' is not must/should/could, counted as could");
        if (!credit_of(clone)) problems.push_back("line " + line + ": clone '" + sget(row, "clone") + "' is not yes/partial/no/skip, counted as no");
        Val c = deep(row);
        c.d->set("weight", Val::integer(weight_of(prio).value_or(1)));
        c.d->set("credit", Val::flt(credit_of(clone).value_or(0.0)));
        counted.push_back(c);
    }
    const auto W = [](const Val& r) { return r.d->at("weight").i; };
    const auto C = [](const Val& r) { return r.d->at("credit").f; };
    long long total = 0; double got = 0;
    for (const Val& r : counted) { total += W(r); got += static_cast<double>(W(r)) * C(r); }
    const double feature_score = total ? 100.0 * got / static_cast<double>(total) : 0.0;

    struct Area { std::string name; double g = 0, t = 0; long long n = 0; };
    std::vector<Area> areas;
    for (const Val& r : counted) {
        std::string a = sget(r, "area"); if (a.empty()) a = "(no area)";
        auto it = std::find_if(areas.begin(), areas.end(), [&](const Area& x) { return x.name == a; });
        if (it == areas.end()) { areas.push_back({a}); it = areas.end() - 1; }
        it->g += static_cast<double>(W(r)) * C(r); it->t += static_cast<double>(W(r)); it->n += 1;
    }
    std::vector<Val> by_area;
    for (const auto& a : areas) by_area.push_back(sdict({{"area", Val::str(a.name)}, {"score", Val::flt(a.t != 0 ? round_n(100.0 * a.g / a.t, 1) : 0.0)}, {"features", Val::integer(a.n)}}));
    std::stable_sort(by_area.begin(), by_area.end(), [](const Val& x, const Val& y) { return get(x, "score").f < get(y, "score").f; });

    std::vector<Val> missing; for (const Val& r : counted) if (C(r) < 1.0) missing.push_back(r);
    std::stable_sort(missing.begin(), missing.end(), [&](const Val& x, const Val& y) {
        if (W(x) != W(y)) return W(x) > W(y);
        if (C(x) != C(y)) return C(x) < C(y);
        const std::string ax = sget(x, "area"), ay = sget(y, "area");
        if (ax != ay) return ax < ay;
        return sget(x, "feature") < sget(y, "feature");
    });
    long long must_total = 0, must_done = 0;
    for (const Val& r : counted) if (W(r) == 3) { ++must_total; if (C(r) == 1.0) ++must_done; }
    const auto briefs = [](const std::vector<Val>& v) { Val o = Val::list(); for (const Val& r : v) o.l->push_back(brief(r)); return o; };
    Val probs = Val::list(); for (const auto& p : problems) probs.l->push_back(Val::str(p));
    return sdict({{"feature_score", Val::flt(round_n(feature_score, 1))}, {"counted", Val::integer(static_cast<long long>(counted.size()))},
                  {"must_have_done", Val::integer(must_done)}, {"must_have_total", Val::integer(must_total)}, {"by_area", Val::list(by_area)},
                  {"missing", briefs(missing)}, {"skipped", briefs(skipped)}, {"extras", briefs(extras)}, {"problems", probs}});
}
inline bool contains(const Val& data, const std::string& key) {   // key in data
    if (data.t == Val::DICT) return data.d->has(key);
    if (data.t == Val::LIST) { for (const Val& x : *data.l) if (x.t == Val::STR && x.s == key) return true; return false; }
    if (data.t == Val::STR) return data.s.find(key) != std::string::npos;
    throw PyErr("TypeError", std::string("argument of type '") + type_name(data) + "' is not iterable");
}
inline double py_float_val(const Val& v) {   // float(v)
    if (v.t == Val::INT || v.t == Val::FLOAT || v.t == Val::BOOL) return v.num();
    if (v.t == Val::STR) return pm::py_float(v.s);
    throw PyErr("TypeError", std::string("float() argument must be a string or a real number, not '") + type_name(v) + "'");
}
inline std::vector<Val> visual_scores(const std::vector<std::string>& paths) {
    std::vector<Val> scores;
    for (const auto& p : paths) {
        const Val data = loads(read_text_utf8(p));
        if (!contains(data, "score")) throw MatrixError(p + " is not imgdiff.py --json output");
        const Val name = pm::as_dict(pm::as_dict(data).at("files", Val::dict())).at("clone", Val::str(p));
        const double sc = py_float_val(pm::as_dict(data).at("score"));
        scores.push_back(sdict({{"file", name}, {"score", Val::flt(sc)}, {"mode", pm::as_dict(data).at("mode", Val::str("layout"))}}));
    }
    return scores;
}
inline Val combine(Val result, const std::vector<Val>& visuals) {
    if (!visuals.empty()) {
        double sum = 0; for (const Val& v : visuals) sum += get(v, "score").f;
        const double layout = sum / static_cast<double>(visuals.size());
        result.d->set("visual", Val::list(visuals));
        result.d->set("layout_score", Val::flt(round_n(layout, 1)));
        result.d->set("overall", Val::flt(round_n(0.8 * get(result, "feature_score").f + 0.2 * layout, 1)));
    } else {
        result.d->set("overall", get(result, "feature_score"));
    }
    return result;
}
inline std::string render(const Val& r, bool markdown) {
    std::vector<std::string> out; const std::string h = markdown ? "## " : "";
    const long long done = get(r, "must_have_done").i, tot = get(r, "must_have_total").i;
    out.push_back(h + "Parity: " + fmt_f(get(r, "overall").f, 1) + " / 100");
    out.push_back("");
    out.push_back("features " + fmt_f(get(r, "feature_score").f, 1) + "  (" + i2s(get(r, "counted").i) + " counted, must-haves " + i2s(done) + " of " + i2s(tot) + " done)");
    if (has(r, "layout_score")) out.push_back("layout   " + fmt_f(get(r, "layout_score").f, 1) + "  (" + i2s(static_cast<long long>(get(r, "visual").l->size())) + " screens compared)");
    if (done < tot) { out.push_back(""); out.push_back("Not shippable yet: " + i2s(tot - done) + " must-have features are not done."); }
    out.push_back("");
    out.push_back(h + "By area, weakest first");
    for (const Val& a : *get(r, "by_area").l)
        out.push_back("- " + ljust(get(a, "area").s, 28) + " " + pct5(get(a, "score").f, 1) + "  (" + i2s(get(a, "features").i) + " features)");
    out.push_back("");
    out.push_back(h + "Missing, in build order");
    if (get(r, "missing").l->empty()) out.push_back("- nothing. Every counted feature is done.");
    for (const Val& m : *get(r, "missing").l) {
        const std::string notes = get(m, "notes").s, area = get(m, "area").s;
        out.push_back("- [" + get(m, "priority").s + "] " + (area.empty() ? "-" : area) + ": " + get(m, "feature").s + ", " + get(m, "clone").s +
                      (notes.empty() ? "" : "  (" + notes + ")"));
    }
    if (!get(r, "skipped").l->empty()) {
        out.push_back(""); out.push_back(h + "Left out on purpose (not scored)");
        for (const Val& m : *get(r, "skipped").l) { const std::string n = get(m, "notes").s; out.push_back("- " + get(m, "feature").s + ": " + (n.empty() ? "no reason given, add one" : n)); }
    }
    if (!get(r, "extras").l->empty()) {
        out.push_back(""); out.push_back(h + "Yours, not in the original (not scored)");
        for (const Val& m : *get(r, "extras").l) out.push_back("- " + get(m, "feature").s);
    }
    if (has(r, "visual") && !get(r, "visual").l->empty()) {
        out.push_back(""); out.push_back(h + "Screens");
        std::vector<Val> v = *get(r, "visual").l;
        std::stable_sort(v.begin(), v.end(), [](const Val& x, const Val& y) { return get(x, "score").f < get(y, "score").f; });
        for (const Val& x : v) out.push_back("- " + ljust(pystr(get(x, "file")), 40) + " " + pct5(get(x, "score").f, 1));
    }
    if (!get(r, "problems").l->empty()) {
        out.push_back(""); out.push_back(h + "Fix in the matrix");
        for (const Val& p : *get(r, "problems").l) out.push_back("- " + p.s);
    }
    return join(out, "\n");
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("parity", "Feature parity score for replica-diff. Standard library only.");
    p.pos("matrix").help("the feature matrix CSV");
    p.opt({"--visual"}).nargs("*").def(Val::list()).help("imgdiff.py --json outputs to fold in");
    p.opt({"--json"}).store_true();
    p.opt({"--markdown"}).store_true();
    p.opt({"--fail-under"}).number();
    const Val a = parse_args(p, argv);
    std::vector<std::string> visual; for (const Val& x : iter(get(a, "visual"))) visual.push_back(x.s);
    Val result;
    try { const Val sc = score(load(S(a, "matrix"))); result = combine(sc, visual_scores(visual)); }
    catch (const PyErr& e) {
        if (e.type != "MatrixError" && !os_or_value(e)) throw;
        eprint(std::string("parity: ") + e.what()); return 2;
    }
    if (B(a, "json")) print_json(result); else print(render(result, B(a, "markdown")));
    const Val fu = get(a, "fail_under");
    if (!fu.is_none() && get(result, "overall").num() < fu.num()) return 1;
    return 0;
}
PACKTOOL("replica-diff/parity", main);
}  // namespace packtools::rp_parity

// ---------------------------------------------------------------------------------------------- replica-entrepreneur/reviews
namespace packtools::rp_reviews {
using namespace pyrt;
using namespace packtools::rp;

struct Date { long long y, m, d; };
inline long long ordinal(const Date& t) {   // days since 1970-01-01
    long long y = t.y; y -= t.m <= 2; const long long era = (y >= 0 ? y : y - 399) / 400; const long long yoe = y - era * 400;
    const long long doy = (153 * (t.m + (t.m > 2 ? -3 : 9)) + 2) / 5 + t.d - 1; const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
inline int month_len(long long y, long long m) {
    static const int L[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return L[m - 1];
}
// datetime.strptime(text[:n], "%Y-%m-%d" | "%Y-%m").date(): CPython's _strptime regexes, a match that must reach the end,
// then date()'s range checks. A failure is the ValueError parse_date swallows.
inline std::optional<Date> parse_date(const std::string& text0) {
    static const std::string F1 = R"((?P<Y>\d\d\d\d)-(?P<m>1[0-2]|0[1-9]|[1-9])-(?P<d>3[0-1]|[1-2]\d|0[1-9]|[1-9]| [1-9]))";
    static const std::string F2 = R"((?P<Y>\d\d\d\d)-(?P<m>1[0-2]|0[1-9]|[1-9]))";
    const std::string text = strip(text0);
    for (const auto& [pat, n] : {std::pair<const std::string*, long>{&F1, 10}, std::pair<const std::string*, long>{&F2, 7}}) {
        const std::string s = cpslice(text, 0, n);
        const re::Regex* rx = &re::compile(*pat, re::I);
        const re::Match m = rx->match(s);
        if (!m || m.end(0) != static_cast<long>(cplen(s))) continue;
        const long long y = std::stoll(pm::ascii_digits(m.group(1))), mo = std::stoll(pm::ascii_digits(m.group(2)));
        const long long d = m.groups() >= 3 ? std::stoll(pm::ascii_digits(strip(m.group(3)))) : 1;
        if (y < 1 || d > month_len(y, mo)) continue;
        return Date{y, mo, d};
    }
    return std::nullopt;
}
inline std::optional<double> parse_rating(const std::string& text0) {
    const std::string text = strip(text0);
    if (text.empty()) return std::nullopt;
    const re::Match m = re::match(R"(^\s*(\d+(?:\.\d+)?))", text);
    if (!m) return std::nullopt;
    const double v = pm::py_float(m.group(1));
    if (v < 1 || v > 5) return std::nullopt;
    return v;
}
struct Review { long long line; std::string source, url; std::optional<Date> date; std::optional<double> rating; std::string text; };
struct Theme { Val id, label, kind; std::vector<const re::Regex*> patterns; };
struct FeedbackError : PyErr { explicit FeedbackError(const std::string& m) : PyErr("FeedbackError", m) {} };

inline const re::Regex* compile_pat(const Val& p) {
    if (p.t != Val::STR) throw PyErr("TypeError", "first argument must be string or compiled pattern");
    return &re::compile(p.s, re::I);
}
inline std::pair<std::vector<Theme>, std::vector<const re::Regex*>> load_themes(const std::string& path) {
    const Val data = loads(read_text_utf8(path));
    const Dict& d = pm::as_dict(data);
    std::vector<Theme> themes;
    for (const Val& t : pm::iterate(d.at("themes", Val::list()))) {
        if (t.t == Val::LIST) throw PyErr("TypeError", "list indices must be integers or slices, not str");
        if (t.t == Val::STR) throw PyErr("TypeError", "string indices must be integers, not 'str'");
        if (t.t != Val::DICT) throw PyErr("TypeError", std::string("'") + type_name(t) + "' object is not subscriptable");
        const Val* id = t.d->get("id");
        if (!id) throw PyErr("KeyError", "'id'");
        Theme th; th.id = *id; th.label = t.d->at("label", *id); th.kind = t.d->at("kind", Val::str("complaint"));
        for (const Val& p : pm::iterate(t.d->at("patterns", Val::list()))) th.patterns.push_back(compile_pat(p));
        themes.push_back(th);
    }
    std::vector<const re::Regex*> requests;
    for (const Val& p : pm::iterate(d.at("request_patterns", Val::list()))) requests.push_back(compile_pat(p));
    return {themes, requests};
}
inline std::string host(const std::string& url) {
    const re::Match m = re::match(R"(^https?://(?:www\.)?([^/]+))", url);
    return m ? m.group(1) : "unknown";
}
struct Loaded { std::vector<Review> kept; long long dropped = 0, dupes = 0; };
inline Loaded load_reviews(const std::string& path) {
    const std::string text = read_text_utf8(path, false, true, true);
    if (csv_rows(text).empty()) throw FeedbackError(path + " is empty");
    const CsvDict cd = csv_dicts(text);
    std::vector<std::string> cols; for (const auto& c : cd.fields) cols.push_back(lower(strip(c)));
    for (const char* need : {"url", "text"})
        if (std::find(cols.begin(), cols.end(), need) == cols.end())
            throw FeedbackError(path + " has no '" + need + "' column. Every review needs the link it came from and its exact text: source,url,date,rating,text");
    Loaded o; std::set<std::string> seen; long long i = 1;
    for (const auto& raw : cd.rows) {
        ++i;
        std::vector<std::pair<std::string, std::string>> row;
        for (const auto& [k, v] : raw) {
            const std::string key = k ? lower(strip(*k)) : std::string();
            std::string val;
            if (v.t == Val::STR) val = strip(v.s);
            else if (truthy(v)) val = strip(need_str(v, "strip"));
            bool set = false;
            for (auto& [a, b] : row) if (a == key) { b = val; set = true; break; }
            if (!set) row.emplace_back(key, val);
        }
        const auto rget = [&](const std::string& k) { for (const auto& [a, b] : row) if (a == k) return b; return std::string(); };
        const std::string url = rget("url"), txt = rget("text");
        if (url.empty() || txt.empty() || !re::match(R"(^https?://)", url)) { ++o.dropped; continue; }
        const std::string key = strip(re::sub(R"(\W+)", " ", lower(txt)));
        if (seen.count(key)) { ++o.dupes; continue; }
        seen.insert(key);
        std::string source = rget("source"); if (source.empty()) source = host(url);
        o.kept.push_back({i, source, url, parse_date(rget("date")), parse_rating(rget("rating")), txt});
    }
    return o;
}
inline double weight(const Review& rv, const Date& today, long long months) {
    double w = rv.rating ? (6.0 - *rv.rating) / 5.0 : 0.6;
    if (rv.date) {
        const long long age_days = ordinal(today) - ordinal(*rv.date);
        if (static_cast<double>(age_days) > static_cast<double>(months) * 30.4) w *= 0.5;
    }
    return w;
}
inline const std::string SENTENCE = R"((?<=[.!?])\s+|\n+)";
inline std::string snippet(const std::string& text, const re::Regex* pattern, long limit = 220) {   // the first matching sentence, verbatim
    for (const auto& sent : re::split(SENTENCE, text)) {
        if (!pattern->search(sent)) continue;
        std::string s = strip(sent);
        if (static_cast<long>(cplen(s)) > limit) {
            const re::Match m = pattern->search(s);
            if (!m) throw PyErr("AttributeError", "'NoneType' object has no attribute 'start'");
            const long start = std::max(0L, m.start() - limit / 2);
            s = strip(cpslice(s, start, start + limit));
            s = (start > 0 ? "..." : "") + s + "...";
        }
        return s;
    }
    const std::string s = strip(text);
    return static_cast<long>(cplen(s)) <= limit ? s : rstrip(cpslice(s, 0, limit)) + "...";
}
inline Val rating_val(const std::optional<double>& r) { return r ? Val::flt(*r) : Val(); }
inline bool id_lt(const Val& a, const Val& b) {   // a < b for the sort key's last element
    const auto num = [](const Val& v) { return v.t == Val::INT || v.t == Val::FLOAT || v.t == Val::BOOL; };
    if (num(a) && num(b)) return a.num() < b.num();
    if (a.t == Val::STR && b.t == Val::STR) return a.s < b.s;
    throw PyErr("TypeError", std::string("'<' not supported between instances of '") + type_name(a) + "' and '" + type_name(b) + "'");
}
inline Val analyse(const std::vector<Review>& reviews, const std::vector<Theme>& themes, const std::vector<const re::Regex*>& request_patterns,
                   const Date& today, long long months) {
    struct Item { const Review* rv; std::string quote; };
    struct Res { Val key, id, label, kind; std::vector<Item> items; double score = 0; };
    std::vector<Res> results;
    const auto find = [&](const Val& k) -> Res* { for (auto& r : results) if (pm::key_eq(r.key, k)) return &r; return nullptr; };
    for (const auto& t : themes) {
        pm::need_hashable(t.id);
        Res fresh; fresh.key = t.id; fresh.id = t.id; fresh.label = t.label; fresh.kind = t.kind;
        if (Res* r = find(t.id)) { fresh.key = r->key; *r = fresh; } else results.push_back(fresh);
    }
    Val requests = Val::list(); std::vector<const Review*> unthemed;
    for (const auto& rv : reviews) {
        const double w = weight(rv, today, months);
        bool matched = false;
        for (const auto& t : themes) {
            const re::Regex* hit = nullptr;
            for (const auto* p : t.patterns) if (p->search(rv.text)) { hit = p; break; }
            if (!hit) continue;
            matched = true;
            Res* res = find(t.id);
            res->score += w;
            res->items.push_back({&rv, snippet(rv.text, hit)});
        }
        const re::Regex* req = nullptr;
        for (const auto* p : request_patterns) if (p->search(rv.text)) { req = p; break; }
        if (req) requests.l->push_back(sdict({{"quote", Val::str(snippet(rv.text, req))}, {"url", Val::str(rv.url)}, {"source", Val::str(rv.source)}, {"rating", rating_val(rv.rating)}}));
        if (!matched && rv.rating && *rv.rating <= 2) unthemed.push_back(&rv);
    }
    const double total = static_cast<double>(reviews.empty() ? 1 : reviews.size());
    std::vector<Val> ranked;
    for (auto& res : results) {
        auto& items = res.items;
        if (items.empty()) continue;
        std::vector<double> rated; std::set<std::string> sources;
        for (const auto& x : items) { if (x.rv->rating) rated.push_back(*x.rv->rating); sources.insert(x.rv->source); }
        std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {   // lowest rated first, then shortest
            const double ra = a.rv->rating ? *a.rv->rating : 3, rb = b.rv->rating ? *b.rv->rating : 3;
            if (ra != rb) return ra < rb;
            return cplen(a.quote) < cplen(b.quote);
        });
        double rsum = 0; for (double x : rated) rsum += x;
        Val srcs = Val::list(); for (const auto& s : sources) srcs.l->push_back(Val::str(s));
        Val quotes = Val::list();
        for (std::size_t k = 0; k < items.size() && k < 3; ++k)
            quotes.l->push_back(sdict({{"quote", Val::str(items[k].quote)}, {"url", Val::str(items[k].rv->url)}, {"source", Val::str(items[k].rv->source)}, {"rating", rating_val(items[k].rv->rating)}}));
        ranked.push_back(sdict({{"id", res.id}, {"label", res.label}, {"kind", res.kind}, {"score", Val::flt(round_n(res.score, 2))},
                                {"count", Val::integer(static_cast<long long>(items.size()))},
                                {"share_pct", Val::flt(round_n(100.0 * static_cast<double>(items.size()) / total, 1))},
                                {"avg_rating", rated.empty() ? Val() : Val::flt(round_n(rsum / static_cast<double>(rated.size()), 2))},
                                {"sources", srcs}, {"thin", Val::boolean(items.size() < 3 || sources.size() < 2)}, {"quotes", quotes}}));
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const Val& a, const Val& b) {
        const double sa = get(a, "score").f, sb = get(b, "score").f;
        if (sa != sb) return sa > sb;
        const long long ca = get(a, "count").i, cb = get(b, "count").i;
        if (ca != cb) return ca > cb;
        return id_lt(get(a, "id"), get(b, "id"));
    });
    std::set<std::string> all_sources; long long rated_n = 0;
    for (const auto& r : reviews) { all_sources.insert(r.source); if (r.rating) ++rated_n; }
    Val srcs = Val::list(); for (const auto& s : all_sources) srcs.l->push_back(Val::str(s));
    Val un = Val::list();
    for (const auto* r : unthemed) un.l->push_back(sdict({{"url", Val::str(r->url)}, {"source", Val::str(r->source)}, {"rating", rating_val(r->rating)}, {"text", Val::str(cpslice(r->text, 0, 220))}}));
    return sdict({{"reviews", Val::integer(static_cast<long long>(reviews.size()))}, {"sources", srcs}, {"rated", Val::integer(rated_n)},
                  {"themes", Val::list(ranked)}, {"requests", requests}, {"unthemed_negative", un}});
}
inline std::string stars(const Val& r) { return r.is_none() ? "-" : fmt_g(r.num()) + "*"; }
inline std::string render(const Val& result, long long dropped, long long dupes) {
    std::vector<std::string> out = {"# What users of the original hate and want", ""};
    std::vector<std::string> srcs; for (const Val& s : *get(result, "sources").l) srcs.push_back(s.s);
    const std::string sj = join(srcs, ", ");
    out.push_back(i2s(get(result, "reviews").i) + " reviews from " + i2s(static_cast<long long>(srcs.size())) + " sources (" + (sj.empty() ? "none" : sj) + "). " +
                  i2s(get(result, "rated").i) + " had star ratings.");
    if (dropped) out.push_back(i2s(dropped) + " rows dropped: no link or no text. Nothing is counted without its source.");
    if (dupes) out.push_back(i2s(dupes) + " duplicate reviews removed.");
    if (get(result, "reviews").i < 30) {
        out.push_back("");
        out.push_back("Small sample. Under 30 reviews supports a direction, not a ranking. Collect more before you bet the roadmap on it.");
    }
    for (const auto& [kind, title] : {std::pair<const char*, const char*>{"complaint", "What they hate"}, {"request", "What they ask for"}}) {
        std::vector<Val> rows;
        for (const Val& t : *get(result, "themes").l) { const Val k = get(t, "kind"); if (k.t == Val::STR && k.s == kind) rows.push_back(t); }
        out.push_back(""); out.push_back(std::string("## ") + title); out.push_back("");
        if (rows.empty()) { out.push_back("Nothing matched. Read the reviews by hand."); continue; }
        out.push_back("| rank | theme | score | reviews | share | avg stars | sources |");
        out.push_back("| --- | --- | --- | --- | --- | --- | --- |");
        long long i = 0;
        for (const Val& t : rows) {
            ++i;
            const Val avg = get(t, "avg_rating");
            out.push_back("| " + i2s(i) + " | " + pystr(get(t, "label")) + (truthy(get(t, "thin")) ? " (thin)" : "") + " | " + fmt_f(get(t, "score").f, 1) + " | " +
                          i2s(get(t, "count").i) + " | " + fmt_f(get(t, "share_pct").f, 0) + "% | " + (avg.is_none() ? "-" : fmt_f(avg.f, 1)) + " | " +
                          i2s(static_cast<long long>(get(t, "sources").l->size())) + " |");
        }
        for (std::size_t k = 0; k < rows.size() && k < 6; ++k) {
            out.push_back(""); out.push_back("**" + pystr(get(rows[k], "label")) + "**");
            for (const Val& q : *get(rows[k], "quotes").l)
                out.push_back("- \"" + get(q, "quote").s + "\" (" + get(q, "source").s + ", " + stars(get(q, "rating")) + ") " + get(q, "url").s);
        }
    }
    const List& reqs = *get(result, "requests").l;
    if (!reqs.empty()) {
        out.push_back(""); out.push_back("## Asked for in their own words"); out.push_back("");
        for (std::size_t k = 0; k < reqs.size() && k < 25; ++k)
            out.push_back("- \"" + get(reqs[k], "quote").s + "\" (" + get(reqs[k], "source").s + ", " + stars(get(reqs[k], "rating")) + ") " + get(reqs[k], "url").s);
    }
    const List& un = *get(result, "unthemed_negative").l;
    if (!un.empty()) {
        out.push_back(""); out.push_back("## Read these by hand"); out.push_back("");
        out.push_back("Low ratings that matched no theme. Either noise, or a theme themes.json does not have yet. Often the most useful part.");
        out.push_back("");
        for (std::size_t k = 0; k < un.size() && k < 15; ++k)
            out.push_back("- (" + get(un[k], "source").s + ", " + stars(get(un[k], "rating")) + ") " + get(un[k], "url").s + ": \"" + get(un[k], "text").s + "\"");
    }
    return join(out, "\n") + "\n";
}
inline Date local_today() {
    const std::time_t t = std::time(nullptr); std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return {tm.tm_year + 1900LL, tm.tm_mon + 1LL, static_cast<long long>(tm.tm_mday)};
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("reviews", "Feedback ranker for replica-entrepreneur. Standard library only.");
    p.pos("reviews").help("CSV with source,url,date,rating,text");
    p.opt({"--themes"}).def(Val::str(skill_file("themes.json")));
    p.opt({"--months"}).integer().def(18).help("reviews older than this count half (default 18)");
    p.opt({"--today"}).help("YYYY-MM-DD, for reproducible runs");
    p.opt({"--out"}).help("write the markdown report here");
    p.opt({"--json"}).store_true();
    const Val a = parse_args(p, argv);
    std::vector<Theme> themes; std::vector<const re::Regex*> req; Loaded ld;
    try {
        auto tr = load_themes(S(a, "themes")); themes = std::move(tr.first); req = std::move(tr.second);
        ld = load_reviews(S(a, "reviews"));
    } catch (const PyErr& e) {
        if (e.type != "FeedbackError" && e.type != "KeyError" && e.type != "re.error" && !os_or_value(e)) throw;
        eprint(std::string("reviews: ") + e.what()); return 2;
    }
    if (ld.kept.empty()) { eprint("reviews: no usable rows. Every row needs a url (http...) and the review text."); return 2; }
    std::optional<Date> today;
    if (!S(a, "today").empty()) today = parse_date(S(a, "today"));
    Val result = analyse(ld.kept, themes, req, today ? *today : local_today(), get(a, "months").i);
    result.d->set("dropped", Val::integer(ld.dropped));
    result.d->set("duplicates", Val::integer(ld.dupes));
    if (B(a, "json")) { print_json(result); return 0; }
    const std::string text = render(result, ld.dropped, ld.dupes);
    if (!S(a, "out").empty()) {
        write_text(S(a, "out"), text);
        print("wrote " + S(a, "out"));
    } else {
        print(text.substr(0, text.size() - 1));
    }
    return 0;
}
PACKTOOL("replica-entrepreneur/reviews", main);
}  // namespace packtools::rp_reviews
