// master_serve.inc - master-build/serve: preview a static site locally, the way static hosts serve it. A port of
// serve.py (master-skills, MIT): its PreviewHandler on top of Python's http.server (HTTP/1.0, one request per
// connection, a thread per connection), with no-cache headers, modern MIME types, byte ranges, host-style 404s,
// --clean-urls, --spa, a free-port search and a --lan URL. The sockets and URL helpers are in master_serve_net.inc.
namespace packtools::master_serve {

inline const char* const VERSION = "1.0.0";

struct Cfg { std::string directory; bool spa = false, clean_urls = false, verbose = false, quiet = false; };

inline std::mutex& log_mutex() { static std::mutex m; return m; }
inline void log_line(const std::string& s) {
    const std::lock_guard<std::mutex> g(log_mutex());
    std::fwrite(s.data(), 1, s.size(), stderr);
    std::fflush(stderr);
}

// http.HTTPStatus phrases and descriptions (Python 3.13 / RFC 9110 names)
inline std::pair<std::string, std::string> status_text(int code) {
    switch (code) {
        case 200: return {"OK", "Request fulfilled, document follows"};
        case 206: return {"Partial Content", "Partial content follows"};
        case 301: return {"Moved Permanently", "Object moved permanently -- see URI list"};
        case 400: return {"Bad Request", "Bad request syntax or unsupported method"};
        case 404: return {"Not Found", "Nothing matches the given URI"};
        case 414: return {"URI Too Long", "URI is too long"};
        case 416: return {"Range Not Satisfiable", "Cannot satisfy request range"};
        case 431: return {"Request Header Fields Too Large", "The server is unwilling to process the request because its header fields are too large"};
        case 501: return {"Not Implemented", "Server does not support this operation"};
        case 505: return {"HTTP Version Not Supported", "Cannot fulfill request"};
        default: return {"???", "???"};
    }
}
inline std::string html_escape_noquote(const std::string& s) {   // html.escape(s, quote=False)
    std::string o;
    for (char c : s) {
        if (c == '&') o += "&amp;";
        else if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else o += c;
    }
    return o;
}
inline std::string control_escape(const std::string& s) {   // BaseHTTPRequestHandler._control_char_table
    std::string o;
    for (const auto& ch : cps(s)) {
        const uint32_t c = cp_of(ch);
        if (c < 0x20 || (c >= 0x7f && c < 0xa0)) { char b[8]; std::snprintf(b, sizeof b, "\\x%02x", static_cast<unsigned>(c)); o += b; }
        else if (c == '\\') o += "\\\\";
        else o += ch;
    }
    return o;
}
inline std::string str_repr(const std::string& s) { return repr(Val::str(s)); }

// the request handler hit an exception: socketserver prints a report and drops the connection
struct HandlerError { std::string text; };

struct Handler {
    sock_t sock;
    std::string peer_ip;
    int peer_port = 0;
    std::shared_ptr<const Cfg> cfg;
    std::string buf;
    std::size_t pos = 0;
    bool eof = false;
    std::string command, path, request_version = "HTTP/0.9";
    bool have_command = false, have_path = false;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string out;

    std::string readline(std::size_t limit) {   // rfile.readline(limit)
        for (;;) {
            const std::size_t nl = buf.find('\n', pos);
            if (nl != std::string::npos && nl - pos + 1 <= limit) { std::string l = buf.substr(pos, nl - pos + 1); pos = nl + 1; return l; }
            if (buf.size() - pos >= limit) { std::string l = buf.substr(pos, limit); pos += limit; return l; }
            if (eof) { std::string l = buf.substr(pos); pos = buf.size(); return l; }
            if (pos > (1u << 20)) { buf.erase(0, pos); pos = 0; }
            char tmp[16384];
            const auto k = ::recv(sock, tmp, static_cast<int>(sizeof tmp), 0);
            if (k <= 0) eof = true;
            else buf.append(tmp, static_cast<std::size_t>(k));
        }
    }
    std::string header(const std::string& name) const {   // self.headers.get(name): the first one
        const std::string want = lower(name);
        for (const auto& [k, v] : headers)
            if (lower(k) == want) return v;
        return "";
    }

