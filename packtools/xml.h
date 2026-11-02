// xml.h - a small XML reader with xml.etree.ElementTree semantics, for pack tools that read feeds (RSS, Atom,
// SEC filings). Elements carry ET-style tags ("{namespace-uri}local" or "local"), attributes, .text (character
// data before the first child, CDATA included) and .tail. Handles the XML declaration's encoding (UTF-8, US-ASCII,
// ISO-8859-1 / Latin-1, Windows-1252), comments, processing instructions, DOCTYPE, CDATA, character references and
// the five predefined entities. Errors read like expat's: "mismatched tag: line 3, column 2".
#pragma once
#include "pyrt.h"
#include <memory>
namespace pyrt::xml {
struct ParseError : std::runtime_error { using std::runtime_error::runtime_error; };
struct Node {
    std::string tag, text, tail;
    std::vector<std::pair<std::string, std::string>> attrib;
    std::vector<std::shared_ptr<Node>> kids;
    std::string get(const std::string& k, const std::string& dflt = "") const { for (const auto& [a, v] : attrib) if (a == k) return v; return dflt; }
    template <class F> void iter(F&& f) const { f(*this); for (const auto& k : kids) k->iter(f); }   // Element.iter(): self, then descendants
};
inline std::string local(const std::string& tag) { const auto k = tag.rfind('}'); return k == std::string::npos ? tag : tag.substr(k + 1); }
namespace detail {
inline std::string to_utf8(const std::string& data, const std::string& enc) {
    static const uint16_t CP1252[32] = {0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
                                        0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178};
    const bool latin1 = enc == "iso-8859-1" || enc == "latin-1" || enc == "latin1" || enc == "iso8859-1" || enc == "l1";
    const bool cp1252 = enc == "windows-1252" || enc == "cp1252";
    if (!latin1 && !cp1252) return data;
    std::string o; o.reserve(data.size() + data.size() / 8);
    for (unsigned char c : data) { if (c < 0x80) o += static_cast<char>(c); else o += utf8(cp1252 && c < 0xA0 ? CP1252[c - 0x80] : c); }
    return o;
}
struct P {
    const std::string& s; std::size_t i = 0;
    std::vector<std::vector<std::pair<std::string, std::string>>> ns;   // prefix -> uri scopes ("" = default)
    explicit P(const std::string& src) : s(src) {}
    [[noreturn]] void fail(const std::string& what, std::size_t at) const {
        long line = 1, col = 0;
        for (std::size_t k = 0; k < at && k < s.size(); ++k) { if (s[k] == '\n') { line++; col = 0; } else if ((static_cast<unsigned char>(s[k]) & 0xC0) != 0x80) col++; }
        throw ParseError(what + ": line " + std::to_string(line) + ", column " + std::to_string(col));
    }
    bool at(const char* lit) const { return s.compare(i, std::strlen(lit), lit) == 0; }
    static bool ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
    static bool name_start(unsigned char c) { return std::isalpha(c) || c == '_' || c == ':' || c >= 0x80; }
    static bool name_char(unsigned char c) { return name_start(c) || std::isdigit(c) || c == '-' || c == '.'; }
    void skip_ws() { while (i < s.size() && ws(s[i])) i++; }
    std::string name() {
        if (i >= s.size() || !name_start(static_cast<unsigned char>(s[i]))) fail("not well-formed (invalid token)", i);
        const std::size_t a = i; while (i < s.size() && name_char(static_cast<unsigned char>(s[i]))) i++; return s.substr(a, i - a);
    }
    // character data with references; stop at '<' (text) or the quote (attribute)
    std::string chars(char stop, bool attr) {
        std::string o;
        while (i < s.size() && s[i] != stop) {
            const char c = s[i];
            if (c == '<' && attr) fail("not well-formed (invalid token)", i);
            if (c == '&') {
                const std::size_t a = i; const std::size_t e = s.find(';', i);
                if (e == std::string::npos || e - i > 12) fail("not well-formed (invalid token)", a);
                const std::string ent = s.substr(i + 1, e - i - 1); i = e + 1;
                if (ent == "amp") o += '&'; else if (ent == "lt") o += '<'; else if (ent == "gt") o += '>'; else if (ent == "quot") o += '"'; else if (ent == "apos") o += '\'';
                else if (!ent.empty() && ent[0] == '#') {
                    const bool hex = ent.size() > 1 && ent[1] == 'x'; const std::string digits = ent.substr(hex ? 2 : 1);
                    if (digits.empty() || digits.find_first_not_of(hex ? "0123456789abcdefABCDEF" : "0123456789") != std::string::npos) fail("not well-formed (invalid token)", a);
                    const unsigned long v = std::stoul(digits, nullptr, hex ? 16 : 10);
                    if (v == 0 || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF) || (v < 0x20 && v != 9 && v != 10 && v != 13)) fail("reference to invalid character number", a);
                    o += utf8(static_cast<uint32_t>(v));
                } else fail("undefined entity", a);
                continue;
            }
            if (c == '\r') { o += attr ? ' ' : '\n'; i++; if (i < s.size() && s[i] == '\n') i++; continue; }   // line-end normalization
            if (attr && (c == '\n' || c == '\t')) { o += ' '; i++; continue; }                                 // attribute-value normalization
            o += c; i++;
        }
        return o;
    }
    std::string resolve(const std::string& qn, bool element, std::size_t pos) {
        const auto k = qn.find(':');
        const std::string pre = k == std::string::npos ? "" : qn.substr(0, k), loc = k == std::string::npos ? qn : qn.substr(k + 1);
        if (pre.empty() && !element) return qn;
        for (auto sc = ns.rbegin(); sc != ns.rend(); ++sc) for (const auto& [p, u] : *sc) if (p == pre) return u.empty() ? loc : "{" + u + "}" + loc;
        if (!pre.empty() && pre != "xml") fail("unbound prefix", pos);
        return pre == "xml" ? "{http://www.w3.org/XML/1998/namespace}" + loc : loc;
    }
    void misc() {   // comments, PIs, DOCTYPE, whitespace outside the root
        for (;;) {
            skip_ws();
            if (at("<!--")) { const auto e = s.find("-->", i + 4); if (e == std::string::npos) fail("unclosed token", i); i = e + 3; }
            else if (at("<?")) { const auto e = s.find("?>", i + 2); if (e == std::string::npos) fail("unclosed token", i); i = e + 2; }
            else if (at("<!DOCTYPE")) {
                int depth = 0; const std::size_t a = i;
                for (; i < s.size(); ++i) { if (s[i] == '[') depth++; else if (s[i] == ']') depth--; else if (s[i] == '>' && depth <= 0) break; }
                if (i >= s.size()) fail("unclosed token", a);
                i++;
            } else return;
        }
    }
    std::shared_ptr<Node> element() {
        const std::size_t start = i; i++;   // '<'
        const std::string qn = name();
        std::vector<std::pair<std::string, std::string>> raw, scope;
        for (;;) {
            const bool had_ws = i < s.size() && ws(s[i]); skip_ws();
            if (i >= s.size()) fail("unclosed token", start);
            if (s[i] == '>' || at("/>")) break;
            if (!had_ws) fail("not well-formed (invalid token)", i);
            const std::size_t apos = i; const std::string an = name(); skip_ws();
            if (i >= s.size() || s[i] != '=') fail("not well-formed (invalid token)", i);
            i++; skip_ws();
            if (i >= s.size() || (s[i] != '"' && s[i] != '\'')) fail("not well-formed (invalid token)", i);
            const char q = s[i++]; std::string v = chars(q, true);
            if (i >= s.size()) fail("unclosed token", start);
            i++;
            for (const auto& [n, _] : raw) if (n == an) fail("duplicate attribute", apos);
            raw.push_back({an, v});
            if (an == "xmlns") scope.push_back({"", v}); else if (starts(an, "xmlns:")) scope.push_back({an.substr(6), v});
        }
        ns.push_back(scope);
        auto n = std::make_shared<Node>();
        n->tag = resolve(qn, true, start);
        for (const auto& [an, v] : raw) if (an != "xmlns" && !starts(an, "xmlns:")) n->attrib.push_back({resolve(an, false, start), v});
        if (at("/>")) { i += 2; ns.pop_back(); return n; }
        i++;   // '>'
        std::string* sink = &n->text;
        for (;;) {
            *sink += chars('<', false);
            if (i >= s.size()) fail("no element found", s.size());
            if (at("</")) {
                const std::size_t cpos = i + 2; i += 2; const std::string cn = name(); skip_ws();   /* expat points at the name after "</" */
                if (cn != qn || i >= s.size() || s[i] != '>') fail("mismatched tag", cpos);
                i++; break;
            }
            if (at("<!--")) { const auto e = s.find("-->", i + 4); if (e == std::string::npos) fail("unclosed token", i); i = e + 3; continue; }
            if (at("<![CDATA[")) { const auto e = s.find("]]>", i + 9); if (e == std::string::npos) fail("unclosed CDATA section", i); *sink += s.substr(i + 9, e - i - 9); i = e + 3; continue; }
            if (at("<?")) { const auto e = s.find("?>", i + 2); if (e == std::string::npos) fail("unclosed token", i); i = e + 2; continue; }
            auto kid = element(); n->kids.push_back(kid); sink = &kid->tail;
        }
        ns.pop_back();
        return n;
    }
};
}  // namespace detail
// ET.fromstring(bytes)
inline std::shared_ptr<Node> fromstring(const std::string& bytes) {
    std::string data = bytes;
    if (starts(data, "\xEF\xBB\xBF")) data = data.substr(3);
    if (starts(data, "<?xml")) {
        const auto e = data.find("?>"); const std::string decl = data.substr(0, e == std::string::npos ? 0 : e);
        const auto k = decl.find("encoding");
        if (k != std::string::npos) { const auto q = decl.find_first_of("\"'", k); if (q != std::string::npos) { const auto q2 = decl.find(decl[q], q + 1); if (q2 != std::string::npos) data = detail::to_utf8(data, lower(decl.substr(q + 1, q2 - q - 1))); } }
    }
    detail::P p(data);
    p.misc();
    if (p.i >= data.size()) p.fail("no element found", data.size());
    if (data[p.i] != '<') p.fail("syntax error", p.i);
    auto root = p.element();
    p.misc();
    if (p.i < data.size()) p.fail("junk after document element", p.i);
    return root;
}
}  // namespace pyrt::xml
