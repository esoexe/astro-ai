// urlparse.h - urllib.parse from CPython 3.13: urlsplit / urlparse (with hostname and port), urlunsplit / urlunparse,
// urljoin, urldefrag, quote and unquote; plus the "idna" codec's encode (nameprep, then punycode) for host names.
// nameprep here maps with lower() (and ß -> ss), removes the B.1 characters and applies NFKC; the stringprep
// prohibition and bidi checks are not repeated (Python raises on those; such hosts are rare in links).
#pragma once
#include "pyrt.h"
#include "unicode.h"
#include <optional>

namespace pyrt::urlp {

inline bool in_list(const std::string& s, std::initializer_list<const char*> l) {
    for (const char* x : l) if (s == x) return true;
    return false;
}
inline bool uses_relative(const std::string& s) { return in_list(s, {"", "ftp", "http", "gopher", "nntp", "imap", "wais", "file", "https", "shttp", "mms", "prospero", "rtsp", "rtsps", "rtspu", "sftp", "svn", "svn+ssh", "ws", "wss"}); }
inline bool uses_netloc(const std::string& s) { return in_list(s, {"", "ftp", "http", "gopher", "nntp", "telnet", "imap", "wais", "file", "mms", "https", "shttp", "snews", "prospero", "rtsp", "rtsps", "rtspu", "rsync", "svn", "svn+ssh", "sftp", "nfs", "git", "git+ssh", "ws", "wss", "itms-services"}); }
inline bool uses_params(const std::string& s) { return in_list(s, {"", "ftp", "hdl", "prospero", "http", "imap", "https", "shttp", "rtsp", "rtsps", "rtspu", "sip", "sips", "mms", "sftp", "tel"}); }

inline std::pair<std::string, std::string> partition(const std::string& s, char c) {
    const auto k = s.find(c);
    if (k == std::string::npos) return {s, ""};
    return {s.substr(0, k), s.substr(k + 1)};
}

struct SplitResult {
    std::string scheme, netloc, path, query, fragment;
    std::pair<std::string, std::optional<std::string>> hostinfo() const {
        const auto at = netloc.rfind('@');
        const std::string hostinfo = at == std::string::npos ? netloc : netloc.substr(at + 1);
        std::string hostname, port;
        const auto br = hostinfo.find('[');
        if (br != std::string::npos) {
            const std::string bracketed = hostinfo.substr(br + 1);
            const auto close = bracketed.find(']');
            hostname = close == std::string::npos ? bracketed : bracketed.substr(0, close);
            const std::string after = close == std::string::npos ? "" : bracketed.substr(close + 1);
            port = partition(after, ':').second;
        } else {
            std::tie(hostname, port) = partition(hostinfo, ':');
            if (hostinfo.find(':') == std::string::npos) port.clear();
        }
        if (port.empty()) return {hostname, std::nullopt};
        return {hostname, port};
    }
    std::optional<std::string> hostname() const {   // lowercased up to any %zone
        std::string h = hostinfo().first;
        if (h.empty()) return std::nullopt;
        const auto pct = h.find('%');
        if (pct == std::string::npos) return lower(h);
        return lower(h.substr(0, pct)) + h.substr(pct);
    }
    std::optional<long long> port() const {
        const auto p = hostinfo().second;
        if (!p) return std::nullopt;
        for (char c : *p)
            if (c < '0' || c > '9') throw PyErr("ValueError", "Port could not be cast to integer value as " + repr(Val::str(*p)));
        std::size_t k = 0;
        while (k + 1 < p->size() && (*p)[k] == '0') k++;
        if (p->size() - k > 5) throw PyErr("ValueError", "Port out of range 0-65535");
        const long long v = std::stoll(p->substr(k));
        if (v > 65535) throw PyErr("ValueError", "Port out of range 0-65535");
        return v;
    }
};
struct ParseResult { std::string scheme, netloc, path, params, query, fragment; };

inline bool ipv6_like(const std::string& h) {   // ipaddress.ip_address(h) is an IPv6 address (loosely)
    const std::string a = h.substr(0, h.find('%'));
    if (a.find(':') == std::string::npos) return false;
    for (char c : a)
        if (!(std::isxdigit(static_cast<unsigned char>(c)) || c == ':' || c == '.')) return false;
    return a.find(":::") == std::string::npos;
}
inline bool ipv4_like(const std::string& h) {
    int dots = 0;
    for (char c : h) { if (c == '.') dots++; else if (c < '0' || c > '9') return false; }
    return dots == 3;
}
inline void check_bracketed_netloc(const std::string& netloc) {
    const auto at = netloc.rfind('@');
    const std::string hp = at == std::string::npos ? netloc : netloc.substr(at + 1);
    std::string hostname;
    const auto br = hp.find('[');
    if (br != std::string::npos) {
        if (br > 0) throw PyErr("ValueError", "Invalid IPv6 URL");
        const std::string bracketed = hp.substr(br + 1);
        const auto close = bracketed.find(']');
        hostname = bracketed.substr(0, close);
        const std::string port = close == std::string::npos ? "" : bracketed.substr(close + 1);
        if (!port.empty() && port[0] != ':') throw PyErr("ValueError", "Invalid IPv6 URL");
    } else hostname = partition(hp, ':').first;
    if (!hostname.empty() && (hostname[0] == 'v' || hostname[0] == 'V')) {
        std::size_t k = 1;
        while (k < hostname.size() && std::isxdigit(static_cast<unsigned char>(hostname[k]))) k++;
        if (k == 1 || k >= hostname.size() || hostname[k] != '.' || k + 1 >= hostname.size()) throw PyErr("ValueError", "IPvFuture address is invalid");
    } else if (ipv4_like(hostname)) throw PyErr("ValueError", "An IPv4 address cannot be in brackets");
    else if (!ipv6_like(hostname)) throw PyErr("ValueError", repr(Val::str(hostname)) + " does not appear to be an IPv4 or IPv6 address");
}
inline void check_netloc(const std::string& netloc) {   // _checknetloc
    bool ascii = true;
    for (unsigned char c : netloc) if (c >= 0x80) ascii = false;
    if (netloc.empty() || ascii) return;
    std::string n;
    for (char c : netloc) if (c != '@' && c != ':' && c != '#' && c != '?') n += c;
    const std::string n2 = uni::normalize("NFKC", n);
    if (n == n2) return;
    for (char c : std::string("/?#@:"))
        if (n2.find(c) != std::string::npos) throw PyErr("ValueError", "netloc '" + netloc + "' contains invalid characters under NFKC normalization");
}

inline SplitResult urlsplit(std::string url, std::string scheme = "") {
    std::size_t a = 0;
    while (a < url.size() && static_cast<unsigned char>(url[a]) <= ' ') a++;
    url = url.substr(a);
    std::string clean;
    for (char c : url) if (c != '\t' && c != '\r' && c != '\n') clean += c;
    url = clean;
    SplitResult r;
    const auto i = url.find(':');
    if (i != std::string::npos && i > 0 && ((url[0] >= 'a' && url[0] <= 'z') || (url[0] >= 'A' && url[0] <= 'Z'))) {
        bool ok = true;
        for (std::size_t k = 0; k < i && ok; ++k) {
            const unsigned char c = static_cast<unsigned char>(url[k]);
            ok = std::isalnum(c) || c == '+' || c == '-' || c == '.';
        }
        if (ok) { scheme = lower(url.substr(0, i)); url = url.substr(i + 1); }
    }
    r.scheme = scheme;
    if (url.compare(0, 2, "//") == 0) {
        const auto d = url.find_first_of("/?#", 2);
        r.netloc = url.substr(2, d == std::string::npos ? std::string::npos : d - 2);
        url = d == std::string::npos ? "" : url.substr(d);
        const bool lb = r.netloc.find('[') != std::string::npos, rb = r.netloc.find(']') != std::string::npos;
        if (lb != rb) throw PyErr("ValueError", "Invalid IPv6 URL");
        if (lb && rb) check_bracketed_netloc(r.netloc);
    }
    if (const auto h = url.find('#'); h != std::string::npos) { r.fragment = url.substr(h + 1); url = url.substr(0, h); }
    if (const auto q = url.find('?'); q != std::string::npos) { r.query = url.substr(q + 1); url = url.substr(0, q); }
    check_netloc(r.netloc);
    r.path = url;
    return r;
}
inline ParseResult urlparse(const std::string& url, const std::string& scheme = "") {
    const SplitResult s = urlsplit(url, scheme);
    ParseResult r{s.scheme, s.netloc, s.path, "", s.query, s.fragment};
    if (uses_params(s.scheme) && s.path.find(';') != std::string::npos) {
        std::size_t k;
        if (s.path.find('/') != std::string::npos) {
            k = s.path.find(';', s.path.rfind('/'));
            if (k == std::string::npos) return r;
        } else k = s.path.find(';');
        r.path = s.path.substr(0, k); r.params = s.path.substr(k + 1);
    }
    return r;
}
inline std::string urlunsplit(const std::string& scheme, const std::string& netloc, std::string url, const std::string& query, const std::string& fragment) {
    if (!netloc.empty()) {
        if (!url.empty() && url[0] != '/') url = "/" + url;
        url = "//" + netloc + url;
    } else if (url.compare(0, 2, "//") == 0) url = "//" + url;
    else if (!scheme.empty() && uses_netloc(scheme) && (url.empty() || url[0] == '/')) url = "//" + url;
    if (!scheme.empty()) url = scheme + ":" + url;
    if (!query.empty()) url += "?" + query;
    if (!fragment.empty()) url += "#" + fragment;
    return url;
}
inline std::string urlunparse(const ParseResult& p) {
    return urlunsplit(p.scheme, p.netloc, p.params.empty() ? p.path : p.path + ";" + p.params, p.query, p.fragment);
}
inline std::string urljoin(const std::string& base, const std::string& url) {
    if (base.empty()) return url;
    if (url.empty()) return base;
    const ParseResult b = urlparse(base);
    ParseResult u = urlparse(url, b.scheme);
    if (u.scheme != b.scheme || !uses_relative(u.scheme)) return url;
    if (uses_netloc(u.scheme)) {
        if (!u.netloc.empty()) return urlunparse(u);
        u.netloc = b.netloc;
    }
    if (u.path.empty() && u.params.empty()) {
        u.path = b.path; u.params = b.params;
        if (u.query.empty()) u.query = b.query;
        return urlunparse(u);
    }
    std::vector<std::string> base_parts = split(b.path, "/");
    if (!base_parts.empty() && !base_parts.back().empty()) base_parts.pop_back();
    std::vector<std::string> segments;
    if (!u.path.empty() && u.path[0] == '/') segments = split(u.path, "/");
    else {
        segments = base_parts;
        for (const auto& s : split(u.path, "/")) segments.push_back(s);
        if (segments.size() > 2) {   // segments[1:-1] = filter(None, segments[1:-1])
            std::vector<std::string> f{segments.front()};
            for (std::size_t k = 1; k + 1 < segments.size(); ++k) if (!segments[k].empty()) f.push_back(segments[k]);
            f.push_back(segments.back());
            segments = f;
        }
    }
    std::vector<std::string> resolved;
    for (const auto& seg : segments) {
        if (seg == "..") { if (!resolved.empty()) resolved.pop_back(); }
        else if (seg == ".") continue;
        else resolved.push_back(seg);
    }
    if (!segments.empty() && (segments.back() == "." || segments.back() == "..")) resolved.push_back("");
    std::string path = join(resolved, "/");
    u.path = path.empty() ? "/" : path;
    return urlunparse(u);
}
inline std::pair<std::string, std::string> urldefrag(const std::string& url) {
    if (url.find('#') == std::string::npos) return {url, ""};
    ParseResult p = urlparse(url);
    const std::string frag = p.fragment;
    p.fragment.clear();
    return {urlunparse(p), frag};
}
inline std::string quote(const std::string& s, const std::string& safe = "/") {
    static const std::string always = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-~";
    std::string o;
    for (unsigned char c : s) {
        if (c < 0x80 && (always.find(static_cast<char>(c)) != std::string::npos || safe.find(static_cast<char>(c)) != std::string::npos)) o += static_cast<char>(c);
        else { char b[4]; std::snprintf(b, sizeof b, "%%%02X", c); o += b; }
    }
    return o;
}
inline int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
inline std::string unquote(const std::string& s) {   // unquote(s): UTF-8, errors='replace'
    if (s.find('%') == std::string::npos) return s;
    std::string o, run;
    auto flush = [&] { if (!run.empty()) { o += decode_utf8_replace(run); run.clear(); } };
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && hexv(s[i + 1]) >= 0 && hexv(s[i + 2]) >= 0) { run += static_cast<char>(hexv(s[i + 1]) * 16 + hexv(s[i + 2])); i += 2; }
        else if (static_cast<unsigned char>(s[i]) < 0x80) run += s[i];
        else { flush(); o += s[i]; }
    }
    flush();
    return o;
}

