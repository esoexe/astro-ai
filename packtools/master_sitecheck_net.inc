// master_sitecheck_net.inc - master-build/sitecheck below the crawler: fetching (Fetcher), the bytes-level helpers
// (decode_html, image_size, human_bytes). A port of sitecheck.py (master-skills, MIT). sitecheck.py fetches through
// urllib.request; plain http:// is spoken here the way urllib and http.client do it (HTTP/1.1, Connection: close,
// Content-Length / chunked / read-to-close bodies, the same redirect rules and the same error wording), and
// https:// goes through the engine's libcurl (packtools::http_ex_hook), which follows redirects the same way.
#include <cctype>
#include <chrono>
#include <set>

namespace packtools::master_sitecheck {
using namespace pyrt;
namespace sv = packtools::master_serve;
namespace up = pyrt::urlp;

inline const char* const VERSION = "1.0.0";
inline std::string user_agent() { return std::string("sitecheck/") + VERSION + " (+https://github.com/Jakeschincariol/master-skills)"; }
inline constexpr std::size_t PAGE_CAP = 5 * 1024 * 1024;     // bytes read from an HTML page
inline constexpr std::size_t CSS_CAP = 4 * 1024 * 1024;      // bytes read from a stylesheet
inline constexpr std::size_t IMAGE_CAP = 32 * 1024 * 1024;   // bytes read from an image (to measure weight)

inline std::string py_g(double v) { char b[64]; std::snprintf(b, sizeof b, "%g", v); return b; }

// ---- Result ---------------------------------------------------------------------------------------------------------
struct Result {
    std::string url;
    long status = 0;
    std::string final_url, error;
    bool dns_fail = false;
    std::string content_type, charset;
    std::map<std::string, std::string> headers;   // lowercased names, the last one wins (a dict comprehension)
    std::string body;
    std::optional<long long> size;
    bool ok() const { return 200 <= status && status < 400; }
    std::string describe() const { return status ? "HTTP " + std::to_string(status) : (error.empty() ? std::string("no response") : error); }
};

// email.message: get_content_type() and get_content_charset() of a Content-Type value
inline std::string content_type_of(const std::string& value) {
    const std::string ct = strip(lower(strip(value.substr(0, value.find(';')))));
    return std::count(ct.begin(), ct.end(), '/') == 1 ? ct : std::string("text/plain");
}
inline std::vector<std::string> parse_params(const std::string& value) {   // email.message._parseparam
    std::string s = ";" + value;
    std::vector<std::string> out;
    while (!s.empty() && s[0] == ';') {
        s = s.substr(1);
        auto quotes = [&](std::size_t end) {
            long q = 0, esc = 0;
            for (std::size_t k = 0; k < end; ++k) {
                if (s[k] == '"') q++;
                if (k + 1 < end && s[k] == '\\' && s[k + 1] == '"') esc++;
            }
            return (q - esc) % 2 != 0;
        };
        std::size_t end = s.find(';');
        while (end != std::string::npos && end > 0 && quotes(end)) end = s.find(';', end + 1);
        if (end == std::string::npos) end = s.size();
        std::string f = s.substr(0, end);
        if (const auto eq = f.find('='); eq != std::string::npos) f = lower(strip(f.substr(0, eq))) + "=" + strip(f.substr(eq + 1));
        out.push_back(strip(f));
        s = s.substr(end);
    }
    return out;
}
inline std::string unquote_param(const std::string& v) {   // email.utils.unquote
    if (v.size() > 1 && v.front() == '"' && v.back() == '"') {
        std::string inner = v.substr(1, v.size() - 2), o;
        for (std::size_t k = 0; k < inner.size(); ++k) {
            if (inner[k] == '\\' && k + 1 < inner.size() && (inner[k + 1] == '\\' || inner[k + 1] == '"')) { o += inner[k + 1]; ++k; }
            else o += inner[k];
        }
        return o;
    }
    if (v.size() > 1 && v.front() == '<' && v.back() == '>') return v.substr(1, v.size() - 2);
    return v;
}
inline std::string charset_of(const std::string& value) {
    for (const auto& p : parse_params(value)) {
        const auto eq = p.find('=');
        const std::string name = strip(eq == std::string::npos ? p : p.substr(0, eq));
        if (lower(name) != "charset") continue;
        const std::string v = unquote_param(strip(eq == std::string::npos ? std::string() : p.substr(eq + 1)));
        for (unsigned char c : v)
            if (c >= 0x80) return "";
        return lower(v);
    }
    return "";
}
inline bool ascii_digits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

// ---- plain http:// as urllib does it ------------------------------------------------------------------------------
struct NetFail {              // what urllib / http.client raised
    enum Kind { DNS, REFUSED, CONNECT_TIMEOUT, READ_TIMEOUT, OTHER } kind;
    std::string text;
};
struct Response {             // http.client.HTTPResponse, headers read
    long status = 0;
    std::vector<std::pair<std::string, std::string>> headers;   // latin-1 bytes as received
    std::string first(const std::string& name) const {
        for (const auto& [k, v] : headers)
            if (lower(k) == name) return v;
        return "";
    }
    bool has(const std::string& name) const {
        for (const auto& kv : headers)
            if (lower(kv.first) == name) return true;
        return false;
    }
};

class Conn {
public:
    Conn(double timeout) : timeout_(timeout) {}
    ~Conn() { if (s_ != sv::BAD_SOCK) sv::sock_close(s_); }
    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;

