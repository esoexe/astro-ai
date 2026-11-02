// seed-closer, part 1: the pieces of Python's standard library that closer.py leans on, in C++.
//   - str.format_map with string.Formatter's parser (CPython's MarkupIterator / parse_field) and format specs
//   - email.message.EmailMessage(policy=SMTP): headers folded by _refold_parse_tree, RFC 2047 encoded words
//     (_fold_as_ew, _ew.encode), set_content(text, charset="utf-8", cte="7bit" | "quoted-printable") and as_bytes()
//   - email.message_from_bytes(policy=default): header unfolding, address lists, encoded words, the body by its
//     Content-Transfer-Encoding and charset (msg.get_content())
//   - email.utils.parseaddr, for matching a reply's From to the pipeline
// What is built is what the Closer writes: one From and one To address, an unstructured Subject, a Message-ID and the
// In-Reply-To / References pair. Display names and subjects in any script fold the way Python folds them; a subject
// holding a literal "=?...?=" is kept as text where Python would decode it first (no real subject does that).
#include <deque>
#include <optional>
namespace packtools::seed_closer {
using namespace pyrt;
using namespace packtools::seed;

// ================================================================ str.format_map

struct FmtPiece { std::string literal; bool field = false; std::string name, spec, conv; bool has_conv = false, spec_nested = false; };

inline std::size_t u8len(unsigned char c) { return c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4; }

// _string.formatter_parser: the literal text, then the field (name, format spec, conversion); ValueError like CPython
inline std::vector<FmtPiece> fmt_parse(const std::string& s) {
    std::vector<FmtPiece> out;
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n) {
        const std::size_t start = i; char c = 0; bool markup = false;
        while (i < n) { c = s[i++]; if (c == '{' || c == '}') { markup = true; break; } }
        const bool at_end = i >= n;
        std::size_t len = i - start;
        if (markup && c == '}' && (at_end || s[i] != '}')) throw PyErr("ValueError", "Single '}' encountered in format string");
        if (markup && at_end && c == '{') throw PyErr("ValueError", "Single '{' encountered in format string");
        if (markup && !at_end) { if (s[i] == c) { ++i; markup = false; } else --len; }
        FmtPiece p; p.literal = s.substr(start, len);
        if (!markup) { out.push_back(p); continue; }
        p.field = true;
        const std::size_t fstart = i; char last = 0;
        while (i < n) {
            last = s[i++];
            if (last == '{') throw PyErr("ValueError", "unexpected '{' in field name");
            if (last == '[') { while (i < n && s[i] != ']') ++i; continue; }
            if (last == '}' || last == ':' || last == '!') break;
        }
        p.name = s.substr(fstart, i - 1 - fstart);
        if (last == '!' || last == ':') {
            if (last == '!') {
                if (i >= n) throw PyErr("ValueError", "end of string while looking for conversion specifier");
                const std::size_t cl = std::min(u8len(static_cast<unsigned char>(s[i])), n - i);
                p.conv = s.substr(i, cl); p.has_conv = true; i += cl;
                if (i < n) {
                    const char c2 = s[i++];
                    if (c2 == '}') { out.push_back(p); continue; }
                    if (c2 != ':') throw PyErr("ValueError", "expected ':' after conversion specifier");
                }
            }
            const std::size_t sstart = i; int count = 1; bool done = false;
            while (i < n) {
                const char c3 = s[i++];
                if (c3 == '{') { p.spec_nested = true; ++count; }
                else if (c3 == '}' && --count == 0) { p.spec = s.substr(sstart, i - 1 - sstart); done = true; break; }
            }
            if (!done) throw PyErr("ValueError", "unmatched '{' in format spec");
            out.push_back(p);
            continue;
        }
        if (last != '}') throw PyErr("ValueError", "expected '}' before end of string");
        out.push_back(p);
    }
    return out;
}

inline std::string char_name(uint32_t c) {   // %c for printable ASCII, else \x..
    if (c > 32 && c < 127) return std::string(1, static_cast<char>(c));
    char b[16]; std::snprintf(b, sizeof b, "\\x%x", static_cast<unsigned>(c)); return b;
}
inline std::string ascii_of(const std::string& r) {   // ascii(): repr with the non-ASCII escaped
    std::string o;
    for (const auto& ch : cps(r)) {
        const uint32_t c = cp_of(ch); char b[16];
        if (c < 0x80) o += ch;
        else if (c < 0x100) { std::snprintf(b, sizeof b, "\\x%02x", static_cast<unsigned>(c)); o += b; }
        else if (c < 0x10000) { std::snprintf(b, sizeof b, "\\u%04x", static_cast<unsigned>(c)); o += b; }
        else { std::snprintf(b, sizeof b, "\\U%08x", static_cast<unsigned>(c)); o += b; }
    }
    return o;
}

// ---------------------------------------------------------------- format(value, spec)
struct Spec { std::string fill = " "; char align = 0; char sign = 0; bool z = false, alt = false, fill_set = false, align_set = false;
              long long width = -1, prec = -1; char sep = 0; uint32_t type = 0; };
