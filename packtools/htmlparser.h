// htmlparser.h - html.parser.HTMLParser as in CPython 3.13 (Lib/html/parser.py and the parts of _markupbase.py it
// uses), with convert_charrefs=True: the same tokenizing, the same handler calls, the same getpos() line numbers.
// The module's regular expressions are written out as small matchers; the one place where the regex engine
// backtracks in a way that changes results (an unclosed quote after "= ") is reproduced in value_group().
// Positions are byte offsets into UTF-8 text; every pattern only tests ASCII characters, so spans are identical.
#pragma once
#include "pyre.h"
#include <optional>

namespace pyrt::htmlp {

struct Attr { std::string name; std::optional<std::string> value; };
using Attrs = std::vector<Attr>;

inline bool ws5(char c) { return c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == ' '; }
inline bool in_set(char c, const char* set) { return c != 0 && std::strchr(set, c) != nullptr; }
inline bool ascii_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
inline char ascii_lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

// _unescape_attrvalue: numeric references always; named ones only on an exact html5 match not followed by "="
inline std::string unescape_attrvalue(const std::string& s) {
    std::string o;
    std::size_t i = 0;
    const std::size_t n = s.size();
    auto digit = [](char c) { return c >= '0' && c <= '9'; };
    auto hexd = [&](char c) { return digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); };
    while (i < n) {
        if (s[i] != '&') { o += s[i++]; continue; }
        std::size_t j = i + 1;
        bool numeric = false;
        if (j < n && s[j] == '#') {
            if (j + 1 < n && digit(s[j + 1])) { j += 1; while (j < n && digit(s[j])) j++; numeric = true; }
            else if (j + 2 < n && (s[j + 1] == 'x' || s[j + 1] == 'X') && hexd(s[j + 2])) { j += 2; while (j < n && hexd(s[j])) j++; numeric = true; }
            else { o += s[i++]; continue; }
        } else if (j < n && ascii_alpha(s[j])) {
            while (j < n && (ascii_alpha(s[j]) || digit(s[j]))) j++;
        } else { o += s[i++]; continue; }
        if (j < n && (s[j] == ';' || s[j] == '=')) j++;
        const std::string ref = s.substr(i, j - i);
        if (numeric) o += html_unescape(ref);
        else if (ref.back() != '=' && html_detail::entity(ref.substr(1))) o += html_unescape(ref);
        else o += ref;
        i = j;
    }
    return o;
}

class HTMLParser {
public:
    virtual ~HTMLParser() = default;
    bool scripting = false;

    void feed(const std::string& data) {
        pending_len_ += data.size();
        if (pending_len_ < parse_threshold_) { pending_.push_back(data); return; }
        if (pending_.empty()) rawdata_ += data;
        else { pending_.push_back(data); for (const auto& p : pending_) rawdata_ += p; pending_.clear(); }
        pending_len_ = 0;
        const std::size_t n = rawdata_.size();
        goahead(false);
        parse_threshold_ = rawdata_.size() < n ? 1 : rawdata_.size();
    }
    void close() {
        if (!pending_.empty()) { for (const auto& p : pending_) rawdata_ += p; pending_.clear(); pending_len_ = 0; }
        goahead(true);
    }
    std::pair<int, int> getpos() const { return {lineno_, offset_}; }

protected:
    virtual void handle_starttag(const std::string&, const Attrs&) {}
    virtual void handle_startendtag(const std::string& tag, const Attrs& attrs) { handle_starttag(tag, attrs); handle_endtag(tag); }
    virtual void handle_endtag(const std::string&) {}
    virtual void handle_data(const std::string&) {}
    virtual void handle_comment(const std::string&) {}
    virtual void handle_decl(const std::string&) {}
    virtual void handle_pi(const std::string&) {}
    virtual void unknown_decl(const std::string&) {}

private:
    std::string rawdata_, cdata_elem_;
    bool escapable_ = true, support_cdata_ = true;
    std::vector<std::string> pending_;
    std::size_t pending_len_ = 0, parse_threshold_ = 1;
    int lineno_ = 1, offset_ = 0;
    static constexpr std::size_t NPOS = std::string::npos;