    void connect(const std::string& host, int port) {   // socket.create_connection((host, port), timeout)
        addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        if (const int rc = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res); rc != 0) throw NetFail{NetFail::DNS, sv::gai_text(rc)};
        NetFail last{NetFail::OTHER, "getaddrinfo returns an empty list"};
        for (addrinfo* a = res; a; a = a->ai_next) {
            const sv::sock_t s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
            if (s == sv::BAD_SOCK) { last = NetFail{NetFail::OTHER, sv::errno_text(sv::sock_errno())}; continue; }
            sv::set_blocking(s, false);
            int err = 0;
            if (::connect(s, a->ai_addr, static_cast<int>(a->ai_addrlen)) != 0) {
                err = sv::sock_errno();
                if (sv::in_progress(err)) {
                    const int pr = sv::poll_one(s, POLLOUT, ms());
                    if (pr == 0) { sv::sock_close(s); last = NetFail{NetFail::CONNECT_TIMEOUT, "timed out"}; continue; }
                    socklen_t len = sizeof err;
                    err = 0;
                    if (getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len) != 0) err = sv::sock_errno();
                }
            }
            if (err == 0) {
                sv::set_blocking(s, true);
                s_ = s;
                freeaddrinfo(res);
                return;
            }
            sv::sock_close(s);
#if defined(_WIN32)
            const bool refused = err == WSAECONNREFUSED;
#else
            const bool refused = err == ECONNREFUSED;
#endif
            last = NetFail{refused ? NetFail::REFUSED : NetFail::OTHER, sv::errno_text(err)};
        }
        freeaddrinfo(res);
        throw last;
    }
    void send(const std::string& data) {
        if (!sv::send_all(s_, data)) throw NetFail{NetFail::OTHER, sv::errno_text(sv::sock_errno())};
    }
    std::string readline(std::size_t limit) {   // fp.readline(limit)
        for (;;) {
            const std::size_t nl = buf_.find('\n', pos_);
            if (nl != std::string::npos && nl - pos_ + 1 <= limit) return take(nl - pos_ + 1);
            if (buf_.size() - pos_ >= limit) return take(limit);
            if (!fill()) return take(buf_.size() - pos_);
        }
    }
    std::string read(std::size_t n) {   // fp.read(n): n bytes, fewer at the end of the stream
        while (buf_.size() - pos_ < n)
            if (!fill()) break;
        return take(std::min(n, buf_.size() - pos_));
    }