inline bool is_align(uint32_t c) { return c == '<' || c == '>' || c == '=' || c == '^'; }
inline long long spec_int(const std::vector<std::string>& v, std::size_t& k, bool& any) {
    long long x = 0; any = false;
    while (k < v.size()) {
        const std::string d = ascii_digits(v[k]);
        if (d.size() != 1 || d[0] < '0' || d[0] > '9') break;
        if (x > (static_cast<long long>(1) << 62) / 10) throw PyErr("ValueError", "Too many decimal digits in format string");
        x = x * 10 + (d[0] - '0'); any = true; ++k;
    }
    return x;
}
inline Spec parse_spec(const std::string& spec, uint32_t default_type, char default_align, const std::string& tname) {
    Spec f; f.align = default_align; f.type = default_type;
    const std::vector<std::string> v = cps(spec);
    std::size_t k = 0;
    auto at = [&](std::size_t j) -> uint32_t { return j < v.size() ? cp_of(v[j]) : 0; };
    if (v.size() >= 2 && is_align(at(1))) { f.align = static_cast<char>(at(1)); f.fill = v[0]; f.fill_set = f.align_set = true; k = 2; }
    else if (!v.empty() && is_align(at(0))) { f.align = static_cast<char>(at(0)); f.align_set = true; k = 1; }
    if (k < v.size() && (at(k) == '+' || at(k) == '-' || at(k) == ' ')) f.sign = static_cast<char>(at(k++));
    if (k < v.size() && at(k) == 'z') { f.z = true; ++k; }
    if (k < v.size() && at(k) == '#') { f.alt = true; ++k; }
    if (!f.fill_set && k < v.size() && at(k) == '0') { f.fill = "0"; if (!f.align_set && default_align == '>') f.align = '='; ++k; }
    bool any = false;
    f.width = spec_int(v, k, any); if (!any) f.width = -1;
    if (k < v.size() && at(k) == ',') { f.sep = ','; ++k; }
    if (k < v.size() && at(k) == '_') { if (f.sep) throw PyErr("ValueError", "Cannot specify both ',' and '_'."); f.sep = '_'; ++k; }
    if (k < v.size() && at(k) == ',' && f.sep == '_') throw PyErr("ValueError", "Cannot specify both ',' and '_'.");
    if (k < v.size() && at(k) == '.') {
        ++k; f.prec = spec_int(v, k, any);
        if (!any) throw PyErr("ValueError", "Format specifier missing precision");
    }
    if (v.size() - k > 1) throw PyErr("ValueError", "Invalid format specifier '" + spec + "' for object of type '" + tname + "'");
    if (v.size() - k == 1) f.type = at(k);
    if (f.sep) {
        const uint32_t t = f.type;
        const bool ok = t == 'd' || t == 'e' || t == 'f' || t == 'g' || t == 'E' || t == 'G' || t == '%' || t == 'F' || t == 0 ||
                        ((t == 'b' || t == 'o' || t == 'x' || t == 'X') && f.sep == '_');
        if (!ok) throw PyErr("ValueError", std::string("Cannot specify '") + f.sep + "' with '" + char_name(t) + "'.");
    }
    return f;
}
inline std::string pad(const std::string& body, const Spec& f, char default_align) {
    const long long len = static_cast<long long>(cplen(body));
    const long long total = f.width > len ? f.width : len;
    const char al = f.align ? f.align : default_align;
    long long l = 0;
    if (al == '>') l = total - len; else if (al == '^') l = (total - len) / 2;
    std::string o;
    for (long long j = 0; j < l; ++j) o += f.fill;
    o += body;
    for (long long j = 0; j < total - len - l; ++j) o += f.fill;
    return o;
}
inline std::string group_digits(const std::string& d, char sep, int every) {
    if (!sep) return d;
    std::string o; const std::size_t n = d.size();
    for (std::size_t j = 0; j < n; ++j) { if (j && (n - j) % static_cast<std::size_t>(every) == 0) o += sep; o += d[j]; }
    return o;
}
// sign + prefix, the integer digits (grouped), the rest; padded, with '=' padding after the sign
inline std::string num_layout(bool neg, const std::string& prefix, const std::string& digits, const std::string& rest, const Spec& f, int every) {
    std::string sg = neg ? "-" : f.sign == '+' ? "+" : f.sign == ' ' ? " " : "";
    std::string head = sg + prefix;
    if (f.align == '=') {
        std::string d = digits;
        if (f.fill == "0" && f.sep) {   // zero padding is grouped too
            while (static_cast<long long>(cplen(head + group_digits(d, f.sep, every) + rest)) < f.width) d = "0" + d;
            return head + group_digits(d, f.sep, every) + rest;
        }
        const std::string body = group_digits(d, f.sep, every) + rest;
        const long long len = static_cast<long long>(cplen(head + body));
        std::string o = head;
        for (long long j = len; j < f.width; ++j) o += f.fill;
        return o + body;
    }
    return pad(head + group_digits(digits, f.sep, every) + rest, f, '>');
}
inline std::string format_float(double x, const Spec& f0, bool from_int) {
    Spec f = f0;
    uint32_t t = f.type;
    if (t != 0 && t != 'e' && t != 'E' && t != 'f' && t != 'F' && t != 'g' && t != 'G' && t != 'n' && t != '%')
        throw PyErr("ValueError", "Unknown format code '" + char_name(t) + "' for object of type '" + std::string(from_int ? "int" : "float") + "'");
    bool add_dot0 = false, repr_mode = false;
    long long prec = f.prec;
    if (t == 0) { add_dot0 = true; repr_mode = prec < 0; if (!repr_mode) t = 'g'; }
    if (t == 'n') t = 'g';
    std::string suffix;
    if (t == '%') { x *= 100; t = 'f'; suffix = "%"; }
    if (prec < 0) prec = 6;
    bool neg = std::signbit(x);
    std::string s;
    const double ax = std::fabs(x);
    const bool upper = t == 'E' || t == 'F' || t == 'G';
    if (std::isnan(x)) s = upper ? "NAN" : "nan";
    else if (std::isinf(x)) s = upper ? "INF" : "inf";
    else if (repr_mode) s = float_repr(ax);
    else if (t == 'f' || t == 'F') s = fmt_f(ax, static_cast<int>(prec));
    else if (t == 'e' || t == 'E') s = fmt_e(ax, static_cast<int>(prec));
    else s = fmt_g(ax, static_cast<int>(prec == 0 ? 1 : prec), f.alt);
    if (upper) s = pyrt::upper(s);
    if (add_dot0 && !repr_mode && s.find_first_of(".eEna") == std::string::npos && !std::isinf(x)) s += ".0";
    if (f.alt && (t == 'f' || t == 'F' || t == 'e' || t == 'E') && s.find('.') == std::string::npos && std::isfinite(x)) {
        const std::size_t e = s.find_first_of("eE");
        if (e == std::string::npos) s += "."; else s.insert(e, ".");
    }
    if (f.z && neg && s.find_first_not_of("0.eE+-") == std::string::npos) neg = false;   // z: no negative zero after rounding
    if (std::isnan(x)) neg = false;
    std::size_t intlen = 0; while (intlen < s.size() && s[intlen] >= '0' && s[intlen] <= '9') ++intlen;
    return num_layout(neg, "", s.substr(0, intlen), s.substr(intlen) + suffix, f, 3);
}
inline std::string format_int(const BigInt& n, const Spec& f, const std::string& tname = "int") {
    const uint32_t t = f.type == 0 ? 'd' : f.type;
    if (t == 'e' || t == 'E' || t == 'f' || t == 'F' || t == 'g' || t == 'G' || t == '%') {
        if (n.abs().str().size() > 308) throw PyErr("OverflowError", "int too large to convert to float");
        return format_float(std::stod((n.negative() ? "-" : "") + n.abs().str()), f, true);
    }
    if (t != 'b' && t != 'c' && t != 'd' && t != 'o' && t != 'x' && t != 'X' && t != 'n')
        throw PyErr("ValueError", "Unknown format code '" + char_name(t) + "' for object of type '" + tname + "'");
    if (f.prec != -1) throw PyErr("ValueError", "Precision not allowed in integer format specifier");
    if (f.z) throw PyErr("ValueError", "Negative zero coercion (z) not allowed in integer format specifier");
    if (t == 'c') {
        if (f.sign) throw PyErr("ValueError", "Sign not allowed with integer format specifier 'c'");
        if (f.alt) throw PyErr("ValueError", "Alternate form (#) not allowed with integer format specifier 'c'");
        if (n.negative() || n.abs().str().size() > 7 || std::stoll(n.abs().str()) > 0x10FFFF) throw PyErr("OverflowError", "%c arg not in range(0x110000)");
        return pad(utf8(static_cast<uint32_t>(std::stoll(n.abs().str()))), f, '<');
    }
    std::string digits = n.abs().str(), prefix; int every = 3;
    if (t == 'b' || t == 'o' || t == 'x' || t == 'X') {
        const int base = t == 'b' ? 2 : t == 'o' ? 8 : 16;
        std::string dec = n.abs().str(), o;
        while (dec != "0" && !dec.empty()) {   // repeated division of the decimal digits
            std::string q; int r = 0;
            for (char ch : dec) { r = r * 10 + (ch - '0'); const int qd = r / base; r %= base; if (!q.empty() || qd) q += static_cast<char>('0' + qd); }
            o += "0123456789abcdef"[r];
            dec = q.empty() ? "0" : q;
        }
        if (o.empty()) o = "0";
        std::reverse(o.begin(), o.end());
        if (t == 'X') o = pyrt::upper(o);
        digits = o; every = 4;
        if (f.alt) prefix = t == 'b' ? "0b" : t == 'o' ? "0o" : t == 'x' ? "0x" : "0X";
    }
    return num_layout(n.negative(), prefix, digits, "", f, every);
}
inline std::string format_value(const Val& v, const std::string& spec) {
    if (spec.empty()) return pystr(v);
    switch (v.t) {
        case Val::STR: {
            const Spec f = parse_spec(spec, 's', '<', "str");
            if (f.type != 's') throw PyErr("ValueError", "Unknown format code '" + char_name(f.type) + "' for object of type 'str'");
            if (f.sign == ' ') throw PyErr("ValueError", "Space not allowed in string format specifier");
            if (f.sign) throw PyErr("ValueError", "Sign not allowed in string format specifier");
            if (f.z) throw PyErr("ValueError", "Negative zero coercion (z) not allowed in string format specifier");
            if (f.alt) throw PyErr("ValueError", "Alternate form (#) not allowed in string format specifier");
            if (f.align == '=') throw PyErr("ValueError", "'=' alignment not allowed in string format specifier");
            std::string s = v.s;
            if (f.prec >= 0 && static_cast<long long>(cplen(s)) > f.prec) s = cpslice(s, 0, static_cast<long>(f.prec));
            return pad(s, f, '<');
        }
        case Val::INT: return format_int(big_of(v), parse_spec(spec, 'd', '>', "int"));
        case Val::BOOL: return format_int(BigInt(v.b ? 1 : 0), parse_spec(spec, 'd', '>', "bool"), "bool");
        case Val::FLOAT: return format_float(v.f, parse_spec(spec, 0, '>', "float"), false);
        default: break;
    }
    throw PyErr("TypeError", std::string("unsupported format string passed to ") + (v.t == Val::NONE ? "NoneType" : type_name(v)) + ".__format__");
}