    // ---- BaseHTTPRequestHandler output ----
    void log_message(const std::string& msg) const {   // serve.py logs these only with --verbose
        if (cfg->verbose && !cfg->quiet) log_line(peer_ip + " - - [" + log_date(std::time(nullptr)) + "] " + control_escape(msg) + "\n");
    }
    void log_request(int code) const {   // PreviewHandler.log_request
        if (cfg->quiet) return;
        if (!have_path) throw HandlerError{"AttributeError: 'BoundPreviewHandler' object has no attribute 'path'"};
        if (cfg->verbose || code >= 400) log_line("  " + std::to_string(code) + " " + (have_command ? command : "None") + " " + path + "\n");
    }
    void send_header(const std::string& k, const std::string& v) {
        if (request_version != "HTTP/0.9") out += k + ": " + v + "\r\n";
    }
    void send_response(int code, const std::string& message = "") {
        log_request(code);
        if (request_version != "HTTP/0.9")
            out += "HTTP/1.0 " + std::to_string(code) + " " + (message.empty() ? status_text(code).first : message) + "\r\n";
        send_header("Server", std::string("build-serve/") + VERSION);
        send_header("Date", http_date(std::time(nullptr)));
    }
    bool end_headers() {
        send_header("Cache-Control", "no-store");
        if (request_version != "HTTP/0.9") out += "\r\n";
        const bool ok = send_all(sock, out);
        out.clear();
        return ok;
    }
    void send_error(int code, std::string message = "", std::string explain = "") {
        const auto [shortmsg, longmsg] = status_text(code);
        if (message.empty()) message = shortmsg;
        if (explain.empty()) explain = longmsg;
        log_message("code " + std::to_string(code) + ", message " + message);
        send_response(code, message);
        send_header("Connection", "close");
        std::string body;
        if (code >= 200 && code != 204 && code != 205 && code != 304) {
            body = "<!DOCTYPE HTML>\n<html lang=\"en\">\n    <head>\n        <meta charset=\"utf-8\">\n"
                   "        <title>Error response</title>\n    </head>\n    <body>\n        <h1>Error response</h1>\n"
                   "        <p>Error code: " + std::to_string(code) + "</p>\n"
                   "        <p>Message: " + html_escape_noquote(message) + ".</p>\n"
                   "        <p>Error code explanation: " + std::to_string(code) + " - " + html_escape_noquote(explain) + ".</p>\n"
                   "    </body>\n</html>\n";
            send_header("Content-Type", "text/html;charset=utf-8");
            send_header("Content-Length", std::to_string(body.size()));
        }
        end_headers();
        if (command != "HEAD" && !body.empty()) send_all(sock, body);
    }

    // ---- BaseHTTPRequestHandler.parse_request ----
    bool parse_request(const std::string& raw) {
        std::string line = latin1_to_utf8(raw);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        const auto words = split_ws(line);
        if (words.empty()) return false;
        if (words.size() >= 3) {
            const std::string version = words.back();
            bool ok = starts(version, "HTTP/");
            std::string base;
            long long major = 0, minor = 0;
            if (ok) {
                base = version.substr(5);
                const auto parts = split(base, ".");
                ok = parts.size() == 2;
                for (const auto& c : parts) {
                    if (c.empty() || c.size() > 10) ok = false;
                    for (char d : c)
                        if (d < '0' || d > '9') ok = false;
                }
                if (ok) { major = std::stoll(parts[0]); minor = std::stoll(parts[1]); }
            }
            if (!ok) { send_error(400, "Bad request version (" + str_repr(version) + ")"); return false; }
            (void)minor;
            if (major >= 2) { send_error(505, "Invalid HTTP version (" + base + ")"); return false; }
            request_version = version;
        }
        if (words.size() < 2 || words.size() > 3) { send_error(400, "Bad request syntax (" + str_repr(line) + ")"); return false; }
        if (words.size() == 2 && words[0] != "GET") { send_error(400, "Bad HTTP/0.9 request type (" + str_repr(words[0]) + ")"); return false; }
        command = words[0]; path = words[1]; have_command = have_path = true;
        if (starts(path, "//")) { std::size_t k = 0; while (k < path.size() && path[k] == '/') k++; path = "/" + path.substr(k); }
        // http.client.parse_headers, read the way the email parser does for the header we use
        std::vector<std::string> lines;
        for (;;) {
            const std::string l = readline(65537);
            if (l.size() > 65536) { send_error(431, "Line too long", "got more than 65536 bytes when reading header line"); return false; }
            lines.push_back(l);
            if (lines.size() > 100) { send_error(431, "Too many headers", "got more than 100 headers"); return false; }
            if (l == "\r\n" || l == "\n" || l.empty()) break;
        }
        for (const auto& rawl : lines) {
            std::string l = latin1_to_utf8(rawl);
            while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();
            if (l.empty()) break;
            if ((l[0] == ' ' || l[0] == '\t')) {
                if (!headers.empty()) headers.back().second += l;
                continue;
            }
            const auto colon = l.find(':');
            bool name_ok = colon != std::string::npos;
            for (std::size_t k = 0; name_ok && k < colon; ++k)
                if (static_cast<unsigned char>(l[k]) < 0x21 || static_cast<unsigned char>(l[k]) > 0x7e) name_ok = false;
            if (!name_ok) break;   // the email parser treats the rest as a body
            std::string v = l.substr(colon + 1);
            std::size_t a = 0;
            while (a < v.size() && (v[a] == ' ' || v[a] == '\t')) a++;
            headers.emplace_back(l.substr(0, colon), v.substr(a));
        }
        return true;
    }