private:
    sv::sock_t s_ = sv::BAD_SOCK;
    double timeout_;
    std::string buf_;
    std::size_t pos_ = 0;
    int ms() const { return static_cast<int>(timeout_ * 1000.0); }
    std::string take(std::size_t n) {
        std::string o = buf_.substr(pos_, n);
        pos_ += n;
        if (pos_ > 65536) { buf_.erase(0, pos_); pos_ = 0; }
        return o;
    }
    bool fill() {
        if (sv::poll_one(s_, POLLIN, ms()) <= 0) throw NetFail{NetFail::READ_TIMEOUT, "timed out"};
        char tmp[65536];
        const auto n = ::recv(s_, tmp, sizeof tmp, 0);
        if (n < 0) throw NetFail{NetFail::OTHER, sv::errno_text(sv::sock_errno())};
        if (n == 0) return false;
        buf_.append(tmp, static_cast<std::size_t>(n));
        return true;
    }
};

inline constexpr std::size_t MAXLINE = 65536;
inline std::string line_text(const std::string& raw) { return sv::latin1_to_utf8(raw); }

// HTTPResponse.begin(): the status line (skipping 100 Continue) and the headers
inline Response read_head(Conn& c) {
    Response r;
    std::string version;
    for (;;) {
        const std::string raw = c.readline(MAXLINE + 1);
        if (raw.size() > MAXLINE) throw NetFail{NetFail::OTHER, "got more than 65536 bytes when reading status line"};
        if (raw.empty()) throw NetFail{NetFail::OTHER, "Remote end closed connection without response"};
        const std::string line = line_text(raw);
        const auto parts = split_ws(line);
        version = parts.empty() ? std::string() : parts[0];
        const std::string status = parts.size() > 1 ? parts[1] : std::string();
        if (version.rfind("HTTP/", 0) != 0) throw NetFail{NetFail::OTHER, line};
        if (status.size() != 3 || !ascii_digits(status)) throw NetFail{NetFail::OTHER, line};
        r.status = std::stol(status);
        if (r.status < 100) throw NetFail{NetFail::OTHER, line};
        if (r.status != 100) break;
        for (;;) {
            const std::string skip = c.readline(MAXLINE + 1);
            if (skip.size() > MAXLINE) throw NetFail{NetFail::OTHER, "got more than 65536 bytes when reading header line"};
            if (strip(skip).empty()) break;
        }
    }
    if (!(version == "HTTP/1.0" || version == "HTTP/0.9" || version.rfind("HTTP/1.", 0) == 0)) throw NetFail{NetFail::OTHER, version};
    for (int count = 0;; ++count) {
        const std::string raw = c.readline(MAXLINE + 1);
        if (raw.size() > MAXLINE) throw NetFail{NetFail::OTHER, "got more than 65536 bytes when reading header line"};
        if (count + 1 > 100) throw NetFail{NetFail::OTHER, "got more than 100 headers"};
        if (raw == "\r\n" || raw == "\n" || raw.empty()) break;
        std::string line = raw;
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        if (!r.headers.empty() && (line[0] == ' ' || line[0] == '\t')) { r.headers.back().second += " " + strip(line); continue; }
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string v = line.substr(colon + 1);
        std::size_t a = 0;
        while (a < v.size() && (v[a] == ' ' || v[a] == '\t')) ++a;
        r.headers.emplace_back(line.substr(0, colon), v.substr(a));
    }
    return r;
}