// str.format_map(values): every field a key of values (positional fields are an error), recursion depth 2
inline std::string format_map(const std::string& text, const Val& values, int depth = 2) {
    if (depth <= 0) throw PyErr("ValueError", "Max string recursion exceeded");
    std::string o;
    for (const FmtPiece& p : fmt_parse(text)) {
        o += p.literal;
        if (!p.field) continue;
        std::string first = p.name; std::string rest;
        const std::size_t cut = first.find_first_of(".[");
        if (cut != std::string::npos) { rest = first.substr(cut); first = first.substr(0, cut); }
        if (first.empty() || std::all_of(first.begin(), first.end(), [](char ch) { return ch >= '0' && ch <= '9'; }))
            throw PyErr("ValueError", "Format string contains positional fields");
        const Val* got = values.t == Val::DICT ? values.d->get(first) : nullptr;
        if (!got) throw PyErr("KeyError", str_repr(first));
        Val obj = *got;
        if (!rest.empty()) {
            if (rest[0] == '.') throw PyErr("AttributeError", std::string("'") + type_name(obj) + "' object has no attribute '" + rest.substr(1, rest.find_first_of(".[", 1) - 1) + "'");
            throw PyErr("TypeError", std::string("'") + type_name(obj) + "' object is not subscriptable");
        }
        if (p.has_conv) {
            const uint32_t c = cp_of(p.conv);
            if (c == 'r') obj = sv(repr(obj));
            else if (c == 's') obj = sv(pystr(obj));
            else if (c == 'a') obj = sv(ascii_of(repr(obj)));
            else throw PyErr("ValueError", "Unknown conversion specifier " + char_name(c));
        }
        const std::string spec = p.spec_nested ? format_map(p.spec, values, depth - 1) : p.spec;
        o += format_value(obj, spec);
    }
    return o;
}

// ================================================================ email: writing

inline const std::string SPECIALS = "()<>@,:;.\"[]";
inline bool any_of_chars(const std::string& s, const std::string& set) { return s.find_first_of(set) != std::string::npos; }
inline bool is_ascii(const std::string& s) { for (unsigned char c : s) if (c >= 0x80) return false; return true; }
inline std::string quote_string(const std::string& s) { return "\"" + replace_all(replace_all(s, "\\", "\\\\"), "\"", "\\\"") + "\""; }

