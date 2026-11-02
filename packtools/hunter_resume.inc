// hunter-resume/ats: the Astro port of hunter-skill's skills/hunter-resume/ats.py (Jake Schincariol, MIT). Same
// subcommands (score, compare, trace, terms), score formula, truth check, text, JSON and exit codes (AtsError:
// "ats: ..." on stderr, exit 2; compare and trace exit 1 when the rewrite invents something).
//
// Python's re is not used here: every pattern of ats.py is a small hand-written matcher over code points with the
// semantics of CPython's sre for str patterns: \s is str.isspace(), \d is a decimal digit (Nd), \w is str.isalnum()
// or "_", and re.IGNORECASE compares the simple lowercase of both sides plus CPython's extra case equivalences
// (_casefix: i/dotless i, s/long s, the Greek symbol forms, the Cyrillic small letter variants...).
// One deliberate difference: a term name in terms.json (or --terms) that is not text is refused when the file is
// loaded (TypeError, exit 1), where the Python would carry it until it printed or sorted it.
namespace packtools::hunter_resume {
using namespace pyrt;

struct AtsError : std::runtime_error { using std::runtime_error::runtime_error; };
using U = std::u32string;

// ---------------------------------------------------------------- code points
inline U to_u(const std::string& s) { U o; for (uint32_t c : ucase_detail::u32(s)) o.push_back(static_cast<char32_t>(c)); return o; }
inline std::string to_s(const U& s) { std::string o; for (char32_t c : s) o += utf8(static_cast<uint32_t>(c)); return o; }
inline U uslice(const U& s, std::size_t a, std::size_t b) { if (a >= s.size() || b <= a) return U(); return s.substr(a, std::min(b, s.size()) - a); }
inline bool sp(char32_t c) { return py_space(static_cast<uint32_t>(c)); }
inline const char* cat(char32_t c) { return uni::category(static_cast<uint32_t>(c)); }
inline bool is_dec(char32_t c) { const char* k = cat(c); return k[0] == 'N' && k[1] == 'd'; }   // \d, str.isdecimal()
inline bool is_alpha(char32_t c) { return cat(c)[0] == 'L'; }                                  // str.isalpha()
inline bool is_alnum(char32_t c) { const char k = cat(c)[0]; return k == 'L' || k == 'N'; }     // str.isalnum()
inline bool is_word(char32_t c) { return c == U'_' || is_alnum(c); }                          // \w
inline bool ascii_alpha(char32_t c) { return (c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z'); }
inline bool is_up(char32_t c) {   // the Uppercase property: Lu plus Other_Uppercase
    const char* k = cat(c);
    if (k[0] == 'L' && k[1] == 'u') return true;
    return (c >= 0x2160 && c <= 0x216F) || (c >= 0x24B6 && c <= 0x24CF) || (c >= 0x1F130 && c <= 0x1F149) || (c >= 0x1F150 && c <= 0x1F169) || (c >= 0x1F170 && c <= 0x1F189);
}
inline bool str_isupper(const U& s) {   // str.isupper()
    bool cased = false;
    for (char32_t c : s) {
        const char* k = cat(c);
        const bool title = k[0] == 'L' && k[1] == 't';
        if (title || (is_cased(static_cast<uint32_t>(c)) && !is_up(c))) return false;
        if (is_up(c)) cased = true;
    }
    return cased;
}
inline int dec_value(char32_t c) {   // Nd digits come in runs of ten starting at zero
    char32_t k = c; while (k > 0 && is_dec(k - 1)) k--;
    return static_cast<int>((c - k) % 10);
}
inline long long py_int_u(const U& s) {   // int(str), Unicode digits included
    std::string b; for (std::size_t k = 0; k < s.size(); ++k) b += is_dec(s[k]) ? std::string(1, static_cast<char>('0' + dec_value(s[k]))) : utf8(static_cast<uint32_t>(s[k]));
    const auto v = py_int(b);
    if (!v) throw PyErr("ValueError", "invalid literal for int() with base 10: " + str_repr(to_s(s)));
    return *v;
}
// re.IGNORECASE: two code points match when their simple lowercase forms are equal or CPython lists them as equivalent
inline char32_t slower(char32_t c) { const auto* m = ucase_detail::simple(static_cast<uint32_t>(c)); return m ? static_cast<char32_t>(m->lo) : c; }
inline char32_t ckey(char32_t c) {
    const char32_t l = slower(c);
    switch (l) {
        case 0x131: return 0x69;   case 0x17F: return 0x73;   case 0x3BC: return 0xB5;
        case 0x3B9: case 0x1FBE: return 0x345;  case 0x1FD3: return 0x390;  case 0x1FE3: return 0x3B0;
        case 0x3D0: return 0x3B2;  case 0x3F5: return 0x3B5;  case 0x3D1: return 0x3B8;  case 0x3F0: return 0x3BA;
        case 0x3D6: return 0x3C0;  case 0x3F1: return 0x3C1;  case 0x3C3: return 0x3C2;  case 0x3D5: return 0x3C6;
        case 0x1C80: return 0x432; case 0x1C81: return 0x434; case 0x1C82: return 0x43E; case 0x1C83: return 0x441;
        case 0x1C84: case 0x1C85: return 0x442; case 0x1C86: return 0x44A; case 0x1C87: return 0x463; case 0xA64B: return 0x1C88;
        case 0x1E9B: return 0x1E61; case 0xFB06: return 0xFB05;
        default: return l;
    }
}
inline bool ci_eq(char32_t a, char32_t b) { return a == b || ckey(a) == ckey(b); }
// does pattern word w (ASCII, matched ignoring case) occur in t at position i?
inline bool ci_at(const U& t, std::size_t i, const char* w) {
    std::size_t k = 0;
    for (; w[k]; ++k) if (i + k >= t.size() || !ci_eq(t[i + k], static_cast<char32_t>(static_cast<unsigned char>(w[k])))) return false;
    return true;
}
inline std::size_t wlen(const char* w) { return std::strlen(w); }
inline bool boundary(const U& t, std::size_t i) {   // \b
    const bool a = i > 0 && is_word(t[i - 1]);
    const bool b = i < t.size() && is_word(t[i]);
    return a != b;
}

// ---------------------------------------------------------------- text helpers
inline U norm_text(const U& s) {
    U o = s;
    for (char32_t& c : o) { if (c == 0x2019 || c == 0x2018) c = U'\''; else if (c == 0xA0) c = U' '; }
    return o;
}
inline U ustrip(const U& s) { std::size_t a = 0, b = s.size(); while (a < b && sp(s[a])) a++; while (b > a && sp(s[b - 1])) b--; return s.substr(a, b - a); }
inline U ustrip_c(const U& s, const U& chars, bool left = true, bool right = true) {
    std::size_t a = 0, b = s.size();
    if (left) while (a < b && chars.find(s[a]) != U::npos) a++;
    if (right) while (b > a && chars.find(s[b - 1]) != U::npos) b--;
    return s.substr(a, b - a);
}
inline std::vector<U> usplit_ws(const U& s) {
    std::vector<U> o; std::size_t i = 0;
    while (i < s.size()) { while (i < s.size() && sp(s[i])) i++; std::size_t j = i; while (j < s.size() && !sp(s[j])) j++; if (j > i) o.push_back(s.substr(i, j - i)); i = j; }
    return o;
}
inline U ulower(const U& s) { return to_u(lower(to_s(s))); }
inline std::vector<U> ulines(const U& s) { std::vector<U> o; for (const auto& l : splitlines(to_s(s))) o.push_back(to_u(l)); return o; }
inline bool starts(const U& s, const U& p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }
inline bool ends(const U& s, const U& p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }
inline U uascii_lower(const U& s) { U o = s; for (char32_t& c : o) if (c >= U'A' && c <= U'Z') c = c - U'A' + U'a'; return o; }
// re.sub(r"^#+\s*", "", s)
inline U strip_hashes(const U& s) {
    std::size_t i = 0; while (i < s.size() && s[i] == U'#') i++;
    if (i == 0) return s;
    while (i < s.size() && sp(s[i])) i++;
    return s.substr(i);
}

// ---------------------------------------------------------------- one term form (form_regex)
// (?<![A-Za-z0-9_+#]) body (?![A-Za-z0-9_+#]); the body is literals, "-" as [-\s]?, " " as \s+, then a suffix group
struct Form {
    std::string form; bool cs = false;
    std::vector<std::pair<char, char32_t>> el;   // 'L' literal, 'H' [-\s]?, 'W' \s+
    int suffix = 0;                              // 0 none, 1 (?:s|es)?, 2 (?:e|s|es|ed|ing|[a-z]ed|[a-z]ing)?
};
inline Form form_regex(const std::string& form, bool case_sensitive) {
    Form F; F.form = form; F.cs = case_sensitive;
    const U f = norm_text(to_u(form));
    std::vector<std::pair<char, char32_t>> parts;
    for (char32_t ch : f) parts.emplace_back(ch == U'-' ? 'H' : ch == U' ' ? 'W' : 'L', ch);
    U g = f; if (!g.empty() && g.back() == U'\n') g.pop_back();   // $ also matches before a final newline
    bool word = g.size() >= 4;
    for (char32_t c : g) if (!(c >= U'a' && c <= U'z')) word = false;
    if (word) {
        // one lowercase word: allow its inflections (debug, debugs, debugged, debugging)
        const bool cut = ends(f, U"e") || (ends(f, U"s") && !ends(f, U"ss"));
        const U stem = cut ? f.substr(0, f.size() - 1) : f;
        for (char32_t c : stem) F.el.emplace_back('L', c);
        F.suffix = 2;
    } else if (f.size() > 2 && f.back() == U's' && is_alpha(f[f.size() - 2])) {
        F.el.assign(parts.begin(), parts.end() - 1);
        F.suffix = 1;   // plural or singular
    } else {
        F.el = parts;
        F.suffix = (!f.empty() && is_alpha(f.back())) ? 1 : 0;
    }
    return F;
}
inline bool edge_char(const Form& F, char32_t c) {   // [A-Za-z0-9_+#]
    const char32_t k = F.cs ? c : ckey(c);
    if (F.cs) return ascii_alpha(k) || (k >= U'0' && k <= U'9') || k == U'_' || k == U'+' || k == U'#';
    return (k >= U'a' && k <= U'z') || (k >= U'0' && k <= U'9') || k == U'_' || k == U'+' || k == U'#';
}
inline bool lit_eq(const Form& F, char32_t t, char32_t p) { return F.cs ? t == p : ci_eq(t, p); }
inline bool az(const Form& F, char32_t c) { const char32_t k = F.cs ? c : ckey(c); return k >= U'a' && k <= U'z'; }
// the end of a match that starts at s, or -1; with must_adv an empty match does not count (finditer after an empty match)
inline long match_at(const Form& F, const U& t, std::size_t s, bool must_adv) {
    const std::size_t n = t.size();
    if (s > 0 && edge_char(F, t[s - 1])) return -1;
    const auto fin = [&](std::size_t e) { return (e >= n || !edge_char(F, t[e])) && (!must_adv || e > s); };
    const auto lits = [&](std::size_t pos, const char* w) -> long {
        for (std::size_t k = 0; w[k]; ++k) if (pos + k >= n || !lit_eq(F, t[pos + k], static_cast<char32_t>(w[k]))) return -1;
        return static_cast<long>(pos + std::strlen(w));
    };
    const auto suffix_end = [&](std::size_t pos) -> long {
        if (F.suffix == 1) {
            for (const char* w : {"s", "es"}) { const long e = lits(pos, w); if (e >= 0 && fin(static_cast<std::size_t>(e))) return e; }
        } else if (F.suffix == 2) {
            for (const char* w : {"e", "s", "es", "ed", "ing"}) { const long e = lits(pos, w); if (e >= 0 && fin(static_cast<std::size_t>(e))) return e; }
            if (pos < n && az(F, t[pos]))
                for (const char* w : {"ed", "ing"}) { const long e = lits(pos + 1, w); if (e >= 0 && fin(static_cast<std::size_t>(e))) return e; }
        }
        return fin(pos) ? static_cast<long>(pos) : -1;
    };
    const std::function<long(std::size_t, std::size_t)> rec = [&](std::size_t i, std::size_t pos) -> long {
        if (i == F.el.size()) return suffix_end(pos);
        const auto [kind, c] = F.el[i];
        if (kind == 'L') return pos < n && lit_eq(F, t[pos], c) ? rec(i + 1, pos + 1) : -1;
        if (kind == 'H') {
            if (pos < n && (t[pos] == U'-' || sp(t[pos]))) { const long r = rec(i + 1, pos + 1); if (r >= 0) return r; }
            return rec(i + 1, pos);
        }
        std::size_t k = pos; while (k < n && sp(t[k])) k++;
        for (std::size_t e = k; e > pos; --e) { const long r = rec(i + 1, e); if (r >= 0) return r; }
        return -1;
    };
    return rec(0, s);
}
inline bool search(const Form& F, const U& t, std::size_t start, bool must_adv, std::size_t& ms, std::size_t& me) {
    for (std::size_t s = start; s <= t.size(); ++s) {
        const long e = match_at(F, t, s, must_adv && s == start);
        if (e >= 0) { ms = s; me = static_cast<std::size_t>(e); return true; }
    }
    return false;
}
inline bool rx_search(const Form& F, const U& t) { std::size_t a, b; return search(F, t, 0, false, a, b); }

// ---------------------------------------------------------------- terms
inline List piter(const Val& v) {   // for x in v, where None is not iterable
    if (v.is_none()) throw PyErr("TypeError", "'NoneType' object is not iterable");
    return iter(v);
}
struct Term { std::string term, match; std::vector<Form> forms; };
inline std::vector<Term> load_terms(const std::string& path, const Val& extra) {
    std::vector<Term> terms;
    std::vector<std::string> paths{path};
    if (truthy(extra)) paths.push_back(pystr(extra));
    for (const auto& p : paths) {
        const Val data = loads(read_text(p));
        for (const Val& t : piter(dget(data, "terms", Val::list()))) {
            if (t.t != Val::DICT) throw PyErr("TypeError", std::string(type_name(t)) + " indices must be integers or slices, not str");
            if (!t.d->has("term")) throw PyErr("KeyError", "'term'");
            const Val name = t.d->at("term");
            if (name.t != Val::STR) throw PyErr("TypeError", std::string("a term name must be text, got ") + type_name(name));
            const Val match = dget(t, "match", Val::str("wording"));
            const Val case_v = dget(t, "case", Val::list());
            std::vector<std::string> cs;
            if (case_v.t == Val::LIST) {
                for (const Val& x : *case_v.l) {
                    if (x.t == Val::LIST || x.t == Val::DICT) throw PyErr("TypeError", std::string("unhashable type: '") + type_name(x) + "'");
                    if (x.t == Val::STR) cs.push_back(x.s);
                }
            } else for (const Val& x : piter(case_v)) cs.push_back(x.s);   // a string is a set of its characters; a number is not iterable
            if (!t.d->has("forms")) throw PyErr("KeyError", "'forms'");
            Term T; T.term = name.s; T.match = match.t == Val::STR ? match.s : pystr(match);
            for (const Val& f : piter(t.d->at("forms"))) {
                if (f.t == Val::LIST || f.t == Val::DICT) throw PyErr("TypeError", std::string("unhashable type: '") + type_name(f) + "'");
                std::string fs;
                if (f.t == Val::STR) fs = f.s;
                else if (truthy(f)) throw PyErr("AttributeError", std::string("'") + type_name(f) + "' object has no attribute 'replace'");
                const bool in_cs = f.t == Val::STR && std::find(cs.begin(), cs.end(), fs) != cs.end();
                Form F = form_regex(fs, in_cs);
                F.form = f.t == Val::STR ? f.s : std::string();
                T.forms.push_back(F);
            }
            terms.push_back(std::move(T));
        }
    }
    return terms;
}

struct Span { std::size_t s, e; int ti; std::string form; };
// Every term in a text, longest match first, no overlaps: [(start, end, term_index, form)]
inline std::vector<Span> find_spans(const U& text0, const std::vector<Term>& terms) {
    const U text = norm_text(text0);
    std::vector<Span> cands;
    for (std::size_t ti = 0; ti < terms.size(); ++ti)
        for (const Form& F : terms[ti].forms) {
            std::size_t pos = 0; bool must = false, ok = true;
            while (pos <= text.size() && ok) {
                std::size_t a, b;
                ok = search(F, text, pos, must, a, b);
                if (!ok) break;
                cands.push_back({a, b, static_cast<int>(ti), F.form});
                must = b == a; pos = b;
            }
        }
    std::stable_sort(cands.begin(), cands.end(), [](const Span& x, const Span& y) {
        const long lx = static_cast<long>(x.e - x.s), ly = static_cast<long>(y.e - y.s);
        if (lx != ly) return lx > ly;
        if (x.s != y.s) return x.s < y.s;
        return x.ti < y.ti;
    });
    std::vector<std::pair<std::size_t, std::size_t>> taken; std::vector<Span> out;
    for (const Span& c : cands) {
        bool clash = false;
        for (const auto& [s, e] : taken) if (c.s < e && s < c.e) { clash = true; break; }
        if (clash) continue;
        taken.emplace_back(c.s, c.e);
        out.push_back(c);
    }
    std::stable_sort(out.begin(), out.end(), [](const Span& x, const Span& y) { return x.s != y.s ? x.s < y.s : x.ti < y.ti; });
    return out;
}

// ---------------------------------------------------------------- word lists
inline const std::set<std::string>& strong_verbs() {
    static const std::set<std::string> v = [] {
        std::set<std::string> o;
        for (const auto& w : split_ws("accelerated achieved added analyzed architected authored automated benchmarked boosted built "
            "championed coached consolidated converted created cut debugged decreased defined delivered deployed "
            "designed developed diagnosed directed doubled drove earned eliminated enabled engineered established "
            "expanded fixed formulated founded generated grew halved hired identified implemented improved "
            "increased initiated instrumented integrated introduced invented launched led lifted lowered "
            "maintained managed measured mentored merged migrated modeled modernized negotiated optimized "
            "orchestrated organized overhauled owned oversaw partnered pioneered planned presented produced "
            "programmed proposed prototyped published raised ran rebuilt recruited redesigned reduced refactored "
            "released reorganized replaced researched resolved restructured revamped reviewed rewrote saved "
            "scaled scoped secured shipped simplified slashed sold solved spearheaded sped standardized started "
            "steered streamlined supervised taught tested trained tripled tuned unified upgraded validated won "
            "wrote made set brought began chose found held kept sent spun took undertook upheld")) o.insert(w);
        return o;
    }();
    return v;
}
inline const std::set<std::string>& weak_verbs() {
    static const std::set<std::string> v = [] {
        std::set<std::string> o;
        for (const auto& w : split_ws("worked helped assisted participated responsible involved tasked duties handled contributed utilized "
            "used was were did tried attended exposed familiarized learned supported aided")) o.insert(w);
        return o;
    }();
    return v;
}
constexpr int BULLET_MIN = 8, BULLET_MAX = 30;
using Words = std::vector<std::string>;
inline const std::vector<std::pair<std::string, Words>>& section_words() {
    static const std::vector<std::pair<std::string, Words>> v = {
        {"experience", {"experience", "employment", "work history", "career history", "professional background"}},
        {"education", {"education", "academic"}},
        {"skills", {"skills", "technologies", "technical skills", "tools", "tech stack", "competencies", "languages"}},
        {"projects", {"projects"}},
        {"summary", {"summary", "objective", "profile", "about"}},
        {"other", {"certifications", "certificates", "awards", "publications", "volunteer", "interests", "honors", "leadership", "activities"}},
    };
    return v;
}
inline const std::vector<std::pair<std::string, Words>>& jd_kinds() {
    static const std::vector<std::pair<std::string, Words>> v = {
        {"ignore", {"about the job", "about the role", "about us", "about the team", "about the company", "benefits", "salary",
                    "compensation", "pay range", "equal opportunity", "perks", "location", "how to apply"}},
        {"preferred", {"preferred qualifications", "preferred", "nice to have", "nice-to-have", "bonus points", "bonus", "pluses",
                       "ideally", "it would be great if"}},
        {"must", {"minimum qualifications", "basic qualifications", "required qualifications", "requirements", "required",
                  "what you'll need", "what you need", "what we're looking for", "must have", "must-have", "qualifications",
                  "who you are", "you have", "you will need"}},
        {"responsibilities", {"responsibilities", "what you'll do", "what you will do", "your role", "the role", "duties", "day to day",
                              "in this role"}},
    };
    return v;
}

// BULLET_RE = ^\s*(?:[-*•·▪]|\d+[.)])\s+(.*)$ : the text after the marker, or false
inline bool bullet(const U& line, U& rest) {
    std::size_t i = 0; while (i < line.size() && sp(line[i])) i++;
    if (i >= line.size()) return false;
    const char32_t c = line[i];
    if (c == U'-' || c == U'*' || c == 0x2022 || c == 0xB7 || c == 0x25AA) i++;
    else if (is_dec(c)) { while (i < line.size() && is_dec(line[i])) i++; if (i >= line.size() || (line[i] != U'.' && line[i] != U')')) return false; i++; }
    else return false;
    if (i >= line.size() || !sp(line[i])) return false;
    while (i < line.size() && sp(line[i])) i++;
    rest = line.substr(i);
    return true;
}

// ---------------------------------------------------------------- the posting
inline bool head_kind(const U& line, std::string& kind, U& rest) {
    U s = ustrip(norm_text(line));
    s = ustrip(ustrip_c(ustrip(strip_hashes(s)), U"*"));
    const std::size_t colon = s.find(U':');
    U head = colon != U::npos ? ulower(ustrip(s.substr(0, colon))) : ulower(s);
    rest = colon != U::npos ? ustrip(s.substr(colon + 1)) : U();
    if (head.empty() || usplit_ws(head).size() > 6) return false;
    head = ustrip_c(head, U".", false, true);
    for (const auto& [k, words] : jd_kinds())
        for (const auto& w8 : words) {
            const U w = to_u(w8);
            if (head == w || starts(head, w + U" ") || ends(head, U" " + w)) {
                if (colon == U::npos && usplit_ws(head).size() > 4) return false;
                kind = k;
                return true;
            }
        }
    return false;
}
struct JD { std::map<std::string, std::vector<U>> sec; };
inline JD parse_jd(const U& text) {
    JD jd;
    for (const char* k : {"must", "preferred", "responsibilities", "ignore", "header"}) jd.sec[k];
    std::string current = "header"; bool found = false;
    for (const U& raw : ulines(norm_text(text))) {
        const U st = ustrip(raw);
        if (st.empty() || starts(st, U"<!--")) continue;
        std::string kind; U rest;
        if (head_kind(raw, kind, rest)) {
            current = kind; found = true;
            if (!rest.empty()) jd.sec[current].push_back(rest);
            continue;
        }
        U b; const U line = ustrip(bullet(raw, b) ? b : raw);
        if (!line.empty()) jd.sec[current].push_back(line);
    }
    if (!found) { jd.sec["must"] = jd.sec["header"]; jd.sec["header"].clear(); }
    return jd;
}
struct Group { bool choice; std::vector<int> terms; };
// (^|\W)(or|e.g.|eg|such as|either|and/or|one of|any of)(\W|$), ignoring case
inline bool has_choice_word(const U& x) {
    static const char* W[] = {"or", "e.g.", "eg", "such as", "either", "and/or", "one of", "any of"};
    for (std::size_t p = 0; p <= x.size(); ++p) {
        std::vector<std::size_t> at;
        if (p == 0) at.push_back(0);
        if (p < x.size() && !is_word(x[p])) at.push_back(p + 1);
        for (std::size_t q : at)
            for (const char* w : W)
                if (ci_at(x, q, w)) { const std::size_t e = q + wlen(w); if (e >= x.size() || !is_word(x[e])) return true; }
    }
    return false;
}
// re.sub(r"(?i)\b(and/or|and|or|e\.g\.|eg|such as|either|including|like|one of|any of)\b", " ", between), then
// re.sub(r"[\s,/;()]+", "", ...) == ""
inline bool only_filler(const U& t) {
    static const char* W[] = {"and/or", "and", "or", "e.g.", "eg", "such as", "either", "including", "like", "one of", "any of"};
    std::size_t i = 0;
    while (i < t.size()) {
        bool hit = false;
        if (boundary(t, i))
            for (const char* w : W)
                if (ci_at(t, i, w) && boundary(t, i + wlen(w))) { i += wlen(w); hit = true; break; }
        if (hit) continue;
        const char32_t c = t[i];
        if (!(sp(c) || c == U',' || c == U'/' || c == U';' || c == U'(' || c == U')')) return false;
        i++;
    }
    return true;
}
// Split one requirement line into groups: a CHOICE group is covered by any one term
inline std::vector<Group> line_groups(const U& line, const std::vector<Term>& terms) {
    const auto spans = find_spans(line, terms);
    std::vector<Group> groups;
    if (spans.empty()) return groups;
    std::vector<Span> run{spans[0]}; std::vector<U> seps;
    const auto close = [&](const std::vector<Span>& r, const std::vector<U>& ss) {
        std::vector<int> names;
        for (const Span& s : r) if (std::find(names.begin(), names.end(), s.ti) == names.end()) names.push_back(s.ti);
        bool choice = false;
        if (names.size() > 1) for (const U& x : ss) if (has_choice_word(x)) { choice = true; break; }
        if (choice) groups.push_back({true, names});
        else for (int n : names) groups.push_back({false, {n}});
    };
    for (std::size_t k = 1; k < spans.size(); ++k) {
        const U between = uslice(line, spans[k - 1].e, spans[k].s);
        if (only_filler(between)) { run.push_back(spans[k]); seps.push_back(between); }
        else { close(run, seps); run = {spans[k]}; seps.clear(); }
    }
    close(run, seps);
    return groups;
}
struct ReqLine { U line; std::string section; std::vector<Group> groups; };
struct Reqs { std::vector<ReqLine> must, preferred; std::map<int, std::set<std::string>> jd_forms; JD sec; };
// The posting as requirement lines with term groups, and the posting's own forms of each term
inline Reqs requirements(const U& jd_text, const std::vector<Term>& terms) {
    Reqs r; r.sec = parse_jd(jd_text);
    for (const auto& [name, target] : std::vector<std::pair<std::string, std::string>>{{"must", "must"}, {"preferred", "preferred"}, {"responsibilities", "preferred"}})
        for (const U& line : r.sec.sec[name]) {
            for (const Span& s : find_spans(line, terms)) r.jd_forms[s.ti].insert(s.form);
            (target == "must" ? r.must : r.preferred).push_back({line, name, line_groups(line, terms)});
        }
    return r;
}

// ---------------------------------------------------------------- the resume
inline std::string section_of(const U& line) {   // "" for None
    const U s = ustrip(norm_text(line));
    bool md = false; U text = s;
    {   // ^#{1,6}\s+(.+)$
        std::size_t h = 0; while (h < s.size() && s[h] == U'#') h++;
        if (h >= 1 && h <= 6 && h < s.size() && sp(s[h])) {
            std::size_t k = h; while (k < s.size() && sp(s[k])) k++;
            if (k < s.size()) { md = true; text = s.substr(k); }
        }
    }
    text = ustrip(ustrip_c(ustrip(ustrip_c(ustrip(text), U"*")), U":", false, true));
    const U low = ulower(text);
    if (text.empty() || usplit_ws(text).size() > 5) return "";
    bool any_alpha = false; for (char32_t c : text) if (is_alpha(c)) any_alpha = true;
    bool is_heading = md || (str_isupper(text) && any_alpha) || ends(s, U":");
    if (!is_heading) for (const auto& [n, ws] : section_words()) for (const auto& w : ws) if (low == to_u(w)) is_heading = true;
    if (!is_heading) return "";
    for (const auto& [name, words] : section_words())
        for (const auto& w8 : words) {
            const U w = to_u(w8);
            if (low == w || ends(low, U" " + w) || starts(low, w + U" ") || starts(low, w + U"&")) return name;
        }
    return (md || str_isupper(text)) ? "other" : "";
}
// [\w.+-]+@[\w-]+\.[\w.-]+
inline bool has_email(const U& t) {
    for (std::size_t k = 1; k < t.size(); ++k) {
        if (t[k] != U'@') continue;
        const char32_t b = t[k - 1];
        if (!(is_word(b) || b == U'.' || b == U'+' || b == U'-')) continue;
        std::size_t j = k + 1; while (j < t.size() && (is_word(t[j]) || t[j] == U'-')) j++;
        if (j == k + 1 || j >= t.size() || t[j] != U'.' || j + 1 >= t.size()) continue;
        const char32_t c = t[j + 1];
        if (is_word(c) || c == U'.' || c == U'-') return true;
    }
    return false;
}
// (?<!\d)(?:\+?\d[\d\s().-]{8,}\d)(?!\d)
inline bool has_phone(const U& t) {
    const std::size_t n = t.size();
    const auto inset = [&](char32_t c) { return is_dec(c) || sp(c) || c == U'(' || c == U')' || c == U'.' || c == U'-'; };
    for (std::size_t p = 0; p < n; ++p) {
        if (p > 0 && is_dec(t[p - 1])) continue;
        const std::size_t q = t[p] == U'+' ? p + 1 : p;
        if (q >= n || !is_dec(t[q])) continue;
        std::size_t r = q + 1; while (r < n && inset(t[r])) r++;   // t[q+1 .. r-1] are in the set
        for (std::size_t e = r; e-- > q + 9;) if (is_dec(t[e]) && (e + 1 >= n || !is_dec(t[e + 1]))) return true;
    }
    return false;
}
struct Resume { std::vector<std::pair<std::string, std::vector<U>>> sections; std::vector<std::pair<std::string, U>> bullets; bool email = false, phone = false;
    std::vector<U>& sec(const std::string& k) { for (auto& [a, v] : sections) if (a == k) return v; sections.emplace_back(k, std::vector<U>{}); return sections.back().second; }
    bool has(const std::string& k) const { for (const auto& [a, v] : sections) if (a == k) return true; return false; }
    std::vector<U> get(const std::string& k) const { for (const auto& [a, v] : sections) if (a == k) return v; return {}; } };
inline Resume parse_resume(const U& text0) {
    const U text = norm_text(text0);
    Resume r; r.sec("header");
    std::string current = "header";
    for (const U& raw : ulines(text)) {
        if (ustrip(raw).empty()) continue;
        const std::string sec = section_of(raw);
        if (!sec.empty()) { current = sec; r.sec(current); continue; }
        r.sec(current).push_back(ustrip(raw));
        U b;
        if (bullet(raw, b) && (current == "experience" || current == "projects")) r.bullets.emplace_back(current, ustrip(b));
    }
    r.email = has_email(text); r.phone = has_phone(text);
    return r;
}
inline std::vector<U> ascii_words(const U& t) {   // re.findall(r"[A-Za-z]+", t)
    std::vector<U> o; std::size_t i = 0;
    while (i < t.size()) { if (!ascii_alpha(t[i])) { i++; continue; } std::size_t j = i; while (j < t.size() && ascii_alpha(t[j])) j++; o.push_back(t.substr(i, j - i)); i = j; }
    return o;
}
inline std::string first_word(const U& t) { const auto w = ascii_words(t); return w.empty() ? "" : to_s(uascii_lower(w[0])); }
inline Val check_bullet(const U& text) {
    const auto words = usplit_ws(text);
    const std::string fw = first_word(text);
    const auto aw = ascii_words(text);
    std::string two;
    for (std::size_t k = 0; k < aw.size() && k < 2; ++k) two += (k ? " " : "") + to_s(uascii_lower(aw[k]));
    const bool weak = weak_verbs().count(fw) || two == "in charge" || two == "worked on" || two == "helped with";
    const bool strong = !weak && (strong_verbs().count(fw) || (fw.size() >= 5 && fw.compare(fw.size() - 2, 2, "ed") == 0));
    bool has_num = false;   // k8s and p99 are names, not numbers
    for (std::size_t k = 0; k < text.size(); ++k) if (is_dec(text[k]) && !(k > 0 && ascii_alpha(text[k - 1]))) { has_num = true; break; }
    const long nw = static_cast<long>(words.size());
    const bool length_ok = BULLET_MIN <= nw && nw <= BULLET_MAX;
    List notes;
    if (!has_num) notes.push_back(Val::str("no number: how much, how many, how fast?"));
    if (!strong) {
        std::string open;
        if (weak) { for (std::size_t k = 0; k < words.size() && k < 2; ++k) open += (k ? " " : "") + to_s(words[k]); }
        else open = words.empty() ? "" : to_s(words[0]);
        notes.push_back(Val::str("weak opening \"" + open + "\": start with what you did (Built, Cut, Led, Shipped)"));
    }
    if (nw < BULLET_MIN) notes.push_back(Val::str(std::to_string(nw) + " words: too short to show the result"));
    if (nw > BULLET_MAX) notes.push_back(Val::str(std::to_string(nw) + " words: too long, one achievement per bullet"));
    Val o = Val::dict();
    o.d->set("number", Val::boolean(has_num)); o.d->set("verb", Val::boolean(strong)); o.d->set("length", Val::boolean(length_ok));
    o.d->set("words", Val::integer(nw));
    o.d->set("points", Val::flt((static_cast<int>(has_num) + static_cast<int>(strong) + static_cast<int>(length_ok)) / 3.0));
    o.d->set("notes", Val::list(notes));
    return o;
}
// does a year (19|20)\d\d start at i?
inline bool year_at(const U& t, std::size_t i) {
    return i + 3 < t.size() && ((t[i] == U'1' && t[i + 1] == U'9') || (t[i] == U'2' && t[i + 1] == U'0')) && is_dec(t[i + 2]) && is_dec(t[i + 3]);
}
// re.findall(r"((?:19|20)\d\d)\s*(?:-|to|until|–)\s*((?:19|20)\d\d|present|current|now|today)", exp, re.I)
inline std::vector<std::pair<U, U>> year_ranges(const U& t) {
    std::vector<std::pair<U, U>> out; std::size_t p = 0;
    while (p < t.size()) {
        bool ok = false; std::size_t end = 0; U b;
        if (year_at(t, p)) {
            std::size_t q = p + 4; while (q < t.size() && sp(t[q])) q++;
            for (const char* sep : {"-", "to", "until", "\xE2\x80\x93"}) {
                const U su = to_u(sep);
                bool m = q + su.size() <= t.size();
                for (std::size_t k = 0; m && k < su.size(); ++k) m = ci_eq(t[q + k], su[k]);
                if (!m) continue;
                std::size_t r = q + su.size(); while (r < t.size() && sp(t[r])) r++;
                if (year_at(t, r)) { ok = true; b = t.substr(r, 4); end = r + 4; break; }
                for (const char* w : {"present", "current", "now", "today"})
                    if (ci_at(t, r, w)) { ok = true; b = t.substr(r, wlen(w)); end = r + wlen(w); break; }
                if (ok) break;
            }
        }
        if (ok) { out.emplace_back(t.substr(p, 4), b); p = end; } else p++;
    }
    return out;
}
// re.findall(r"(\d+)\+?\s+years?", line, re.I)
inline std::vector<long long> years_asked(const U& t) {
    std::vector<long long> out; std::size_t p = 0;
    while (p < t.size()) {
        if (is_dec(t[p])) {
            std::size_t k = p; while (k < t.size() && is_dec(t[k])) k++;
            std::size_t q = k; if (q < t.size() && t[q] == U'+') q++;
            const std::size_t s0 = q; while (q < t.size() && sp(t[q])) q++;
            if (q > s0 && ci_at(t, q, "year")) {
                std::size_t e = q + 4; if (e < t.size() && ci_eq(t[e], U's')) e++;
                out.push_back(py_int_u(t.substr(p, k - p))); p = e; continue;
            }
        }
        p++;
    }
    return out;
}
inline double years_in(const Resume& r, long long year) {
    U exp; bool first = true;
    for (const U& l : r.get("experience")) { if (!first) exp += U"\n"; exp += l; first = false; }
    double total = 0.0;
    for (const auto& [a, b] : year_ranges(exp)) {
        const long long end = !isdigit_str(to_s(b.substr(0, 1))) ? year : py_int_u(b);
        total += static_cast<double>(std::max(0LL, end - py_int_u(a)));
    }
    return total;
}

// ---------------------------------------------------------------- scoring
inline const Val& at(const Val& v, const char* k) { return *v.d->get(k); }
// ('yes' | 'reword' | 'no', what the resume says)
inline std::pair<std::string, Val> term_present(const Term& term, int ti, const std::vector<Span>& rspans, const std::set<std::string>& jd_forms, const U& rtext) {
    std::vector<Span> hits; for (const Span& s : rspans) if (s.ti == ti) hits.push_back(s);
    if (hits.empty()) return {"no", Val()};
    const auto piece = [&](const Span& s) { return uslice(rtext, s.s, s.e); };
    if (term.match == "meaning") return {"yes", Val::str(to_s(piece(hits[0])))};
    std::vector<const Form*> rxs; for (const Form& F : term.forms) if (jd_forms.count(F.form)) rxs.push_back(&F);
    for (const Span& s : hits) { const U p = piece(s); for (const Form* F : rxs) if (rx_search(*F, p)) return {"yes", Val::str(to_s(p))}; }
    return {"reword", Val::str(to_s(piece(hits[0])))};
}
inline Val score(const U& resume_text, const U& jd_text, const std::vector<Term>& terms, bool with_years = false, long long year = 0) {
    Reqs reqs = requirements(jd_text, terms);
    const U rtext = norm_text(resume_text);
    const auto rspans = find_spans(rtext, terms);
    const Resume resume = parse_resume(resume_text);
    std::map<int, std::pair<std::string, Val>> status;
    for (const auto& [ti, forms] : reqs.jd_forms) status[ti] = term_present(terms[static_cast<std::size_t>(ti)], ti, rspans, forms, rtext);

    const auto cover = [&](const std::vector<ReqLine>& lines, List& rows) -> std::optional<double> {
        std::vector<double> vals;
        for (const ReqLine& ln : lines) {
            Val row = Val::dict(); row.d->set("line", Val::str(to_s(ln.line))); row.d->set("section", Val::str(ln.section));
            if (ln.groups.empty()) { row.d->set("coverage", Val()); row.d->set("groups", Val::list()); rows.push_back(row); continue; }
            int covered = 0; List gout;
            for (const Group& g : ln.groups) {
                bool ok = false; List ts;
                for (int t : g.terms) {
                    const auto& [st, says] = status.at(t);
                    if (st == "yes") ok = true;
                    Val e = Val::dict(); e.d->set("term", Val::str(terms[static_cast<std::size_t>(t)].term)); e.d->set("status", Val::str(st)); e.d->set("resume_says", says);
                    ts.push_back(e);
                }
                covered += ok ? 1 : 0;
                Val go = Val::dict(); go.d->set("choice", Val::boolean(g.choice)); go.d->set("covered", Val::boolean(ok)); go.d->set("terms", Val::list(ts));
                gout.push_back(go);
            }
            const double cov = covered / static_cast<double>(ln.groups.size());
            vals.push_back(cov);
            row.d->set("coverage", Val::flt(cov)); row.d->set("groups", Val::list(gout));
            rows.push_back(row);
        }
        if (vals.empty()) return std::nullopt;
        return fsum(vals) / static_cast<double>(vals.size());
    };
    List must_rows, pref_rows;
    std::optional<double> R = cover(reqs.must, must_rows), P = cover(reqs.preferred, pref_rows);
    long long wR = 50, wP = 20; const long long wB = 20, wS = 10;
    if (!P && R) { wR = 70; wP = 0; P = 0.0; }
    if (!R && P) { R = P; P = 0.0; must_rows = pref_rows; pref_rows.clear(); wR = 70; wP = 0; }
    if (!R) throw AtsError("no known terms in the job description: add the posting's skills with --terms extra.json");

    List checks; std::vector<double> pts;
    for (const auto& [sec, text] : resume.bullets) {
        Val c = Val::dict(); c.d->set("section", Val::str(sec)); c.d->set("text", Val::str(to_s(text)));
        const Val chk = check_bullet(text);
        for (const auto& [k, v] : chk.d->items) c.d->set(k, v);
        pts.push_back(chk.d->get("points")->f);
        checks.push_back(c);
    }
    const double Bv = pts.empty() ? 0.0 : fsum(pts) / static_cast<double>(pts.size());
    Val s_parts = Val::dict();
    s_parts.d->set("experience", Val::boolean(resume.has("experience"))); s_parts.d->set("education", Val::boolean(resume.has("education")));
    s_parts.d->set("skills", Val::boolean(resume.has("skills"))); s_parts.d->set("contact", Val::boolean(resume.email || resume.phone));
    int nyes = 0; for (const auto& [k, v] : s_parts.d->items) nyes += v.b ? 1 : 0;
    const double Sv = nyes / 4.0;

    const std::vector<std::pair<std::string, double>> parts{{"R", *R}, {"P", *P}, {"B", Bv}, {"S", Sv}};
    const std::map<std::string, long long> w{{"R", wR}, {"P", wP}, {"B", wB}, {"S", wS}};
    std::vector<double> terms_sum; for (const auto& [k, v] : parts) terms_sum.push_back(static_cast<double>(w.at(k)) * v);
    const double raw = fsum(terms_sum);
    const long long total = static_cast<long long>(raw + 1e-9 + 0.5);

    List reword, missing; std::set<std::string> seen;
    for (const List* rows : {&must_rows, &pref_rows})
        for (const Val& row : *rows)
            for (const Val& g : *at(row, "groups").l) {
                if (at(g, "covered").b) continue;
                for (const Val& t : *at(g, "terms").l) {
                    const std::string name = at(t, "term").s;
                    if (seen.count(name)) continue;
                    seen.insert(name);
                    if (at(t, "status").s == "reword") {
                        int idx = 0; while (terms[static_cast<std::size_t>(idx)].term != name) idx++;
                        const auto f = reqs.jd_forms.find(idx);
                        if (f == reqs.jd_forms.end()) throw PyErr("KeyError", std::to_string(idx));
                        Val r = Val::dict(); r.d->set("term", Val::str(name)); r.d->set("you_wrote", at(t, "resume_says"));
                        r.d->set("posting_says", Val::str(*f->second.begin())); r.d->set("where", at(row, "section"));
                        reword.push_back(r);
                    } else {
                        Val m = Val::dict(); m.d->set("term", Val::str(name)); m.d->set("where", at(row, "section")); m.d->set("choice", at(g, "choice"));
                        missing.push_back(m);
                    }
                }
            }
    std::vector<long long> asked; for (const ReqLine& ln : reqs.must) for (long long n : years_asked(ln.line)) asked.push_back(n);
    Val out = Val::dict();
    out.d->set("score", Val::integer(total));
    out.d->set("raw", Val::flt(round_n(raw, 2)));
    out.d->set("formula", Val::str("score = 50 x R + 20 x P + 20 x B + 10 x S, rounded half up"));
    Val wv = Val::dict(), pv = Val::dict(), ptv = Val::dict();
    for (const auto& [k, v] : parts) {
        wv.d->set(k, Val::integer(w.at(k)));
        pv.d->set(k, Val::flt(round_n(v, 4)));
        ptv.d->set(k, Val::flt(round_n(static_cast<double>(w.at(k)) * v, 2)));
    }
    out.d->set("weights", wv); out.d->set("parts", pv); out.d->set("points", ptv);
    out.d->set("must", Val::list(must_rows)); out.d->set("preferred", Val::list(pref_rows));
    out.d->set("reword", Val::list(reword)); out.d->set("missing", Val::list(missing));
    out.d->set("bullets", Val::list(checks)); out.d->set("sections", s_parts);
    if (with_years) {
        Val y = Val::dict(); y.d->set("resume", Val::flt(years_in(resume, year)));
        y.d->set("asked", asked.empty() ? Val() : Val::integer(*std::max_element(asked.begin(), asked.end())));
        out.d->set("years", y);
    }
    return out;
}

// ---------------------------------------------------------------- truth check
inline std::set<std::string> numbers(const U& text0) {   // \d+(?:[.,]\d+)*, commas dropped
    const U t = norm_text(text0); std::set<std::string> o; std::size_t p = 0;
    while (p < t.size()) {
        if (!is_dec(t[p])) { p++; continue; }
        std::size_t k = p; while (k < t.size() && is_dec(t[k])) k++;
        while (k + 1 < t.size() && (t[k] == U'.' || t[k] == U',') && is_dec(t[k + 1])) { k++; while (k < t.size() && is_dec(t[k])) k++; }
        U n; for (std::size_t i = p; i < k; ++i) if (t[i] != U',') n.push_back(t[i]);
        o.insert(to_s(n)); p = k;
    }
    return o;
}
// re.finditer(r"[A-Za-z][A-Za-z" + extra + "]*", t): (start, token)
inline std::vector<std::pair<std::size_t, U>> tokens(const U& t, const U& extra) {
    std::vector<std::pair<std::size_t, U>> o; std::size_t i = 0;
    while (i < t.size()) {
        if (!ascii_alpha(t[i])) { i++; continue; }
        std::size_t j = i + 1; while (j < t.size() && (ascii_alpha(t[j]) || extra.find(t[j]) != U::npos)) j++;
        o.emplace_back(i, t.substr(i, j - i)); i = j;
    }
    return o;
}
// What the rewrite says that the original (plus facts) does not
inline Val trace(const U& before, const U& after, const std::vector<Term>& terms, const U& facts) {
    const U source = before + U"\n" + facts;
    const auto na = numbers(after), ns = numbers(source);
    std::vector<std::string> new_numbers; for (const auto& x : na) if (!ns.count(x)) new_numbers.push_back(x);
    std::stable_sort(new_numbers.begin(), new_numbers.end(), [](const std::string& a, const std::string& b) {
        const std::size_t la = to_u(a).size(), lb = to_u(b).size(); return la != lb ? la < lb : a < b; });
    std::set<int> src_terms, aft_terms;
    for (const Span& s : find_spans(source, terms)) src_terms.insert(s.ti);
    for (const Span& s : find_spans(after, terms)) aft_terms.insert(s.ti);
    std::vector<std::string> new_terms; for (int i : aft_terms) if (!src_terms.count(i)) new_terms.push_back(terms[static_cast<std::size_t>(i)].term);
    std::sort(new_terms.begin(), new_terms.end());
    std::set<U> src_words;
    for (const auto& [st, w] : tokens(norm_text(source), U"'+#.-")) src_words.insert(ustrip_c(uascii_lower(w), U".'", false, true));
    std::vector<U> names;
    for (const U& line : ulines(norm_text(after))) {
        if (!section_of(line).empty()) continue;   // a section heading, not a claim
        U b; const U body = bullet(line, b) ? b : strip_hashes(ustrip(line));
        std::vector<std::pair<std::size_t, std::size_t>> covered; for (const Span& sp_ : find_spans(body, terms)) covered.emplace_back(sp_.s, sp_.e);
        std::size_t i = 0;
        while (i < body.size()) {   // re.finditer(r"[^.!?:|\u00b7]+", body)
            const auto stop = [](char32_t c) { return c == U'.' || c == U'!' || c == U'?' || c == U':' || c == U'|' || c == 0xB7; };
            if (stop(body[i])) { i++; continue; }
            std::size_t j = i; while (j < body.size() && !stop(body[j])) j++;
            const U sent = body.substr(i, j - i);
            const auto toks = tokens(sent, U"'+#-");
            for (std::size_t n = 0; n < toks.size(); ++n) {
                const std::size_t start = i + toks[n].first;
                bool in = false; for (const auto& [a, e] : covered) if (a <= start && start < e) in = true;
                if (in) continue;   // a known term: the term check above covers it
                const U t = ustrip_c(toks[n].second, U"'", false, true);
                const U low = uascii_lower(t); const std::string ls = to_s(low);
                if (n == 0 && (strong_verbs().count(ls) || weak_verbs().count(ls) || ends(low, U"ed"))) continue;   // a bullet's opening verb is a rewording, not a new fact
                if (!t.empty() && t[0] >= U'A' && t[0] <= U'Z' && !src_words.count(low) && !src_words.count(ustrip_c(low, U"s", false, true)))
                    if (std::find(names.begin(), names.end(), t) == names.end()) names.push_back(t);
            }
            i = j;
        }
    }
    List nn, nt, nm;
    for (const auto& x : new_numbers) nn.push_back(Val::str(x));
    for (const auto& x : new_terms) nt.push_back(Val::str(x));
    for (const auto& x : names) nm.push_back(Val::str(to_s(x)));
    Val o = Val::dict();
    o.d->set("invented_numbers", Val::list(nn)); o.d->set("invented_terms", Val::list(nt)); o.d->set("new_names", Val::list(nm));
    o.d->set("clean", Val::boolean(new_numbers.empty() && new_terms.empty()));
    return o;
}

// ---------------------------------------------------------------- reports
inline std::string pad_r(const std::string& s, std::size_t w) { const std::size_t n = to_u(s).size(); return n >= w ? s : s + std::string(w - n, ' '); }
inline std::string pad_l(const std::string& s, std::size_t w) { const std::size_t n = to_u(s).size(); return n >= w ? s : std::string(w - n, ' ') + s; }
inline std::string render(const Val& res, const std::string& label) {
    const Val& w = at(res, "weights"); const Val& p = at(res, "parts"); const Val& pts = at(res, "points");
    std::vector<std::string> out{"ATS match: " + std::to_string(at(res, "score").i) + " / 100" + (label.empty() ? "" : "   " + label), "", "Formula: " + at(res, "formula").s};
    const std::map<std::string, std::string> names{{"R", "minimum qualifications covered"}, {"P", "preferred + responsibilities covered"}, {"B", "bullet strength"}, {"S", "sections and contact"}};
    for (const char* k : {"R", "P", "B", "S"})
        out.push_back(std::string("  ") + k + "  " + pad_r(names.at(k), 38) + " " + fmt_f(at(p, k).f, 2) + " x " + pad_r(std::to_string(at(w, k).i), 2) + " = " + pad_l(fmt_f(at(pts, k).f, 2), 5));
    out.push_back("  total " + fmt_f(at(res, "raw").f, 2) + ", rounded " + std::to_string(at(res, "score").i));
    const auto rows = [&](const std::string& title, const Val& items) {
        out.push_back(""); out.push_back(title);
        for (const Val& r : *items.l) {
            const Val& cov = at(r, "coverage");
            if (cov.is_none()) continue;
            const std::string mark = cov.f == 1 ? "x" : cov.f > 0 ? "~" : " ";
            out.push_back("  [" + mark + "] " + at(r, "line").s);
            for (const Val& g : *at(r, "groups").l) {
                if (at(g, "covered").b) continue;
                std::string desc;
                if (at(g, "choice").b) { std::vector<std::string> ns; for (const Val& t : *at(g, "terms").l) ns.push_back(at(t, "term").s); desc = join(ns, " or "); }
                else desc = at((*at(g, "terms").l)[0], "term").s;
                std::vector<std::string> why;
                for (const Val& t : *at(g, "terms").l) if (at(t, "status").s == "reword") why.push_back("you wrote \"" + at(t, "resume_says").s + "\"");
                out.push_back("        missing " + desc + (why.empty() ? "" : ": " + join(why, "; ")));
            }
        }
    };
    rows("Minimum qualifications", at(res, "must"));
    if (!at(res, "preferred").l->empty()) rows("Preferred qualifications and responsibilities", at(res, "preferred"));
    if (!at(res, "reword").l->empty()) {
        out.push_back(""); out.push_back("Reword (you have it, say it the posting's way):");
        for (const Val& r : *at(res, "reword").l) out.push_back("  \"" + at(r, "you_wrote").s + "\" -> \"" + at(r, "posting_says").s + "\"");
    }
    if (!at(res, "missing").l->empty()) {
        out.push_back(""); out.push_back("Missing (add only if it is true; otherwise leave it out and cover it in the cover letter or interview):");
        std::vector<std::string> ms; for (const Val& m : *at(res, "missing").l) ms.push_back(at(m, "term").s);
        out.push_back("  " + join(ms, ", "));
    }
    out.push_back("");
    out.push_back("Bullets (number, strong verb, " + std::to_string(BULLET_MIN) + " to " + std::to_string(BULLET_MAX) + " words):");
    if (at(res, "bullets").l->empty()) out.push_back("  none found under Experience or Projects: one line per achievement, starting with - ");
    long i = 0;
    for (const Val& b : *at(res, "bullets").l) {
        ++i;
        std::string flag;
        if (at(b, "points").f == 1) flag = "ok";
        else { std::vector<std::string> ns; for (const Val& n : *at(b, "notes").l) ns.push_back(n.s); flag = join(ns, "; "); }
        const U tu = to_u(at(b, "text").s);
        const std::string text = tu.size() <= 70 ? at(b, "text").s : to_s(tu.substr(0, 67)) + "...";
        out.push_back("  " + pad_l(std::to_string(i), 2) + ". " + text + "\n      " + flag);
    }
    std::vector<std::string> ss; for (const auto& [k, v] : at(res, "sections").d->items) ss.push_back(k + " " + (v.b ? "yes" : "MISSING"));
    out.push_back(""); out.push_back("Sections: " + join(ss, ", "));
    if (res.d->has("years") && !at(at(res, "years"), "asked").is_none() && at(at(res, "years"), "asked").i != 0) {
        const Val& y = at(res, "years");
        out.push_back("Years: the posting asks for " + std::to_string(at(y, "asked").i) + "; the experience dates add up to about " +
                      std::to_string(static_cast<long long>(at(y, "resume").f)) + " (information, not scored).");
    }
    return join(out, "\n");
}
inline std::string render_trace(const Val& t) {
    std::vector<std::string> out;
    const auto csv = [](const Val& l) { std::vector<std::string> o; for (const Val& x : *l.l) o.push_back(x.s); return join(o, ", "); };
    if (at(t, "clean").b) out.push_back("Truth check: CLEAN. Every number and skill in the rewrite is in the original.");
    else {
        out.push_back("Truth check: INVENTED content. Remove it or confirm it with the user and add it to the original first.");
        if (!at(t, "invented_numbers").l->empty()) out.push_back("  numbers not in the original: " + csv(at(t, "invented_numbers")));
        if (!at(t, "invented_terms").l->empty()) out.push_back("  skills or terms not in the original: " + csv(at(t, "invented_terms")));
    }
    if (!at(t, "new_names").l->empty()) out.push_back("  new capitalized words to double-check: " + csv(at(t, "new_names")));
    return join(out, "\n");
}
inline U read_doc(const Val& path) {
    if (!truthy(path) || !exists(pystr(path))) throw AtsError("no file at " + pystr(path));
    return to_u(read_text(pystr(path)));
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("ats", "Resume vs job description: a 0-100 match with its formula, and a truth check.");
    p.subcommands("cmd", false);
    { Parser& s = p.sub("score"); s.help_line = "score one resume against a job description"; s.pos("resume"); s.opt({"--jd"}).required();
      s.opt({"--terms"}).help("extra terms JSON (same shape as terms.json)");
      s.opt({"--today"}).help("YYYY-MM-DD, only for the years line (default: the computer's date)"); s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("compare"); s.help_line = "before and after scores plus the truth check"; s.pos("before"); s.pos("after"); s.opt({"--jd"}).required();
      s.opt({"--facts"}).help("notes the user confirmed (numbers, tools) that the rewrite may use"); s.opt({"--terms"}); s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("trace"); s.help_line = "did the rewrite invent numbers or skills? (exit 1 if so)"; s.pos("before"); s.pos("after");
      s.opt({"--facts"}); s.opt({"--terms"}); s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("terms"); s.help_line = "what the posting asks for, line by line"; s.opt({"--jd"}).required(); s.opt({"--terms"}); s.opt({"--json"}).store_true(); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { out(ap::help_text(ap::norm(p, p.prog))); return 2; }
    DumpOpts jo; jo.indent = 2;
    try {
        const std::vector<Term> terms = load_terms(skill_file("terms.json"), get(a, "terms"));
        if (cmd == "score") {
            long long year;
            if (truthy(get(a, "today"))) {
                std::vector<long long> v; for (const auto& x : split(S(a, "today"), "-")) v.push_back(py_int_u(to_u(x)));
                if (v.size() < 3) throw PyErr("TypeError", std::string("function missing required argument '") + (v.size() == 1 ? "month' (pos 2)" : "day' (pos 3)"));
                if (v.size() > 3) throw PyErr("TypeError", "function takes at most 3 arguments (" + std::to_string(v.size()) + " given)");
                py_date(v[0], v[1], v[2]); year = v[0];
            } else year = std::stoll(today_iso().substr(0, 4));
            const U resume = read_doc(get(a, "resume")); const U jd = read_doc(get(a, "jd"));
            const Val res = score(resume, jd, terms, true, year);
            print(B(a, "json") ? dumps(res, jo) : render(res, basename(S(a, "resume"))));
            return 0;
        }
        if (cmd == "compare") {
            const U before = read_doc(get(a, "before")), after = read_doc(get(a, "after"));
            const U facts = truthy(get(a, "facts")) ? read_doc(get(a, "facts")) : U();
            const Val r1 = score(before, read_doc(get(a, "jd")), terms);
            const Val r2 = score(after, read_doc(get(a, "jd")), terms);
            const Val t = trace(before, after, terms, facts);
            if (B(a, "json")) {
                Val o = Val::dict();
                o.d->set("before", at(r1, "score")); o.d->set("after", at(r2, "score"));
                o.d->set("before_parts", at(r1, "parts")); o.d->set("after_parts", at(r2, "parts")); o.d->set("trace", t);
                print(dumps(o, jo));
            } else {
                print("Before " + std::to_string(at(r1, "score").i) + " -> after " + std::to_string(at(r2, "score").i));
                for (const char* k : {"R", "P", "B", "S"}) print(std::string("  ") + k + "  " + fmt_f(at(at(r1, "parts"), k).f, 2) + " -> " + fmt_f(at(at(r2, "parts"), k).f, 2));
                print(""); print(render_trace(t)); print(""); print(render(r2, basename(S(a, "after"))));
            }
            return at(t, "clean").b ? 0 : 1;
        }
        if (cmd == "trace") {
            const U facts = truthy(get(a, "facts")) ? read_doc(get(a, "facts")) : U();
            const U before = read_doc(get(a, "before")); const U after = read_doc(get(a, "after"));
            const Val t = trace(before, after, terms, facts);
            print(B(a, "json") ? dumps(t, jo) : render_trace(t));
            return at(t, "clean").b ? 0 : 1;
        }
        if (cmd == "terms") {
            const Reqs reqs = requirements(read_doc(get(a, "jd")), terms);
            const auto gname = [&](int x) { return terms[static_cast<std::size_t>(x)].term; };
            if (B(a, "json")) {
                Val o = Val::dict();
                for (const auto& [k, v] : std::vector<std::pair<std::string, const std::vector<ReqLine>*>>{{"must", &reqs.must}, {"preferred", &reqs.preferred}}) {
                    List ls;
                    for (const ReqLine& l : *v) {
                        List gs;
                        for (const Group& g : l.groups) { List ns; for (int x : g.terms) ns.push_back(Val::str(gname(x))); Val gv = Val::dict(); gv.d->set("choice", Val::boolean(g.choice)); gv.d->set("terms", Val::list(ns)); gs.push_back(gv); }
                        Val lv = Val::dict(); lv.d->set("line", Val::str(to_s(l.line))); lv.d->set("section", Val::str(l.section)); lv.d->set("groups", Val::list(gs));
                        ls.push_back(lv);
                    }
                    o.d->set(k, Val::list(ls));
                }
                print(dumps(o, jo));
            } else {
                for (const auto& [v, title] : std::vector<std::pair<const std::vector<ReqLine>*, std::string>>{{&reqs.must, "Minimum qualifications"}, {&reqs.preferred, "Preferred and responsibilities"}}) {
                    print(title);
                    for (const ReqLine& l : *v) {
                        std::vector<std::string> gs;
                        for (const Group& g : l.groups) {
                            if (g.choice) { std::vector<std::string> ns; for (int x : g.terms) ns.push_back(gname(x)); gs.push_back("(" + join(ns, " or ") + ")"); }
                            else gs.push_back(gname(g.terms[0]));
                        }
                        print("  - " + to_s(l.line) + "\n      terms: " + (gs.empty() ? std::string("none known") : join(gs, ", ")));
                    }
                    print("");
                }
            }
            return 0;
        }
    } catch (const AtsError& e) {
        eprint(std::string("ats: ") + e.what()); return 2;
    }
    return 2;
}
PACKTOOL("hunter-resume/ats", main);
}  // namespace packtools::hunter_resume