// HTTPResponse.read(amt)
inline std::string read_body(Conn& c, const Response& r, const std::string& method, std::size_t amt) {
    if (r.status == 204 || r.status == 304 || (r.status >= 100 && r.status < 200) || method == "HEAD") return "";
    if (lower(r.first("transfer-encoding")) == "chunked") {
        std::string out;
        while (out.size() < amt) {
            const std::string raw = c.readline(MAXLINE + 1);
            if (raw.size() > MAXLINE) throw NetFail{NetFail::OTHER, "got more than 65536 bytes when reading chunk size"};
            std::string hex = raw.substr(0, raw.find(';'));
            const std::string h = strip(hex);
            if (raw.empty()) throw NetFail{NetFail::OTHER, "IncompleteRead(" + std::to_string(out.size()) + " bytes read)"};
            bool good = !h.empty();
            for (char ch : h)
                if (!std::isxdigit(static_cast<unsigned char>(ch))) good = false;
            if (!good) throw NetFail{NetFail::OTHER, "invalid literal for int() with base 16: " + repr(Val::str(line_text(hex)))};
            const unsigned long long size = std::stoull(h, nullptr, 16);
            if (size == 0) break;
            const std::size_t want = static_cast<std::size_t>(std::min<unsigned long long>(size, amt - out.size()));
            const std::string piece = c.read(want);
            out += piece;
            if (piece.size() < want) throw NetFail{NetFail::OTHER, "IncompleteRead(" + std::to_string(out.size() - piece.size()) + " bytes read)"};
            if (want < size) break;   // amt reached inside this chunk
            c.read(2);                // the CRLF after the chunk
        }
        return out;
    }
    std::size_t n = amt;
    if (r.has("content-length")) {
        const std::string len = strip(r.first("content-length"));
        if (ascii_digits(len) && len.size() < 18) n = std::min<std::size_t>(n, static_cast<std::size_t>(std::stoll(len)));
    }
    return c.read(n);
}

struct Target { std::string scheme, host_port, host, selector, full_url; int port = 80; };
inline Target target_of(const std::string& url) {   // urllib.request.Request + http.client's _get_hostport
    Target t;
    t.full_url = url;
    std::string u = url;
    if (const auto hash = u.rfind('#'); hash != std::string::npos) u = u.substr(0, hash);
    const auto colon = u.find(':');
    if (colon == std::string::npos || colon == 0 || u.find('/') < colon) throw NetFail{NetFail::OTHER, "unknown url type: " + repr(Val::str(url))};
    t.scheme = lower(u.substr(0, colon));
    const std::string rest = u.substr(colon + 1);
    if (rest.rfind("//", 0) == 0) {
        const std::size_t end = rest.find_first_of("/#?", 2);
        t.host_port = rest.substr(2, end == std::string::npos ? std::string::npos : end - 2);
        t.selector = end == std::string::npos ? std::string() : rest.substr(end);
        if (!t.selector.empty() && t.selector[0] != '/') t.selector = "/" + t.selector;
    }
    if (t.host_port.empty()) throw NetFail{NetFail::OTHER, "no host given"};
    t.host_port = up::unquote(t.host_port);
    std::string host = t.host_port;
    const auto i = host.rfind(':'), j = host.rfind(']');
    if (i != std::string::npos && (j == std::string::npos || i > j)) {
        const std::string p = host.substr(i + 1);
        if (p.empty()) t.port = 80;
        else if (ascii_digits(p) && p.size() < 6) t.port = std::stoi(p);
        else throw NetFail{NetFail::OTHER, "nonnumeric port: '" + p + "'"};
        host = host.substr(0, i);
    }
    if (host.size() > 1 && host.front() == '[' && host.back() == ']') host = host.substr(1, host.size() - 2);
    t.host = host;
    return t;
}

struct Fetched {   // one urllib.request.urlopen() outcome, before sitecheck reads it
    bool http_error = false;   // urllib raised HTTPError (any final status outside 2xx)
    long status = 0;
    std::string final_url, body;
    std::vector<std::pair<std::string, std::string>> headers;
    std::optional<NetFail> body_fail;   // resp.read() raised after the status and headers arrived
};