    // ---- PreviewHandler ----
    struct Target { bool redirected = false; std::string fs; int status = 0; };
    Target not_found(const std::string& url_path) const {
        const std::string ext = lower(posix_splitext(url_path).second);
        if (cfg->clean_urls && ext.empty() && url_path != "" && url_path != "/") {
            std::string base = url_path;
            while (!base.empty() && base.back() == '/') base.pop_back();
            const std::string cand = translate_path(cfg->directory, base + ".html");
            if (is_file(cand)) return {false, cand, 200};
        }
        if (cfg->spa && (ext.empty() || ext == ".html" || ext == ".htm")) {
            const std::string index = path_join2(cfg->directory, "index.html");
            if (is_file(index)) return {false, index, 200};
        }
        const std::string custom = path_join2(cfg->directory, "404.html");
        if (is_file(custom)) return {false, custom, 404};
        return {false, "", 404};
    }
    Target resolve() {
        const Split parts = urlsplit(path);
        const std::string url_path = parts.path.empty() ? "/" : parts.path;
        const std::string fs = translate_path(cfg->directory, path);
        if (is_dir(fs)) {
            if (url_path.empty() || url_path.back() != '/') {
                send_response(301);
                send_header("Location", urlunsplit_path(url_path + "/", parts.query));
                send_header("Content-Length", "0");
                end_headers();
                return {true, "", 0};
            }
            const std::string index = path_join2(fs, "index.html");
            return is_file(index) ? Target{false, index, 200} : not_found(url_path);
        }
        if (is_file(fs)) return {false, fs, 200};
        return not_found(url_path);
    }
    void send_head(bool body) {
        const Target t = resolve();
        if (t.redirected) return;
        if (t.fs.empty()) { send_error(404, "File not found"); return; }
        std::ifstream f(fspath(t.fs), std::ios::binary);
        std::error_code ec;
        const auto fsize = std::filesystem::file_size(fspath(t.fs), ec);
        if (!f || ec) { send_error(404, "File not found"); return; }
        const long long size = static_cast<long long>(fsize);
        long long start = 0, end = size - 1;
        const std::string rng = header("Range");
        const Range parsed = (!rng.empty() && t.status == 200 && size) ? parse_range(rng, size) : Range{};
        if (parsed.kind == RangeKind::Unsatisfiable) {
            send_response(416);
            send_header("Content-Range", "bytes */" + std::to_string(size));
            send_header("Content-Length", "0");
            end_headers();
            return;
        }
        if (parsed.kind == RangeKind::Part) {
            start = parsed.start; end = parsed.end;
            send_response(206);
            send_header("Content-Range", "bytes " + std::to_string(start) + "-" + std::to_string(end) + "/" + std::to_string(size));
        } else {
            send_response(t.status);
        }
        const long long length = size ? end - start + 1 : 0;
        std::time_t mtime = 0;
        file_mtime(t.fs, mtime);
        send_header("Content-Type", guess_type(t.fs));
        send_header("Content-Length", std::to_string(length));
        send_header("Accept-Ranges", "bytes");
        send_header("Last-Modified", http_date(mtime));
        if (!end_headers() || !body) return;
        f.seekg(start);
        long long remaining = length;
        std::vector<char> chunk(65536);
        while (remaining > 0) {   // copyfile: a cancelled download (video seeking) just stops
            f.read(chunk.data(), static_cast<std::streamsize>(remaining < 65536 ? remaining : 65536));
            const auto got = f.gcount();
            if (got <= 0) break;
            if (!send_all(sock, chunk.data(), static_cast<std::size_t>(got))) break;
            remaining -= got;
        }
    }