// ---- the "idna" codec (IDNA 2003) ---------------------------------------------------------------------------------
inline std::string punycode(const std::vector<uint32_t>& in) {   // RFC 3492
    const uint32_t base = 36, tmin = 1, tmax = 26, skew = 38, damp = 700;
    std::string out;
    for (uint32_t c : in) if (c < 0x80) out += static_cast<char>(c);
    const std::size_t b = out.size();
    std::size_t h = b;
    if (b > 0) out += '-';
    uint32_t n = 128, bias = 72;
    uint64_t delta = 0;
    auto digit = [](uint32_t d) { return static_cast<char>(d < 26 ? 'a' + d : '0' + (d - 26)); };
    auto adapt = [&](uint64_t d, std::size_t numpoints, bool first) {
        d = first ? d / damp : d / 2;
        d += d / numpoints;
        uint32_t k = 0;
        while (d > ((base - tmin) * tmax) / 2) { d /= base - tmin; k += base; }
        return static_cast<uint32_t>(k + (base - tmin + 1) * d / (d + skew));
    };
    while (h < in.size()) {
        uint32_t m = 0xFFFFFFFF;
        for (uint32_t c : in) if (c >= n && c < m) m = c;
        delta += static_cast<uint64_t>(m - n) * (h + 1);
        n = m;
        for (uint32_t c : in) {
            if (c < n) delta++;
            if (c == n) {
                uint64_t q = delta;
                for (uint32_t k = base;; k += base) {
                    const uint32_t t = k <= bias ? tmin : (k >= bias + tmax ? tmax : k - bias);
                    if (q < t) break;
                    out += digit(static_cast<uint32_t>(t + (q - t) % (base - t)));
                    q = (q - t) / (base - t);
                }
                out += digit(static_cast<uint32_t>(q));
                bias = adapt(delta, h + 1, h == b);
                delta = 0;
                h++;
            }
        }
        delta++; n++;
    }
    return out;
}
inline std::optional<std::string> idna_encode(const std::string& host) {   // host.encode("idna"); nullopt on UnicodeError
    std::vector<std::string> labels;
    std::string cur;
    for (const auto& ch : cps(host)) {
        const uint32_t c = cp_of(ch);
        if (c == '.' || c == 0x3002 || c == 0xFF0E || c == 0xFF61) { labels.push_back(cur); cur.clear(); }
        else cur += ch;
    }
    labels.push_back(cur);
    std::string trailing;
    if (!labels.empty() && labels.back().empty()) { trailing = "."; labels.pop_back(); }
    std::vector<std::string> out;
    for (const auto& label : labels) {
        bool ascii = true;
        for (unsigned char c : label) if (c >= 0x80) ascii = false;
        std::string res;
        if (ascii) res = label;
        else {
            std::string mapped;   // nameprep: B.1 removed, B.2 case mapping, NFKC
            for (const auto& ch : cps(label)) {
                const uint32_t c = cp_of(ch);
                if (c == 0xAD || c == 0x34F || c == 0x1806 || (c >= 0x180B && c <= 0x180D) || (c >= 0x200B && c <= 0x200D) || c == 0x2060 || (c >= 0xFE00 && c <= 0xFE0F) || c == 0xFEFF) continue;
                mapped += c == 0xDF ? std::string("ss") : lower(ch);
            }
            const std::string prepped = uni::normalize("NFKC", mapped);
            bool now_ascii = true;
            for (unsigned char c : prepped) if (c >= 0x80) now_ascii = false;
            if (now_ascii) res = prepped;
            else {
                if (lower(prepped).compare(0, 4, "xn--") == 0) return std::nullopt;
                std::vector<uint32_t> v;
                for (const auto& ch : cps(prepped)) v.push_back(cp_of(ch));
                res = "xn--" + punycode(v);
            }
        }
        if (res.empty() || res.size() >= 64) return std::nullopt;
        out.push_back(res);
    }
    return join(out, ".") + trailing;
}

}  // namespace pyrt::urlp