inline Fetched via_hook(const std::string& url, const std::string& method, const std::vector<std::string>& headers, double timeout,
                        std::size_t max_bytes, bool insecure) {
    const auto hook = packtools::http_ex_hook();
    if (!hook) throw NetFail{NetFail::OTHER, "https needs the A.S.T.R.O AI engine (run this helper through astro-server tool)"};
    packtools::HttpExReq q;
    q.url = url; q.method = method; q.headers = headers; q.timeout_s = timeout; q.max_bytes = method == "HEAD" ? 0 : max_bytes; q.insecure = insecure;
    const packtools::HttpExResp r = hook(q);
    if (r.status == 0) {
        if (r.dns_fail) throw NetFail{NetFail::DNS, r.error};
        if (r.refused) throw NetFail{NetFail::REFUSED, r.error};
        if (r.timed_out) throw NetFail{NetFail::READ_TIMEOUT, r.error};
        if (r.tls_untrusted) throw NetFail{NetFail::OTHER, "TLS certificate not trusted (for a local dev certificate, add --insecure)"};
        throw NetFail{NetFail::OTHER, r.error.empty() ? std::string("no response") : r.error};
    }
    Fetched f;
    f.status = r.status;
    f.final_url = r.final_url.empty() ? url : r.final_url;
    f.headers = r.headers;
    f.http_error = !(r.status >= 200 && r.status < 300);
    if (!f.http_error) f.body = r.body;
    return f;
}

// urllib.request: OpenerDirector.open with HTTPRedirectHandler (max 10 redirects, the same URL at most 4 times),
// HTTPErrorProcessor (anything outside 2xx raises HTTPError) and HTTPDefaultErrorHandler
inline Fetched urlopen(std::string url, std::string method, const std::vector<std::pair<std::string, std::string>>& extra, double timeout,
                       std::size_t max_bytes, bool insecure) {
    std::map<std::string, int> visited;
    std::vector<std::string> hook_headers = {"User-Agent: " + user_agent(), "Accept-Encoding: identity", "Accept: text/html,application/xhtml+xml,*/*;q=0.8"};
    for (const auto& [k, v] : extra) hook_headers.push_back(k + ": " + v);
    for (;;) {
        const Target t = target_of(url);
        if (t.scheme == "https") return via_hook(url, method, hook_headers, timeout, max_bytes, insecure);
        if (t.scheme != "http") throw NetFail{NetFail::OTHER, "unknown url type: " + t.scheme};
        Conn c(timeout);
        try {
            c.connect(t.host, t.port);
            std::string req = method + " " + (t.selector.empty() ? std::string("/") : t.selector) + " HTTP/1.1\r\n";
            req += "Host: " + t.host_port + "\r\n";
            for (const auto& h : hook_headers) req += h + "\r\n";
            req += "Connection: close\r\n\r\n";
            c.send(req);
        } catch (NetFail& f) {   // h.request() failures are wrapped in URLError: a timeout there reads "timed out"
            if (f.kind == NetFail::READ_TIMEOUT) f.kind = NetFail::CONNECT_TIMEOUT;
            throw;
        }
        const Response r = read_head(c);
        if (r.status >= 200 && r.status < 300) {
            Fetched f;
            f.status = r.status; f.final_url = t.full_url; f.headers = r.headers;
            if (method != "HEAD" && max_bytes) {
                try { f.body = read_body(c, r, method, max_bytes + 1); } catch (const NetFail& e) { f.body_fail = e; }
            }
            return f;
        }
        const bool redirect = r.status == 301 || r.status == 302 || r.status == 303 || r.status == 307 || r.status == 308;
        std::string loc;
        bool have_loc = false;
        if (redirect) {
            if (r.has("location")) { loc = r.first("location"); have_loc = true; }
            else if (r.has("uri")) { loc = r.first("uri"); have_loc = true; }
        }
        Fetched err;
        err.http_error = true; err.status = r.status; err.final_url = t.full_url; err.headers = r.headers;
        if (!have_loc) return err;
        // http_error_302
        up::ParseResult parts;
        try { parts = up::urlparse(line_text(loc)); } catch (const PyErr&) { throw NetFail{NetFail::OTHER, "Invalid IPv6 URL"}; }
        const std::string sch = parts.scheme;
        if (!(sch == "http" || sch == "https" || sch == "ftp" || sch.empty())) { err.final_url = line_text(loc); return err; }
        if (parts.path.empty() && !parts.netloc.empty()) parts.path = "/";
        std::string newurl = up::urlunparse(parts);
        std::string raw_new;   // quote(newurl, encoding="iso-8859-1", safe=string.punctuation)
        {
            std::string latin;   // back to the latin-1 bytes
            for (const auto& ch : cps(newurl)) {
                const uint32_t cp = cp_of(ch);
                latin += cp < 0x100 ? std::string(1, static_cast<char>(cp)) : std::string("?");
            }
            raw_new = up::quote(latin, "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~");
        }
        newurl = up::urljoin(t.full_url, raw_new);
        size_t spaces = 0;
        while ((spaces = newurl.find(' ')) != std::string::npos) newurl.replace(spaces, 1, "%20");
        if (!visited.empty()) {
            const auto seen = visited.find(newurl);
            if ((seen != visited.end() && seen->second >= 4) || visited.size() >= 10) return err;
        }
        visited[newurl] += 1;
        if (method != "HEAD") method = "GET";
        url = newurl;
    }
}