    void handle_one_request() {
        const std::string raw = readline(65537);
        if (raw.size() > 65536) { request_version = ""; command = ""; have_command = true; send_error(414); return; }
        if (raw.empty()) return;
        if (!parse_request(raw)) return;
        if (command == "GET") send_head(true);
        else if (command == "HEAD") send_head(false);
        else send_error(501, "Unsupported method (" + str_repr(command) + ")");
    }
    void run() {
        try {
            handle_one_request();
        } catch (const HandlerError& e) {
            report(e.text);
        } catch (const PyErr& e) {
            report(e.type + ": " + e.what());
        } catch (const std::exception& e) {
            report(std::string("Exception: ") + e.what());
        }
#if defined(_WIN32)
        ::shutdown(sock, SD_SEND);
#else
        ::shutdown(sock, SHUT_WR);
#endif
        sock_close(sock);
    }
    void report(const std::string& what) const {   // socketserver.BaseServer.handle_error, without the traceback
        log_line("----------------------------------------\nException occurred during processing of request from ('" + peer_ip + "', " +
                 std::to_string(peer_port) + ")\n" + what + "\n----------------------------------------\n");
    }
};

inline volatile std::sig_atomic_t g_stop = 0;
inline void on_sigint(int) { g_stop = 1; }

inline void open_browser(const std::string& url) {   // webbrowser.open: best effort, never an error
#if defined(_WIN32)
    const int rc = std::system(("start \"\" \"" + url + "\"").c_str()); (void)rc;
#elif defined(__APPLE__)
    const int rc = std::system(("open '" + url + "' >/dev/null 2>&1 &").c_str()); (void)rc;
#else
    const int rc = std::system(("xdg-open '" + url + "' >/dev/null 2>&1 &").c_str()); (void)rc;
#endif
}

inline std::string path_str(const std::filesystem::path& p) { const std::u8string u = p.u8string(); return std::string(u.begin(), u.end()); }
inline std::string abspath(const std::string& p) {   // os.path.abspath
#if defined(_WIN32)
    std::error_code ec;
    auto a = std::filesystem::absolute(fspath(p), ec).lexically_normal();
    std::string s = path_str(a);
    while (s.size() > 3 && (s.back() == '\\' || s.back() == '/')) s.pop_back();
    return s;
#else
    if (!p.empty() && p[0] == '/') return normpath(p);
    std::error_code ec;
    const std::string cwd = path_str(std::filesystem::current_path(ec));
    return normpath(cwd + "/" + p);
#endif
}

// serve_forever: a thread per connection until stop() says so (checked every 200 ms)
template <class Stop>
void accept_loop(sock_t ls, const std::shared_ptr<const Cfg>& cfg, Stop stop) {
    while (!stop()) {
        if (poll_one(ls, POLLIN, 200) <= 0) continue;
        sockaddr_in peer{}; socklen_t len = sizeof peer;
        const sock_t c = ::accept(ls, reinterpret_cast<sockaddr*>(&peer), &len);
        if (c == BAD_SOCK) continue;
#if defined(SO_NOSIGPIPE)
        int one = 1; setsockopt(c, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
        char ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof ip);
        auto h = std::make_shared<Handler>();
        h->sock = c; h->peer_ip = ip; h->peer_port = ntohs(peer.sin_port); h->cfg = cfg;
        std::thread([h] { h->run(); }).detach();   // daemon_threads: a stuck client never blocks the server
    }
}

// serve.start()/stop(): a background server for other helpers (sitecheck checks a folder through it). 127.0.0.1,
// any free port, quiet; start() returns the base URL ("http://127.0.0.1:PORT/") or throws OSError.
struct Background {
    sock_t ls = BAD_SOCK;
    std::atomic<bool> stopping{false};
    std::thread thread;
    std::string base;
};
inline std::shared_ptr<Background> start(const std::string& root, bool spa, bool clean_urls, bool quiet = true) {
    if (!is_dir(root)) throw PyErr("FileNotFoundError", "not a directory: " + root);
    auto cfg = std::make_shared<Cfg>();
    cfg->directory = abspath(root); cfg->spa = spa; cfg->clean_urls = clean_urls; cfg->quiet = quiet;
    auto bg = std::make_shared<Background>();
    std::string err; int port = 0;
    bg->ls = bind_listen("127.0.0.1", 0, err, port);
    if (bg->ls == BAD_SOCK) throw PyErr("OSError", "could not bind 127.0.0.1:0 (" + err + ")");
    bg->base = "http://127.0.0.1:" + std::to_string(port) + "/";
    Background* raw = bg.get();
    std::shared_ptr<const Cfg> ccfg = cfg;
    bg->thread = std::thread([raw, ccfg] { accept_loop(raw->ls, ccfg, [raw] { return raw->stopping.load(); }); });
    return bg;
}
inline void stop(const std::shared_ptr<Background>& bg) {
    if (!bg || bg->ls == BAD_SOCK) return;
    bg->stopping = true;
    if (bg->thread.joinable()) bg->thread.join();
    sock_close(bg->ls);
    bg->ls = BAD_SOCK;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("serve", "Preview a static site locally the way static hosts serve it (no-cache, modern MIME types, byte ranges, host-style 404s).");
    p.epilog = "Example: serve site/ --lan   (then open the printed URL)";
    p.pos("dir").nargs("?").def(Val::str(".")).help("folder to serve (default: current folder)");
    p.opt({"--port"}).integer().def(8000).help("port to try first (default 8000; 0 = any free port)");
    p.opt({"--host"}).def("127.0.0.1").help("address to bind (default 127.0.0.1, this computer only)");
    p.opt({"--lan"}).store_true().help("also listen on your Wi-Fi so a phone can open the site");
    p.opt({"--spa"}).store_true().help("serve index.html for unknown routes (single-page apps)");
    p.opt({"--clean-urls"}).store_true().help("serve about.html for /about (like Netlify, GitHub Pages)");
    p.opt({"--open"}).store_true().help("open the site in your default browser");
    p.opt({"--verbose"}).store_true().help("log every request (default: only 404s and errors)");
    p.opt({"--version"}).version(std::string("serve ") + VERSION);
    const Val a = parse_args(p, argv);

    const std::string dir = S(a, "dir");
    const std::string root = abspath(dir);
    if (!is_dir(root)) { eprint("serve: not a folder: " + dir); return 2; }
#if defined(_WIN32)
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    const bool lan = B(a, "lan"), spa = B(a, "spa"), clean = B(a, "clean_urls");
    const std::string host = lan ? "0.0.0.0" : S(a, "host");
    const long long want = get(a, "port").i;
    auto cfg = std::make_shared<Cfg>();
    cfg->directory = root; cfg->spa = spa; cfg->clean_urls = clean; cfg->verbose = B(a, "verbose");

    // bind(): the port, or the next free one of 20; port 0 picks any free port
    std::vector<long long> candidates;
    if (want == 0) candidates.push_back(0);
    else for (long long q = want; q < want + 20; ++q) candidates.push_back(q);
    std::string last_error = "None";
    sock_t ls = BAD_SOCK; int port = 0;
    for (const long long q : candidates) {
        if (q < 0 || q > 65535) throw PyErr("OverflowError", "getsockaddrarg: port must be 0-65535.");
        if (q && port_in_use(host, static_cast<int>(q))) { last_error = "port " + std::to_string(q) + " is in use"; continue; }
        std::string err;
        ls = bind_listen(host, static_cast<int>(q), err, port);
        if (ls != BAD_SOCK) break;
        last_error = err;
    }
    if (ls == BAD_SOCK) { eprint("serve: could not bind " + host + ":" + std::to_string(want) + " (" + last_error + ")"); return 2; }

    const std::string local = "http://127.0.0.1:" + std::to_string(port) + "/";
    std::string banner = "Serving " + root + "\n  Local:  " + local + "\n";
    if (lan) {
        const std::string ip = lan_ip();
        banner += ip.empty() ? "  Phone:  no Wi-Fi address found\n" : "  Phone:  http://" + ip + ":" + std::to_string(port) + "/   (same Wi-Fi)\n";
    }
    if (!is_file(path_join2(root, "index.html")) && !spa) banner += "  Note:   no index.html in this folder, so / will 404\n";
    std::vector<std::string> modes;
    if (spa) modes.push_back("spa fallback");
    if (clean) modes.push_back("clean urls");
    if (!modes.empty()) banner += "  Mode:   " + join(modes, ", ") + "\n";
    banner += "Logging 404s and errors. Ctrl+C to stop.\n";
    std::fwrite(banner.data(), 1, banner.size(), stdout);
    std::fflush(stdout);
    if (B(a, "open")) open_browser(local);

    g_stop = 0;
    const auto previous = std::signal(SIGINT, on_sigint);
    accept_loop(ls, cfg, [] { return g_stop != 0; });
    std::signal(SIGINT, previous);
    std::fputs("\nStopped.\n", stdout);
    std::fflush(stdout);
    sock_close(ls);
    return 0;
}
PACKTOOL("master-build/serve", main);
}  // namespace packtools::master_serve