    static int cp_count(const std::string& s, std::size_t a, std::size_t b) {
        int k = 0;
        for (std::size_t i = a; i < b; ++i)
            if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) k++;
        return k;
    }
    std::size_t updatepos(std::size_t i, std::size_t j) {   // _markupbase.ParserBase.updatepos
        if (i >= j) return j;
        int nl = 0;
        std::size_t last = NPOS;
        for (std::size_t k = i; k < j; ++k)
            if (rawdata_[k] == '\n') { nl++; last = k; }
        if (nl) { lineno_ += nl; offset_ = cp_count(rawdata_, last + 1, j); }
        else offset_ += cp_count(rawdata_, i, j);
        return j;
    }
    bool starts_at(std::size_t i, const char* lit) const { return rawdata_.compare(i, std::strlen(lit), lit) == 0; }

    void set_cdata_mode(const std::string& elem, bool escapable) { cdata_elem_ = lower(elem); escapable_ = escapable; }
    void clear_cdata_mode() { cdata_elem_.clear(); escapable_ = true; }
    // self.interesting.search in CDATA/RCDATA mode: "</elem" (ASCII, any case) followed by [\t\n\r\f />]
    std::size_t find_cdata_end(std::size_t i) const {
        const std::size_t n = rawdata_.size();
        if (cdata_elem_ == "plaintext") return n;   // \Z
        const std::size_t L = cdata_elem_.size();
        for (std::size_t p = rawdata_.find("</", i); p != NPOS; p = rawdata_.find("</", p + 1)) {
            if (p + 2 + L >= n) return NPOS;
            bool same = true;
            for (std::size_t k = 0; k < L && same; ++k) same = ascii_lower(rawdata_[p + 2 + k]) == cdata_elem_[k];
            if (same && in_set(rawdata_[p + 2 + L], "\t\n\r\f />")) return p;
        }
        return NPOS;
    }

    // the "value indicator" group shared by attrfind_tolerant and locatetagend; returns the end, or NPOS if it does not match
    std::size_t value_group(std::size_t p, std::size_t& vstart, std::size_t& vend) const {
        const std::size_t n = rawdata_.size();
        std::size_t q = p;
        while (q < n && ws5(rawdata_[q])) q++;
        if (q >= n || rawdata_[q] != '=') return NPOS;
        q++;
        std::size_t r = q;
        while (r < n && ws5(rawdata_[r])) r++;
        auto alt = [&](std::size_t s, std::size_t& e) {
            if (s < n && (rawdata_[s] == '\'' || rawdata_[s] == '"')) {
                const std::size_t close = rawdata_.find(rawdata_[s], s + 1);
                if (close == NPOS) return false;
                e = close + 1; return true;
            }
            e = s;
            while (e < n && !in_set(rawdata_[e], ">\t\n\r\f ")) e++;
            return true;
        };
        std::size_t e = 0;
        if (alt(r, e)) { vstart = r; vend = e; return e; }
        if (r > q) { vstart = vend = r - 1; return r - 1; }   // backtrack one blank: an empty bare value
        return NPOS;
    }
    std::size_t locatetagend(std::size_t p) const {   // match at p (a letter); returns the end
        const std::size_t n = rawdata_.size();
        p++;
        while (p < n && !in_set(rawdata_[p], "\t\n\r\f />")) p++;
        while (p < n && in_set(rawdata_[p], "\t\n\r\f /")) p++;
        while (p < n && in_set(rawdata_[p - 1], "'\"\t\n\r\f /") && !in_set(rawdata_[p], "\t\n\r\f />")) {
            p++;
            while (p < n && !in_set(rawdata_[p], "\t\n\r\f /=>")) p++;
            std::size_t vs = 0, ve = 0;
            const std::size_t v = value_group(p, vs, ve);
            if (v != NPOS) p = v;
            while (p < n && in_set(rawdata_[p], "\t\n\r\f /")) p++;
        }
        if (p < n && rawdata_[p] == '>') p++;
        return p;
    }
    // (?:[\t\n\r\f ]|/(?!>))*
    std::size_t skip_tolerant(std::size_t p) const {
        const std::size_t n = rawdata_.size();
        while (p < n && (ws5(rawdata_[p]) || (rawdata_[p] == '/' && !(p + 1 < n && rawdata_[p + 1] == '>')))) p++;
        return p;
    }
    std::size_t tagfind(std::size_t p, std::string& name) const {   // tagfind_tolerant.match at p (a letter)
        const std::size_t n = rawdata_.size();
        std::size_t e = p + 1;
        while (e < n && !in_set(rawdata_[e], "\t\n\r\f />")) e++;
        name = rawdata_.substr(p, e - p);
        return skip_tolerant(e);
    }

    std::ptrdiff_t check_for_whole_start_tag(std::size_t i) const {
        const std::size_t j = locatetagend(i + 1);
        if (rawdata_[j - 1] != '>') return -1;
        return static_cast<std::ptrdiff_t>(j);
    }
    std::ptrdiff_t parse_starttag(std::size_t i) {
        const std::ptrdiff_t endp = check_for_whole_start_tag(i);
        if (endp < 0) return endp;
        const std::size_t endpos = static_cast<std::size_t>(endp);
        Attrs attrs;
        std::string rawtag;
        std::size_t k = tagfind(i + 1, rawtag);
        const std::string tag = lower(rawtag);
        while (k < endpos) {   // attrfind_tolerant
            if (!in_set(rawdata_[k - 1], "'\"\t\n\r\f /") || in_set(rawdata_[k], "\t\n\r\f />")) break;
            std::size_t e = k + 1;
            while (e < rawdata_.size() && !in_set(rawdata_[e], "\t\n\r\f /=>")) e++;
            Attr a; a.name = lower(rawdata_.substr(k, e - k));
            std::size_t vs = 0, ve = 0;
            const std::size_t v = value_group(e, vs, ve);
            if (v != NPOS) {
                std::string val = rawdata_.substr(vs, ve - vs);
                if (val.size() >= 2 && (val[0] == '\'' || val[0] == '"') && val.back() == val[0]) val = val.substr(1, val.size() - 2);
                else if (val.size() == 1 && (val[0] == '\'' || val[0] == '"')) val.clear();
                if (!val.empty()) val = unescape_attrvalue(val);
                a.value = val;
                e = v;
            }
            attrs.push_back(std::move(a));
            k = skip_tolerant(e);
        }
        const std::string end = strip(rawdata_.substr(k, endpos - k));
        if (end != ">" && end != "/>") { handle_data(rawdata_.substr(i, endpos - i)); return endp; }
        if (end == "/>") handle_startendtag(tag, attrs);
        else {
            handle_starttag(tag, attrs);
            static const char* const cdata[] = {"script", "style", "xmp", "iframe", "noembed", "noframes"};
            bool raw = tag == "plaintext" || (scripting && tag == "noscript");
            for (const char* c : cdata) raw = raw || tag == c;
            if (raw) set_cdata_mode(tag, false);
            else if (tag == "textarea" || tag == "title") set_cdata_mode(tag, true);
        }
        return endp;
    }
    std::ptrdiff_t parse_bogus_comment(std::size_t i) {
        const std::size_t pos = rawdata_.find('>', i + 2);
        if (pos == NPOS) return -1;
        handle_comment(rawdata_.substr(i + 2, pos - i - 2));
        return static_cast<std::ptrdiff_t>(pos + 1);
    }
    std::ptrdiff_t parse_endtag(std::size_t i) {
        if (rawdata_.find('>', i + 2) == NPOS) return -1;
        if (!(i + 2 < rawdata_.size() && ascii_alpha(rawdata_[i + 2]))) {
            if (i + 2 < rawdata_.size() && rawdata_[i + 2] == '>') return static_cast<std::ptrdiff_t>(i + 3);
            return parse_bogus_comment(i);
        }
        const std::size_t j = locatetagend(i + 2);
        if (rawdata_[j - 1] != '>') return -1;
        std::string name;
        tagfind(i + 2, name);
        handle_endtag(lower(name));
        clear_cdata_mode();
        return static_cast<std::ptrdiff_t>(j);
    }
    std::ptrdiff_t parse_comment(std::size_t i) {
        const std::size_t n = rawdata_.size();
        std::size_t ms = NPOS, me = NPOS;
        if (i + 4 < n && rawdata_[i + 4] == '>') { ms = i + 4; me = i + 5; }               // commentabruptclose: -?>
        else if (starts_at(i + 4, "->")) { ms = i + 4; me = i + 6; }
        else {
            for (std::size_t p = rawdata_.find("--", i + 4); p != NPOS; p = rawdata_.find("--", p + 1)) {   // commentclose: --!?>
                if (starts_at(p + 2, ">")) { ms = p; me = p + 3; break; }
                if (starts_at(p + 2, "!>")) { ms = p; me = p + 4; break; }
            }
            if (ms == NPOS) return -1;
        }
        handle_comment(rawdata_.substr(i + 4, ms - i - 4));
        return static_cast<std::ptrdiff_t>(me);
    }
    std::ptrdiff_t parse_pi(std::size_t i) {
        const std::size_t j = rawdata_.find('>', i + 2);
        if (j == NPOS) return -1;
        handle_pi(rawdata_.substr(i + 2, j - i - 2));
        return static_cast<std::ptrdiff_t>(j + 1);
    }
    bool doctype_at(std::size_t i) const {
        if (i + 9 > rawdata_.size()) return false;
        static const char* const d = "<!doctype";
        for (std::size_t k = 0; k < 9; ++k)
            if (ascii_lower(rawdata_[i + k]) != d[k]) return false;
        return true;
    }
    std::ptrdiff_t parse_html_declaration(std::size_t i) {
        if (starts_at(i, "<!--")) return parse_comment(i);
        if (starts_at(i, "<![CDATA[") && support_cdata_) {
            const std::size_t j = rawdata_.find("]]>", i + 9);
            if (j == NPOS) return -1;
            unknown_decl(rawdata_.substr(i + 3, j - i - 3));
            return static_cast<std::ptrdiff_t>(j + 3);
        }
        if (doctype_at(i)) {
            const std::size_t gt = rawdata_.find('>', i + 9);
            if (gt == NPOS) return -1;
            handle_decl(rawdata_.substr(i + 2, gt - i - 2));
            return static_cast<std::ptrdiff_t>(gt + 1);
        }
        return parse_bogus_comment(i);
    }
    void data(std::size_t i, std::size_t j) {
        const std::string t = rawdata_.substr(i, j - i);
        handle_data(escapable_ ? html_unescape(t) : t);
    }

    void goahead(bool end) {
        std::size_t i = 0;
        const std::size_t n = rawdata_.size();
        while (i < n) {
            std::size_t j;
            if (cdata_elem_.empty()) {
                j = rawdata_.find('<', i);
                if (j == NPOS) {
                    // a charref may be cut in half at the end: wait unless the last "&" is followed by a blank or ";"
                    const std::size_t lo = std::max(i, n >= 34 ? n - 34 : std::size_t{0});
                    const std::size_t amp = rawdata_.rfind('&');
                    if (amp != NPOS && amp >= lo && rawdata_.find_first_of("\t\n\r\f ;", amp) == NPOS) break;
                    j = n;
                }
            } else {
                j = find_cdata_end(i);
                if (j == NPOS) break;
            }
            if (i < j) data(i, j);
            i = updatepos(i, j);
            if (i == n) break;
            std::ptrdiff_t k;
            if (i + 1 < n && ascii_alpha(rawdata_[i + 1])) k = parse_starttag(i);
            else if (starts_at(i, "</")) k = parse_endtag(i);
            else if (starts_at(i, "<!--")) k = parse_comment(i);
            else if (starts_at(i, "<?")) k = parse_pi(i);
            else if (starts_at(i, "<!")) k = parse_html_declaration(i);
            else if (i + 1 < n || end) { handle_data("<"); k = static_cast<std::ptrdiff_t>(i + 1); }
            else break;
            if (k < 0) {
                if (!end) break;
                if (i + 1 < n && ascii_alpha(rawdata_[i + 1])) {
                    // an unfinished start tag is dropped
                } else if (starts_at(i, "</")) {
                    if (i + 2 == n) handle_data("</");
                    else if (!(i + 2 < n && ascii_alpha(rawdata_[i + 2]))) handle_comment(rawdata_.substr(i + 2));
                } else if (starts_at(i, "<!--")) {
                    std::size_t e = n;
                    for (const char* suffix : {"--!", "--", "-"}) {
                        const std::size_t L = std::strlen(suffix);
                        if (n >= i + 4 + L && rawdata_.compare(n - L, L, suffix) == 0) { e -= L; break; }
                    }
                    handle_comment(rawdata_.substr(i + 4, e - i - 4));
                } else if (starts_at(i, "<![CDATA[") && support_cdata_) {
                    unknown_decl(rawdata_.substr(i + 3));
                } else if (doctype_at(i)) {
                    handle_decl(rawdata_.substr(i + 2));
                } else if (starts_at(i, "<!")) {
                    handle_comment(rawdata_.substr(i + 2));
                } else if (starts_at(i, "<?")) {
                    handle_pi(rawdata_.substr(i + 2));
                }
                k = static_cast<std::ptrdiff_t>(n);
            }
            i = updatepos(i, static_cast<std::size_t>(k));
        }
        if (end && i < n) { data(i, n); i = updatepos(i, n); }
        rawdata_ = rawdata_.substr(i);
    }
};

}  // namespace pyrt::htmlp