inline std::mutex& count_mutex() { static std::mutex m; return m; }

class Fetcher {
public:
    Fetcher(double timeout, bool insecure) : timeout(timeout), insecure(insecure) {}
    double timeout;
    bool insecure;
    long count = 0;

    Result fetch(const std::string& url, const std::string& method = "GET", std::size_t max_bytes = PAGE_CAP,
                 const std::vector<std::pair<std::string, std::string>>& extra = {}) {
        Result res = once(url, method, max_bytes, extra);
        const bool transient = (res.status == 0 && !res.dns_fail) || res.status == 502 || res.status == 503 || res.status == 504;
        if (transient) {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            res = once(url, method, max_bytes, extra);
        }
        return res;
    }
    // status check suited to the kind of URL (downloads images to weigh them)
    Result check(const std::string& url, const std::string& kind) {
        if (kind == "img") return fetch(url, "GET", IMAGE_CAP);
        if (kind == "css") return fetch(url, "GET", CSS_CAP);
        Result res = fetch(url, "HEAD", 0);
        if (res.status >= 400 || (res.status == 0 && !res.dns_fail && res.error.find("timed out") == std::string::npos)) {
            std::vector<std::pair<std::string, std::string>> extra;
            if (kind == "media") extra.emplace_back("Range", "bytes=0-1023");
            res = fetch(url, "GET", 2048, extra);
        }
        return res;
    }

private:
    Result once(const std::string& url, const std::string& method, std::size_t max_bytes, const std::vector<std::pair<std::string, std::string>>& extra) {
        {
            std::lock_guard<std::mutex> lock(count_mutex());
            count += 1;
        }
        Result res;
        res.url = url;
        try {
            const Fetched f = urlopen(url, method, extra, timeout, max_bytes, insecure);
            res.status = f.status;
            res.final_url = f.final_url.empty() ? url : f.final_url;
            for (const auto& [k, v] : f.headers) res.headers[lower(line_text(k))] = line_text(v);
            if (f.http_error) return res;
            bool have_ct = false;
            for (const auto& kv : f.headers)
                if (lower(kv.first) == "content-type") {
                    res.content_type = content_type_of(line_text(kv.second));
                    res.charset = charset_of(line_text(kv.second));
                    have_ct = true;
                    break;
                }
            (void)have_ct;
            const auto len = res.headers.find("content-length");
            if (len != res.headers.end() && ascii_digits(len->second) && len->second.size() < 18) res.size = std::stoll(len->second);
            if (f.body_fail) throw *f.body_fail;   // status, headers and size stay, as in sitecheck.py
            if (method != "HEAD" && max_bytes) {
                if (f.body.size() <= max_bytes && res.status == 200) res.size = static_cast<long long>(f.body.size());
                res.body = f.body.substr(0, max_bytes);
            }
        } catch (const NetFail& e) {
            switch (e.kind) {
                case NetFail::DNS: res.error = "domain does not resolve"; res.dns_fail = true; break;
                case NetFail::REFUSED: res.error = "connection refused (is the server running?)"; break;
                case NetFail::CONNECT_TIMEOUT: res.error = "timed out"; break;
                case NetFail::READ_TIMEOUT: res.error = "timed out after " + py_g(timeout) + "s"; break;
                default: res.error = cpslice(e.text, 0, 160); break;
            }
        } catch (const std::exception& e) {
            res.error = cpslice(e.what(), 0, 160);
        }
        return res;
    }
};

