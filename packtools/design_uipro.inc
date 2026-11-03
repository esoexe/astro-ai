// design_uipro.inc - ui-ux-pro-max/search: the design-intelligence search of UI/UX Pro Max, re-written in C++.
// Upstream: github.com/nextlevelbuilder/ui-ux-pro-max-skill (MIT, (c) 2024 Next Level Builder), scripts/core.py,
// design_system.py and reasoning_contract.py. The data (styles, palettes, font pairings, UX rules, stacks...) ships as
// CSV files in the skill's data/ folder; this file only reads them.
//   ${ASTRO_TOOL} ui-ux-pro-max/search "<query>" [--domain D] [--stack S] [-n 3] [--json] [--full]
//   ${ASTRO_TOOL} ui-ux-pro-max/search "<query>" --design-system [-p "Name"] [--format markdown]
//            [--persist --output-dir <project root> [--page name] [--force]] [--variance 1-10] [--motion 1-10] [--density 1-10]
// The same BM25 ranking, tokenizer, synonyms, stopwords, domain detection, score floors, exact-identity lookups and the
// design-system reasoning rules as the original. Differences, said out loud: the google-fonts domain (a 750 KB list of
// every Google font) is not shipped, so font questions go to the 74 curated pairings in typography; the stack
// version-number check is simpler (an explicit "legacy"/"deprecated" asks for old guidance); --format ascii prints the
// same plain Markdown as --format markdown (no coloured terminal box).
#include <fstream>
#include <set>
#include <unordered_map>
#include <ctime>
#include <functional>