inline std::string b64(const std::string& in) {
    static const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o; std::size_t k = 0;
    for (; k + 2 < in.size(); k += 3) {
        const unsigned v = (static_cast<unsigned char>(in[k]) << 16) | (static_cast<unsigned char>(in[k + 1]) << 8) | static_cast<unsigned char>(in[k + 2]);
        o += A[v >> 18]; o += A[(v >> 12) & 63]; o += A[(v >> 6) & 63]; o += A[v & 63];
    }
    if (k + 1 == in.size()) { const unsigned v = static_cast<unsigned char>(in[k]) << 16; o += A[v >> 18]; o += A[(v >> 12) & 63]; o += "=="; }
    else if (k + 2 == in.size()) { const unsigned v = (static_cast<unsigned char>(in[k]) << 16) | (static_cast<unsigned char>(in[k + 1]) << 8); o += A[v >> 18]; o += A[(v >> 12) & 63]; o += A[(v >> 6) & 63]; o += '='; }
    return o;
}
inline std::string q_byte(unsigned char b) {   // email._encoded_words._q_byte_map
    if ((b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') || (b >= '0' && b <= '9') || b == '-' || b == '!' || b == '*' || b == '+' || b == '/')
        return std::string(1, static_cast<char>(b));
    if (b == ' ') return "_";
    char h[4]; std::snprintf(h, sizeof h, "=%02X", b); return h;
}
inline std::string ew_encode(const std::string& text, const std::string& charset = "utf-8") {   // _ew.encode: q unless b is 5+ shorter
    long long qlen = 0; for (unsigned char b : text) qlen += static_cast<long long>(q_byte(b).size());
    const long long blen = static_cast<long long>((text.size() + 2) / 3 * 4);
    std::string body;
    const bool q = qlen - blen < 5;
    if (q) for (unsigned char b : text) body += q_byte(b); else body = b64(text);
    return "=?" + charset + "?" + (q ? "q" : "b") + "?" + body + "?=";
}

// ---------------------------------------------------------------- decoding encoded words (get_unstructured)
inline int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
inline std::string latin1_to_utf8(const std::string& b) { std::string o; for (unsigned char c : b) o += utf8(c); return o; }
inline std::string norm_charset(std::string cs) {
    cs = lower(cs); for (char& c : cs) if (c == '-' || c == ' ') c = '_';
    if (cs == "utf_8" || cs == "utf8" || cs == "u8" || cs == "utf" || cs == "utf8_ucs2" || cs == "utf8_ucs4") return "utf-8";
    if (cs == "us_ascii" || cs == "ascii" || cs == "646" || cs == "ansi_x3.4_1968" || cs == "us" || cs == "iso646_us") return "ascii";
    if (cs == "latin_1" || cs == "latin1" || cs == "iso_8859_1" || cs == "iso8859_1" || cs == "8859" || cs == "cp819" || cs == "l1" || cs == "latin" ||
        cs == "iso_ir_100" || cs == "csisolatin1" || cs == "iso_8859_1:1987") return "latin-1";
    return "";
}
inline std::optional<std::string> b64_decode(const std::string& s) {   // _ew.decode_b: lenient, padding repaired
    std::string clean; for (char c : s) if (std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '/') clean += c;
    if (clean.size() % 4 == 1) return std::nullopt;
    std::string o; unsigned v = 0; int bits = 0;
    for (char c : clean) {
        const int d = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : 63;
        v = (v << 6) | static_cast<unsigned>(d); bits += 6;
        if (bits >= 8) { bits -= 8; o += static_cast<char>((v >> bits) & 0xFF); }
    }
    return o;
}
// '=?charset?q|b?text?=' -> raw bytes in UTF-8 terms (latin-1 converted); nullopt when it is not a valid encoded word
inline std::optional<std::string> ew_decode(const std::string& ew) {
    const std::vector<std::string> parts = split(ew, "?");
    if (parts.size() != 5 || parts[0] != "=" || parts[4] != "=") return std::nullopt;
    std::string cs = parts[1]; const std::size_t star = cs.find('*'); if (star != std::string::npos) cs = cs.substr(0, star);
    const std::string cte = lower(parts[2]);
    std::string bytes;
    if (cte == "q") {
        const std::string t = replace_all(parts[3], "_", " ");
        for (std::size_t k = 0; k < t.size(); ++k) {
            if (t[k] == '=' && k + 2 < t.size() && hexv(t[k + 1]) >= 0 && hexv(t[k + 2]) >= 0) {
                bytes += static_cast<char>(hexv(t[k + 1]) * 16 + hexv(t[k + 2])); k += 2;
            } else bytes += t[k];
        }
    } else if (cte == "b") {
        const auto d = b64_decode(parts[3]); if (!d) return std::nullopt; bytes = *d;
    } else return std::nullopt;
    return norm_charset(cs) == "latin-1" ? latin1_to_utf8(bytes) : bytes;
}
inline bool wsp(char c) { return c == ' ' || c == '\t'; }
// str(get_unstructured(value)): encoded words decoded, the blank between two of them dropped; raw UTF-8 bytes in,
// the decoded text out (invalid bytes become U+FFFD, as utils._sanitize does)
inline std::string decode_unstructured(const std::string& value) {
    static const char* const EW_RE = R"(=\?[^?]*\?[qQbB]\?.*?\?=)";
    struct T { std::string text; int kind; };   // 0 vtext, 1 fws, 2 encoded word
    std::vector<T> toks; std::string v = value; bool valid_ew = true;
    while (!v.empty()) {
        if (wsp(v[0])) {   // get_fws: the leading whitespace (str.lstrip: any Unicode space)
            const auto c = cps(v); std::size_t k = 0, bytes = 0;
            while (k < c.size() && py_space(cp_of(c[k]))) { bytes += c[k].size(); ++k; }
            toks.push_back({v.substr(0, bytes), 1}); v = v.substr(bytes); continue;
        }
        if (v.compare(0, 2, "=?") == 0) {   // get_encoded_word
            const std::string body = v.substr(2);
            const std::size_t e = body.find("?=");
            if (e != std::string::npos) {
                std::string tok = body.substr(0, e), remstr = body.substr(e + 2);
                if (remstr.size() > 1 && hexv(remstr[0]) >= 0 && hexv(remstr[1]) >= 0 && std::count(tok.begin(), tok.end(), '?') < 2) {
                    const std::size_t e2 = remstr.find("?=");
                    tok += "?=" + remstr.substr(0, e2);
                    remstr = e2 == std::string::npos ? std::string() : remstr.substr(e2 + 2);
                }
                const auto dec = ew_decode("=?" + tok + "?=");
                if (dec) {
                    const bool have_ws = toks.empty() || toks.back().kind == 1;
                    if (have_ws && toks.size() > 1 && toks[toks.size() - 2].kind == 2) toks.back().text.clear();
                    toks.push_back({*dec, 2}); v = remstr; continue;
                }
                valid_ew = false;
            }
        }
        std::size_t k = 0; while (k < v.size() && !wsp(v[k])) ++k;
        std::string tok = v.substr(0, k), rest = v.substr(k);
        if (valid_ew && re::search(EW_RE, tok)) {   // value.partition('=?'): the text before it, then '=?...' next time round
            const std::size_t at = v.find("=?");
            if (at != std::string::npos && at > 0) { tok = v.substr(0, at); rest = v.substr(at); }
        }
        toks.push_back({tok, 0}); v = rest;
    }
    std::string o; for (const T& t : toks) o += t.text;
    return decode_utf8_replace(o);
}

// ---------------------------------------------------------------- _refold_parse_tree
struct Tok {
    bool terminal = true; std::string text, type;
    bool ew_ok = true, combine_ok = true, brk = true, bare_quoted = false;
    std::vector<Tok> kids;
    std::string str() const {
        if (terminal) return text;
        std::string o; for (const Tok& k : kids) o += k.str();
        return bare_quoted ? quote_string(o) : o;
    }
    bool startswith_fws() const { return terminal ? type == "fws" : (!kids.empty() && kids[0].startswith_fws()); }
};
inline Tok term(std::string text, std::string type) { Tok t; t.text = std::move(text); t.type = std::move(type); return t; }
inline Tok tlist(std::string type, std::vector<Tok> kids, bool ew_ok = true) { Tok t; t.terminal = false; t.type = std::move(type); t.kids = std::move(kids); t.ew_ok = ew_ok; return t; }

inline std::string steal_wsp(std::vector<std::string>& lines) {   // _steal_trailing_WSP_if_exists
    if (!lines.empty() && !lines.back().empty() && wsp(lines.back().back())) { const char c = lines.back().back(); lines.back().pop_back(); return std::string(1, c); }
    return "";
}
inline long cl(const std::string& s) { return static_cast<long>(cplen(s)); }

inline std::optional<long> fold_as_ew(std::string to_encode, std::vector<std::string>& lines, long maxlen, std::optional<long> last_ew,
                                      bool combine, const std::string& charset, std::string leading_ws) {
    if (last_ew && combine) {
        const std::vector<std::string> c = cps(lines.back());
        std::string tail; for (std::size_t k = static_cast<std::size_t>(*last_ew); k < c.size(); ++k) tail += c[k];
        to_encode = decode_unstructured(tail + to_encode);
        lines.back() = cpslice(lines.back(), 0, *last_ew);
    } else if (!to_encode.empty() && wsp(to_encode[0])) {
        const std::string lw(1, to_encode[0]); to_encode.erase(0, 1);
        if (cl(lines.back()) == maxlen) lines.push_back(steal_wsp(lines));
        lines.back() += lw;
    }
    std::string trailing;
    if (!to_encode.empty() && wsp(to_encode.back())) { trailing = std::string(1, to_encode.back()); to_encode.pop_back(); }
    long new_last = last_ew ? *last_ew : cl(lines.back());
    const std::string enc_as = charset == "us-ascii" ? "utf-8" : charset;
    const long chrome = static_cast<long>(enc_as.size()) + 7;
    std::vector<std::string> rest = cps(to_encode);
    while (!rest.empty()) {
        const long remaining = maxlen - cl(lines.back());
        const long text_space = remaining - chrome - cl(leading_ws);
        if (text_space <= 0) { lines.push_back(" "); continue; }
        if (lines.size() > 1 && cl(lines.back()) == 1 && !leading_ws.empty()) { lines.back() += ew_encode(leading_ws, enc_as); leading_ws.clear(); }
        std::size_t take = std::min(rest.size(), static_cast<std::size_t>(text_space));
        auto chunk = [&](std::size_t n) { std::string s; for (std::size_t k = 0; k < n; ++k) s += rest[k]; return s; };
        std::string word = ew_encode(chunk(take), enc_as);
        while (static_cast<long>(word.size()) - remaining > 0) { --take; word = ew_encode(chunk(take), enc_as); }
        lines.back() += word;
        rest.erase(rest.begin(), rest.begin() + static_cast<long>(take));
        leading_ws.clear();
        if (!rest.empty()) { lines.push_back(" "); new_last = cl(lines.back()); }
    }
    lines.back() += trailing;
    if (combine) return new_last;
    return std::nullopt;
}

inline std::vector<std::string> refold_lines(const std::vector<Tok>& top, long maxlen) {
    std::vector<std::string> lines{""};
    std::string leading_ws; std::optional<long> last_ew; std::string last_charset;
    int blocked = 0; bool want = false;
    struct P { const Tok* t; bool end; };
    std::deque<P> parts; for (const Tok& t : top) parts.push_back({&t, false});
    while (!parts.empty()) {
        const P p = parts.front(); parts.pop_front();
        if (p.end) { --blocked; continue; }
        const Tok& part = *p.t; const std::string tstr = part.str();
        if (!want) want = part.type == "ptext" ? any_of_chars(tstr, SPECIALS + "\r\n") : any_of_chars(tstr, "\r\n");
        std::string charset = "us-ascii";
        if (!is_ascii(tstr)) { charset = "utf-8"; want = true; }
        if (want && !blocked) {
            if (!part.ew_ok) {
                want = false; last_ew.reset();
                if (part.brk && !part.terminal) {
                    std::vector<std::string> sub = refold_lines(part.kids, maxlen);
                    if (sub.size() == 1) {
                        if (cl(sub[0]) > maxlen - cl(lines.back())) lines.push_back(steal_wsp(lines));
                        lines.back() += sub[0];
                        continue;
                    }
                }
            }
            if (!part.terminal) {
                for (auto it = part.kids.rbegin(); it != part.kids.rend(); ++it) parts.push_front({&*it, false});
                want = false;
                continue;
            }
            if (part.ew_ok) {
                if (last_ew && charset != last_charset && (last_charset == "unknown-8bit" || (last_charset == "utf-8" && charset != "us-ascii"))) last_ew.reset();
                last_ew = fold_as_ew(tstr, lines, maxlen, last_ew, part.combine_ok, charset, leading_ws);
                leading_ws.clear(); last_charset = charset; want = false;
                continue;
            }
            last_ew.reset(); want = false;
        }
        if (cl(tstr) <= maxlen - cl(lines.back())) { lines.back() += tstr; continue; }
        leading_ws.clear();
        if (part.brk && cl(tstr) + 1 <= maxlen) {
            const std::string nl = steal_wsp(lines);
            if (!nl.empty() || part.startswith_fws()) {
                lines.push_back(nl + tstr);
                for (char c : lines.back()) { if (!wsp(c)) break; leading_ws += c; }
                last_ew.reset();
                continue;
            }
        }
        if (!part.terminal) {
            if (!part.ew_ok) { ++blocked; parts.push_front({nullptr, true}); }
            for (auto it = part.kids.rbegin(); it != part.kids.rend(); ++it) parts.push_front({&*it, false});
            continue;
        }
        if (part.ew_ok && !blocked) { parts.push_front(p); want = true; continue; }
        const std::string nl = steal_wsp(lines);
        if (!nl.empty() || part.startswith_fws()) lines.push_back(nl + tstr); else lines.back() += tstr;
    }
    return lines;
}
inline std::string fold_header(const std::string& name, const Tok& value, long maxlen = 78) {
    std::vector<Tok> top;
    top.push_back(tlist("header-label", {term(name, "header-name"), term(":", "header-sep")}, false));
    top.push_back(tlist("cfws", {term(" ", "fws")}));
    top.push_back(value);
    return join(refold_lines(top, maxlen), "\r\n") + "\r\n";
}

// get_unstructured(value) as a parse tree: runs of blanks (fws) and the text between them (vtext)
inline Tok unstructured_tree(const std::string& value) {
    std::vector<Tok> kids; std::string v = value;
    while (!v.empty()) {
        if (wsp(v[0])) {
            const auto c = cps(v); std::size_t k = 0, bytes = 0;
            while (k < c.size() && py_space(cp_of(c[k]))) { bytes += c[k].size(); ++k; }
            kids.push_back(term(v.substr(0, bytes), "fws")); v = v.substr(bytes); continue;
        }
        std::size_t k = 0; while (k < v.size() && !wsp(v[k])) ++k;
        kids.push_back(term(v.substr(0, k), "vtext")); v = v.substr(k);
    }
    return tlist("unstructured", kids);
}
inline std::string addr_str(const std::string& display, const std::string& addr) {   // str(headerregistry.Address)
    if (display.empty()) return addr;
    return (any_of_chars(display, SPECIALS) ? quote_string(display) : display) + " <" + addr + ">";
}
// the parse tree of str(Address(display, addr)): a display name (atoms, or one quoted string) and the angle address
inline Tok address_tree(const std::string& display, const std::string& addr) {
    const std::size_t at = addr.rfind('@');
    Tok spec = tlist("addr-spec", at == std::string::npos ? std::vector<Tok>{term(addr, "atext")}
                                                          : std::vector<Tok>{term(addr.substr(0, at), "atext"), term("@", "address-at-symbol"), term(addr.substr(at + 1), "atext")}, false);
    if (display.empty()) return tlist("address-list", {spec});
    std::vector<Tok> words;
    const std::vector<std::string> parts = split(display, " ");
    if (any_of_chars(display, SPECIALS)) {   // one quoted string: ptext and blanks inside, then the blank before '<'
        Tok bare = tlist("bare-quoted-string", {}); bare.bare_quoted = true;
        for (std::size_t k = 0; k < parts.size(); ++k) { if (k) bare.kids.push_back(term(" ", "fws")); bare.kids.push_back(term(parts[k], "ptext")); }
        words.push_back(tlist("quoted-string", {bare, tlist("cfws", {term(" ", "fws")})}));
    } else {
        for (const std::string& w : parts) words.push_back(tlist("atom", {term(w, "atext"), tlist("cfws", {term(" ", "fws")})}));
    }
    Tok name = tlist("display-name", words); name.combine_ok = false;
    Tok angle = tlist("angle-addr", {term("<", "angle-addr-start"), spec, term(">", "angle-addr-end")});
    return tlist("address-list", {tlist("name-addr", {name, angle})});
}
inline Tok msgid_tree(const std::string& id) {
    const std::size_t at = id.rfind('@');
    std::vector<Tok> k;
    if (id.size() > 2 && id.front() == '<' && id.back() == '>' && at != std::string::npos) {
        k = {term("<", "msg-id-start"), term(id.substr(1, at - 1), "atext"), term("@", "address-at-symbol"), term(id.substr(at + 1, id.size() - at - 2), "atext"), term(">", "msg-id-end")};
    } else k = {term(id, "atext")};
    return tlist("message-id", {tlist("msg-id", k, false)}, false);
}

// ---------------------------------------------------------------- the body: quopri body_encode
inline std::string qp_body_encode(const std::string& body, std::size_t maxlinelen = 78) {
    if (body.empty()) return body;
    std::string t;   // body.translate(_QUOPRI_BODY_ENCODE_MAP) over the latin-1 view of the bytes
    for (unsigned char c : body) {
        const bool safe = (c >= 32 && c <= 126 && c != '=') || c == '\t' || c == '\r' || c == '\n';
        if (safe) t += static_cast<char>(c); else { char h[4]; std::snprintf(h, sizeof h, "=%02X", c); t += h; }
    }
    std::vector<std::string> out;
    std::vector<std::string> lines; {   // str.splitlines on \r, \n, \r\n (nothing else survives the translate)
        std::string cur;
        for (std::size_t k = 0; k < t.size(); ++k) {
            if (t[k] == '\r' || t[k] == '\n') { lines.push_back(cur); cur.clear(); if (t[k] == '\r' && k + 1 < t.size() && t[k + 1] == '\n') ++k; }
            else cur += t[k];
        }
        if (!cur.empty()) lines.push_back(cur);
    }
    const long maxlen1 = static_cast<long>(maxlinelen) - 1;
    for (const std::string& line : lines) {
        long start = 0;
        const long laststart = static_cast<long>(line.size()) - 1 - static_cast<long>(maxlinelen);
        while (start <= laststart) {
            const long stop = start + maxlen1;
            if (line[static_cast<std::size_t>(stop - 2)] == '=') { out.push_back(line.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(stop - 1 - start))); start = stop - 2; }
            else if (line[static_cast<std::size_t>(stop - 1)] == '=') { out.push_back(line.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(stop - start))); start = stop - 1; }
            else { out.push_back(line.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(stop - start)) + "="); start = stop; }
        }
        if (!line.empty() && wsp(line.back())) {
            const long room = start - laststart;
            char h[4]; std::snprintf(h, sizeof h, "=%02X", static_cast<unsigned char>(line.back()));
            std::string q;
            if (room >= 3) q = h; else if (room == 2) q = std::string(1, line.back()) + "=\n"; else q = std::string("=\n") + h;
            out.push_back(line.substr(static_cast<std::size_t>(start), line.size() - 1 - static_cast<std::size_t>(start)) + q);
        } else out.push_back(line.substr(static_cast<std::size_t>(start)));
    }
    if (t.back() == '\r' || t.back() == '\n') out.push_back("");
    return join(out, "\n");
}
inline std::vector<std::string> bytes_splitlines(const std::string& b) {   // bytes.splitlines(): \n, \r, \r\n
    std::vector<std::string> o; std::string cur;
    for (std::size_t k = 0; k < b.size(); ++k) {
        if (b[k] == '\r' || b[k] == '\n') { o.push_back(cur); cur.clear(); if (b[k] == '\r' && k + 1 < b.size() && b[k + 1] == '\n') ++k; }
        else cur += b[k];
    }
    if (!cur.empty()) o.push_back(cur);
    return o;
}
inline std::string crlf(const std::string& s) {   // Generator._write_lines: every \r\n, \r or \n becomes CRLF
    std::string o;
    for (std::size_t k = 0; k < s.size(); ++k) {
        if (s[k] == '\r') { o += "\r\n"; if (k + 1 < s.size() && s[k + 1] == '\n') ++k; }
        else if (s[k] == '\n') o += "\r\n";
        else o += s[k];
    }
    return o;
}
inline void header_value_check(const std::string& v) {   // header_store_parse: no line breaks in a header value
    if (splitlines(v).size() > 1) throw PyErr("ValueError", "Header values may not contain linefeed or carriage return characters");
}
inline const char* const DAY_ABBR[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
inline const char* const MON_ABBR[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// build_eml: the RFC 5322 message closer.py writes, byte for byte (CRLF, plain text, utf-8)
inline std::string build_eml(const std::string& from_name, const std::string& from_email, const std::string& to_name, const std::string& to_email,
                             const std::string& subject, const std::string& body, long long day, const std::string& mid, const std::string& in_reply_to) {
    std::string o;
    o += fold_header("From", address_tree(from_name, from_email));
    o += fold_header("To", address_tree(to_name, to_email));
    header_value_check(subject);
    o += fold_header("Subject", unstructured_tree(subject));
    long long y; unsigned m, d; civil_from(day, y, m, d);
    char date[64]; std::snprintf(date, sizeof date, "Date: %s, %02u %s %04lld 13:00:00 +0000\r\n", DAY_ABBR[((day % 7) + 7 + 3) % 7], d, MON_ABBR[m - 1], y);
    o += date;
    o += fold_header("Message-ID", msgid_tree(mid));
    if (!in_reply_to.empty()) {
        header_value_check(in_reply_to);
        o += fold_header("In-Reply-To", unstructured_tree(in_reply_to));
        o += fold_header("References", unstructured_tree(in_reply_to));
    }
    std::string text = body; while (!text.empty() && text.back() == '\n') text.pop_back(); text += "\n";
    bool plain = is_ascii(text);
    if (plain) for (const std::string& l : split(text, "\n")) if (l.size() > 78) { plain = false; break; }
    std::string norm; for (const std::string& l : bytes_splitlines(text)) norm += l + "\n";
    const std::string payload = plain ? norm : qp_body_encode(norm, 78);
    o += "Content-Type: text/plain; charset=\"utf-8\"\r\n";
    o += std::string("Content-Transfer-Encoding: ") + (plain ? "7bit" : "quoted-printable") + "\r\n";
    o += "MIME-Version: 1.0\r\n\r\n";
    o += crlf(payload);
    return o;
}

// ================================================================ email: reading

struct Addr { std::string display, local, domain; std::string addr_spec() const; std::string str() const; };
inline std::string Addr::addr_spec() const {
    std::string lp = local;
    if (any_of_chars(lp, "()<>@,:;\"[] \t")) lp = quote_string(lp);
    if (!domain.empty()) return lp + "@" + domain;
    if (lp.empty()) return "<>";
    return lp;
}
inline std::string Addr::str() const {
    const std::string spec = addr_spec() == "<>" ? std::string() : addr_spec();
    if (display.empty()) return addr_spec();
    return (any_of_chars(display, SPECIALS) ? quote_string(display) : display) + " <" + spec + ">";
}
struct Group { std::optional<std::string> name; std::vector<Addr> addrs; };

// a lenient RFC 5322 address-list reader: quoted strings, comments, angle addresses, groups, encoded words in names
struct ATok { int kind; std::string text; };   // 0 atom, 1 quoted, 2 special, 3 blank, 4 comment, 5 angle (its inside)
inline std::vector<ATok> addr_tokens(const std::string& s) {
    std::vector<ATok> t; std::size_t k = 0;
    while (k < s.size()) {
        const char c = s[k];
        if (wsp(c) || c == '\r' || c == '\n') { std::size_t j = k; while (j < s.size() && (wsp(s[j]) || s[j] == '\r' || s[j] == '\n')) ++j; t.push_back({3, " "}); k = j; continue; }
        if (c == '"') {
            std::string q; ++k;
            while (k < s.size() && s[k] != '"') { if (s[k] == '\\' && k + 1 < s.size()) ++k; q += s[k++]; }
            ++k; t.push_back({1, q}); continue;
        }
        if (c == '(') {
            int depth = 0; std::string q;
            while (k < s.size()) { if (s[k] == '\\' && k + 1 < s.size()) { q += s[k + 1]; k += 2; continue; } if (s[k] == '(') ++depth; else if (s[k] == ')' && --depth == 0) { ++k; break; } q += s[k++]; }
            t.push_back({4, q}); continue;
        }
        if (c == '<') {
            std::string q; ++k; bool inq = false;
            while (k < s.size() && (inq || s[k] != '>')) { if (s[k] == '"') inq = !inq; q += s[k++]; }
            ++k; t.push_back({5, q}); continue;
        }
        if (c == ',' || c == ';' || c == ':' || c == '@' || c == '>') { t.push_back({2, std::string(1, c)}); ++k; continue; }
        std::size_t j = k;
        while (j < s.size() && !wsp(s[j]) && s[j] != '\r' && s[j] != '\n' && std::string("\"(<,;:@>").find(s[j]) == std::string::npos) ++j;
        t.push_back({0, s.substr(k, j - k)}); k = j;
    }
    return t;
}
inline std::string phrase_of(const std::vector<ATok>& t) {   // the display name: words joined by one blank, encoded words decoded
    std::vector<std::pair<std::string, bool>> words;   // (text, was an encoded word)
    for (const ATok& x : t) {
        if (x.kind == 0) { const std::string d = decode_unstructured(x.text); words.push_back({d, d != x.text && x.text.compare(0, 2, "=?") == 0}); }
        else if (x.kind == 1) words.push_back({x.text, false});
        else if (x.kind == 2 && x.text != ",") words.push_back({x.text, false});
    }
    std::string o;
    for (std::size_t k = 0; k < words.size(); ++k) {
        if (k && !(words[k].second && words[k - 1].second)) o += " ";
        o += words[k].first;
    }
    return o;
}
inline Addr spec_of(const std::string& inside, std::string display) {
    std::string s = inside;
    const std::size_t colon = s.find(':');   // an obsolete route "@a,@b:" is dropped
    if (!s.empty() && s[0] == '@' && colon != std::string::npos) s = s.substr(colon + 1);
    std::string local, domain, cur; bool seen_at = false;
    for (const ATok& x : addr_tokens(s)) {
        if (x.kind == 3 || x.kind == 4) continue;
        if (x.kind == 2 && x.text == "@" && !seen_at) { seen_at = true; local = cur; cur.clear(); continue; }
        cur += x.text;
    }
    if (seen_at) domain = cur; else local = cur;
    return {std::move(display), local, domain};
}
inline std::vector<Group> parse_address_list(const std::string& value) {
    std::vector<Group> groups;
    const std::vector<ATok> toks = addr_tokens(value);
    std::size_t k = 0;
    auto mailbox = [&](const std::vector<ATok>& t) -> std::optional<Addr> {
        bool any = false; for (const ATok& x : t) if (x.kind != 3 && x.kind != 4) any = true;
        if (!any) return std::nullopt;
        for (std::size_t j = 0; j < t.size(); ++j) if (t[j].kind == 5) {
            const std::vector<ATok> before(t.begin(), t.begin() + static_cast<long>(j));
            return spec_of(t[j].text, decode_utf8_replace(phrase_of(before)));
        }
        std::string raw; for (const ATok& x : t) { if (x.kind == 3 || x.kind == 4) continue; raw += x.kind == 1 ? quote_string(x.text) : x.text; }
        Addr a = spec_of(raw, "");
        if (!a.local.empty() && a.local.front() == '"' && a.local.back() == '"' && a.local.size() >= 2) a.local = a.local.substr(1, a.local.size() - 2);
        return a;
    };
    while (k < toks.size()) {
        std::vector<ATok> cur;
        std::size_t j = k; bool group = false;
        while (j < toks.size() && !(toks[j].kind == 2 && toks[j].text == ",")) {
            if (toks[j].kind == 2 && toks[j].text == ":") { group = true; break; }
            cur.push_back(toks[j]); ++j;
        }
        if (group) {
            Group g; g.name = decode_utf8_replace(phrase_of(cur)); ++j;
            std::vector<ATok> member;
            while (j < toks.size() && !(toks[j].kind == 2 && toks[j].text == ";")) {
                if (toks[j].kind == 2 && toks[j].text == ",") { if (auto a = mailbox(member)) g.addrs.push_back(*a); member.clear(); }
                else member.push_back(toks[j]);
                ++j;
            }
            if (auto a = mailbox(member)) g.addrs.push_back(*a);
            if (j < toks.size()) ++j;
            groups.push_back(g);
            while (j < toks.size() && toks[j].kind != 2) ++j;
            if (j < toks.size() && toks[j].text == ",") ++j;
            k = j; continue;
        }
        if (auto a = mailbox(cur)) { Group g; g.addrs.push_back(*a); groups.push_back(g); }
        k = j + 1;
    }
    return groups;
}
inline std::string groups_str(const std::vector<Group>& gs) {
    std::vector<std::string> o;
    for (const Group& g : gs) {
        if (!g.name && g.addrs.size() == 1) { o.push_back(g.addrs[0].str()); continue; }
        std::string disp = g.name ? *g.name : "None";
        if (g.name && any_of_chars(disp, SPECIALS)) disp = quote_string(disp);
        std::vector<std::string> a; for (const Addr& x : g.addrs) a.push_back(x.str());
        const std::string as = join(a, ", ");
        o.push_back(disp + ":" + (as.empty() ? "" : " " + as) + ";");
    }
    return join(o, ", ");
}

struct Eml {
    std::vector<std::pair<std::string, std::string>> headers;   // name, raw value (unfolded later)
    std::string body;                                           // the raw bytes after the blank line
    const std::string* raw(const std::string& name) const {
        for (const auto& [n, v] : headers) if (lower(n) == lower(name)) return &v;
        return nullptr;
    }
    std::optional<std::string> value(const std::string& name) const {   // header_fetch_parse: \r and \n removed
        const std::string* r = raw(name); if (!r) return std::nullopt;
        std::string o; for (char c : *r) if (c != '\r' && c != '\n') o += c; return o;
    }
    bool has(const std::string& name) const { return raw(name) != nullptr; }
    std::vector<Addr> addresses(const std::string& name) const {
        std::vector<Addr> o; const auto v = value(name); if (!v) return o;
        for (const Group& g : parse_address_list(*v)) for (const Addr& a : g.addrs) o.push_back(a);
        return o;
    }
    std::string str(const std::string& name) const {   // str(msg[name]) under policy.default
        const auto v = value(name); if (!v) return "None";
        const std::string n = lower(name);
        if (n == "to" || n == "from" || n == "cc" || n == "bcc" || n == "sender" || n == "reply-to" || n.compare(0, 7, "resent-") == 0)
            return groups_str(parse_address_list(*v));
        return decode_unstructured(*v);
    }
    std::string content() const;   // msg.get_content() for text/*
};
// email.message_from_bytes: the header block up to the first empty line (feedparser rules), then the body as is
inline Eml parse_eml(const std::string& data) {
    Eml m;
    std::vector<std::string> lines; std::size_t k = 0;
    while (k < data.size()) {   // NLCRE_crack: lines keep their \r\n, \r or \n
        std::size_t j = k; while (j < data.size() && data[j] != '\r' && data[j] != '\n') ++j;
        if (j < data.size()) { if (data[j] == '\r' && j + 1 < data.size() && data[j + 1] == '\n') j += 2; else ++j; }
        lines.push_back(data.substr(k, j - k)); k = j;
    }
    std::size_t at = 0, pos = 0;
    std::string lastname; std::vector<std::string> lastval; bool have = false;
    auto flush_header = [&]() {
        if (!have) return;
        const std::string& first = lastval[0];
        std::string v = first.substr(first.find(':') + 1);
        std::size_t a = 0; while (a < v.size() && wsp(v[a])) ++a;
        v = v.substr(a);
        for (std::size_t i = 1; i < lastval.size(); ++i) v += lastval[i];
        while (!v.empty() && (v.back() == '\r' || v.back() == '\n')) v.pop_back();
        m.headers.push_back({lastname, v});
        have = false;
    };
    for (; at < lines.size(); ++at) {
        const std::string& line = lines[at];
        if (line == "\r\n" || line == "\n" || line == "\r") { pos += line.size(); ++at; break; }
        const bool cont = !line.empty() && wsp(line[0]);
        bool header_like = cont || line.compare(0, 5, "From ") == 0;
        if (!header_like) {   // [\041-\071\073-\176]*:
            std::size_t c = 0;
            while (c < line.size() && static_cast<unsigned char>(line[c]) >= 0x21 && static_cast<unsigned char>(line[c]) <= 0x7E && line[c] != ':') ++c;
            header_like = c < line.size() && line[c] == ':';
        }
        if (!header_like) break;   // a missing blank line: the body starts here
        pos += line.size();
        if (cont) { if (have) lastval.push_back(line); continue; }
        flush_header();
        if (line.compare(0, 5, "From ") == 0) continue;
        const std::size_t i = line.find(':');
        if (i == 0 || i == std::string::npos) continue;
        lastname = line.substr(0, i); lastval = {line}; have = true;
    }
    flush_header();
    m.body = data.substr(std::min(pos, data.size()));
    return m;
}
inline std::string qp_decode(const std::string& in) {   // binascii.a2b_qp (header=False)
    std::string o; std::size_t k = 0; const std::size_t n = in.size();
    while (k < n) {
        if (in[k] == '=') {
            ++k; if (k >= n) break;
            if (in[k] == '\n' || in[k] == '\r') { if (in[k] != '\n') while (k < n && in[k] != '\n') ++k; if (k < n) ++k; }
            else if (in[k] == '=') { o += '='; ++k; }
            else if (k + 1 < n && hexv(in[k]) >= 0 && hexv(in[k + 1]) >= 0) { o += static_cast<char>(hexv(in[k]) * 16 + hexv(in[k + 1])); k += 2; }
            else o += '=';
        } else o += in[k++];
    }
    return o;
}
inline std::string param_of(const std::string& value, const std::string& key) {   // Message.get_param
    std::vector<std::string> parts; std::string cur; bool inq = false;
    for (std::size_t k = 0; k < value.size(); ++k) {
        const char c = value[k];
        if (c == '"') inq = !inq;
        if (c == '\\' && inq && k + 1 < value.size()) { cur += c; cur += value[++k]; continue; }
        if (c == ';' && !inq) { parts.push_back(cur); cur.clear(); continue; }
        cur += c;
    }
    parts.push_back(cur);
    for (std::size_t k = 1; k < parts.size(); ++k) {
        const std::size_t eq = parts[k].find('=');
        if (eq == std::string::npos) continue;
        if (lower(strip(parts[k].substr(0, eq))) != lower(key)) continue;
        std::string v = strip(parts[k].substr(eq + 1));
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
            std::string u; for (std::size_t j = 1; j + 1 < v.size(); ++j) { if (v[j] == '\\' && j + 2 < v.size()) ++j; u += v[j]; }
            v = u;
        }
        return v;
    }
    return "";
}
inline std::string Eml::content() const {
    const auto ct = value("content-type");
    std::string ctype = "text/plain";
    if (ct) {
        std::string c = lower(strip(split(*ct, ";")[0]));
        if (std::count(c.begin(), c.end(), '/') == 1) ctype = c;
    }
    const std::string main = ctype.substr(0, ctype.find('/'));
    if (main == "multipart") throw PyErr("KeyError", str_repr(ctype));
    if (main != "text") throw PyErr("TypeError", "a bytes-like object is required, not 'str'");
    const std::string cte = lower(value("content-transfer-encoding").value_or(""));
    std::string bytes = body;
    if (cte == "quoted-printable") bytes = qp_decode(body);
    else if (cte == "base64") { std::string joined; for (const std::string& l : bytes_splitlines(body)) joined += l; bytes = b64_decode(joined).value_or(""); }
    std::string cs = ct ? param_of(*ct, "charset") : "";
    if (cs.empty()) cs = "ASCII";
    const std::string norm = norm_charset(cs);
    if (norm == "utf-8") return decode_utf8_replace(bytes);
    if (norm == "latin-1") return latin1_to_utf8(bytes);
    if (norm == "ascii") { std::string o; for (unsigned char c : bytes) { if (c < 0x80) o += static_cast<char>(c); else o += "\xEF\xBF\xBD"; } return o; }
    throw PyErr("LookupError", "unknown encoding: " + cs);
}

// email.utils.parseaddr(addr)[1]: the address of a single sender, '' when it does not parse as exactly one
inline std::string parseaddr_email(const std::string& s) {
    int depth = 0; bool esc = false;
    for (char c : s) {   // _check_parenthesis
        if (esc) { esc = false; continue; }
        if (c == '\\') { esc = true; continue; }
        if (c == '(') ++depth; else if (c == ')') { if (--depth < 0) return ""; }
    }
    if (depth) return "";
    std::vector<std::string> parts; std::string cur; bool inq = false; int angle = 0, par = 0;
    for (std::size_t k = 0; k < s.size(); ++k) {
        const char c = s[k];
        if (c == '\\' && k + 1 < s.size()) { cur += c; cur += s[++k]; continue; }
        if (c == '"' && !par) inq = !inq;
        else if (!inq && c == '(') ++par; else if (!inq && c == ')') --par;
        else if (!inq && !par && c == '<') ++angle; else if (!inq && !par && c == '>') --angle;
        if (c == ',' && !inq && !par && angle <= 0) { parts.push_back(cur); cur.clear(); continue; }
        cur += c;
    }
    parts.push_back(cur);
    std::vector<std::string> real; for (const std::string& p : parts) if (!strip(p).empty()) real.push_back(p);
    if (real.size() != 1) return "";
    std::string p, plain; int dep = 0; bool q = false;   // comments out
    for (char c : real[0]) {
        if (c == '"' && !dep) q = !q;
        if (!q && c == '(') { ++dep; continue; }
        if (!q && c == ')' && dep) { --dep; continue; }
        if (!dep) p += c;
    }
    std::string addr;
    const std::size_t lt = p.find('<');
    if (lt != std::string::npos) {
        const std::size_t gt = p.find('>', lt);
        std::string in = p.substr(lt + 1, gt == std::string::npos ? std::string::npos : gt - lt - 1);
        const std::size_t colon = in.find(':');
        if (!in.empty() && strip(in)[0] == '@' && colon != std::string::npos) in = in.substr(colon + 1);
        for (char c : in) if (!wsp(c)) addr += c;
    } else if (p.find('@') != std::string::npos) {
        for (char c : p) if (!wsp(c) && c != '"') addr += c;
    } else {
        const auto w = split_ws(p); addr = w.empty() ? "" : w[0];
    }
    if (addr.find('[') != std::string::npos) return "";
    return addr;
}
}  // namespace packtools::seed_closer