inline bool is_html(const Result& res) {
    if (!res.content_type.empty()) return res.content_type == "text/html" || res.content_type == "application/xhtml+xml";
    std::string head = res.body.substr(0, 512);
    std::size_t a = 0;
    while (a < head.size() && (head[a] == ' ' || (head[a] >= 9 && head[a] <= 13) || (head[a] >= 0x1C && head[a] <= 0x1F))) ++a;
    head = head.substr(a);
    for (auto& ch : head)
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    return head.rfind("<!doctype html", 0) == 0 || head.rfind("<html", 0) == 0;
}

// ---- bytes ----------------------------------------------------------------------------------------------------------
// bytes.decode(charset, errors="replace") for the codecs pages declare; an unknown name falls back to UTF-8 as the
// LookupError branch does. (Python knows many more codecs; pages in those are decoded as UTF-8 here.)
inline std::string codec_key(const std::string& name) {   // encodings.normalize_encoding, lowercased
    std::string o; bool pending = false;
    for (char c : name) {
        const bool keep = std::isalnum(static_cast<unsigned char>(c)) || c == '.';
        if (keep) { if (pending && !o.empty()) o += '_'; pending = false; o += static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
        else pending = true;
    }
    return o;
}
inline std::string decode_bytes(const std::string& body, const std::string& charset) {
    const std::string k = codec_key(charset);
    static const std::set<std::string> latin1 = {"latin_1", "latin1", "iso_8859_1", "iso8859_1", "8859", "cp819", "l1", "latin", "iso_ir_100", "csisolatin1", "ibm819"};
    static const std::set<std::string> ascii = {"ascii", "us_ascii", "646", "ansi_x3.4_1968", "ansi_x3_4_1968", "ansi_x3.4_1986", "cp367", "csascii", "ibm367", "iso646_us", "iso_646.irv_1991", "iso_ir_6", "us"};
    static const std::set<std::string> cp1252 = {"cp1252", "windows_1252", "1252"};
    if (latin1.count(k)) return sv::latin1_to_utf8(body);
    if (ascii.count(k)) {
        std::string o;
        for (unsigned char c : body) o += c < 0x80 ? std::string(1, static_cast<char>(c)) : std::string("\xEF\xBF\xBD");
        return o;
    }
    if (cp1252.count(k)) {
        static const uint32_t hi[32] = {0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
                                        0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178};
        std::string o;
        for (unsigned char c : body) {
            if (c < 0x80) o += static_cast<char>(c);
            else if (c < 0xA0) o += hi[c - 0x80] ? utf8(hi[c - 0x80]) : std::string("\xEF\xBF\xBD");
            else o += utf8(c);
        }
        return o;
    }
    return decode_utf8_replace(body);
}
inline std::string decode_html(const std::string& body, const std::string& charset) {
    std::string cs = charset;
    if (body.rfind("\xEF\xBB\xBF", 0) == 0) return "\xEF\xBB\xBF" + decode_utf8_replace(body.substr(3));
    if (cs.empty()) {
        const std::string head = sv::latin1_to_utf8(body.substr(0, 4096));   // one code point per byte, as a bytes pattern sees it
        const std::string pat = "<meta[^>]+charset[ \\t\\n\\r\\f\\v]*=[ \\t\\n\\r\\f\\v]*[\"']?([A-Za-z0-9_\\-]+)";
        const auto& rx = re::compile(pat, re::I);
        const auto m = rx.search(head);
        if (m.ok) cs = m.group(1);
    }
    return decode_bytes(body, cs.empty() ? std::string("utf-8") : cs);
}

inline uint32_t be16(const std::string& d, std::size_t i) { return (static_cast<uint32_t>(static_cast<unsigned char>(d[i])) << 8) | static_cast<unsigned char>(d[i + 1]); }
inline uint32_t le16(const std::string& d, std::size_t i) { return (static_cast<uint32_t>(static_cast<unsigned char>(d[i + 1])) << 8) | static_cast<unsigned char>(d[i]); }
inline uint32_t be32(const std::string& d, std::size_t i) { return (be16(d, i) << 16) | be16(d, i + 2); }
inline uint32_t le24(const std::string& d, std::size_t i) { return le16(d, i) | (static_cast<uint32_t>(static_cast<unsigned char>(d[i + 2])) << 16); }
inline uint32_t le32(const std::string& d, std::size_t i) { return le16(d, i) | (le16(d, i + 2) << 16); }

// (width, height) from PNG, GIF, JPEG, WebP or AVIF headers
inline std::optional<std::pair<long long, long long>> image_size(const std::string& d) {
    using WH = std::pair<long long, long long>;
    const std::size_t n = d.size();
    if (n == 0) return std::nullopt;
    if (d.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0 && n >= 16 && d.compare(12, 4, "IHDR") == 0) {
        if (n < 24) return std::nullopt;
        return WH{be32(d, 16), be32(d, 20)};
    }
    if ((d.compare(0, 6, "GIF87a") == 0 || d.compare(0, 6, "GIF89a") == 0) && n >= 10) return WH{le16(d, 6), le16(d, 8)};
    if (n >= 2 && static_cast<unsigned char>(d[0]) == 0xFF && static_cast<unsigned char>(d[1]) == 0xD8) {
        std::size_t i = 2;
        while (i + 9 < n) {
            if (static_cast<unsigned char>(d[i]) != 0xFF) { i += 1; continue; }
            const unsigned marker = static_cast<unsigned char>(d[i + 1]);
            if (marker == 0xFF) { i += 1; continue; }
            if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) { i += 2; continue; }
            const uint32_t seg = be16(d, i + 2);
            static const std::set<unsigned> sof = {0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF};
            if (sof.count(marker)) return WH{be16(d, i + 7), be16(d, i + 5)};
            i += 2 + seg;
        }
        return std::nullopt;
    }
    if (n >= 12 && d.compare(0, 4, "RIFF") == 0 && d.compare(8, 4, "WEBP") == 0 && n >= 30) {
        const std::string chunk = d.substr(12, 4);
        if (chunk == "VP8 " && d.compare(23, 3, "\x9d\x01\x2a", 3) == 0) return WH{le16(d, 26) & 0x3FFF, le16(d, 28) & 0x3FFF};
        if (chunk == "VP8L" && static_cast<unsigned char>(d[20]) == 0x2F) {
            const uint32_t bits = le32(d, 21);
            return WH{(bits & 0x3FFF) + 1, ((bits >> 14) & 0x3FFF) + 1};
        }
        if (chunk == "VP8X") return WH{static_cast<long long>(le24(d, 24)) + 1, static_cast<long long>(le24(d, 27)) + 1};
    }
    if (n >= 12 && d.compare(4, 4, "ftyp") == 0 && (d.compare(8, 4, "avif") == 0 || d.compare(8, 4, "avis") == 0 || d.compare(8, 4, "mif1") == 0)) {
        const auto k = d.find("ispe");
        if (k != std::string::npos && k + 16 <= n) return WH{be32(d, k + 8), be32(d, k + 12)};
    }
    return std::nullopt;
}

inline std::string human_bytes(std::optional<long long> n) {
    if (!n) return "unknown size";
    char b[64];
    if (*n >= 1024 * 1024) std::snprintf(b, sizeof b, "%.1f MB", static_cast<double>(*n) / 1048576.0);
    else std::snprintf(b, sizeof b, "%lld KB", static_cast<long long>(std::nearbyint(static_cast<double>(*n) / 1024.0)));
    return b;
}

}  // namespace packtools::master_sitecheck