namespace packtools {
namespace uipro {
using std::string;
using std::vector;

// ---------------------------------------------------------------- rows and tables
struct Row {
    vector<std::pair<string, string>> kv;
    const string* find(const string& k) const { for (const auto& [a, v] : kv) if (a == k) return &v; return nullptr; }
    string get(const string& k, const string& dflt = "") const { const string* p = find(k); return p ? *p : dflt; }
    bool has(const string& k) const { return find(k) != nullptr; }
    bool empty() const { return kv.empty(); }
};
struct Table { vector<string> fields; vector<Row> rows; bool ok = false; };

inline string read_all(const string& path, bool& ok) {
    std::ifstream f(pyrt::fspath(path), std::ios::binary); ok = static_cast<bool>(f);
    if (!ok) return "";
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF && static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF) s.erase(0, 3);
    return s;
}
inline string data_path(const string& rel) { return skill_file(pyrt::path_join("data", rel)); }
inline bool data_exists(const string& rel) { return pyrt::exists(data_path(rel)); }
inline const Table& load(const string& rel) {
    static std::map<string, Table> cache;
    auto it = cache.find(rel);
    if (it != cache.end()) return it->second;
    Table t; bool ok = false; const string text = read_all(data_path(rel), ok);
    if (ok) {
        auto rows = pyrt::csv_rows(text);
        if (!rows.empty()) {
            t.fields = rows[0];
            for (std::size_t r = 1; r < rows.size(); ++r) {
                if (rows[r].empty()) continue;
                Row row;
                for (std::size_t j = 0; j < t.fields.size(); ++j) row.kv.emplace_back(t.fields[j], j < rows[r].size() ? rows[r][j] : string());
                t.rows.push_back(std::move(row));
            }
        }
        t.ok = true;
    }
    return cache.emplace(rel, std::move(t)).first->second;
}

// ---------------------------------------------------------------- text helpers (Python's \w and \s on code points)
inline vector<uint32_t> u32s(const string& s) { vector<uint32_t> v; for (const auto& ch : pyrt::cps(s)) v.push_back(pyrt::cp_of(ch)); return v; }
inline std::size_t cplen(const string& s) { return pyrt::cps(s).size(); }
inline uint32_t cp_before(const string& s, std::size_t i) {   // code point that ends just before byte i (0 = none)
    if (i == 0) return 0;
    std::size_t k = i - 1;
    while (k > 0 && (static_cast<unsigned char>(s[k]) & 0xC0) == 0x80) --k;
    return pyrt::cp_of(s.substr(k, i - k));
}
inline uint32_t cp_at(const string& s, std::size_t i) {   // code point that starts at byte i (0 = none)
    if (i >= s.size()) return 0;
    const unsigned char c = static_cast<unsigned char>(s[i]);
    const std::size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
    return pyrt::cp_of(s.substr(i, n));
}
inline bool wordc(uint32_t c) { return c != 0 && pyrt::py_word(c); }
inline bool has_word(const string& s) { for (uint32_t c : u32s(s)) if (wordc(c)) return true; return false; }
inline string lower(const string& s) { return pyrt::lower(s); }
inline string strip(const string& s) { return pyrt::strip(s); }
// find `needle` in `hay` at a token boundary: (?<!\w)needle(?!\w)
inline std::size_t find_bounded(const string& hay, const string& needle, std::size_t from = 0) {
    if (needle.empty()) return string::npos;
    for (std::size_t p = hay.find(needle, from); p != string::npos; p = hay.find(needle, p + 1))
        if (!wordc(cp_before(hay, p)) && !wordc(cp_at(hay, p + needle.size()))) return p;
    return string::npos;
}
inline bool contains_phrase(const string& text, const string& phrase) {
    return has_word(phrase) ? find_bounded(text, phrase) != string::npos : text.find(phrase) != string::npos;
}
inline vector<string> words(const string& s) {   // re.findall(r"\w+", s)
    vector<string> o; string cur;
    for (const auto& ch : pyrt::cps(s)) { if (wordc(pyrt::cp_of(ch))) cur += ch; else if (!cur.empty()) { o.push_back(cur); cur.clear(); } }
    if (!cur.empty()) o.push_back(cur);
    return o;
}
inline vector<string> split_bar(const string& s) {   // "a|b" -> stripped, non-empty
    vector<string> o; for (const auto& x : pyrt::split(s, "|")) { const string t = strip(x); if (!t.empty()) o.push_back(t); } return o;
}
inline vector<string> split_plus(const string& s) {   // "a + b" -> stripped parts (empty kept out)
    vector<string> o; for (const auto& x : pyrt::split(s, "+")) { const string t = strip(x); if (!t.empty()) o.push_back(t); } return o;
}
inline string join(const vector<string>& v, const string& sep) { string o; for (std::size_t i = 0; i < v.size(); ++i) { if (i) o += sep; o += v[i]; } return o; }
inline string replace_all(string s, const string& a, const string& b) { if (a.empty()) return s; for (std::size_t p = s.find(a); p != string::npos; p = s.find(a, p + b.size())) s.replace(p, a.size(), b); return s; }

// ---------------------------------------------------------------- tokenizer (core.py)
inline const std::set<string>& stopwords() {
    static const std::set<string> s = {"to", "in", "on", "at", "is", "of", "by", "or", "an", "if", "no", "so", "do", "be", "we", "it", "as", "the", "and", "for", "are", "was"};
    return s;
}
inline const vector<std::pair<string, string>>& synonyms() {   // longest variant first
    static const vector<std::pair<string, string>> v = [] {
        vector<std::pair<string, string>> x = {
            {"q&a", "question answer"}, {"e-commerce", "ecommerce"}, {"dark-mode", "dark"}, {"darkmode", "dark"}, {"light-mode", "light"},
            {"lightmode", "light"}, {"a11y", "accessibility"}, {"nav", "navigation"}, {"sign-up", "signup"}, {"log-in", "login"},
            {"colour", "color"}, {"colours", "colors"}, {"customisation", "customization"}, {"organisation", "organization"},
            {"behaviour", "behavior"}, {"ux/ui", "ux ui"}};
        std::stable_sort(x.begin(), x.end(), [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });
        return x;
    }();
    return v;
}
inline string normalize(const string& text_in) {   // text is lower-cased by the callers (the original matches case-insensitively)
    string t = text_in;
    for (const auto& [variant, canonical] : synonyms()) {
        string o; std::size_t from = 0;
        for (std::size_t p = find_bounded(t, variant); p != string::npos; p = find_bounded(t, variant, from)) { o += t.substr(from, p - from); o += canonical; from = p + variant.size(); }
        o += t.substr(from); t = o;
    }
    return t;
}
inline vector<string> tokenize(const string& text) {
    const string t = normalize(lower(text));
    vector<string> out; string cur;
    auto flush = [&] { if (!cur.empty()) { if (cplen(cur) >= 2 && !stopwords().count(cur)) out.push_back(cur); cur.clear(); } };
    for (const auto& ch : pyrt::cps(t)) {
        const uint32_t c = pyrt::cp_of(ch);
        if (wordc(c)) cur += ch; else flush();   // [^\w\s] -> space, then split()
    }
    flush();
    return out;
}

// ---------------------------------------------------------------- BM25 (k1 = 1.5, b = 0.75)
struct BM25 {
    vector<std::unordered_map<string, int>> tf; vector<std::size_t> len;
    std::unordered_map<string, int> df; std::unordered_map<string, double> idf; vector<string> vocab;
    double avgdl = 1.0; std::size_t N = 0;
    void fit(const vector<string>& docs) {
        N = docs.size(); if (!N) return;
        double total = 0;
        for (const auto& d : docs) {
            const auto toks = tokenize(d); len.push_back(toks.size()); total += static_cast<double>(toks.size());
            std::unordered_map<string, int> m; vector<string> order;
            for (const auto& w : toks) { if (!m.count(w)) order.push_back(w); m[w]++; }
            for (const auto& w : order) { if (!df.count(w)) vocab.push_back(w); df[w]++; }
            tf.push_back(std::move(m));
        }
        avgdl = total / static_cast<double>(N); if (avgdl == 0) avgdl = 1.0;
        for (const auto& w : vocab) { const double f = df[w]; idf[w] = std::log((static_cast<double>(N) - f + 0.5) / (f + 0.5) + 1); }
    }
    vector<std::pair<std::size_t, double>> score(const string& query) const {
        const auto q = tokenize(query); vector<std::pair<std::size_t, double>> s;
        for (std::size_t i = 0; i < N; ++i) {
            double sc = 0;
            for (const auto& t : q) {
                auto it = idf.find(t); if (it == idf.end()) continue;
                auto ft = tf[i].find(t); const double f = ft == tf[i].end() ? 0 : ft->second;
                sc += it->second * f * (1.5 + 1) / (f + 1.5 * (1 - 0.75 + 0.75 * static_cast<double>(len[i]) / avgdl));
            }
            s.emplace_back(i, sc);
        }
        std::stable_sort(s.begin(), s.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        return s;
    }
    bool knows(const string& t) const { return idf.count(t) != 0; }
};

// ---------------------------------------------------------------- domains (CSV_CONFIG)
struct Domain { string name, file; vector<string> search_cols, output_cols; };
inline const vector<Domain>& domains() {
    static const vector<Domain> d = {
        {"style", "styles.csv", {"Style ID", "Style Category", "Aliases", "Keywords", "Best For", "Type", "AI Prompt Keywords"},
         {"Style ID", "Style Category", "Aliases", "Status", "Parent Style ID", "Preferred Mode", "Type", "Keywords", "Primary Colors", "Effects & Animation", "Best For", "Light Mode \xE2\x9C\x93", "Dark Mode \xE2\x9C\x93", "Performance", "Accessibility", "Framework Compatibility", "Complexity", "AI Prompt Keywords", "CSS/Technical Keywords", "Implementation Checklist", "Design System Variables", "rtl_level"}},
        {"color", "colors.csv", {"Product Type", "Notes"},
         {"Product Type", "Primary", "On Primary", "Secondary", "On Secondary", "Accent", "On Accent", "Background", "Foreground", "Card", "Card Foreground", "Muted", "Muted Foreground", "Border", "Destructive", "On Destructive", "Ring", "Notes"}},
        {"chart", "charts.csv", {"Data Type", "Keywords", "Best Chart Type", "When to Use", "When NOT to Use", "Accessibility Notes"},
         {"Data Type", "Keywords", "Best Chart Type", "Secondary Options", "When to Use", "When NOT to Use", "Data Volume Threshold", "Color Guidance", "Accessibility Grade", "Accessibility Risk", "Accessibility Notes", "A11y Fallback", "Library Recommendation", "Interactive Level"}},
        {"landing", "landing.csv", {"Pattern ID", "Pattern Name", "Aliases", "Keywords", "Conversion Optimization", "Section Order"},
         {"Pattern ID", "Pattern Name", "Aliases", "Keywords", "Section Order", "Primary CTA Placement", "Color Strategy", "Conversion Optimization"}},
        {"product", "products.csv", {"Product Type", "Keywords", "Primary Style Recommendation", "Key Considerations"},
         {"Product Type", "Keywords", "Primary Style Recommendation", "Secondary Styles", "Landing Page Pattern", "Dashboard Style (if applicable)", "Color Palette Focus", "rtl_level"}},
        {"ux", "ux-guidelines.csv", {"Category", "Issue", "Description", "Platform"},
         {"Category", "Issue", "Platform", "Description", "Do", "Don't", "Code Example Good", "Code Example Bad", "Severity"}},
        {"typography", "typography.csv", {"Font Pairing Name", "Category", "Mood/Style Keywords", "Best For", "Heading Font", "Body Font"},
         {"Font Pairing Name", "Category", "Heading Font", "Body Font", "Mood/Style Keywords", "Best For", "Google Fonts URL", "CSS Import", "Tailwind Config", "Notes"}},
        {"icons", "icons.csv", {"Category", "Icon Name", "Keywords", "Best For", "Library"},
         {"Category", "Icon Name", "Keywords", "Library", "Import Code", "Usage", "Best For", "Style", "Semantic Role", "Allowed Contexts"}},
        {"gsap", "motion.csv", {"Category", "Intensity Tier", "Keywords", "Trigger"},
         {"Category", "Intensity Tier", "Trigger", "Duration", "Easing", "GSAP Snippet", "Framework Notes", "Do", "Don't", "Performance Notes"}},
        {"react", "react-performance.csv", {"Category", "Issue", "Keywords", "Description"},
         {"Category", "Issue", "Platform", "Description", "Do", "Don't", "Code Example Good", "Code Example Bad", "Severity"}},
        {"web", "app-interface.csv", {"Category", "Issue", "Keywords", "Description"},
         {"Category", "Issue", "Platform", "Description", "Do", "Don't", "Code Example Good", "Code Example Bad", "Severity"}},
    };
    return d;
}
inline const Domain* domain_of(const string& name) { for (const auto& d : domains()) if (d.name == name) return &d; return nullptr; }
inline const std::set<string>& untruncated() {
    static const std::set<string> s = {"Code Example Good", "Code Example Bad", "Code Good", "Code Bad", "Implementation Checklist", "Design System Variables", "CSS Import", "Tailwind Config", "GSAP Snippet"};
    return s;
}
inline const vector<string>& stacks() {
    static const vector<string> s = {"react", "nextjs", "vue", "svelte", "astro", "swiftui", "react-native", "flutter", "nuxtjs", "nuxt-ui", "html-tailwind",
                                     "shadcn", "jetpack-compose", "threejs", "angular", "laravel", "javafx", "wpf", "winui", "avalonia", "uno", "uwp"};
    return s;
}
inline bool stack_has_current_version(const string& s) {   // STACK_CURRENT_VERSIONS keys (every stack except wpf, uno, uwp)
    return s != "wpf" && s != "uno" && s != "uwp";
}
inline const vector<string>& stack_search_cols() { static const vector<string> v = {"Category", "Guideline", "Description", "Do", "Don't", "Code Good", "Code Bad"}; return v; }
inline const vector<string>& stack_output_cols() { static const vector<string> v = {"Category", "Guideline", "Description", "Do", "Don't", "Code Good", "Code Bad", "Severity", "Docs URL", "Applies To", "Status", "Verified At"}; return v; }

struct Threshold { double min_score = 0, min_coverage = 0; };
inline Threshold domain_threshold(const string& d) {
    Threshold t;
    if (d == "style") t.min_score = 4.3; else if (d == "landing") t.min_score = 4.0; else if (d == "product") t.min_score = 6.0;
    else if (d == "icons") t.min_score = 5.8; else if (d == "react") t.min_score = 3.3;
    if (d == "landing") t.min_coverage = 0.5;
    return t;
}
inline Threshold stack_threshold() { Threshold t; t.min_score = 3.6; t.min_coverage = 1.0 / 3.0; return t; }

// query rewrites for routing-only words ("" = drop, no replacement)
inline string rewrite_of(const string& domain, const string& kw) {
    static const std::map<string, std::map<string, string>> m = {
        {"color", {{"color", ""}, {"palette", ""}, {"hex", ""}, {"rgb", ""}, {"token", ""}, {"semantic", ""}, {"destructive", ""}, {"muted", ""}, {"foreground", ""}}},
        {"landing", {{"testimonial", "testimonials"}}},
        {"style", {{"css", ""}, {"implementation", ""}, {"variable", ""}, {"checklist", ""}, {"tailwind", ""}}},
        {"ux", {{"ux", "accessibility"}, {"usability", "accessibility"}, {"wcag", "accessibility"}}},
        {"google-fonts", {{"typography", "font"}}},
        {"icons", {{"lucide", ""}, {"symbol", ""}, {"glyph", ""}, {"pictogram", ""}}},
        {"gsap", {{"gsap", "animation"}, {"quickto", ""}, {"scrolltrigger", "scroll"}, {"flip plugin", ""}, {"splittext", ""}}},
        {"react", {{"nextjs", "react"}, {"usecallback", "memoization"}, {"useeffect", "effects"}}},
        {"web", {{"aria", "accessibility"}, {"outline", "focus"}, {"semantic", ""}, {"autocomplete", "input"}, {"preconnect", ""}}},
    };
    auto a = m.find(domain); if (a == m.end()) return ""; auto b = a->second.find(kw); return b == a->second.end() ? "" : b->second;
}

inline vector<string> product_keywords() {
    std::set<string> k = {"saas", "ecommerce", "fintech", "healthcare", "gaming", "portfolio", "crypto", "fitness", "marketplace", "banking", "cybersecurity",
                          "education", "travel", "restaurant", "real estate", "social media", "beauty", "spa", "salon", "wellness", "booking"};
    for (const auto& row : load("products.csv").rows) {
        string label = row.get("Product Type"), o; int depth = 0;
        for (char c : label) { if (c == '(') depth++; else if (c == ')' && depth > 0) depth--; else if (!depth) o += c; }
        o = lower(strip(o)); if (cplen(o) >= 4) k.insert(o);
    }
    vector<string> v(k.begin(), k.end());
    std::stable_sort(v.begin(), v.end(), [](const string& a, const string& b) { return cplen(a) > cplen(b); });
    return v;
}
inline const vector<std::pair<string, vector<string>>>& domain_keywords() {
    static const vector<std::pair<string, vector<string>>> d = {
        {"color", {"color", "palette", "hex", "rgb", "token", "semantic", "accent", "destructive", "muted", "foreground"}},
        {"chart", {"time series", "chart", "graph", "visualization", "trend", "bar chart", "pie", "scatter", "heatmap", "funnel", "forecast"}},
        {"landing", {"landing", "page", "cta", "conversion", "hero", "testimonial", "pricing", "section"}},
        {"product", product_keywords()},
        {"style", {"style", "design", "ui", "minimalism", "glassmorphism", "neumorphism", "brutalism", "dark mode", "flat", "aurora", "css", "implementation", "variable", "checklist", "tailwind"}},
        {"ux", {"ux", "usability", "accessibility", "wcag", "touch", "scroll", "animation", "keyboard", "navigation", "mobile"}},
        {"typography", {"font pairing", "typography pairing", "heading font", "body font"}},
        {"google-fonts", {"google font", "font family", "font weight", "font style", "variable font", "noto", "font for", "find font", "font subset", "font language", "monospace font", "serif font", "sans serif font", "display font", "handwriting font", "font", "typography", "serif", "sans"}},
        {"icons", {"icon", "icons", "lucide", "phosphor", "heroicons", "symbol", "glyph", "pictogram", "svg icon"}},
        {"gsap", {"gsap", "quickto", "scrolltrigger", "stagger", "magnetic cursor", "parallax", "page transition", "scroll reveal", "scroll-triggered", "scrollytelling", "flip plugin", "splittext", "shimmer", "skeleton loader"}},
        {"react", {"react", "next.js", "nextjs", "suspense", "memo", "usecallback", "useeffect", "rerender", "bundle", "waterfall", "barrel", "dynamic import", "rsc", "server component"}},
        {"web", {"aria", "focus", "outline", "semantic", "virtualize", "autocomplete", "form", "input type", "preconnect", "drag reorder", "single pointer", "touch target", "native accessibility"}},
    };
    return d;
}
inline const vector<string>* keywords_of(const string& domain) { for (const auto& [n, k] : domain_keywords()) if (n == domain) return &k; return nullptr; }

inline std::pair<string, vector<string>> rewrite_query(const string& query, const string& domain, const BM25& idx) {
    const vector<string>* kws = keywords_of(domain); if (!kws) return {query, {}};
    const string normalized = normalize(lower(query));
    std::set<string> repl, rewrites;
    for (const auto& kw : *kws) {
        if (!contains_phrase(normalized, kw)) continue;
        bool known = false; for (const auto& t : tokenize(kw)) if (idx.knows(t)) { known = true; break; }
        if (known) continue;
        const string r = rewrite_of(domain, kw);
        if (!r.empty()) { rewrites.insert(kw + "->" + r); repl.insert(r); }
    }
    if (repl.empty()) return {query, {}};
    return {query + " " + join(vector<string>(repl.begin(), repl.end()), " "), vector<string>(rewrites.begin(), rewrites.end())};
}

inline const vector<string>& tiebreak() { static const vector<string> v = {"ux", "product", "style", "color", "typography", "google-fonts", "chart", "landing", "icons", "gsap", "react", "web"}; return v; }
inline int tie_rank(const string& d) { const auto& v = tiebreak(); for (std::size_t i = 0; i < v.size(); ++i) if (v[i] == d) return static_cast<int>(i); return 999; }
inline bool has_hex_color(const string& q) {   // (?<!\w)#[0-9a-f]{3,8}(?!\w)
    for (std::size_t p = q.find('#'); p != string::npos; p = q.find('#', p + 1)) {
        if (wordc(cp_before(q, p))) continue;
        std::size_t k = p + 1; while (k < q.size() && std::isxdigit(static_cast<unsigned char>(q[k]))) ++k;
        const std::size_t n = k - p - 1; if (n >= 3 && n <= 8 && !wordc(cp_at(q, k))) return true;
    }
    return false;
}
inline std::pair<string, string> detect_domain(const string& query) {   // {best, runner-up or ""}
    const string q = normalize(lower(query));
    vector<std::pair<string, double>> scores;
    for (const auto& [d, kws] : domain_keywords()) {
        double total = 0;
        for (const auto& kw : kws) if (contains_phrase(q, kw)) { const double spec = std::max<double>(1, static_cast<double>(pyrt::split_ws(kw).size())); total += d != "product" ? 2.0 * spec : spec; }
        scores.emplace_back(d, total);
    }
    if (has_hex_color(q)) for (auto& [d, s] : scores) if (d == "color") s += 2.0;
    std::stable_sort(scores.begin(), scores.end(), [](const auto& a, const auto& b) { if (a.second != b.second) return a.second > b.second; return tie_rank(a.first) < tie_rank(b.first); });
    const string best = scores[0].second > 0 ? scores[0].first : "style";
    const string runner = scores.size() > 1 && scores[1].second > 0 ? scores[1].first : "";
    return {best, runner};
}

// ---------------------------------------------------------------- difflib.SequenceMatcher(None, a, b).ratio()
inline std::size_t matched_chars(const vector<uint32_t>& a, std::size_t alo, std::size_t ahi, const vector<uint32_t>& b, std::size_t blo, std::size_t bhi) {
    if (alo >= ahi || blo >= bhi) return 0;
    // longest common run; ties go to the run that ends first in a, then in b (find_longest_match without junk)
    std::size_t besti = alo, bestj = blo, bestk = 0;
    const std::size_t m = bhi - blo;
    vector<std::size_t> prev(m + 1, 0), cur(m + 1, 0);   // [j + 1] = length of the run ending at a[i], b[blo + j]
    for (std::size_t i = alo; i < ahi; ++i) {
        for (std::size_t j = 0; j < m; ++j) {
            const std::size_t k = a[i] == b[blo + j] ? prev[j] + 1 : 0; cur[j + 1] = k;
            if (k > bestk) { besti = i + 1 - k; bestj = blo + j + 1 - k; bestk = k; }
        }
        std::swap(prev, cur);
    }
    if (!bestk) return 0;
    return bestk + matched_chars(a, alo, besti, b, blo, bestj) + matched_chars(a, besti + bestk, ahi, b, bestj + bestk, bhi);
}
inline double seq_ratio(const string& x, const string& y) {
    const auto a = u32s(x), b = u32s(y); const std::size_t t = a.size() + b.size();
    if (!t) return 1.0;
    return 2.0 * static_cast<double>(matched_chars(a, 0, a.size(), b, 0, b.size())) / static_cast<double>(t);
}

// ---------------------------------------------------------------- searching one CSV
struct Result {
    string domain, stack, query, file, error, runner_up, redirect_domain, redirect_id;
    bool auto_detected = false; vector<Row> results; vector<string> suggestions;
};
using RowPred = std::function<bool(const Row&)>;
inline Row project(const Row& row, const vector<string>& cols) { Row o; for (const auto& c : cols) if (const string* v = row.find(c)) o.kv.emplace_back(c, *v); return o; }
inline double coverage(const BM25& idx, const string& q) {
    std::set<string> toks; for (const auto& t : tokenize(q)) toks.insert(t);
    if (toks.empty()) return 0.0;
    double n = 0; for (const auto& t : toks) if (idx.knows(t)) n += 1; return n / static_cast<double>(toks.size());
}
inline bool passes(const BM25& idx, const string& q, const Threshold& th) {
    const auto r = idx.score(q); const double top = r.empty() ? 0.0 : r[0].second;
    return top > th.min_score && coverage(idx, q) >= th.min_coverage;
}
struct CsvHit { vector<Row> rows; std::shared_ptr<BM25> idx; };
inline CsvHit search_csv(const vector<Row>& data, const vector<string>& search_cols, const vector<string>& output_cols, const string& query,
                         int max_results, const Threshold& th, const string& routing_domain, const RowPred& ranked_filter) {
    CsvHit hit; if (data.empty()) return hit;
    vector<string> docs; for (const auto& row : data) { vector<string> parts; for (const auto& c : search_cols) parts.push_back(row.get(c)); docs.push_back(join(parts, " ")); }
    hit.idx = std::make_shared<BM25>(); hit.idx->fit(docs);
    const string q = rewrite_query(query, routing_domain, *hit.idx).first;
    auto ranked = hit.idx->score(q);
    if (ranked_filter) { vector<std::pair<std::size_t, double>> f; for (const auto& x : ranked) if (ranked_filter(data[x.first])) f.push_back(x); ranked = f; }
    const double top = ranked.empty() ? 0.0 : ranked[0].second;
    const bool abstain = top <= th.min_score || coverage(*hit.idx, q) < th.min_coverage;
    if (!abstain) for (std::size_t i = 0; i < ranked.size() && static_cast<int>(i) < max_results; ++i) { if (ranked[i].second <= 0) continue; hit.rows.push_back(project(data[ranked[i].first], output_cols)); }
    return hit;
}
inline vector<string> suggest_terms(const BM25* idx, const string& query, const Threshold& th) {
    if (!idx) return {};
    std::set<string> qt; for (const auto& t : tokenize(query)) qt.insert(t);
    if (qt.empty()) return {};
    struct C { double sim; int df; string term; };
    vector<C> c;
    for (const auto& term : idx->vocab) {
        if (qt.count(term)) continue;
        double sim = 0; for (const auto& t : qt) sim = std::max(sim, seq_ratio(t, term));
        if (sim >= 0.72 && passes(*idx, term, th)) c.push_back({sim, idx->df.at(term), term});
    }
    std::sort(c.begin(), c.end(), [](const C& a, const C& b) { if (a.sim != b.sim) return a.sim > b.sim; if (a.df != b.df) return a.df > b.df; return a.term < b.term; });
    vector<string> o; for (std::size_t i = 0; i < c.size() && i < 6; ++i) o.push_back(c[i].term); return o;
}
inline vector<string> identities(const Row& row, const vector<string>& fields) {
    vector<string> o;
    for (const auto& f : fields) { if (f == "Aliases") { for (const auto& a : split_bar(row.get(f))) o.push_back(a); } else { const string v = strip(row.get(f)); if (!v.empty()) o.push_back(v); } }
    return o;
}
inline vector<string> suggest_identities(const vector<Row>& rows, const string& query, const vector<string>& fields) {
    std::set<string> qt; for (const auto& t : tokenize(query)) qt.insert(t);
    if (qt.empty()) return {};
    struct C { double sim; std::size_t n; string id; };
    vector<C> c; const string folded = lower(strip(query));
    for (const auto& row : rows) for (const auto& id : identities(row, fields)) {
        std::set<string> it; for (const auto& t : tokenize(id)) it.insert(t);
        if (it.empty()) continue;
        double sim = 0; for (const auto& a : qt) for (const auto& b : it) sim = std::max(sim, seq_ratio(a, b));
        if (sim >= 0.72 && lower(id) != folded) c.push_back({sim, it.size(), id});
    }
    std::sort(c.begin(), c.end(), [](const C& a, const C& b) { if (a.sim != b.sim) return a.sim > b.sim; if (a.n != b.n) return a.n < b.n; return a.id < b.id; });
    vector<string> o; for (const auto& x : c) { if (o.size() >= 6) break; if (std::find(o.begin(), o.end(), x.id) == o.end()) o.push_back(x.id); } return o;
}
inline const vector<string>& style_id_fields() { static const vector<string> v = {"Style ID", "Style Category", "Aliases"}; return v; }
inline const vector<string>& landing_id_fields() { static const vector<string> v = {"Pattern ID", "Pattern Name", "Aliases"}; return v; }
inline const Row* style_identity(const vector<Row>& rows, const string& query, bool allow_contained) {
    const string folded = lower(strip(query));
    std::set<string> qt; for (const auto& w : words(normalize(folded))) qt.insert(w);
    static const std::set<string> generic = {"app", "design", "interface", "style", "system", "ui"};
    struct C { std::size_t distinct, ntok, len; const Row* row; };
    vector<C> c;
    for (const auto& row : rows) {
        const auto ids = identities(row, style_id_fields());
        for (const auto& id : ids) if (lower(id) == folded) return &row;
        if (!allow_contained) continue;
        for (const auto& id : ids) {
            std::set<string> it; for (const auto& w : words(normalize(lower(id)))) it.insert(w);
            if (it.empty()) continue;
            bool subset = true, long4 = false; std::size_t distinct = 0;
            for (const auto& t : it) { if (!qt.count(t)) subset = false; if (cplen(t) >= 4) long4 = true; if (!generic.count(t)) distinct++; }
            if (subset && long4) c.push_back({distinct, it.size(), cplen(id), &row});
        }
    }
    if (c.empty()) return nullptr;
    std::stable_sort(c.begin(), c.end(), [](const C& a, const C& b) { if (a.distinct != b.distinct) return a.distinct > b.distinct; return a.ntok > b.ntok; });
    std::set<string> ids; const Row* one = nullptr;
    for (const auto& x : c) if (x.distinct == c[0].distinct && x.ntok == c[0].ntok && x.len == c[0].len) { if (ids.insert(x.row->get("Style ID")).second && !one) one = x.row; }
    return ids.size() == 1 ? one : nullptr;
}
inline const Row* exact_identity(const vector<Row>& rows, const string& query, const vector<string>& fields) {
    const string folded = lower(strip(query)); const Row* hit = nullptr; int n = 0;
    for (const auto& row : rows) { for (const auto& id : identities(row, fields)) if (lower(id) == folded) { if (!hit) hit = &row; n++; break; } }
    return n == 1 ? hit : nullptr;
}
inline const Row* row_by_style_id(const vector<Row>& rows, const string& id) { for (const auto& r : rows) if (r.get("Style ID") == id) return &r; return nullptr; }
// a deprecated style resolves to its parent or replacement, or names another domain to search instead
inline const Row* style_destination(const vector<Row>& rows, const Row* matched, string& rdomain, string& rid) {
    if (!matched || matched->get("Status", "active") != "deprecated") return matched;
    const string parent = strip(matched->get("Parent Style ID"));
    if (!parent.empty()) return row_by_style_id(rows, parent);
    const string dom = strip(matched->get("Replacement Domain")), rep = strip(matched->get("Replacement ID"));
    if (dom == "style" && !rep.empty()) return row_by_style_id(rows, rep);
    if (!dom.empty() && !rep.empty()) { rdomain = dom; rid = rep; }
    return nullptr;
}
inline bool rtl_ok(const Row& row, const string& level) {
    const string v = row.get("rtl_level");
    return level == "all" ? (v == "full" || v == "partial" || v == "caveats") : v == level;
}

inline Result search(const string& query, string domain, int max_results, const string& rtl = "") {
    Result out; out.query = query;
    const bool auto_detected = domain.empty();
    const Row* exact = nullptr; string rdom, rid;
    const vector<Row>& style_rows = load("styles.csv").rows;
    if (auto_detected) {
        const Row* m = style_identity(style_rows, query, false);
        if (m) { domain = "style"; exact = style_destination(style_rows, m, rdom, rid); }
        else { auto [best, runner] = detect_domain(query); domain = best; out.runner_up = runner; }
    }
    string search_domain = domain;
    if (search_domain == "google-fonts" && !data_exists("google-fonts.csv")) search_domain = "typography";   // not shipped: use the curated pairings
    const Domain* cfg = domain_of(search_domain);
    if (!cfg) { cfg = domain_of("style"); search_domain = "style"; }
    out.domain = domain; out.file = cfg->file; out.auto_detected = auto_detected;
    const Table& tab = load(cfg->file);
    if (!tab.ok) { out.error = "File not found: " + data_path(cfg->file); return out; }
    if (search_domain == "style" && !exact && rdom.empty()) exact = style_destination(style_rows, style_identity(style_rows, query, true), rdom, rid);
    else if (search_domain == "landing") exact = exact_identity(tab.rows, query, landing_id_fields());
    if (!rtl.empty() && exact && !rtl_ok(*exact, rtl)) exact = nullptr;
    CsvHit hit;
    if (exact) out.results.push_back(project(*exact, cfg->output_cols));
    else if (!rdom.empty()) { out.redirect_domain = rdom; out.redirect_id = rid; }
    else {
        vector<Row> data;
        for (const auto& row : tab.rows) if (search_domain != "style" || row.get("Status", "active") == "active") data.push_back(row);
        RowPred rf; if (!rtl.empty()) rf = [rtl](const Row& r) { return rtl_ok(r, rtl); };
        hit = search_csv(data, cfg->search_cols, cfg->output_cols, query, max_results, domain_threshold(search_domain), search_domain, rf);
        out.results = hit.rows;
    }
    if (search_domain == "icons" && contains_phrase(normalize(lower(query)), "lucide")) out.results.clear();
    if (out.results.empty() && out.redirect_domain.empty()) {
        if (search_domain == "landing") out.suggestions = suggest_identities(tab.rows, query, landing_id_fields());
        else out.suggestions = suggest_terms(hit.idx.get(), query, domain_threshold(search_domain));
    }
    return out;
}

// ---------------------------------------------------------------- stack guidelines
inline bool requests_legacy(const string& query, const string& stack) {
    if (stack == "uwp") return true;
    const auto ws = words(normalize(lower(query)));
    for (const auto& w : ws) for (const char* s : {"migrat", "upgrad", "replac"}) if (w.rfind(s, 0) == 0) return false;
    for (const auto& w : ws) if (w == "instead" || w == "modern" || w == "current") return false;
    for (const auto& w : ws) if (w == "legacy" || w == "deprecated") return true;
    return false;
}
inline bool ci_bounded(const string& hay, const string& needle) {   // (?<![A-Za-z0-9_])needle(?![A-Za-z0-9_]), case-insensitive
    const string h = lower(hay), n = lower(needle);
    auto idc = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    for (std::size_t p = h.find(n); p != string::npos; p = h.find(n, p + 1))
        if ((p == 0 || !idc(h[p - 1])) && (p + n.size() >= h.size() || !idc(h[p + n.size()]))) return true;
    return false;
}
inline Result search_stack(const string& query, const string& stack, int max_results) {
    Result out; out.domain = "stack"; out.stack = stack; out.query = query; out.file = "stacks/" + stack + ".csv";
    if (std::find(stacks().begin(), stacks().end(), stack) == stacks().end()) { out.error = "Unknown stack: " + stack + ". Available: " + join(stacks(), ", "); return out; }
    const Table& tab = load(out.file);
    if (!tab.ok) { out.error = "Stack file not found: " + data_path(out.file); return out; }
    std::set<string> statuses; for (const auto& r : tab.rows) statuses.insert(r.get("Status", "unverified"));
    const bool legacy = requests_legacy(query, stack), has_legacy = statuses.count("deprecated") != 0;
    RowPred status_ok; bool legacy_only = false;
    if (has_legacy && legacy) { status_ok = [](const Row& r) { return r.get("Status") == "deprecated"; }; legacy_only = true; }
    else if (legacy && stack_has_current_version(stack)) status_ok = [](const Row&) { return false; };
    else if (statuses.count("active")) status_ok = [](const Row& r) { return r.get("Status") == "active"; };
    else status_ok = [](const Row& r) { return r.get("Status", "unverified") != "deprecated"; };
    RowPred keep = status_ok;
    if (stack == "shadcn") {
        const string nq = normalize(lower(query)); string base;
        if (nq.find("base ui") != string::npos) base = "base"; else if (nq.find("react aria") != string::npos) base = "aria";
        else if (nq.find("radix") != string::npos || nq.find("aschild") != string::npos) base = "radix";
        if (!base.empty()) keep = [status_ok, base](const Row& r) {
            const string at = lower(r.get("Applies To")); const std::size_t p = at.find("base=");
            if (p == string::npos || (p > 0 && wordc(cp_before(at, p)))) return false;
            const string list = at.substr(p + 5, at.find(';', p) == string::npos ? string::npos : at.find(';', p) - p - 5);
            const auto bases = pyrt::split(list, "|");
            return status_ok(r) && std::find(bases.begin(), bases.end(), base) != bases.end();
        };
    }
    const Threshold th = legacy_only ? Threshold{} : stack_threshold();
    const string ident = strip(query);
    const Row* exact = nullptr;
    if (cplen(ident) >= 6 && ident.find_first_of(" \t\r\n") == string::npos) {
        int n = 0;
        for (const auto& r : tab.rows) {
            if (!keep(r)) continue;
            bool m = false; for (const char* f : {"Guideline", "Description", "Do", "Don't", "Code Good", "Code Bad"}) if (ci_bounded(r.get(f), ident)) { m = true; break; }
            if (m) { if (!exact) exact = &r; n++; }
        }
        if (n != 1) exact = nullptr;
    }
    CsvHit hit;
    if (exact) out.results.push_back(project(*exact, stack_output_cols()));
    else {
        vector<Row> data; for (const auto& r : tab.rows) if (keep(r)) data.push_back(r);
        hit = search_csv(data, stack_search_cols(), stack_output_cols(), query, max_results, th, "", nullptr);
        out.results = hit.rows;
    }
    if (out.results.empty()) out.suggestions = suggest_terms(hit.idx.get(), query, th);
    return out;
}

// ---------------------------------------------------------------- output
inline string format_result(const Result& r, bool full) {
    if (!r.error.empty()) return "Error: " + r.error;
    vector<string> o;
    if (!r.stack.empty()) { o.push_back("## UI Pro Max Stack Guidelines"); o.push_back("**Stack:** " + r.stack + " | **Query:** " + r.query); }
    else {
        o.push_back("## UI Pro Max Search Results");
        string note = r.domain;
        if (r.auto_detected) { note += " (auto-detected"; if (!r.runner_up.empty()) note += ", runner-up: " + r.runner_up; note += ")"; }
        o.push_back("**Domain:** " + note + " | **Query:** " + r.query);
    }
    o.push_back("**Source:** " + r.file + " | **Found:** " + std::to_string(r.results.size()) + " results\n");
    if (r.results.empty()) {
        if (!r.redirect_domain.empty()) {
            o.push_back("This legacy style label is now modeled in the `" + r.redirect_domain + "` domain as `" + r.redirect_id + "`. Search that domain instead of treating a page composition as a visual style.");
            return join(o, "\n");
        }
        o.push_back("No matches. This is not a match with an empty value -- the query did not hit the database. Retry with broader/different keywords before falling back to general defaults, and say explicitly that no database match was found if you do fall back.");
        if (!r.suggestions.empty()) o.push_back("**Closest known terms:** " + join(r.suggestions, ", "));
        return join(o, "\n");
    }
    for (std::size_t i = 0; i < r.results.size(); ++i) {
        o.push_back("### Result " + std::to_string(i + 1));
        for (const auto& [k, v] : r.results[i].kv) {
            string s = v;
            if (!full && !untruncated().count(k)) { const auto c = pyrt::cps(s); if (c.size() > 300) { s.clear(); for (std::size_t j = 0; j < 300; ++j) s += c[j]; s += "..."; } }
            o.push_back("- **" + k + ":** " + s);
        }
        o.push_back("");
    }
    return join(o, "\n");
}
inline string jq(const string& s) { return pyrt::jstr(s, false); }
inline string json_row(const Row& r, const string& ind) {
    if (r.kv.empty()) return "{}";
    string o = "{\n";
    for (std::size_t i = 0; i < r.kv.size(); ++i) o += ind + "  " + jq(r.kv[i].first) + ": " + jq(r.kv[i].second) + (i + 1 < r.kv.size() ? ",\n" : "\n");
    return o + ind + "}";
}
inline string json_list(const vector<string>& v) { string o = "["; for (std::size_t i = 0; i < v.size(); ++i) o += (i ? ", " : "") + jq(v[i]); return o + "]"; }
inline string json_result(const Result& r) {
    vector<string> f;
    if (!r.error.empty()) { f.push_back("  \"error\": " + jq(r.error)); }
    f.push_back("  \"domain\": " + jq(r.domain));
    if (!r.stack.empty()) f.push_back("  \"stack\": " + jq(r.stack));
    f.push_back("  \"query\": " + jq(r.query));
    f.push_back("  \"file\": " + jq(r.file));
    f.push_back("  \"count\": " + std::to_string(r.results.size()));
    string res = "  \"results\": [";
    for (std::size_t i = 0; i < r.results.size(); ++i) res += string(i ? "," : "") + "\n    " + json_row(r.results[i], "    ");
    res += r.results.empty() ? "]" : "\n  ]";
    f.push_back(res);
    if (r.auto_detected) { f.push_back("  \"auto_detected\": true"); if (!r.runner_up.empty()) f.push_back("  \"runner_up_domain\": " + jq(r.runner_up)); }
    if (!r.redirect_domain.empty()) f.push_back("  \"redirect\": {\"domain\": " + jq(r.redirect_domain) + ", \"id\": " + jq(r.redirect_id) + "}");
    if (r.results.empty() && r.error.empty()) f.push_back("  \"suggestions\": " + json_list(r.suggestions));
    return "{\n" + join(f, ",\n") + "\n}";
}

// ---------------------------------------------------------------- design system (design_system.py + reasoning_contract.py)
inline const vector<std::pair<string, vector<string>>>& condition_signals() {
    static const vector<std::pair<string, vector<string>>> v = {
        {"if_booking", {"booking", "appointment", "calendar"}}, {"if_boutique", {"boutique"}}, {"if_casual", {"casual", "playful"}},
        {"if_checkout", {"checkout", "payment", "purchase"}}, {"if_children", {"child", "children", "kids"}},
        {"if_collaboration", {"collaboration", "multiplayer", "co-edit"}}, {"if_competitive", {"competitive", "leaderboard"}},
        {"if_content_focused", {"content", "article", "reading", "documentation"}}, {"if_conversion_focused", {"conversion", "sales", "signup", "purchase"}},
        {"if_creative_field", {"creative", "artist", "portfolio"}}, {"if_crop_focused", {"crop", "farm", "agriculture"}},
        {"if_dashboard", {"dashboard", "operations", "monitoring"}}, {"if_data_heavy", {"data heavy", "data-heavy", "analytics", "large dataset"}},
        {"if_delivery", {"delivery", "courier", "shipping"}}, {"if_discovery_focused", {"discover", "discovery", "browse", "directory"}},
        {"if_engagement_metric", {"engagement", "retention", "contribution"}}, {"if_experience_focused", {"experience", "immersive", "journey"}},
        {"if_gamification", {"gamification", "badges", "streak"}}, {"if_health", {"health", "medical", "patient"}},
        {"if_hero_needed", {"hero", "showcase", "launch"}}, {"if_large_dataset", {"large dataset", "thousands", "millions"}},
        {"if_light_mode_needed", {"light mode", "light theme"}}, {"if_low_performance", {"low performance", "low-end", "slow device"}},
        {"if_luxury", {"luxury", "premium", "high-end"}}, {"if_medication", {"medication", "medicine", "prescription"}},
        {"if_meditation", {"meditation", "breathing", "mindfulness"}}, {"if_minimal_portfolio", {"minimal portfolio", "simple portfolio"}},
        {"if_mobile", {"mobile", "phone", "tablet", "ios", "android"}}, {"if_personalized", {"personalized", "personalised", "recommendation"}},
        {"if_pre_launch", {"pre-launch", "prelaunch", "coming soon", "waitlist"}}, {"if_salary_focused", {"salary", "compensation", "pay range"}},
        {"if_team_collaboration", {"team collaboration", "team workspace"}}, {"if_trust_needed", {"trust", "secure", "verified", "authority"}},
        {"if_ux_focused", {"ux", "usability", "accessibility", "accessible"}}, {"if_video_ready", {"video ready", "product video", "demo video"}},
    };
    return v;
}
inline bool condition_active(const string& cond, const string& folded) {
    if (cond == "must_have") return true;
    for (const auto& [c, sig] : condition_signals()) if (c == cond) { for (const auto& s : sig) if (find_bounded(folded, s) != string::npos) return true; return false; }
    return false;
}
struct Reasoning {
    string pattern = "Hero + Features + CTA", color_mood = "Professional", typography_mood = "Clean", key_effects = "Subtle hover transitions", anti_patterns, mode, severity = "MEDIUM";
    vector<string> style_priority = {"Minimalism", "Flat Design"}, constraints; bool is_default = true;
};
inline const Row* resolve_style(const string& reference) {
    static std::map<string, const Row*> lookup;
    if (lookup.empty()) for (const auto& s : load("styles.csv").rows) {
        vector<string> keys = {s.get("Style ID"), s.get("Style Category")}; for (const auto& a : pyrt::split(s.get("Aliases"), "|")) keys.push_back(a);
        for (const auto& k : keys) { const string t = lower(strip(k)); if (!t.empty()) lookup[t] = &s; }
    }
    auto it = lookup.find(lower(strip(reference))); const Row* st = it == lookup.end() ? nullptr : it->second;
    std::set<string> seen;
    while (st && st->get("Status", "active") == "deprecated") {
        const string id = st->get("Style ID"), parent = st->get("Parent Style ID");
        if (parent.empty() || seen.count(id)) return nullptr;
        seen.insert(id); auto p = lookup.find(lower(parent)); st = p == lookup.end() ? nullptr : p->second;
    }
    return st;
}
inline Reasoning apply_reasoning(const string& category, const string& query) {
    Reasoning r; const Row* rule = nullptr;
    for (const auto& row : load("ui-reasoning.csv").rows) if (lower(strip(row.get("UI_Category"))) == lower(strip(category))) { rule = &row; break; }
    if (!rule) return r;
    r.is_default = false; r.style_priority.clear();
    vector<string> style_ids; string pattern;
    const string folded = lower(query);
    try {
        const pyrt::Val rules = pyrt::loads(rule->get("Decision_Rules", "{}").empty() ? "{}" : rule->get("Decision_Rules", "{}"));
        if (rules.t == pyrt::Val::DICT) for (const auto& [cond, acts] : rules.d->items) {
            if (acts.t != pyrt::Val::LIST || !condition_active(cond, folded)) continue;
            for (const auto& a : *acts.l) {
                if (a.t != pyrt::Val::STR) continue; const std::size_t c = a.s.find(':'); if (c == string::npos) continue;
                const string pre = a.s.substr(0, c), val = a.s.substr(c + 1);
                if (pre == "style") { if (std::find(style_ids.begin(), style_ids.end(), val) == style_ids.end()) style_ids.push_back(val); }
                else if (pre == "constraint") { if (std::find(r.constraints.begin(), r.constraints.end(), val) == r.constraints.end()) r.constraints.push_back(val); }
                else if (pre == "pattern") pattern = val;
                else if (pre == "mode" && (val == "dark" || val == "light")) r.mode = val;
            }
        }
    } catch (const std::exception&) { /* a broken rule row only loses its extra rules */ }
    for (const auto& id : style_ids) { const Row* s = resolve_style(id); r.style_priority.push_back(s ? s->get("Style Category", id) : id); }
    for (const auto& s : pyrt::split(rule->get("Style_Priority"), "+")) r.style_priority.push_back(strip(s));
    r.pattern = !pattern.empty() ? pattern : rule->get("Recommended_Pattern");
    r.color_mood = rule->get("Color_Mood"); r.typography_mood = rule->get("Typography_Mood"); r.key_effects = rule->get("Key_Effects");
    r.anti_patterns = rule->get("Anti_Patterns"); r.severity = rule->get("Severity", "MEDIUM");
    return r;
}
inline std::optional<double> luminance(const string& hex) {
    string v = strip(hex); while (!v.empty() && v[0] == '#') v.erase(0, 1);
    if (v.size() == 3) { string w; for (char c : v) { w += c; w += c; } v = w; }
    if (v.size() != 6) return std::nullopt;
    double ch[3];
    for (int i = 0; i < 3; ++i) {
        const string part = v.substr(static_cast<std::size_t>(i) * 2, 2);
        if (!std::isxdigit(static_cast<unsigned char>(part[0])) || !std::isxdigit(static_cast<unsigned char>(part[1]))) return std::nullopt;
        const double c = std::strtol(part.c_str(), nullptr, 16) / 255.0;
        ch[i] = c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    }
    return 0.2126 * ch[0] + 0.7152 * ch[1] + 0.0722 * ch[2];
}
inline double contrast(const string& a, const string& b) {
    const auto x = luminance(a), y = luminance(b); if (!x || !y) return 0;
    return (std::max(*x, *y) + 0.05) / (std::min(*x, *y) + 0.05);
}
inline bool palette_dark(const Row& p) { const auto l = luminance(p.get("Background")); return l && *l < 0.18; }
inline bool style_dark_primary(const Row& s) {
    if (s.empty()) return false;
    const string pm = lower(strip(s.get("Preferred Mode")));
    if (pm == "dark" || pm == "light") return pm == "dark";
    const string lm = s.get("Light Mode \xE2\x9C\x93"), dm = s.get("Dark Mode \xE2\x9C\x93");
    if (lm == "not-recommended" && dm == "supported") return true;
    const string decl = lower(lm + " " + dm);
    for (const char* m : {"dark mode primary", "dark primary", "dark-only", "dark only", "dark preferred", "dark focused", "dark-first", "dark rich", "light mode only as exception"}) if (decl.find(m) != string::npos) return true;
    return false;
}
inline bool query_wants_dark(const string& q) {
    const string l = lower(q);
    for (const char* m : {"dark mode", "dark theme", "dark ui", "dark-mode", "darkmode", "night mode", "midnight", "oled"}) if (l.find(m) != string::npos) return true;
    return false;
}
inline Row derive_dark(const Row& p) {
    Row d = p; const string bg = "#0F172A"; string ring = "#60A5FA";
    for (const string& c : {p.get("Ring"), p.get("Accent"), p.get("Primary"), string("#60A5FA")}) if (contrast(c, bg) >= 3) { ring = c; break; }
    auto set = [&](const string& k, const string& v) { for (auto& [a, b] : d.kv) if (a == k) { b = v; return; } d.kv.emplace_back(k, v); };
    set("Background", bg); set("Foreground", "#F8FAFC"); set("Card", "#111827"); set("Card Foreground", "#F8FAFC"); set("Muted", "#1E293B");
    set("Muted Foreground", "#CBD5E1"); set("Border", "#334155"); set("Ring", ring);
    return d;
}
inline Row pick_palette(const vector<Row>& pals, const string& mode, const string& category) {
    if (pals.empty()) return {};
    for (const auto& p : pals) if (p.get("Product Type") == category) return mode == "dark" && !palette_dark(p) ? derive_dark(p) : p;
    if (mode == "dark") for (const auto& p : pals) if (palette_dark(p)) return p;
    return pals[0];
}
inline string filter_anti(const string& anti, const string& mode) {
    if (mode != "dark" || anti.empty()) return anti;
    vector<string> kept;
    for (const auto& c : pyrt::split(anti, "+")) { const string l = lower(c); if (l.find("dark mode") != string::npos || l.find("dark modes") != string::npos || l.find("dark theme") != string::npos) continue; const string t = strip(c); if (!t.empty()) kept.push_back(t); }
    return join(kept, " + ");
}
struct Dial { int value = 0; string label; vector<string> keywords; string tier; vector<std::pair<string, string>> spacing; };
inline std::optional<Dial> resolve_dial(const string& name, int v) {
    if (v <= 0) return std::nullopt;
    v = std::max(1, std::min(10, v)); const int t = v <= 3 ? 0 : v <= 7 ? 1 : 2; Dial d; d.value = v;
    if (name == "variance") {
        static const char* L[] = {"Centered / Minimal", "Balanced / Modern", "Bold / Asymmetric"}; d.label = L[t];
        if (t == 0) d.keywords = {"Minimalism", "Exaggerated Minimalism", "centered", "symmetric", "grid-based"};
        else if (t == 1) d.keywords = {"modern", "structured", "balanced"}; else d.keywords = {"Brutalism", "Bento Grids", "asymmetric", "experimental"};
    } else if (name == "motion") { static const char* L[] = {"Subtle", "Standard", "Complex"}; d.label = L[t]; d.tier = L[t]; }
    else {
        static const char* L[] = {"Spacious", "Standard", "Dense / Dashboard"}; d.label = L[t];
        static const char* S[3][7] = {{"4px", "8px", "24px", "32px", "48px", "64px", "96px"}, {"4px", "8px", "16px", "24px", "32px", "48px", "64px"}, {"2px", "4px", "8px", "12px", "16px", "24px", "32px"}};
        static const char* K[] = {"xs", "sm", "md", "lg", "xl", "2xl", "3xl"};
        for (int i = 0; i < 7; ++i) d.spacing.emplace_back(K[i], S[t][i]);
    }
    return d;
}
struct DesignSystem {
    string project, category, query; Reasoning reasoning; Row style, palette, typo, landing, motion; string mode, effects, anti;
    std::optional<Dial> variance, motion_dial, density;
    string color(const string& k, const string& dflt = "") const { const string v = palette.get(k); return v.empty() ? dflt : v; }
};
inline Row select_best(const vector<Row>& results, const vector<string>& prio) {
    if (results.empty()) return {};
    if (prio.empty()) return results[0];
    for (const auto& p : prio) if (const Row* r = resolve_style(p); r && r->get("Status", "active") != "deprecated") return *r;
    int best = 0; std::size_t bi = 0;
    for (std::size_t i = 0; i < results.size(); ++i) {
        const Row& r = results[i]; string all; for (const auto& [k, v] : r.kv) all += lower(k) + " " + lower(v) + " ";
        int sc = 0;
        for (const auto& kw : prio) {
            std::set<string> kt, nt, kwt;
            for (const auto& w : words(lower(kw))) kt.insert(w);
            for (const auto& w : words(lower(r.get("Style Category")))) nt.insert(w);
            for (const auto& w : words(lower(r.get("Keywords")))) kwt.insert(w);
            bool sub = !kt.empty(); for (const auto& t : kt) if (!nt.count(t)) sub = false;
            bool inter = false; for (const auto& t : kt) if (kwt.count(t)) inter = true;
            bool any = false; for (const auto& t : kt) if (all.find(t) != string::npos) any = true;
            sc += sub ? 10 : inter ? 3 : any ? 1 : 0;
        }
        if (sc > best) { best = sc; bi = i; }
    }
    return results[best > 0 ? bi : 0];
}
inline DesignSystem generate(const string& query, const string& project, int variance, int motion, int density) {
    DesignSystem ds; ds.query = query; ds.project = project.empty() ? pyrt::upper(query) : project;
    ds.variance = resolve_dial("variance", variance); ds.motion_dial = resolve_dial("motion", motion); ds.density = resolve_dial("density", density);
    const Result prod = search(query, "product", 1);
    ds.category = prod.results.empty() ? "General" : prod.results[0].get("Product Type", "General");
    ds.reasoning = apply_reasoning(ds.category, query);
    vector<string> prio = ds.reasoning.style_priority;
    if (ds.variance) { vector<string> p = ds.variance->keywords; p.insert(p.end(), prio.begin(), prio.end()); prio = p; }
    vector<string> cons; for (const auto& c : ds.reasoning.constraints) cons.push_back(replace_all(c, "-", " "));
    vector<string> parts; for (const string& s : {query, ds.category, join(cons, " ")}) if (!s.empty()) parts.push_back(s);
    const string resolved = join(parts, " ");
    const Result st = !prio.empty() ? search(resolved + " " + join(vector<string>(prio.begin(), prio.begin() + static_cast<long>(std::min<std::size_t>(2, prio.size()))), " "), "style", 3) : search(query, "style", 3);
    const Result co = search(ds.reasoning.color_mood + " " + resolved, "color", 5);
    string lq;
    { bool known = false; const string pl = lower(ds.reasoning.pattern);
      for (const auto& row : load("landing.csv").rows) for (const auto& id : identities(row, landing_id_fields())) if (lower(id) == pl) known = true;
      lq = known ? ds.reasoning.pattern : ds.reasoning.pattern + " " + resolved; if (strip(lq).empty()) lq = query; }
    const Result la = search(lq, "landing", 2);
    const Result ty = search(ds.reasoning.typography_mood + " " + resolved, "typography", 2);
    ds.style = select_best(st.results, prio);
    ds.mode = !ds.reasoning.mode.empty() ? ds.reasoning.mode : (query_wants_dark(query) || style_dark_primary(ds.style)) ? "dark" : "light";
    ds.palette = pick_palette(co.results, ds.mode, ds.category);
    if (!ty.results.empty()) ds.typo = ty.results[0];
    for (const auto& r : la.results) if (r.get("Pattern Name") == ds.reasoning.pattern) { ds.landing = r; break; }
    if (ds.landing.empty() && !la.results.empty()) ds.landing = la.results[0];
    if (ds.motion_dial) {
        const Result m = search(query + " " + ds.motion_dial->tier, "gsap", 5);
        for (const auto& r : m.results) if (r.get("Intensity Tier") == ds.motion_dial->tier) { ds.motion = r; break; }
        if (ds.motion.empty() && !m.results.empty()) ds.motion = m.results[0];
    }
    const string se = ds.style.get("Effects & Animation");
    ds.effects = !se.empty() ? se : ds.reasoning.key_effects;
    ds.anti = filter_anti(ds.reasoning.anti_patterns, ds.mode);
    return ds;
}
struct ColorEntry { const char* label; const char* col; const char* css; };
inline const vector<ColorEntry>& color_entries() {
    static const vector<ColorEntry> v = {
        {"Primary", "Primary", "--color-primary"}, {"On Primary", "On Primary", "--color-on-primary"}, {"Secondary", "Secondary", "--color-secondary"},
        {"On Secondary", "On Secondary", "--color-on-secondary"}, {"Accent/CTA", "Accent", "--color-accent"}, {"On Accent/CTA", "On Accent", "--color-on-accent"},
        {"Background", "Background", "--color-background"}, {"Foreground", "Foreground", "--color-foreground"}, {"Card", "Card", "--color-card"},
        {"Card Foreground", "Card Foreground", "--color-card-foreground"}, {"Muted", "Muted", "--color-muted"}, {"Muted Foreground", "Muted Foreground", "--color-muted-foreground"},
        {"Border", "Border", "--color-border"}, {"Destructive", "Destructive", "--color-destructive"}, {"On Destructive", "On Destructive", "--color-on-destructive"}, {"Ring", "Ring", "--color-ring"}};
    return v;
}
inline string color_or_default(const DesignSystem& ds, const string& col) {   // the generator's fallbacks for the four core roles
    if (col == "Primary") return ds.color(col, "#2563EB"); if (col == "Secondary") return ds.color(col, "#3B82F6");
    if (col == "Accent") return ds.color(col, "#F97316"); if (col == "Background") return ds.color(col, "#F8FAFC");
    if (col == "Foreground") return ds.color(col, "#1E293B"); return ds.color(col);
}
inline std::pair<string, string> mode_labels(const DesignSystem& ds) {
    const string light = ds.style.get("Light Mode \xE2\x9C\x93"); string dark = ds.style.get("Dark Mode \xE2\x9C\x93");
    const string a = lower(ds.anti); const bool avoids = a.find("dark mode") != string::npos || a.find("dark theme") != string::npos;
    if (!dark.empty() && dark != "not-recommended" && avoids) dark += ", but avoid for this product";
    return {light, dark};
}
inline string pattern_name(const DesignSystem& ds) { return ds.landing.get("Pattern Name", ds.reasoning.pattern.empty() ? "Hero + Features + CTA" : ds.reasoning.pattern); }
inline string heading_font(const DesignSystem& ds) { return ds.typo.get("Heading Font", "Inter"); }
inline string body_font(const DesignSystem& ds) { return ds.typo.get("Body Font", "Inter"); }
inline string typo_mood(const DesignSystem& ds) { return ds.typo.get("Mood/Style Keywords", ds.reasoning.typography_mood); }
inline void motion_md(vector<string>& L, const Row& m, bool master) {
    L.push_back("**" + m.get("Category") + "** (" + m.get("Intensity Tier") + ") \xE2\x80\x94 Trigger: " + m.get("Trigger") + " | Duration: " + m.get("Duration") + " | Easing: `" + m.get("Easing") + "`");
    if (master) L.push_back("");
    L.push_back("```js"); L.push_back(m.get("GSAP Snippet")); L.push_back("```");
    if (master) L.push_back("");
    if (!m.get("Framework Notes").empty()) { L.push_back(master ? "**Framework notes:** " + m.get("Framework Notes") : "*Framework notes: " + m.get("Framework Notes") + "*"); if (master) L.push_back(""); }
    if (!m.get("Do").empty()) L.push_back("- \xE2\x9C\x85 " + m.get("Do"));
    if (!m.get("Don't").empty()) L.push_back("- \xE2\x9D\x8C " + m.get("Don't"));
    if (master && !m.get("Performance Notes").empty()) L.push_back("- \xE2\x9A\xA1 " + m.get("Performance Notes"));
    L.push_back("");
}
inline string format_markdown(const DesignSystem& ds) {
    vector<string> L; L.push_back("## Design System: " + ds.project); L.push_back("");
    if (ds.variance || ds.motion_dial || ds.density) {
        L.push_back("### Design Dials");
        if (ds.variance) L.push_back("- **Variance:** " + std::to_string(ds.variance->value) + "/10 \xE2\x80\x94 " + ds.variance->label);
        if (ds.motion_dial) L.push_back("- **Motion:** " + std::to_string(ds.motion_dial->value) + "/10 \xE2\x80\x94 " + ds.motion_dial->label);
        if (ds.density) L.push_back("- **Density:** " + std::to_string(ds.density->value) + "/10 \xE2\x80\x94 " + ds.density->label);
        L.push_back("");
    }
    L.push_back("### Pattern"); L.push_back("- **Name:** " + pattern_name(ds));
    if (!ds.landing.get("Conversion Optimization").empty()) L.push_back("- **Conversion Focus:** " + ds.landing.get("Conversion Optimization"));
    L.push_back("- **CTA Placement:** " + ds.landing.get("Primary CTA Placement", "Above fold"));
    if (!ds.landing.get("Color Strategy").empty()) L.push_back("- **Color Strategy:** " + ds.landing.get("Color Strategy"));
    L.push_back("- **Sections:** " + ds.landing.get("Section Order", "Hero > Features > CTA")); L.push_back("");
    L.push_back("### Style"); L.push_back("- **Name:** " + ds.style.get("Style Category", "Minimalism"));
    const auto [light, dark] = mode_labels(ds);
    if (!light.empty() || !dark.empty()) L.push_back("- **Mode Support:** Light " + light + " | Dark " + dark);
    if (!ds.style.get("Keywords").empty()) L.push_back("- **Keywords:** " + ds.style.get("Keywords"));
    if (!ds.style.get("Best For").empty()) L.push_back("- **Best For:** " + ds.style.get("Best For"));
    if (!ds.style.get("Performance").empty() || !ds.style.get("Accessibility").empty()) L.push_back("- **Performance:** " + ds.style.get("Performance") + " | **Accessibility:** " + ds.style.get("Accessibility"));
    L.push_back("");
    L.push_back("### Colors"); L.push_back("| Role | Hex | CSS Variable |"); L.push_back("|------|-----|--------------|");
    for (const auto& e : color_entries()) { const string h = color_or_default(ds, e.col); if (!h.empty()) L.push_back(string("| ") + e.label + " | `" + h + "` | `" + e.css + "` |"); }
    if (!ds.palette.get("Notes").empty()) L.push_back("\n*Notes: " + ds.palette.get("Notes") + "*");
    L.push_back("");
    L.push_back("### Typography"); L.push_back("- **Heading:** " + heading_font(ds)); L.push_back("- **Body:** " + body_font(ds));
    if (!typo_mood(ds).empty()) L.push_back("- **Mood:** " + typo_mood(ds));
    if (!ds.typo.get("Best For").empty()) L.push_back("- **Best For:** " + ds.typo.get("Best For"));
    if (!ds.typo.get("Google Fonts URL").empty()) L.push_back("- **Google Fonts:** " + ds.typo.get("Google Fonts URL"));
    if (!ds.typo.get("CSS Import").empty()) { L.push_back("- **CSS Import:**"); L.push_back("```css"); L.push_back(ds.typo.get("CSS Import")); L.push_back("```"); }
    L.push_back("");
    if (!ds.effects.empty()) { L.push_back("### Key Effects"); L.push_back(ds.effects); L.push_back(""); }
    if (!ds.motion.empty()) { L.push_back("### Motion"); motion_md(L, ds.motion, false); }
    if (!ds.anti.empty()) { L.push_back("### Avoid (Anti-patterns)"); L.push_back("- " + replace_all(ds.anti, " + ", "\n- ")); L.push_back(""); }
    L.push_back("### Pre-Delivery Checklist");
    for (const char* c : {"No emojis as icons (use SVG: Heroicons/Lucide)", "cursor-pointer on all clickable elements", "Hover states with smooth transitions (150-300ms)",
                          "Light mode: text contrast 4.5:1 minimum", "Focus states visible for keyboard nav", "prefers-reduced-motion respected", "Responsive: 375px, 768px, 1024px, 1440px"})
        L.push_back(string("- [ ] ") + c);
    L.push_back("");
    return join(L, "\n");
}
inline string now_local() { const std::time_t t = std::time(nullptr); std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char b[32]; std::strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", &tmv); return b; }
inline string outline_text_color(const DesignSystem& ds) {
    const string p = color_or_default(ds, "Primary"), bg = color_or_default(ds, "Background");
    if (contrast(p, bg) >= 4.5) return p; const string fg = ds.color("Foreground"); return fg.empty() ? p : fg;
}
inline string format_master(const DesignSystem& ds) {
    vector<string> L;
    for (const char* s : {"# Design System Master File", "", "> **LOGIC:** When building a specific page, first check `design-system/pages/[page-name].md`.",
                          "> If that file exists, its rules **override** this Master file.", "> If not, strictly follow the rules below.", "", "---", ""}) L.push_back(s);
    L.push_back("**Project:** " + ds.project); L.push_back("**Generated:** " + now_local()); L.push_back("**Category:** " + ds.category);
    if (ds.variance || ds.motion_dial || ds.density) {
        vector<string> p;
        if (ds.variance) p.push_back("Variance " + std::to_string(ds.variance->value) + "/10 (" + ds.variance->label + ")");
        if (ds.motion_dial) p.push_back("Motion " + std::to_string(ds.motion_dial->value) + "/10 (" + ds.motion_dial->label + ")");
        if (ds.density) p.push_back("Density " + std::to_string(ds.density->value) + "/10 (" + ds.density->label + ")");
        L.push_back("**Design Dials:** " + join(p, " | "));
    }
    for (const char* s : {"", "---", "", "## Global Rules", "", "### Color Palette", "", "| Role | Hex | CSS Variable |", "|------|-----|--------------|"}) L.push_back(s);
    for (const auto& e : color_entries()) { const string h = color_or_default(ds, e.col); if (!h.empty()) L.push_back(string("| ") + e.label + " | `" + h + "` | `" + e.css + "` |"); }
    L.push_back("");
    if (!ds.palette.get("Notes").empty()) { L.push_back("**Color Notes:** " + ds.palette.get("Notes")); L.push_back(""); }
    L.push_back("### Typography"); L.push_back(""); L.push_back("- **Heading Font:** " + heading_font(ds)); L.push_back("- **Body Font:** " + body_font(ds));
    if (!typo_mood(ds).empty()) L.push_back("- **Mood:** " + typo_mood(ds));
    if (!ds.typo.get("Google Fonts URL").empty()) L.push_back("- **Google Fonts:** [" + ds.typo.get("Heading Font") + " + " + ds.typo.get("Body Font") + "](" + ds.typo.get("Google Fonts URL") + ")");
    L.push_back("");
    if (!ds.typo.get("CSS Import").empty()) { L.push_back("**CSS Import:**"); L.push_back("```css"); L.push_back(ds.typo.get("CSS Import")); L.push_back("```"); L.push_back(""); }
    const Dial mid = *resolve_dial("density", 5); const auto& scale = ds.density ? ds.density->spacing : mid.spacing;
    static const std::map<string, string> usage = {{"xs", "Tight gaps"}, {"sm", "Icon gaps, inline spacing"}, {"md", "Standard padding"}, {"lg", "Section padding"}, {"xl", "Large gaps"}, {"2xl", "Section margins"}, {"3xl", "Hero padding"}};
    L.push_back("### Spacing Variables"); L.push_back("");
    if (ds.density) { L.push_back("*Density: " + std::to_string(ds.density->value) + "/10 \xE2\x80\x94 " + ds.density->label + "*"); L.push_back(""); }
    L.push_back("| Token | Value | Usage |"); L.push_back("|-------|-------|-------|");
    for (const auto& [tok, px] : scale) {
        const double rem = std::atof(px.c_str()) / 16.0; char rb[32]; std::snprintf(rb, sizeof rb, "%g", rem);
        L.push_back("| `--space-" + tok + "` | `" + px + "` / `" + rb + "rem` | " + usage.at(tok) + " |");
    }
    for (const char* s : {"", "### Shadow Depths", "", "| Level | Value | Usage |", "|-------|-------|-------|", "| `--shadow-sm` | `0 1px 2px rgba(0,0,0,0.05)` | Subtle lift |",
                          "| `--shadow-md` | `0 4px 6px rgba(0,0,0,0.1)` | Cards, buttons |", "| `--shadow-lg` | `0 10px 15px rgba(0,0,0,0.1)` | Modals, dropdowns |",
                          "| `--shadow-xl` | `0 20px 25px rgba(0,0,0,0.15)` | Hero images, featured cards |", "", "---", "", "## Component Specs", "", "### Buttons", "", "```css", "/* Primary Button */", ".btn-primary {"}) L.push_back(s);
    const string cta = color_or_default(ds, "Accent"), on_cta = ds.color("On Accent").empty() ? "white" : ds.color("On Accent"), prim = color_or_default(ds, "Primary");
    L.push_back("  background: " + cta + ";"); L.push_back("  color: " + on_cta + ";");
    for (const char* s : {"  padding: 12px 24px;", "  border-radius: 8px;", "  font-weight: 600;", "  transition: all 200ms ease;", "  cursor: pointer;", "}", "", ".btn-primary:hover {", "  opacity: 0.9;", "  transform: translateY(-1px);", "}", "", "/* Secondary Button */", ".btn-secondary {", "  background: transparent;"}) L.push_back(s);
    L.push_back("  color: " + outline_text_color(ds) + ";"); L.push_back("  border: 2px solid " + prim + ";");
    for (const char* s : {"  padding: 12px 24px;", "  border-radius: 8px;", "  font-weight: 600;", "  transition: all 200ms ease;", "  cursor: pointer;", "}", "```", "", "### Cards", "", "```css", ".card {"}) L.push_back(s);
    L.push_back("  background: " + ds.color("Background", "#FFFFFF") + ";");
    for (const char* s : {"  border-radius: 12px;", "  padding: 24px;", "  box-shadow: var(--shadow-md);", "  transition: all 200ms ease;", "  cursor: pointer;", "}", "", ".card:hover {", "  box-shadow: var(--shadow-lg);", "  transform: translateY(-2px);", "}", "```", "",
                          "### Inputs", "", "```css", ".input {", "  padding: 12px 16px;", "  border: 1px solid #E2E8F0;", "  border-radius: 8px;", "  font-size: 16px;", "  transition: border-color 200ms ease;", "}", "", ".input:focus {"}) L.push_back(s);
    L.push_back("  border-color: " + prim + ";"); L.push_back("  outline: none;"); L.push_back("  box-shadow: 0 0 0 3px " + prim + "20;");
    for (const char* s : {"}", "```", "", "### Modals", "", "```css", ".modal-overlay {", "  background: rgba(0, 0, 0, 0.5);", "  backdrop-filter: blur(4px);", "}", "", ".modal {", "  background: white;", "  border-radius: 16px;", "  padding: 32px;",
                          "  box-shadow: var(--shadow-xl);", "  max-width: 500px;", "  width: 90%;", "}", "```", "", "---", "", "## Style Guidelines", ""}) L.push_back(s);
    L.push_back("**Style:** " + ds.style.get("Style Category", "Minimalism")); L.push_back("");
    if (!ds.style.get("Keywords").empty()) { L.push_back("**Keywords:** " + ds.style.get("Keywords")); L.push_back(""); }
    if (!ds.style.get("Best For").empty()) { L.push_back("**Best For:** " + ds.style.get("Best For")); L.push_back(""); }
    if (!ds.effects.empty()) { L.push_back("**Key Effects:** " + ds.effects); L.push_back(""); }
    L.push_back("### Page Pattern"); L.push_back(""); L.push_back("**Pattern Name:** " + pattern_name(ds)); L.push_back("");
    if (!ds.landing.get("Conversion Optimization").empty()) L.push_back("- **Conversion Strategy:** " + ds.landing.get("Conversion Optimization"));
    L.push_back("- **CTA Placement:** " + ds.landing.get("Primary CTA Placement", "Above fold"));
    L.push_back("- **Section Order:** " + ds.landing.get("Section Order", "Hero > Features > CTA")); L.push_back("");
    if (!ds.motion.empty()) { L.push_back("---"); L.push_back(""); L.push_back("## Motion"); L.push_back(""); motion_md(L, ds.motion, true); }
    L.push_back("---"); L.push_back(""); L.push_back("## Anti-Patterns (Do NOT Use)"); L.push_back("");
    for (const auto& a : split_plus(ds.anti)) L.push_back("- \xE2\x9D\x8C " + a);
    for (const char* s : {"", "### Additional Forbidden Patterns", "", "- \xE2\x9D\x8C **Emojis as icons** \xE2\x80\x94 Use SVG icons (Heroicons, Lucide, Simple Icons)",
                          "- \xE2\x9D\x8C **Missing cursor:pointer** \xE2\x80\x94 All clickable elements must have cursor:pointer", "- \xE2\x9D\x8C **Layout-shifting hovers** \xE2\x80\x94 Avoid scale transforms that shift layout",
                          "- \xE2\x9D\x8C **Low contrast text** \xE2\x80\x94 Maintain 4.5:1 minimum contrast ratio", "- \xE2\x9D\x8C **Instant state changes** \xE2\x80\x94 Always use transitions (150-300ms)",
                          "- \xE2\x9D\x8C **Invisible focus states** \xE2\x80\x94 Focus states must be visible for a11y", "", "---", "", "## Pre-Delivery Checklist", "", "Before delivering any UI code, verify:", "",
                          "- [ ] No emojis used as icons (use SVG instead)", "- [ ] All icons from consistent icon set (Heroicons/Lucide)", "- [ ] `cursor-pointer` on all clickable elements",
                          "- [ ] Hover states with smooth transitions (150-300ms)", "- [ ] Light mode: text contrast 4.5:1 minimum", "- [ ] Focus states visible for keyboard navigation",
                          "- [ ] `prefers-reduced-motion` respected", "- [ ] Responsive: 375px, 768px, 1024px, 1440px", "- [ ] No content hidden behind fixed navbars", "- [ ] No horizontal scroll on mobile", ""}) L.push_back(s);
    return join(L, "\n");
}
inline string title_case(const string& s) {   // str.title() on ASCII letters
    string o; bool prev_alpha = false;
    for (char c : s) { const bool a = std::isalpha(static_cast<unsigned char>(c)) != 0; o += a ? static_cast<char>(prev_alpha ? std::tolower(static_cast<unsigned char>(c)) : std::toupper(static_cast<unsigned char>(c))) : c; prev_alpha = a; }
    return o;
}
inline string page_type_of(const string& ctx, const vector<Row>& style_rows) {
    static const vector<std::pair<vector<string>, string>> pats = {
        {{"dashboard", "admin", "analytics", "data", "metrics", "stats", "monitor", "overview"}, "Dashboard / Data View"},
        {{"checkout", "payment", "cart", "purchase", "order", "billing"}, "Checkout / Payment"},
        {{"settings", "profile", "account", "preferences", "config"}, "Settings / Profile"},
        {{"landing", "marketing", "homepage", "hero", "home", "promo"}, "Landing / Marketing"},
        {{"login", "signin", "signup", "register", "auth", "password"}, "Authentication"},
        {{"pricing", "plans", "subscription", "tiers", "packages"}, "Pricing / Plans"},
        {{"blog", "article", "post", "news", "content", "story"}, "Blog / Article"},
        {{"product", "item", "detail", "pdp", "shop", "store"}, "Product Detail"},
        {{"search", "results", "browse", "filter", "catalog", "list"}, "Search Results"},
        {{"empty", "404", "error", "not found", "zero"}, "Empty State"}};
    const string c = lower(ctx);
    for (const auto& [kws, name] : pats) for (const auto& k : kws) if (c.find(k) != string::npos) return name;
    if (!style_rows.empty()) { const string bf = lower(style_rows[0].get("Best For")); if (bf.find("dashboard") != string::npos || bf.find("data") != string::npos) return "Dashboard / Data View"; if (bf.find("landing") != string::npos || bf.find("marketing") != string::npos) return "Landing / Marketing"; }
    return "General";
}
inline string format_page(const DesignSystem& ds, const string& page, const string& page_query) {
    const string ctx = lower(page) + " " + lower(page_query);
    const Result st = search(ctx, "style", 1), ux = search(ctx, "ux", 3), la = search(ctx, "landing", 1);
    vector<std::pair<string, string>> layout, spacing, colors; vector<string> comps, recs;
    if (!st.results.empty()) {
        const Row& s = st.results[0]; const string kw = lower(s.get("Keywords")); auto any = [&](std::initializer_list<const char*> l) { for (const char* x : l) if (kw.find(x) != string::npos) return true; return false; };
        if (any({"data", "dense", "dashboard", "grid"})) { layout = {{"Max Width", "1400px or full-width"}, {"Grid", "12-column grid for data flexibility"}}; spacing = {{"Content Density", "High \xE2\x80\x94 optimize for information display"}}; }
        else if (any({"minimal", "simple", "clean", "single"})) { layout = {{"Max Width", "800px (narrow, focused)"}, {"Layout", "Single column, centered"}}; spacing = {{"Content Density", "Low \xE2\x80\x94 focus on clarity"}}; }
        else layout = {{"Max Width", "1200px (standard)"}, {"Layout", "Full-width sections, centered content"}};
        if (!s.get("Effects & Animation").empty()) recs.push_back("Effects: " + s.get("Effects & Animation"));
    }
    for (const auto& u : ux.results) { if (!u.get("Do").empty()) recs.push_back(u.get("Category") + ": " + u.get("Do")); if (!u.get("Don't").empty()) comps.push_back("Avoid: " + u.get("Don't")); }
    if (!la.results.empty()) {
        const Row& l = la.results[0];
        if (!l.get("Section Order").empty()) layout.emplace_back("Sections", l.get("Section Order"));
        if (!l.get("Primary CTA Placement").empty()) recs.push_back("CTA Placement: " + l.get("Primary CTA Placement"));
        if (!l.get("Color Strategy").empty()) colors.emplace_back("Strategy", l.get("Color Strategy"));
    }
    if (layout.empty()) layout = {{"Max Width", "1200px"}, {"Layout", "Responsive grid"}};
    if (recs.empty()) recs = {"Refer to MASTER.md for all design rules", "Add specific overrides as needed for this page"};
    vector<string> L;
    L.push_back("# " + title_case(replace_all(replace_all(page, "-", " "), "_", " ")) + " Page Overrides"); L.push_back("");
    L.push_back("> **PROJECT:** " + ds.project); L.push_back("> **Generated:** " + now_local()); L.push_back("> **Page Type:** " + page_type_of(ctx, st.results)); L.push_back("");
    L.push_back("> \xE2\x9A\xA0\xEF\xB8\x8F **IMPORTANT:** Rules in this file **override** the Master file (`design-system/MASTER.md`).");
    L.push_back("> Only deviations from the Master are documented here. For all other rules, refer to the Master.");
    for (const char* s : {"", "---", "", "## Page-Specific Rules", ""}) L.push_back(s);
    auto kv = [&](const char* title, const vector<std::pair<string, string>>& m, const char* none) { L.push_back(title); L.push_back(""); if (m.empty()) L.push_back(none); else for (const auto& [k, v] : m) L.push_back("- **" + k + ":** " + v); L.push_back(""); };
    kv("### Layout Overrides", layout, "- No overrides \xE2\x80\x94 use Master layout");
    kv("### Spacing Overrides", spacing, "- No overrides \xE2\x80\x94 use Master spacing");
    kv("### Typography Overrides", {}, "- No overrides \xE2\x80\x94 use Master typography");
    kv("### Color Overrides", colors, "- No overrides \xE2\x80\x94 use Master colors");
    L.push_back("### Component Overrides"); L.push_back(""); if (comps.empty()) L.push_back("- No overrides \xE2\x80\x94 use Master component specs"); else for (const auto& c : comps) L.push_back("- " + c); L.push_back("");
    for (const char* s : {"---", "", "## Page-Specific Components", "", "- No unique components for this page", "", "---", "", "## Recommendations", ""}) L.push_back(s);
    for (const auto& r : recs) L.push_back("- " + r);
    L.push_back("");
    return join(L, "\n");
}
inline string safe_slug(const string& name, const string& fallback) {   // only [a-z0-9_-]: a project or page name can never leave its folder
    string o; bool dash = false;
    for (char c : lower(name)) { const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; if (ok) { o += c; dash = false; } else if (!dash) { o += '-'; dash = true; } }
    while (!o.empty() && o.front() == '-') o.erase(0, 1);
    while (!o.empty() && o.back() == '-') o.pop_back();
    return o.empty() ? fallback : o;
}
struct Persist { string status, dir, master, message; vector<string> created; };
inline bool write_new(const string& path, const string& content, bool force) {   // false = it exists and force is off
    if (!force && pyrt::exists(path)) return false;
    const string tmp = path + ".tmp"; pyrt::write_text(tmp, content); pyrt::os_replace(tmp, path); return true;
}
inline Persist persist(const DesignSystem& ds, const string& page, const string& out_dir, const string& page_query, bool force) {
    Persist p; const string base = out_dir.empty() ? std::filesystem::current_path().string() : out_dir;
    p.dir = pyrt::path_join(pyrt::path_join(base, "design-system"), safe_slug(ds.project.empty() ? "default" : ds.project, "default"));
    const string pages = pyrt::path_join(p.dir, "pages"); pyrt::makedirs(pages);
    p.master = pyrt::path_join(p.dir, "MASTER.md");
    if (write_new(p.master, format_master(ds), force)) p.created.push_back(p.master);
    else if (page.empty()) { p.status = "skipped_exists"; p.message = p.master + " already exists and was not modified. Read it first to check for prior design decisions, then re-run with --force to overwrite."; return p; }
    if (!page.empty()) {
        const string pf = pyrt::path_join(pages, safe_slug(page, "page") + ".md");
        if (write_new(pf, format_page(ds, page, page_query), force)) p.created.push_back(pf);
        else if (p.created.empty()) { p.status = "skipped_exists"; p.message = pf + " already exists and was not modified."; return p; }
    }
    p.status = "success"; return p;
}
inline string json_ds(const DesignSystem& ds, const std::optional<Persist>& p) {
    auto kvs = [](const vector<std::pair<string, string>>& v, const string& ind) { string o = "{"; for (std::size_t i = 0; i < v.size(); ++i) o += string(i ? "," : "") + "\n" + ind + "  " + jq(v[i].first) + ": " + jq(v[i].second); return o + (v.empty() ? "}" : "\n" + ind + "}"); };
    vector<std::pair<string, string>> col; for (const auto& e : color_entries()) col.emplace_back(lower(replace_all(e.css + 8, "-", "_")), color_or_default(ds, e.col));
    col.emplace_back("notes", ds.palette.get("Notes"));
    const auto [light, dark] = mode_labels(ds);
    string o = "{\n  \"design_system\": {\n";
    o += "    \"project_name\": " + jq(ds.project) + ",\n    \"category\": " + jq(ds.category) + ",\n    \"color_mode\": " + jq(ds.mode) + ",\n";
    o += "    \"pattern\": " + kvs({{"name", pattern_name(ds)}, {"sections", ds.landing.get("Section Order", "Hero > Features > CTA")}, {"cta_placement", ds.landing.get("Primary CTA Placement", "Above fold")}, {"color_strategy", ds.landing.get("Color Strategy")}, {"conversion", ds.landing.get("Conversion Optimization")}}, "    ") + ",\n";
    o += "    \"style\": " + kvs({{"id", ds.style.get("Style ID", "minimalism-and-swiss-style")}, {"name", ds.style.get("Style Category", "Minimalism")}, {"type", ds.style.get("Type", "General")}, {"effects", ds.style.get("Effects & Animation")}, {"keywords", ds.style.get("Keywords")}, {"best_for", ds.style.get("Best For")}, {"performance", ds.style.get("Performance")}, {"accessibility", ds.style.get("Accessibility")}, {"light_mode", light}, {"dark_mode", dark}}, "    ") + ",\n";
    o += "    \"colors\": " + kvs(col, "    ") + ",\n";
    o += "    \"typography\": " + kvs({{"heading", heading_font(ds)}, {"body", body_font(ds)}, {"mood", typo_mood(ds)}, {"best_for", ds.typo.get("Best For")}, {"google_fonts_url", ds.typo.get("Google Fonts URL")}, {"css_import", ds.typo.get("CSS Import")}}, "    ") + ",\n";
    o += "    \"key_effects\": " + jq(ds.effects) + ",\n    \"anti_patterns\": " + jq(ds.anti) + ",\n    \"constraints\": " + json_list(ds.reasoning.constraints) + ",\n";
    o += "    \"reasoning_default\": " + string(ds.reasoning.is_default ? "true" : "false") + ",\n    \"severity\": " + jq(ds.reasoning.severity) + ",\n";
    o += "    \"motion_snippet\": " + json_row(ds.motion, "    ") + ",\n";
    o += "    \"spacing_scale\": " + (ds.density ? kvs(ds.density->spacing, "    ") : string("null")) + "\n  },\n  \"persistence\": ";
    if (!p) o += "null";
    else { o += "{\"status\": " + jq(p->status) + ", \"design_system_dir\": " + jq(p->dir) + ", \"master_file\": " + jq(p->master) + ", \"created_files\": " + json_list(p->created); if (!p->message.empty()) o += ", \"message\": " + jq(p->message); o += "}"; }
    return o + "\n}";
}

// ---------------------------------------------------------------- command line
inline const char* usage() {
    return "usage: ui-ux-pro-max/search [-h] [--domain DOMAIN] [--rtl [LEVEL]] [--stack STACK] [--max-results 1-20] [--json] [--full]\n"
           "                            [--design-system] [--project-name NAME] [--format {ascii,markdown}] [--persist] [--page PAGE]\n"
           "                            [--output-dir DIR] [--force] [--variance 1-10] [--motion 1-10] [--density 1-10] query\n";
}
inline int fail(const string& msg) { std::fputs(usage(), stderr); std::fprintf(stderr, "ui-ux-pro-max/search: error: %s\n", msg.c_str()); return 2; }
inline int cli(const std::vector<std::string>& argv) {
    string query, domain, rtl, stack, project, format = "ascii", page, out_dir; bool have_q = false, json = false, full = false, ds = false, do_persist = false, force = false, rtl_set = false;
    int n = 3, variance = 0, motion = 0, density = 0;
    auto int_arg = [&](std::size_t& i, const string& name, int lo, int hi, int& dst) -> string {
        if (i + 1 >= argv.size()) return "argument " + name + ": expected one argument";
        const auto v = pyrt::py_int(argv[++i]); if (!v || *v < lo || *v > hi) return "argument " + name + ": invalid choice: '" + argv[i] + "' (choose from " + std::to_string(lo) + "-" + std::to_string(hi) + ")";
        dst = static_cast<int>(*v); return "";
    };
    for (std::size_t i = 0; i < argv.size(); ++i) {
        const string& a = argv[i]; string err;
        auto val = [&](string& dst) { if (i + 1 >= argv.size()) { err = "argument " + a + ": expected one argument"; return; } dst = argv[++i]; };
        if (a == "-h" || a == "--help") { std::fputs(usage(), stdout); return 0; }
        else if (a == "--domain" || a == "-d") { val(domain); if (err.empty() && !domain_of(domain) && domain != "google-fonts") err = "argument --domain/-d: invalid choice: '" + domain + "'"; }
        else if (a == "--rtl") { rtl_set = true; rtl = "all"; if (i + 1 < argv.size() && (argv[i + 1] == "all" || argv[i + 1] == "full" || argv[i + 1] == "partial" || argv[i + 1] == "caveats")) rtl = argv[++i]; }
        else if (a == "--stack" || a == "-s") { val(stack); if (err.empty() && std::find(stacks().begin(), stacks().end(), stack) == stacks().end()) err = "argument --stack/-s: invalid choice: '" + stack + "'"; }
        else if (a == "--max-results" || a == "-n") err = int_arg(i, "--max-results/-n", 1, 20, n);
        else if (a == "--json") json = true;
        else if (a == "--full") full = true;
        else if (a == "--design-system" || a == "-ds") ds = true;
        else if (a == "--project-name" || a == "-p") val(project);
        else if (a == "--format" || a == "-f") { val(format); if (err.empty() && format != "ascii" && format != "markdown") err = "argument --format/-f: invalid choice: '" + format + "'"; }
        else if (a == "--persist") do_persist = true;
        else if (a == "--page") val(page);
        else if (a == "--output-dir" || a == "-o") val(out_dir);
        else if (a == "--force") force = true;
        else if (a == "--variance") err = int_arg(i, "--variance", 1, 10, variance);
        else if (a == "--motion") err = int_arg(i, "--motion", 1, 10, motion);
        else if (a == "--density") err = int_arg(i, "--density", 1, 10, density);
        else if (a.size() > 1 && a[0] == '-' && !have_q) err = "unrecognized arguments: " + a;
        else if (!have_q) { query = a; have_q = true; }
        else err = "unrecognized arguments: " + a;
        if (!err.empty()) return fail(err);
    }
    if (!have_q) return fail("the following arguments are required: query");
    if (ds) {
        if (rtl_set) return fail("--rtl is not supported with --design-system; run a separate --domain style or --domain product query");
        if (!stack.empty()) std::fprintf(stderr, "note: --stack %s is ignored in --design-system mode; run a separate --stack query for stack-specific guidelines\n", stack.c_str());
        const DesignSystem d = generate(query, project, variance, motion, density);
        std::optional<Persist> p; if (do_persist) p = persist(d, page, out_dir, query, force);
        if (json) { std::printf("%s\n", json_ds(d, p).c_str()); return 0; }
        std::printf("%s\n", format_markdown(d).c_str());
        if (p) {
            std::printf("\n%s\n", string(60, '=').c_str());
            if (p->status == "skipped_exists") std::printf("Note: %s\n", p->message.c_str());
            else {
                std::printf("Design system saved to %s/\n", p->dir.c_str());
                for (const auto& f : p->created) std::printf("   %s\n", f.c_str());
                std::printf("\nUsage: When building a page, check %s/pages/[page].md first.\n   If it exists, its rules override MASTER.md. Otherwise, use MASTER.md.\n", p->dir.c_str());
            }
            std::printf("%s\n", string(60, '=').c_str());
        }
        return 0;
    }
    if (!stack.empty()) {
        if (rtl_set) return fail("--rtl cannot be combined with --stack; use --domain style or --domain product");
        const Result r = search_stack(query, stack, n);
        std::printf("%s\n", (json ? json_result(r) : format_result(r, full)).c_str()); return 0;
    }
    if (rtl_set && domain != "style" && domain != "product") return fail("--rtl is only supported with --domain style or --domain product; got " + (domain.empty() ? string("no explicit domain") : domain));
    const Result r = search(query, domain, n, rtl_set ? rtl : "");
    std::printf("%s\n", (json ? json_result(r) : format_result(r, full)).c_str());
    return 0;
}
}  // namespace uipro
}  // namespace packtools
PACKTOOL("ui-ux-pro-max/search", packtools::uipro::cli);
