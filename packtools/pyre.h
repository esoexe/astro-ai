// pyre.h - Python's re module (str patterns) and html.unescape(), on code points, for the pack tools.
// A backtracking matcher with CPython's semantics where the ports depend on them: \d \s \w \b are Unicode
// (str.isdecimal / str.isspace / str.isalnum + '_'), IGNORECASE compares simple lowercases plus re's extra case
// equivalences (_casefix.py: s / long s, i / dotless i, ...), sets follow _optimize_charset's IGNORECASE rules,
// lookbehind must be fixed-width, a backreference under IGNORECASE compares lowercases, and empty matches in
// sub()/findall()/split() follow Python 3.7+ (an empty match right after a non-empty one counts).
// Supported: literals and escapes, . ^ $ \A \Z \b \B, classes, groups (capturing, (?:), (?P<n>), (?P=n)),
// lookahead/lookbehind, alternation, greedy and lazy quantifiers, backreferences, flags I M S X A (inline at the
// start or scoped (?i:...)). Not supported (re.error): possessive quantifiers, atomic groups, conditionals, \N{...}.
#pragma once
#include "pyrt.h"
#include "html_entities.h"
#include <map>
#include <memory>

namespace pyrt {
using U32 = std::vector<uint32_t>;
inline U32 to_u32(const std::string& s) {   // WTF-8 (as pyrt strings are) -> code points
    U32 o; o.reserve(s.size());
    const auto* u = reinterpret_cast<const unsigned char*>(s.data()); const std::size_t n = s.size();
    for (std::size_t k = 0; k < n;) {
        const unsigned char c = u[k];
        std::size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
        if (k + len > n) len = n - k;
        o.push_back(cp_of(s.substr(k, len))); k += len;
    }
    return o;
}
inline std::string from_u32(const U32& v, std::size_t a = 0, std::size_t b = std::string::npos) {
    std::string o; if (b > v.size()) b = v.size();
    for (std::size_t k = a; k < b; ++k) o += utf8(v[k]);
    return o;
}
namespace uprop {
inline uint32_t lower1(uint32_t c) { const auto* m = ucase_detail::simple(c); return m ? m->lo : c; }   // simple mappings
inline uint32_t upper1(uint32_t c) { const auto* m = ucase_detail::simple(c); return m ? m->up : c; }
inline bool decimal(uint32_t c) { return ucase_detail::in_ranges(ucase::DECIMAL, c); }
inline bool digit(uint32_t c) { return ucase_detail::in_ranges(ucase::DIGIT, c); }
inline bool numeric(uint32_t c) { return ucase_detail::in_ranges(ucase::NUMERIC, c); }
inline bool alpha(uint32_t c) { return ucase_detail::in_ranges(ucase::ALPHA, c); }
inline bool alnum(uint32_t c) { return alpha(c) || decimal(c) || digit(c) || numeric(c); }
inline bool upper(uint32_t c) { return ucase_detail::in_ranges(ucase::UPPER, c); }
inline bool lower(uint32_t c) { return ucase_detail::in_ranges(ucase::LOWER, c); }
inline bool title(uint32_t c) { return ucase_detail::in_ranges(ucase::TITLE, c); }
inline bool word(uint32_t c) { return c == '_' || alnum(c); }
inline bool iscased(uint32_t c) { return c != lower1(c) || c != upper1(c); }   // _sre.unicode_iscased
inline const ucase::Extra* extra(uint32_t c) {
    const auto* b = std::begin(ucase::EXTRA_CASES); const auto* e = std::end(ucase::EXTRA_CASES);
    const auto* it = std::lower_bound(b, e, c, [](const ucase::Extra& m, uint32_t x) { return m.cp < x; });
    return it != e && it->cp == c ? it : nullptr;
}
}  // namespace uprop
// str.isalpha() / isalnum() / isdecimal() / isnumeric() / isspace() / isupper() / islower()
template <class F> inline bool all_cps(const std::string& s, F f) { const U32 v = to_u32(s); if (v.empty()) return false; for (uint32_t c : v) if (!f(c)) return false; return true; }
inline bool isalpha_str(const std::string& s) { return all_cps(s, uprop::alpha); }
inline bool isalnum_str(const std::string& s) { return all_cps(s, uprop::alnum); }
inline bool isdecimal_str(const std::string& s) { return all_cps(s, uprop::decimal); }
inline bool isnumeric_str(const std::string& s) { return all_cps(s, [](uint32_t c) { return uprop::numeric(c) || uprop::digit(c) || uprop::decimal(c); }); }
inline bool isspace_str(const std::string& s) { return all_cps(s, py_space); }
inline bool isupper_str(const std::string& s) {
    bool cased = false;
    for (uint32_t c : to_u32(s)) { if (uprop::lower(c) || uprop::title(c)) return false; if (!cased && uprop::upper(c)) cased = true; }
    return cased;
}
inline bool islower_str(const std::string& s) {
    bool cased = false;
    for (uint32_t c : to_u32(s)) { if (uprop::upper(c) || uprop::title(c)) return false; if (!cased && uprop::lower(c)) cased = true; }
    return cased;
}

namespace re {
enum : int { I = 2, L = 4, M = 8, S = 16, U = 32, X = 64, A = 256 };
[[noreturn]] inline void error(const std::string& m) { throw PyErr("re.error", m); }

struct Set {
    bool neg = false;
    std::vector<std::pair<uint32_t, uint32_t>> items;   // literals are (c, c)
    std::vector<char> cats;                             // d D s S w W
    bool icase = false, hascased = false, ascii = false;
    std::vector<bool> bmp;                              // IGNORECASE: lowercased members (and their extra cases) below 0x10000
    std::vector<std::pair<uint32_t, uint32_t>> tail;    // IGNORECASE: ranges reaching past the BMP (RANGE_UNI_IGNORE)
    std::vector<uint32_t> tail_lits;
};
inline bool cat_test(char k, uint32_t c, bool ascii) {
    switch (k) {
        case 'd': return ascii ? (c >= '0' && c <= '9') : uprop::decimal(c);
        case 'D': return !cat_test('d', c, ascii);
        case 's': return ascii ? (c == ' ' || (c >= 9 && c <= 13)) : py_space(c);
        case 'S': return !cat_test('s', c, ascii);
        case 'w': return ascii ? (c < 128 && (std::isalnum(static_cast<int>(c)) || c == '_')) : uprop::word(c);
        case 'W': return !cat_test('w', c, ascii);
    }
    return false;
}
inline uint32_t fold(uint32_t c, bool ascii) { return ascii ? (c >= 'A' && c <= 'Z' ? c + 32 : c) : uprop::lower1(c); }
inline void compile_set(Set& s) {   // _optimize_charset with fixup = tolower, fixes = _EXTRA_CASES
    if (!s.icase) return;
    const auto mark = [&](uint32_t lo) { s.bmp[lo] = true; if (s.ascii) return; if (const auto* x = uprop::extra(lo)) { s.bmp[x->a] = true; if (x->b) s.bmp[x->b] = true; } };
    s.bmp.assign(0x10000, false);
    for (const auto& [a, b] : s.items) {
        if (a == b) {
            const uint32_t lo = fold(a, s.ascii);
            if (lo < 0x10000) mark(lo); else s.tail_lits.push_back(lo);
            if (!s.hascased && (s.ascii ? lo != a || (a >= 'a' && a <= 'z') : uprop::iscased(lo))) s.hascased = true;
            continue;
        }
        bool tailed = false;
        for (uint32_t i = a;; ++i) {
            const uint32_t lo = fold(i, s.ascii);
            if (lo >= 0x10000) { s.tail.push_back({a, b}); s.hascased = true; tailed = true; break; }
            mark(lo);
            if (i == b) break;
        }
        if (!tailed && !s.hascased) for (uint32_t i = a;; ++i) { if (s.ascii ? ((i | 32) >= 'a' && (i | 32) <= 'z') : uprop::iscased(i)) { s.hascased = true; break; } if (i == b) break; }
    }
}
inline bool set_test(const Set& s, uint32_t ch) {
    bool hit = false;
    if (s.icase && s.hascased) {
        const uint32_t c = fold(ch, s.ascii);
        hit = c < 0x10000 && s.bmp[c];
        for (std::size_t k = 0; !hit && k < s.tail_lits.size(); ++k) hit = s.tail_lits[k] == c;
        for (std::size_t k = 0; !hit && k < s.tail.size(); ++k) { const uint32_t u = uprop::upper1(c); hit = (s.tail[k].first <= c && c <= s.tail[k].second) || (s.tail[k].first <= u && u <= s.tail[k].second); }
        for (std::size_t k = 0; !hit && k < s.cats.size(); ++k) hit = cat_test(s.cats[k], c, s.ascii);
    } else {
        for (std::size_t k = 0; !hit && k < s.items.size(); ++k) hit = s.items[k].first <= ch && ch <= s.items[k].second;
        for (std::size_t k = 0; !hit && k < s.cats.size(); ++k) hit = cat_test(s.cats[k], ch, s.ascii);
    }
    return hit != s.neg;
}

enum class Op { Lit, Any, Set, Bol, Eol, Beg, End, WordB, NWordB, Group, Rep, Backref, Look };
struct Node;
using Seq = std::vector<Node*>;
struct Node {
    Op op = Op::Lit;
    uint32_t ch = 0; uint32_t lits[3] = {0, 0, 0}; int nlits = 0;   // Lit: exact (nlits 0) or folded alternatives
    int flags = 0;
    std::shared_ptr<Set> set;
    int group = 0;                  // Group: capture index (0 = none); Backref: the index
    std::vector<Seq> alts;          // Group, Look
    Node* body = nullptr; int min = 0, max = -1; bool lazy = false, simple = false;   // Rep
    bool neg = false, behind = false; int width = 0;   // Look
};

class Parser {
public:
    Parser(const U32& p, int flags) : p_(p), flags_(flags) {}
    std::vector<std::unique_ptr<Node>> pool;
    int groups = 0;
    std::map<std::string, int> names;
    Seq parse_all(int& flags_out) {
        global_flags();
        Seq s; std::vector<Seq> alts = alternation();
        if (i_ < p_.size()) error(p_[i_] == ')' ? "unbalanced parenthesis at position " + std::to_string(i_) : "unexpected end of pattern");
        flags_out = flags_;
        if (alts.size() == 1) return alts[0];
        Node* g = mk(Op::Group); g->alts = std::move(alts); s.push_back(g); return s;
    }
private:
    const U32& p_; std::size_t i_ = 0; int flags_;
    Node* mk(Op op) { pool.push_back(std::make_unique<Node>()); Node* n = pool.back().get(); n->op = op; n->flags = flags_; return n; }
    bool more() const { return i_ < p_.size(); }
    uint32_t peek(std::size_t k = 0) const { return i_ + k < p_.size() ? p_[i_ + k] : 0; }
    static int flag_of(uint32_t c) { switch (c) { case 'i': return I; case 'm': return M; case 's': return S; case 'x': return X; case 'a': return A; case 'u': return U; case 'L': return L; } return 0; }
    void global_flags() {   // (?imsx) at the very start apply to the whole pattern
        while (peek() == '(' && peek(1) == '?' && flag_of(peek(2))) {
            std::size_t k = i_ + 2; int f = 0;
            while (k < p_.size() && flag_of(p_[k])) f |= flag_of(p_[k++]);
            if (k < p_.size() && p_[k] == ')') { flags_ |= f; i_ = k + 1; } else break;
        }
    }
    void skip_verbose() {
        if (!(flags_ & X)) return;
        while (more()) {
            if (py_space(peek())) { i_++; continue; }
            if (peek() == '#') { while (more() && peek() != '\n') i_++; continue; }
            break;
        }
    }
    std::vector<Seq> alternation() {
        std::vector<Seq> alts; alts.push_back(sequence());
        while (more() && peek() == '|') { i_++; alts.push_back(sequence()); }
        return alts;
    }
    Seq sequence() {
        Seq s;
        for (;;) {
            skip_verbose();
            if (!more() || peek() == '|' || peek() == ')') break;
            Node* a = atom();
            if (!a) continue;
            skip_verbose();
            a = quantify(a);
            s.push_back(a);
        }
        return s;
    }
    bool read_int(std::size_t& k, int& v) const { std::size_t st = k; v = 0; while (k < p_.size() && p_[k] >= '0' && p_[k] <= '9') { v = v * 10 + static_cast<int>(p_[k] - '0'); if (v > 1000000000) v = 1000000000; k++; } return k > st; }
    Node* quantify(Node* a) {
        int mn = 0, mx = -1;
        const uint32_t c = peek();
        if (c == '*') { mn = 0; mx = -1; i_++; }
        else if (c == '+') { mn = 1; mx = -1; i_++; }
        else if (c == '?') { mn = 0; mx = 1; i_++; }
        else if (c == '{' && is_quant_brace()) {
            std::size_t k = i_ + 1; int lo = 0, hi = -1; const bool has_lo = read_int(k, lo);
            if (p_[k] == '}') { hi = lo; k++; }
            else { k++; int h = 0; if (read_int(k, h)) hi = h; k++; }
            if (!has_lo) lo = 0;
            if (hi >= 0 && hi < lo) error("min repeat greater than max repeat");
            mn = lo; mx = hi; i_ = k;
        } else return a;
        if (a->op == Op::Bol || a->op == Op::Eol || a->op == Op::Beg || a->op == Op::End || a->op == Op::WordB || a->op == Op::NWordB) error("nothing to repeat");
        bool lazy = false;
        if (peek() == '?') { lazy = true; i_++; }
        else if (peek() == '+') error("possessive quantifiers are not supported");
        Node* r = mk(Op::Rep); r->body = a; r->min = mn; r->max = mx; r->lazy = lazy;
        r->simple = a->op == Op::Lit || a->op == Op::Any || a->op == Op::Set;
        skip_verbose();
        if (peek() == '*' || peek() == '+' || peek() == '?' || (peek() == '{' && is_quant_brace())) error("multiple repeat");
        return r;
    }
    bool is_quant_brace() const { std::size_t k = i_ + 1; int v = 0; const bool lo = read_int(k, v); if (k < p_.size() && p_[k] == '}') return lo; if (k < p_.size() && p_[k] == ',') { k++; read_int(k, v); return k < p_.size() && p_[k] == '}'; } return false; }
    Node* lit(uint32_t c) {
        Node* n = mk(Op::Lit); n->ch = c;
        if (flags_ & I) {
            const bool ascii = flags_ & A;
            const bool cased = ascii ? ((c | 32) >= 'a' && (c | 32) <= 'z') : uprop::iscased(c);
            if (cased) {
                const uint32_t lo = fold(c, ascii); n->lits[0] = lo; n->nlits = 1;
                if (!ascii) if (const auto* x = uprop::extra(lo)) { n->lits[1] = x->a; n->nlits = 2; if (x->b) { n->lits[2] = x->b; n->nlits = 3; } }
            }
        }
        return n;
    }
    uint32_t hex(std::size_t n) {
        uint32_t v = 0;
        for (std::size_t k = 0; k < n; ++k) {
            const uint32_t c = peek(); int d = c >= '0' && c <= '9' ? static_cast<int>(c - '0') : c >= 'a' && c <= 'f' ? static_cast<int>(c - 'a' + 10) : c >= 'A' && c <= 'F' ? static_cast<int>(c - 'A' + 10) : -1;
            if (d < 0) error("incomplete escape");
            v = v * 16 + static_cast<uint32_t>(d); i_++;
        }
        if (v > 0x10FFFF) error("bad escape");
        return v;
    }
    // an escape after the backslash; returns true and sets c for a single character, or sets cat for \d etc.
    bool escape_char(bool in_class, uint32_t& c, char& cat) {
        if (!more()) error("bad escape (end of pattern)");
        const uint32_t e = p_[i_++]; cat = 0;
        switch (e) {
            case 'd': case 'D': case 's': case 'S': case 'w': case 'W': cat = static_cast<char>(e); return false;
            case 'n': c = '\n'; return true; case 't': c = '\t'; return true; case 'r': c = '\r'; return true;
            case 'f': c = '\f'; return true; case 'v': c = '\v'; return true; case 'a': c = 7; return true;
            case 'x': c = hex(2); return true; case 'u': c = hex(4); return true; case 'U': c = hex(8); return true;
            case 'b': if (in_class) { c = 8; return true; } break;
            case '0': { uint32_t v = 0; for (int k = 0; k < 2 && peek() >= '0' && peek() <= '7'; ++k) v = v * 8 + (p_[i_++] - '0'); c = v; return true; }
            default: break;
        }
        if (e < 128 && std::isalnum(static_cast<int>(e))) error(std::string("bad escape \\") + static_cast<char>(e));
        c = e; return true;
    }
    Node* parse_set() {   // after '['
        auto s = std::make_shared<Set>(); s->icase = flags_ & I; s->ascii = flags_ & A;
        if (peek() == '^') { s->neg = true; i_++; }
        bool first = true;
        for (;;) {
            if (!more()) error("unterminated character set");
            uint32_t c = p_[i_];
            if (c == ']' && !first) { i_++; break; }
            first = false; i_++;
            char cat = 0;
            if (c == '\\') { if (!escape_char(true, c, cat)) { s->cats.push_back(cat); continue; } }
            if (peek() == '-' && peek(1) != ']' && i_ + 1 < p_.size()) {
                i_++; uint32_t d = p_[i_++]; char cat2 = 0;
                if (d == '\\') { if (!escape_char(true, d, cat2)) error("bad character range"); }
                if (d < c) error("bad character range");
                s->items.push_back({c, d});
            } else s->items.push_back({c, c});
        }
        compile_set(*s);
        Node* n = mk(Op::Set); n->set = s; return n;
    }
    std::string name_until(uint32_t end) { std::string o; while (more() && peek() != end) o += utf8(p_[i_++]); if (!more()) error("missing " + utf8(end)); i_++; return o; }
    Node* group_body(Node* g) { g->alts = alternation(); if (!more() || peek() != ')') error("missing ), unterminated subpattern"); i_++; return g; }
    Node* atom() {
        const uint32_t c = p_[i_++];
        switch (c) {
            case '.': return mk(Op::Any);
            case '^': return mk(Op::Bol);
            case '$': return mk(Op::Eol);
            case '[': return parse_set();
            case '*': case '+': case '?': error("nothing to repeat at position " + std::to_string(i_ - 1));
            case '{': if (is_quant_brace_at(i_ - 1)) error("nothing to repeat at position " + std::to_string(i_ - 1)); return lit(c);
            case '(': return paren();
            case '\\': {
                if (!more()) error("bad escape (end of pattern)");
                const uint32_t e = peek();
                if (e == 'A') { i_++; return mk(Op::Beg); }
                if (e == 'Z') { i_++; return mk(Op::End); }
                if (e == 'b') { i_++; return mk(Op::WordB); }
                if (e == 'B') { i_++; return mk(Op::NWordB); }
                if (e >= '1' && e <= '9') {
                    int v = static_cast<int>(e - '0'); i_++;
                    if (peek() >= '0' && peek() <= '9') { v = v * 10 + static_cast<int>(peek() - '0'); i_++; }
                    if (v > groups) error("invalid group reference " + std::to_string(v));
                    Node* n = mk(Op::Backref); n->group = v; return n;
                }
                uint32_t ch = 0; char cat = 0;
                if (!escape_char(false, ch, cat)) {
                    auto s = std::make_shared<Set>(); s->cats.push_back(cat); s->icase = flags_ & I; s->ascii = flags_ & A; compile_set(*s);
                    Node* n = mk(Op::Set); n->set = s; return n;
                }
                return lit(ch);
            }
            default: return lit(c);
        }
    }
    bool is_quant_brace_at(std::size_t at) const { std::size_t save = i_; const_cast<Parser*>(this)->i_ = at; const bool r = is_quant_brace(); const_cast<Parser*>(this)->i_ = save; return r; }
    Node* paren() {
        if (peek() != '?') { Node* g = mk(Op::Group); g->group = ++groups; return group_body(g); }
        i_++;
        const uint32_t c = peek();
        if (c == ':') { i_++; return group_body(mk(Op::Group)); }
        if (c == 'P' && peek(1) == '<') { i_ += 2; const std::string nm = name_until('>'); Node* g = mk(Op::Group); g->group = ++groups; names[nm] = g->group; return group_body(g); }
        if (c == 'P' && peek(1) == '=') { i_ += 2; const std::string nm = name_until(')'); auto it = names.find(nm); if (it == names.end()) error("unknown group name '" + nm + "'"); Node* n = mk(Op::Backref); n->group = it->second; return n; }
        if (c == '=' || c == '!') { i_++; Node* n = mk(Op::Look); n->neg = c == '!'; return group_body(n); }
        if (c == '<' && (peek(1) == '=' || peek(1) == '!')) {
            Node* n = mk(Op::Look); n->behind = true; n->neg = peek(1) == '!'; i_ += 2; group_body(n);
            int w = -1; for (const auto& a : n->alts) { const int x = width(a); if (x < 0 || (w >= 0 && x != w)) error("look-behind requires fixed-width pattern"); w = x; }
            n->width = w; return n;
        }
        if (c == '#') { while (more() && peek() != ')') i_++; if (!more()) error("missing ), unterminated comment"); i_++; return nullptr; }
        if (flag_of(c) || c == '-') {   // scoped flags (?i:...) / (?-i:...)
            int add = 0, del = 0; bool minus = false;
            while (more() && (flag_of(peek()) || peek() == '-')) { if (peek() == '-') minus = true; else (minus ? del : add) |= flag_of(peek()); i_++; }
            if (peek() == ')') error("global flags not at the start of the expression");
            if (peek() != ':') error("missing :");
            i_++;
            const int save = flags_; flags_ = (flags_ | add) & ~del;
            Node* g = group_body(mk(Op::Group)); flags_ = save; return g;
        }
        error("unknown extension ?" + utf8(c));
    }
    static int width(const Seq& s) {
        int w = 0;
        for (const Node* n : s) {
            int x = 0;
            switch (n->op) {
                case Op::Lit: case Op::Any: case Op::Set: x = 1; break;
                case Op::Bol: case Op::Eol: case Op::Beg: case Op::End: case Op::WordB: case Op::NWordB: case Op::Look: x = 0; break;
                case Op::Backref: return -1;
                case Op::Group: { int g = -1; for (const auto& a : n->alts) { const int y = width(a); if (y < 0 || (g >= 0 && y != g)) return -1; g = y; } x = g < 0 ? 0 : g; break; }
                case Op::Rep: { if (n->max != n->min) return -1; Seq one{n->body}; const int y = width(one); if (y < 0) return -1; x = y * n->min; break; }
            }
            w += x;
        }
        return w;
    }
};

// ------------------------------------------------------------------ matching
struct FnRef {   // a non-owning reference to a continuation (no allocation on the hot path)
    void* o; bool (*f)(void*, std::size_t);
    template <class F> FnRef(F& x) : o(&x), f([](void* p, std::size_t i) { return (*static_cast<F*>(p))(i); }) {}
    bool operator()(std::size_t i) const { return f(o, i); }
};
struct Match {
    bool ok = false;
    std::vector<long> spans;               // 2 per group, code point offsets, -1 = did not take part
    std::vector<std::string> strs;         // group texts ("" when unset)
    explicit operator bool() const { return ok; }
    bool has(int g) const { return ok && 2 * g + 1 < static_cast<int>(spans.size()) && spans[2 * g] >= 0; }
    std::string group(int g = 0) const { return has(g) ? strs[g] : std::string(); }
    long start(int g = 0) const { return ok ? spans[2 * g] : -1; }
    long end(int g = 0) const { return ok ? spans[2 * g + 1] : -1; }
    int groups() const { return ok ? static_cast<int>(spans.size() / 2) - 1 : 0; }
};
class Matcher {
public:
    Matcher(const U32& s, int ngroups) : s_(s), n_(s.size()), caps(2 * (ngroups + 1), -1) {}
    const U32& s_; const std::size_t n_; std::vector<long> caps;
    bool one(const Node* n, std::size_t i) const {   // a single-character node at i (i < n_)
        const uint32_t c = s_[i];
        switch (n->op) {
            case Op::Lit:
                if (!n->nlits) return c == n->ch;
                { const uint32_t lo = fold(c, n->flags & A); for (int k = 0; k < n->nlits; ++k) if (lo == n->lits[k]) return true; }
                return false;
            case Op::Any: return (n->flags & S) || c != '\n';
            case Op::Set: return set_test(*n->set, c);
            default: return false;
        }
    }
    bool word_at(std::size_t i, bool ascii) const { const uint32_t c = s_[i]; return ascii ? (c < 128 && (std::isalnum(static_cast<int>(c)) || c == '_')) : uprop::word(c); }
    bool seq(const Seq& q, std::size_t idx, std::size_t i, FnRef k) {
        if (idx == q.size()) return k(i);
        auto next = [&](std::size_t p) { return seq(q, idx + 1, p, k); };
        return node(q[idx], i, FnRef(next));
    }
    bool rep(const Node* n, int count, std::size_t i, FnRef k) {
        const bool can_more = n->max < 0 || count < n->max;
        auto again = [&](std::size_t p) { if (p == i && count >= n->min) return false; return rep(n, count + 1, p, k); };
        if (!n->lazy) {
            if (can_more && node(n->body, i, FnRef(again))) return true;
            return count >= n->min && k(i);
        }
        if (count >= n->min && k(i)) return true;
        return can_more && node(n->body, i, FnRef(again));
    }
    bool node(const Node* n, std::size_t i, FnRef k) {
        switch (n->op) {
            case Op::Lit: case Op::Any: case Op::Set: return i < n_ && one(n, i) && k(i + 1);
            case Op::Bol: return (i == 0 || ((n->flags & M) && s_[i - 1] == '\n')) && k(i);
            case Op::Eol: return (i == n_ || (i + 1 == n_ && s_[i] == '\n') || ((n->flags & M) && s_[i] == '\n')) && k(i);
            case Op::Beg: return i == 0 && k(i);
            case Op::End: return i == n_ && k(i);
            case Op::WordB: case Op::NWordB: {
                if (n_ == 0) return false;
                const bool ascii = n->flags & A;
                const bool that = i > 0 && word_at(i - 1, ascii), here = i < n_ && word_at(i, ascii);
                return ((n->op == Op::WordB) == (that != here)) && k(i);
            }
            case Op::Group: {
                for (const auto& alt : n->alts) {
                    if (n->group) {
                        const int g = n->group;
                        auto close = [&](std::size_t p) {
                            const long ob = caps[2 * g], oe = caps[2 * g + 1];
                            caps[2 * g] = static_cast<long>(i); caps[2 * g + 1] = static_cast<long>(p);
                            if (k(p)) return true;
                            caps[2 * g] = ob; caps[2 * g + 1] = oe; return false;
                        };
                        if (seq(alt, 0, i, FnRef(close))) return true;
                    } else if (seq(alt, 0, i, k)) return true;
                }
                return false;
            }
            case Op::Look: {
                const auto saved = caps; bool hit = false;
                if (n->behind) {
                    if (i >= static_cast<std::size_t>(n->width)) {
                        const std::size_t st = i - static_cast<std::size_t>(n->width);
                        auto at_i = [&](std::size_t p) { return p == i; };
                        for (const auto& alt : n->alts) if (seq(alt, 0, st, FnRef(at_i))) { hit = true; break; }
                    }
                } else {
                    auto yes = [](std::size_t) { return true; };
                    for (const auto& alt : n->alts) if (seq(alt, 0, i, FnRef(yes))) { hit = true; break; }
                }
                if (n->neg) { caps = saved; if (hit) return false; return k(i); }
                if (!hit) { caps = saved; return false; }
                if (k(i)) return true;
                caps = saved; return false;
            }
            case Op::Backref: {
                const int g = n->group; const long b = caps[2 * g], e = caps[2 * g + 1];
                if (b < 0) return false;
                const std::size_t len = static_cast<std::size_t>(e - b);
                if (i + len > n_) return false;
                const bool ic = n->flags & I, ascii = n->flags & A;
                for (std::size_t k2 = 0; k2 < len; ++k2) {
                    const uint32_t x = s_[static_cast<std::size_t>(b) + k2], y = s_[i + k2];
                    if (ic ? fold(x, ascii) != fold(y, ascii) : x != y) return false;
                }
                return k(i + len);
            }
            case Op::Rep: {
                if (n->simple) {
                    const std::size_t mx = n->max < 0 ? n_ - i : std::min<std::size_t>(static_cast<std::size_t>(n->max), n_ - i);
                    const std::size_t mn = static_cast<std::size_t>(n->min);
                    if (!n->lazy) {
                        std::size_t c = 0; while (c < mx && one(n->body, i + c)) c++;
                        if (c < mn) return false;
                        for (std::size_t t = c + 1; t-- > mn;) if (k(i + t)) return true;
                        return false;
                    }
                    for (std::size_t c = 0;; ++c) {
                        if (c >= mn && k(i + c)) return true;
                        if (c >= mx || !one(n->body, i + c)) return false;
                    }
                }
                return rep(n, 0, i, k);
            }
        }
        return false;
    }
};

class Regex {
public:
    explicit Regex(const std::string& pattern, int flags = 0) : pat_(to_u32(pattern)) {
        Parser p(pat_, flags); root_ = p.parse_all(flags_); pool_ = std::move(p.pool); groups_ = p.groups; names_ = p.names;
        if (!root_.empty() && root_[0]->op == Op::Lit && !root_[0]->nlits) { first_ = true; first_ch_ = root_[0]->ch; }
    }
    int groups() const { return groups_; }
    int flags() const { return flags_; }
    int group_index(const std::string& name) const { auto it = names_.find(name); return it == names_.end() ? -1 : it->second; }
    // a match starting exactly at `start`; must_advance: refuse an empty match there; full: must end at the end
    Match match_at(const U32& s, std::size_t start, bool must_advance = false, bool full = false) const {
        Matcher m(s, groups_); Match out; std::size_t endp = 0;
        auto fin = [&](std::size_t p) { if (must_advance && p == start) return false; if (full && p != s.size()) return false; endp = p; return true; };
        if (const_cast<Matcher&>(m).seq(root_, 0, start, FnRef(fin))) {
            out.ok = true; out.spans = m.caps; out.spans[0] = static_cast<long>(start); out.spans[1] = static_cast<long>(endp);
            out.strs.resize(static_cast<std::size_t>(groups_ + 1));
            for (int g = 0; g <= groups_; ++g) if (out.spans[2 * g] >= 0) out.strs[g] = from_u32(s, static_cast<std::size_t>(out.spans[2 * g]), static_cast<std::size_t>(out.spans[2 * g + 1]));
        }
        return out;
    }
    Match search(const U32& s, std::size_t pos = 0, bool must_advance = false) const {
        for (std::size_t st = pos; st <= s.size(); ++st) {
            if (first_ && (st >= s.size() || s[st] != first_ch_)) continue;
            Match m = match_at(s, st, must_advance && st == pos);
            if (m) return m;
        }
        return {};
    }
    Match search(const std::string& s) const { return search(to_u32(s)); }
    Match match(const std::string& s) const { return match_at(to_u32(s), 0); }
    Match fullmatch(const std::string& s) const { return match_at(to_u32(s), 0, false, true); }
    std::vector<Match> finditer(const U32& s) const {
        std::vector<Match> o; std::size_t pos = 0; bool must = false;
        while (pos <= s.size()) {
            Match m = search(s, pos, must); if (!m) break;
            pos = static_cast<std::size_t>(m.end()); must = m.start() == m.end();
            o.push_back(std::move(m));
        }
        return o;
    }
    std::vector<Match> finditer(const std::string& s) const { return finditer(to_u32(s)); }
    // findall: the whole match with no groups, group 1 with one group (rows of every group: findall_groups)
    std::vector<std::string> findall(const std::string& s) const { std::vector<std::string> o; for (const auto& m : finditer(s)) o.push_back(m.group(groups_ ? 1 : 0)); return o; }
    std::vector<std::vector<std::string>> findall_groups(const std::string& s) const {
        std::vector<std::vector<std::string>> o;
        for (const auto& m : finditer(s)) { std::vector<std::string> r; for (int g = 1; g <= groups_; ++g) r.push_back(m.group(g)); o.push_back(r); }
        return o;
    }
    template <class F> std::string sub_fn(F fn, const std::string& text, int count = 0) const {
        const U32 s = to_u32(text); std::string o; std::size_t last = 0, pos = 0; bool must = false; int n = 0;
        while ((count <= 0 || n < count) && pos <= s.size()) {
            Match m = search(s, pos, must); if (!m) break;
            const auto b = static_cast<std::size_t>(m.start()), e = static_cast<std::size_t>(m.end());
            o += from_u32(s, last, b); o += fn(m);
            last = e; pos = e; must = b == e; n++;
        }
        return o + from_u32(s, last);
    }
    std::string sub(const std::string& repl, const std::string& text, int count = 0) const {
        const auto parts = parse_template(repl);
        return sub_fn([&](const Match& m) { std::string r; for (const auto& [lit, g] : parts) r += g >= 0 ? m.group(g) : lit; return r; }, text, count);
    }
    // split: pieces between matches, with the groups' texts in between ("" for a group that did not take part)
    std::vector<std::string> split(const std::string& text, int maxsplit = 0) const {
        const U32 s = to_u32(text); std::vector<std::string> o; std::size_t last = 0, pos = 0; bool must = false; int n = 0;
        while ((maxsplit <= 0 || n < maxsplit) && pos <= s.size()) {
            Match m = search(s, pos, must); if (!m) break;
            const auto b = static_cast<std::size_t>(m.start()), e = static_cast<std::size_t>(m.end());
            o.push_back(from_u32(s, last, b)); for (int g = 1; g <= groups_; ++g) o.push_back(m.group(g));
            last = e; pos = e; must = b == e; n++;
        }
        o.push_back(from_u32(s, last)); return o;
    }
private:
    U32 pat_; Seq root_; std::vector<std::unique_ptr<Node>> pool_; int groups_ = 0, flags_ = 0; std::map<std::string, int> names_;
    bool first_ = false; uint32_t first_ch_ = 0;
    std::vector<std::pair<std::string, int>> parse_template(const std::string& repl) const {   // (literal, -1) or ("", group)
        std::vector<std::pair<std::string, int>> o; const U32 r = to_u32(repl); std::string lit;
        const auto flush = [&]() { if (!lit.empty()) { o.push_back({lit, -1}); lit.clear(); } };
        for (std::size_t k = 0; k < r.size(); ++k) {
            if (r[k] != '\\' || k + 1 >= r.size()) { if (r[k] == '\\') error("bad escape (end of pattern)"); lit += utf8(r[k]); continue; }
            const uint32_t e = r[++k];
            if (e == 'g') {
                if (k + 1 >= r.size() || r[k + 1] != '<') error("missing <");
                std::size_t j = k + 2; std::string nm; while (j < r.size() && r[j] != '>') nm += utf8(r[j++]);
                if (j >= r.size()) error("missing >, unterminated name");
                int g = -1;
                if (!nm.empty() && std::all_of(nm.begin(), nm.end(), [](char c) { return c >= '0' && c <= '9'; })) g = std::stoi(nm); else g = group_index(nm);
                if (g < 0 || g > groups_) error(g < 0 ? "unknown group name '" + nm + "'" : "invalid group reference " + nm);
                flush(); o.push_back({"", g}); k = j; continue;
            }
            if (e >= '1' && e <= '9') {
                int g = static_cast<int>(e - '0');
                if (k + 1 < r.size() && r[k + 1] >= '0' && r[k + 1] <= '9') { g = g * 10 + static_cast<int>(r[k + 1] - '0'); k++; }
                if (g > groups_) error("invalid group reference " + std::to_string(g));
                flush(); o.push_back({"", g}); continue;
            }
            switch (e) {
                case 'n': lit += '\n'; break; case 't': lit += '\t'; break; case 'r': lit += '\r'; break;
                case 'f': lit += '\f'; break; case 'v': lit += '\v'; break; case 'a': lit += '\a'; break; case 'b': lit += '\b'; break;
                case '\\': lit += '\\'; break;
                case '0': { uint32_t v = 0; int d = 0; while (d < 2 && k + 1 < r.size() && r[k + 1] >= '0' && r[k + 1] <= '7') { v = v * 8 + (r[++k] - '0'); d++; } lit += utf8(v); break; }
                default:
                    if (e < 128 && std::isalpha(static_cast<int>(e))) error(std::string("bad escape \\") + static_cast<char>(e));
                    lit += '\\'; lit += utf8(e);
            }
        }
        flush(); return o;
    }
};

// compiled patterns are cached per thread, as re caches them
inline const Regex& compile(const std::string& pattern, int flags = 0) {
    thread_local std::map<std::pair<std::string, int>, std::unique_ptr<Regex>> cache;
    auto& slot = cache[{pattern, flags}];
    if (!slot) slot = std::make_unique<Regex>(pattern, flags);
    return *slot;
}
inline Match search(const std::string& p, const std::string& s, int flags = 0) { return compile(p, flags).search(s); }
inline Match match(const std::string& p, const std::string& s, int flags = 0) { return compile(p, flags).match(s); }
inline Match fullmatch(const std::string& p, const std::string& s, int flags = 0) { return compile(p, flags).fullmatch(s); }
inline std::string sub(const std::string& p, const std::string& repl, const std::string& s, int count = 0, int flags = 0) { return compile(p, flags).sub(repl, s, count); }
inline std::vector<std::string> findall(const std::string& p, const std::string& s, int flags = 0) { return compile(p, flags).findall(s); }
inline std::vector<std::string> split(const std::string& p, const std::string& s, int maxsplit = 0, int flags = 0) { return compile(p, flags).split(s, maxsplit); }
inline std::string escape(const std::string& s) {   // re.escape (3.7+): only the special characters
    static const std::string special = "()[]{}?*+-|^$\\.&~# \t\n\r\v\f";
    std::string o; for (char c : s) { if (special.find(c) != std::string::npos) o += '\\'; o += c; } return o;
}
}  // namespace re

// ------------------------------------------------------------------ html.unescape()
namespace html_detail {
inline const html5::Ent* entity(const std::string& name) {
    const auto* b = std::begin(html5::ENTITIES); const auto* e = std::end(html5::ENTITIES);
    const auto* it = std::lower_bound(b, e, name, [](const html5::Ent& x, const std::string& n) { return std::strcmp(x.name, n.c_str()) < 0; });
    return it != e && name == it->name ? it : nullptr;
}
inline std::string ent_text(const html5::Ent* x) { std::string o = utf8(x->a); if (x->b) o += utf8(x->b); return o; }
inline bool invalid_cp(uint32_t n) {
    return (n >= 0x1 && n <= 0x8) || (n >= 0xE && n <= 0x1F) || (n >= 0x7F && n <= 0x9F) || (n >= 0xFDD0 && n <= 0xFDEF) || n == 0xB || (n & 0xFFFE) == 0xFFFE;
}
inline std::string charref(uint64_t num) {
    static const uint32_t win[32] = {0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
                                     0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178};
    if (num == 0) return utf8(0xFFFD);
    if (num == 0x0D) return "\r";
    if (num >= 0x80 && num <= 0x9F) return utf8(win[num - 0x80]);
    if ((num >= 0xD800 && num <= 0xDFFF) || num > 0x10FFFF) return utf8(0xFFFD);
    if (invalid_cp(static_cast<uint32_t>(num))) return "";
    return utf8(static_cast<uint32_t>(num));
}
}  // namespace html_detail
inline std::string html_unescape(const std::string& text) {
    if (text.find('&') == std::string::npos) return text;
    const U32 s = to_u32(text); std::string o; const std::size_t n = s.size();
    const auto isdig = [](uint32_t c) { return c >= '0' && c <= '9'; };
    const auto ishex = [](uint32_t c) { return (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'f'); };
    for (std::size_t i = 0; i < n;) {
        if (s[i] != '&') { o += utf8(s[i++]); continue; }
        std::size_t j = i + 1;
        if (j < n && s[j] == '#') {
            const bool hx = j + 1 < n && (s[j + 1] == 'x' || s[j + 1] == 'X');
            std::size_t d = j + (hx ? 2 : 1), e = d; uint64_t num = 0; bool big = false;
            while (e < n && (hx ? ishex(s[e]) : isdig(s[e]))) {
                const uint32_t c = s[e]; const uint64_t v = c <= '9' ? c - '0' : (c | 32) - 'a' + 10;
                if (!big) { num = num * (hx ? 16 : 10) + v; if (num > 0x10FFFF) big = true; }
                e++;
            }
            if (e > d) { if (e < n && s[e] == ';') e++; o += html_detail::charref(big ? 0x110000 : num); i = e; continue; }
            o += '&'; i++; continue;   // "&#" with no digits: the regex needs a name, and '#' cannot start one
        }
        // a name: 1 to 32 characters other than \t \n \f space < & # ;  then an optional ';'
        std::size_t e = j;
        while (e < n && e - j < 32 && s[e] != '\t' && s[e] != '\n' && s[e] != '\f' && s[e] != ' ' && s[e] != '<' && s[e] != '&' && s[e] != '#' && s[e] != ';') e++;
        if (e == j) { o += '&'; i++; continue; }
        if (e < n && s[e] == ';') e++;
        const std::string name = from_u32(s, j, e);
        if (const auto* x = html_detail::entity(name)) { o += html_detail::ent_text(x); i = e; continue; }
        const U32 nm(s.begin() + static_cast<long>(j), s.begin() + static_cast<long>(e));
        bool done = false;
        for (std::size_t len = nm.size() - 1; len > 1; --len) {   // the longest prefix that is a name (names without ';')
            if (const auto* x = html_detail::entity(from_u32(nm, 0, len))) { o += html_detail::ent_text(x) + from_u32(nm, len); done = true; break; }
        }
        if (!done) o += "&" + name;
        i = e;
    }
    return o;
}
}  // namespace pyrt
