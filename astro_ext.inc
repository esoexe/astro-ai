// =============================================================================================
// Astro AI extensions (included inside workspace_server.cpp's anonymous namespace):
//   * login (PBKDF2-SHA256 password, bearer session tokens)       /api/auth/*
//   * provider key manager (stored 0600 on the server, restart)    /api/keys, /api/keys/test
//   * per-provider usage meter (today, UTC)                        /api/usage
//   * web search (Tavily/Brave API keys, Brave/DuckDuckGo pages, Wikipedia) and page reader
//                                                                  /api/web/search, /api/web/read
//   * image understanding through vision models                    /api/vision
//   * cross-device sync (server file + Supabase gateway mirror)    /api/sync
//   * static app files (PWA manifest, service worker, icons)
// =============================================================================================
const char* kUserAgent = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0 Safari/537.36 AstroAI/1.0";

std::string data_dir() {
    const char* d = std::getenv("ASTRO_DATA_DIR");
    return d && *d ? std::string(d) : std::string(".");
}
std::string data_path(const std::string& name) { return data_dir() + "/" + name; }

std::optional<std::string> read_file_text(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
bool write_file_private(const std::string& path, const std::string& content) {
    const auto slash = path.rfind('/');
    if (slash != std::string::npos) { std::error_code ec; std::filesystem::create_directories(path.substr(0, slash), ec); }
    const std::string tmp = path + ".tmp";
    const int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_BINARY, 0600);
    if (fd < 0) return false;
    std::size_t done = 0;
    while (done < content.size()) {
        const ssize_t n = write(fd, content.data() + done, content.size() - done);
        if (n < 0) { if (errno == EINTR) continue; close(fd); return false; }
        done += static_cast<std::size_t>(n);
    }
    fsync(fd);
    close(fd);
    std::error_code rename_ec;   // filesystem::rename replaces an existing file on every platform (std::rename does not on Windows)
    std::filesystem::rename(tmp, path, rename_ec);
    return !rename_ec;
}
std::string hex_encode(const unsigned char* p, std::size_t n) {
    static const char* digits = "0123456789abcdef";
    std::string out; out.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) { out += digits[p[i] >> 4]; out += digits[p[i] & 15]; }
    return out;
}
std::string random_hex(std::size_t bytes) {
    std::vector<unsigned char> buf(bytes);
    if (!astro_rand_bytes(buf.data(), bytes)) {
        std::ifstream u("/dev/urandom", std::ios::binary);
        u.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(bytes));
    }
    return hex_encode(buf.data(), bytes);
}
std::string sha256_hex(const std::string& text) {
    unsigned char out[32];
    astro_sha256(text.data(), text.size(), out);
    return hex_encode(out, sizeof out);
}
std::string pbkdf2_hex(const std::string& password, const std::string& salt, int iterations) {
    unsigned char out[32];
    astro_pbkdf2_sha256(password, salt, iterations, out);
    return hex_encode(out, sizeof out);
}
bool same_secret(const std::string& a, const std::string& b) {
    return a.size() == b.size() && astro_memeq(a.data(), b.data(), a.size());
}

// ---------------------------------------------------------------------------------------------
// Login
// ---------------------------------------------------------------------------------------------
struct AuthState {
    bool loaded = false;
    std::string salt, hash;
    int iterations = 0;
    std::map<std::string, long long> sessions; // sha256(token) -> expiry (ms)
    std::vector<long long> failures;
};
std::mutex g_auth_mutex;
AuthState g_auth;
constexpr long long kSessionMs = 30LL * 24 * 3600 * 1000;

bool auth_disabled() {
    const char* e = std::getenv("ASTRO_AUTH");
    return e && std::string(e) == "off";
}
void save_sessions_locked() {
    std::string out;
    for (const auto& [h, exp] : g_auth.sessions) out += h + " " + std::to_string(exp) + "\n";
    write_file_private(data_path(".astro_sessions"), out);
}
bool set_password_locked(const std::string& password) {
    g_auth.salt = random_hex(16);
    g_auth.iterations = 210000;
    g_auth.hash = pbkdf2_hex(password, g_auth.salt, g_auth.iterations);
    Json::Object obj;
    obj["salt"] = Json(g_auth.salt); obj["hash"] = Json(g_auth.hash); obj["iter"] = Json(g_auth.iterations);
    return write_file_private(data_path(".astro_auth.json"), dump_json(Json(std::move(obj))) + "\n");
}
void auth_load_locked() {
    if (g_auth.loaded) return;
    g_auth.loaded = true;
    if (auto txt = read_file_text(data_path(".astro_auth.json"))) {
        try {
            const Json j = JsonParser(*txt).parse();
            g_auth.salt = get_string(j, "salt"); g_auth.hash = get_string(j, "hash"); g_auth.iterations = json_int(j, "iter", 0);
        } catch (...) {}
    }
    if (auto txt = read_file_text(data_path(".astro_sessions"))) {
        std::istringstream in(*txt);
        std::string h; long long exp = 0;
        while (in >> h >> exp) if (exp > now_ms()) g_auth.sessions[h] = exp;
    }
    if (g_auth.hash.empty())
        if (const char* pw = std::getenv("ASTRO_PASSWORD"); pw && std::strlen(pw) >= 6) set_password_locked(pw);
}
std::string new_session_locked() {
    const std::string token = random_hex(32);
    g_auth.sessions[sha256_hex(token)] = now_ms() + kSessionMs;
    save_sessions_locked();
    return token;
}
// The session token can arrive three ways. Preview proxies may replace or strip Authorization,
// so every candidate is checked: x-astro-token header, Authorization: Bearer, astro_session cookie.
std::vector<std::string> request_tokens(const HttpRequest& r) {
    std::vector<std::string> out;
    if (auto it = r.headers.find("x-astro-token"); it != r.headers.end()) out.push_back(trim(it->second));
    if (auto it = r.headers.find("authorization"); it != r.headers.end() && it->second.rfind("Bearer ", 0) == 0) out.push_back(trim(it->second.substr(7)));
    if (auto it = r.headers.find("cookie"); it != r.headers.end()) {
        const std::string& c = it->second;
        for (std::size_t p = c.find("astro_session="); p != std::string::npos; p = c.find("astro_session=", p + 1)) {
            if (p > 0 && c[p - 1] != ' ' && c[p - 1] != ';') continue;
            const std::size_t start = p + 14, end = c.find(';', start);
            out.push_back(trim(c.substr(start, end == std::string::npos ? std::string::npos : end - start)));
        }
    }
    std::erase_if(out, [](const std::string& t) { return t.size() < 32 || t.size() > 200; });
    return out;
}
bool via_https(const HttpRequest& r) {
    const auto it = r.headers.find("x-forwarded-proto");
    return it != r.headers.end() && lower_copy(it->second).find("https") != std::string::npos;
}
// Sends {"token":...} and also sets an HttpOnly session cookie (works even if headers are stripped).
void send_session(int fd, const HttpRequest& r, const std::string& token) {
    Json::Object obj; obj["token"] = Json(token);
    const std::string body = dump_json(Json(std::move(obj)));
    const std::string attrs = via_https(r) ? "; Secure; SameSite=None; Partitioned" : "; SameSite=Lax";
    std::ostringstream h;
    h << "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: " << body.size()
      << "\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
      << "Set-Cookie: astro_session=" << token << "; Path=/; Max-Age=" << (token.empty() ? 0 : 2592000) << "; HttpOnly" << attrs << "\r\n\r\n";
    send_all(fd, h.str());
    send_all(fd, body);
}
bool auth_ok(const HttpRequest& r) {
    if (auth_disabled()) return true;
    std::lock_guard<std::mutex> lock(g_auth_mutex);
    auth_load_locked();
    if (g_auth.hash.empty()) return false;
    for (const auto& token : request_tokens(r)) {
        const auto it = g_auth.sessions.find(sha256_hex(token));
        if (it != g_auth.sessions.end() && it->second > now_ms()) return true;
    }
    return false;
}
bool login_throttled_locked() {
    const long long now = now_ms();
    std::erase_if(g_auth.failures, [&](long long t) { return now - t > 10LL * 60 * 1000; });
    return g_auth.failures.size() >= 10;
}

void handle_auth(int fd, const HttpRequest& request) {
    if (request.path == "/api/auth/state") {
        Json::Object obj;
        if (auth_disabled()) { obj["enabled"] = Json(false); obj["setup"] = Json(false); obj["ok"] = Json(true); send_json(fd, 200, Json(std::move(obj))); return; }
        bool setup = false;
        { std::lock_guard<std::mutex> lock(g_auth_mutex); auth_load_locked(); setup = g_auth.hash.empty(); }
        obj["enabled"] = Json(true); obj["setup"] = Json(setup); obj["ok"] = Json(!setup && auth_ok(request));
        send_json(fd, 200, Json(std::move(obj)));
        return;
    }
    if (request.method != "POST") { send_json(fd, 405, error_json("Use POST.")); return; }
    const auto body_opt = parse_request_json(request, fd);
    if (!body_opt) return;
    const Json& body = *body_opt;
    const std::string password = get_string(body, "password");
    if (request.path == "/api/auth/setup") {
        std::lock_guard<std::mutex> lock(g_auth_mutex);
        auth_load_locked();
        if (!g_auth.hash.empty()) { send_json(fd, 409, error_json("A password is already set. Sign in instead.")); return; }
        if (password.size() < 6 || password.size() > 200) { send_json(fd, 400, error_json("Choose a password with at least 6 characters.")); return; }
        if (!set_password_locked(password)) { send_json(fd, 500, error_json("Could not save the password on the server.")); return; }
        send_session(fd, request, new_session_locked());
        return;
    }
    if (request.path == "/api/auth/login") {
        std::unique_lock<std::mutex> lock(g_auth_mutex);
        auth_load_locked();
        if (g_auth.hash.empty()) { send_json(fd, 409, error_json("No password yet: create one first.")); return; }
        if (login_throttled_locked()) { send_json(fd, 429, error_json("Too many wrong passwords. Wait 10 minutes.")); return; }
        if (!same_secret(pbkdf2_hex(password, g_auth.salt, g_auth.iterations), g_auth.hash)) {
            g_auth.failures.push_back(now_ms());
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
            send_json(fd, 401, error_json("Wrong password."));
            return;
        }
        send_session(fd, request, new_session_locked());
        return;
    }
    if (!auth_ok(request)) { send_json(fd, 401, error_json("Sign in first.")); return; }
    if (request.path == "/api/auth/logout") {
        std::lock_guard<std::mutex> lock(g_auth_mutex);
        for (const auto& t : request_tokens(request)) g_auth.sessions.erase(sha256_hex(t));
        save_sessions_locked();
        send_session(fd, request, "");
        return;
    }
    if (request.path == "/api/auth/password") {
        const std::string next = get_string(body, "new_password");
        std::lock_guard<std::mutex> lock(g_auth_mutex);
        if (!same_secret(pbkdf2_hex(password, g_auth.salt, g_auth.iterations), g_auth.hash)) { send_json(fd, 401, error_json("The current password is wrong.")); return; }
        if (next.size() < 6 || next.size() > 200) { send_json(fd, 400, error_json("The new password needs at least 6 characters.")); return; }
        set_password_locked(next);
        g_auth.sessions.clear();
        send_session(fd, request, new_session_locked());
        return;
    }
    send_json(fd, 404, error_json("Not found."));
}

// ---------------------------------------------------------------------------------------------
// Provider keys entered in the app (Settings -> Providers). Stored 0600 next to the server,
// never sent back to the browser. Saving restarts the server process (exec) to rebuild the brain.
// ---------------------------------------------------------------------------------------------
std::map<std::string, std::string> g_app_keys; // loaded at startup, read-only afterwards

void load_app_keys() {
    const auto txt = read_file_text(data_path(".astro_keys.env"));
    if (!txt) return;
    std::istringstream in(*txt);
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos || line[0] == '#') continue;
        const std::string name = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        if (!name.empty() && !value.empty()) { g_app_keys[name] = value; g_keys[name] = value; }
    }
    if (!g_app_keys.empty()) std::cout << "Loaded " << g_app_keys.size() << " provider key(s) saved in the app." << std::endl;
}
std::vector<std::string> known_key_names() {
    std::vector<std::string> names = {"TAVILY_API_KEY", "BRAVE_API_KEY"};
    const char* path_env = std::getenv("PROVIDERS_FILE");
    const auto txt = read_file_text(path_env && *path_env ? path_env : "providers.json");
    if (!txt) return names;
    try {
        const Json root = JsonParser(*txt).parse();
        if (const Json* list = root.get("providers"); list && list->array_ptr())
            for (const Json& p : *list->array_ptr()) {
                const std::string k = get_string(p, "key");
                if (!k.empty()) names.push_back(k);
                for (const auto& r : json_strings(p.get("requires"))) names.push_back(r);
            }
    } catch (...) {}
    return names;
}
void schedule_restart() {
    std::thread([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::cout << "Restarting Astro AI to load the new provider keys..." << std::endl;
        // Desktop app: the launcher supervises the server and starts it again on exit code 75.
        if (const char* sup = std::getenv("ASTRO_SUPERVISED"); sup && std::string(sup) == "1") std::exit(75);
#if defined(_WIN32)
        wchar_t self[MAX_PATH * 2];
        const DWORD n = GetModuleFileNameW(nullptr, self, MAX_PATH * 2);
        if (n > 0) {
            winp::Child child; std::string error;
            if (winp::spawn(winp::quote_arg(winp::narrow(std::wstring(self, n))), "", {}, nullptr, nullptr, nullptr, child, error,
                            nullptr, /*in_job=*/false)) {
                std::exit(0);
            }
        }
        std::cerr << "Restart failed." << std::endl;
#elif defined(__APPLE__)
        std::cerr << "Restart: please start Astro AI again to load the new keys." << std::endl;
        std::exit(0);
#else
        char* args[] = {const_cast<char*>("workspace_server"), nullptr};
        execv("/proc/self/exe", args);
        std::cerr << "Restart failed: " << std::strerror(errno) << std::endl;
#endif
    }).detach();
}

// ---------------------------------------------------------------------------------------------
// Usage meter (per provider, today in UTC; persisted so restarts keep the count)
// ---------------------------------------------------------------------------------------------
struct UsageStats { long long requests = 0, ok = 0, failed = 0, limited = 0, total_ms = 0, last_at = 0; std::string last_error; };
std::mutex g_usage_mutex;
std::map<std::string, UsageStats> g_usage;
long long g_usage_day = -1;

void usage_load_locked() {
    if (g_usage_day >= 0) return;
    g_usage_day = now_ms() / 86'400'000LL;
    const auto txt = read_file_text(data_path("astro_data/usage.json"));
    if (!txt) return;
    try {
        const Json j = JsonParser(*txt).parse();
        if (json_int(j, "day", -1) != static_cast<int>(g_usage_day)) return;
        if (const Json* ps = j.get("providers"); ps && ps->object_ptr())
            for (const auto& [id, v] : *ps->object_ptr()) {
                UsageStats s;
                s.requests = json_int(v, "requests", 0); s.ok = json_int(v, "ok", 0); s.failed = json_int(v, "failed", 0);
                s.limited = json_int(v, "limited", 0); s.total_ms = json_int(v, "total_ms", 0); s.last_error = get_string(v, "last_error");
                g_usage[id] = s;
            }
    } catch (...) {}
}
Json usage_json_locked() {
    Json::Object ps;
    for (const auto& [id, s] : g_usage) {
        Json::Object o;
        o["requests"] = Json(static_cast<double>(s.requests)); o["ok"] = Json(static_cast<double>(s.ok));
        o["failed"] = Json(static_cast<double>(s.failed)); o["limited"] = Json(static_cast<double>(s.limited));
        o["total_ms"] = Json(static_cast<double>(s.total_ms)); o["last_error"] = Json(s.last_error);
        o["last_at"] = Json(static_cast<double>(s.last_at));
        ps[id] = Json(std::move(o));
    }
    Json::Object root; root["day"] = Json(static_cast<double>(g_usage_day)); root["providers"] = Json(std::move(ps));
    return Json(std::move(root));
}
void usage_record(const std::string& provider, bool ok, long status, const std::string& error, long long ms) {
    std::lock_guard<std::mutex> lock(g_usage_mutex);
    usage_load_locked();
    const long long day = now_ms() / 86'400'000LL;
    if (day != g_usage_day) { g_usage.clear(); g_usage_day = day; }
    UsageStats& s = g_usage[provider];
    s.requests++; s.last_at = now_ms(); s.total_ms += ms;
    if (ok) s.ok++;
    else {
        s.failed++;
        if (status == 429 || status == 402 || looks_rate_limited(error)) s.limited++;
        s.last_error = truncate(error, 200);
    }
    write_file_private(data_path("astro_data/usage.json"), dump_json(usage_json_locked()));
}

// ---------------------------------------------------------------------------------------------
// Outbound HTTP for the web reader / search / vision. Public addresses only (blocks the
// sandbox's private services and cloud metadata), http(s) only, size and time capped.
// ---------------------------------------------------------------------------------------------
bool public_address(const sockaddr* sa) {
    if (sa->sa_family == AF_INET) {
        const uint32_t ip = ntohl(reinterpret_cast<const sockaddr_in*>(sa)->sin_addr.s_addr);
        const unsigned a = ip >> 24, b = (ip >> 16) & 255;
        if (a == 0 || a == 10 || a == 127 || a >= 224) return false;
        if ((a == 169 && b == 254) || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) || (a == 100 && b >= 64 && b <= 127)) return false;
        return true;
    }
    if (sa->sa_family == AF_INET6) {
        const in6_addr& v6 = reinterpret_cast<const sockaddr_in6*>(sa)->sin6_addr;
        if (IN6_IS_ADDR_LOOPBACK(&v6) || IN6_IS_ADDR_UNSPECIFIED(&v6) || IN6_IS_ADDR_LINKLOCAL(&v6) || IN6_IS_ADDR_MULTICAST(&v6)) return false;
        if ((v6.s6_addr[0] & 0xfe) == 0xfc) return false; // unique local
        if (IN6_IS_ADDR_V4MAPPED(&v6)) {
            sockaddr_in v4{}; v4.sin_family = AF_INET; std::memcpy(&v4.sin_addr, v6.s6_addr + 12, 4);
            return public_address(reinterpret_cast<const sockaddr*>(&v4));
        }
        return true;
    }
    return false;
}
curl_socket_t safe_open_socket(void*, curlsocktype purpose, struct curl_sockaddr* address) {
    if (purpose == CURLSOCKTYPE_IPCXN && !public_address(&address->addr)) return CURL_SOCKET_BAD;
    return socket(address->family, address->socktype, address->protocol);
}
struct CappedBody { std::string data; std::size_t cap = 0; bool truncated = false; };
std::size_t capped_write(char* ptr, std::size_t size, std::size_t count, void* userdata) {
    auto* body = static_cast<CappedBody*>(userdata);
    const std::size_t n = size * count;
    if (body->data.size() + n > body->cap) {
        body->data.append(ptr, body->cap - std::min(body->cap, body->data.size()));
        body->truncated = true;
        return 0; // stop the transfer
    }
    body->data.append(ptr, n);
    return n;
}
struct FetchResult { long status = 0; std::string body, content_type, final_url, error; bool truncated = false; };
FetchResult http_fetch(const std::string& url, const std::vector<std::string>& headers = {}, const std::string* post = nullptr,
                       long timeout = 20, std::size_t cap = 4'000'000, bool public_only = true) {
    FetchResult out;
    CURL* curl = curl_easy_init();
    if (!curl) { out.error = "curl unavailable"; return out; }
    CappedBody body; body.cap = cap;
    struct curl_slist* list = nullptr;
    for (const auto& h : headers) list = curl_slist_append(list, h.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, capped_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    if (public_only) {
        curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, safe_open_socket);
        curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
    }
    if (post) { curl_easy_setopt(curl, CURLOPT_POST, 1L); curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post->c_str()); curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(post->size())); }
    const CURLcode code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out.status);
    char* ct = nullptr; curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct); if (ct) out.content_type = lower_copy(ct);
    char* eff = nullptr; curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff); if (eff) out.final_url = eff;
    if (code != CURLE_OK && !(code == CURLE_WRITE_ERROR && body.truncated))
        out.error = code == CURLE_COULDNT_CONNECT && public_only ? "Could not connect (private or blocked address)." : curl_easy_strerror(code);
    out.body = std::move(body.data); out.truncated = body.truncated;
    curl_slist_free_all(list);
    curl_easy_cleanup(curl);
    return out;
}
std::string url_escape(const std::string& text) {
    CURL* curl = curl_easy_init();
    if (!curl) return text;
    char* esc = curl_easy_escape(curl, text.c_str(), static_cast<int>(text.size()));
    std::string out = esc ? esc : text;
    if (esc) curl_free(esc);
    curl_easy_cleanup(curl);
    return out;
}
std::string url_unescape(const std::string& text) {
    CURL* curl = curl_easy_init();
    if (!curl) return text;
    int len = 0;
    char* raw = curl_easy_unescape(curl, text.c_str(), static_cast<int>(text.size()), &len);
    std::string out = raw ? std::string(raw, static_cast<std::size_t>(len)) : text;
    if (raw) curl_free(raw);
    curl_easy_cleanup(curl);
    return out;
}

// ---------- HTML -> readable text ----------
void append_utf8(std::string& out, unsigned long cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x110000) { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
}
std::string decode_entities(const std::string& in) {
    static const std::map<std::string, std::string> named = {
        {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"}, {"nbsp", " "}, {"ndash", "\xE2\x80\x93"}, {"mdash", "\xE2\x80\x94"},
        {"hellip", "\xE2\x80\xA6"}, {"rsquo", "\xE2\x80\x99"}, {"lsquo", "\xE2\x80\x98"}, {"rdquo", "\xE2\x80\x9D"}, {"ldquo", "\xE2\x80\x9C"},
        {"copy", "\xC2\xA9"}, {"reg", "\xC2\xAE"}, {"deg", "\xC2\xB0"}, {"middot", "\xC2\xB7"}, {"bull", "\xE2\x80\xA2"}, {"times", "\xC3\x97"}, {"euro", "\xE2\x82\xAC"}};
    std::string out; out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '&') { out += in[i]; continue; }
        const auto semi = in.find(';', i);
        if (semi == std::string::npos || semi - i > 10) { out += in[i]; continue; }
        const std::string ent = in.substr(i + 1, semi - i - 1);
        if (!ent.empty() && ent[0] == '#') {
            try {
                const unsigned long cp = (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X')) ? std::stoul(ent.substr(2), nullptr, 16) : std::stoul(ent.substr(1));
                append_utf8(out, cp == 160 ? 32 : cp); i = semi; continue;
            } catch (...) {}
        } else if (auto it = named.find(ent); it != named.end()) { out += it->second; i = semi; continue; }
        out += in[i];
    }
    return out;
}
std::string strip_tags_inline(const std::string& html) {
    std::string out;
    bool in_tag = false;
    for (char c : html) { if (c == '<') in_tag = true; else if (c == '>') in_tag = false; else if (!in_tag) out += c; }
    return trim(decode_entities(out));
}
// Removes <tag ...>...</tag> blocks (case-insensitive) from html.
void drop_blocks(std::string& html, std::string& lower, const std::string& tag) {
    const std::string open = "<" + tag, close = "</" + tag + ">";
    std::size_t pos = 0;
    while ((pos = lower.find(open, pos)) != std::string::npos) {
        const char next = pos + open.size() < lower.size() ? lower[pos + open.size()] : '>';
        if (!(next == '>' || next == ' ' || next == '\t' || next == '\n' || next == '/')) { pos += open.size(); continue; }
        auto end = lower.find(close, pos);
        end = end == std::string::npos ? lower.size() : end + close.size();
        html.erase(pos, end - pos); lower.erase(pos, end - pos);
    }
}
// End of an HTML tag starting at i, ignoring the closing bracket inside quoted attribute values.
std::size_t tag_end(const std::string& html, std::size_t i) {
    char quote = 0;
    for (std::size_t k = i + 1; k < html.size(); ++k) {
        const char c = html[k];
        if (quote) { if (c == quote) quote = 0; }
        else if (c == '"' || c == '\'') { if (html[k - 1] == '=') quote = c; }
        else if (c == '>') return k;
    }
    return std::string::npos;
}

std::string html_to_text(const std::string& source, std::string& title) {
    std::string html = source;
    std::string lower = lower_copy(html);
    if (auto t = lower.find("<title"); t != std::string::npos) {
        const auto gt = lower.find('>', t), end = lower.find("</title>", t);
        if (gt != std::string::npos && end != std::string::npos && end > gt) title = truncate(strip_tags_inline(html.substr(gt + 1, end - gt - 1)), 300);
    }
    for (auto p = lower.find("<!--"); p != std::string::npos; p = lower.find("<!--", p)) {
        auto e = lower.find("-->", p); e = e == std::string::npos ? lower.size() : e + 3;
        html.erase(p, e - p); lower.erase(p, e - p);
    }
    for (const char* tag : {"script", "style", "noscript", "svg", "head", "template", "iframe", "nav", "footer", "form", "button", "select"})
        drop_blocks(html, lower, tag);
    // Prefer the main article when the page has one.
    for (const char* tag : {"article", "main"}) {
        const std::string open = std::string("<") + tag, close = std::string("</") + tag + ">";
        const auto s = lower.find(open), e = lower.rfind(close);
        if (s != std::string::npos && e != std::string::npos && e > s && e - s > 1500) { html = html.substr(s, e - s); lower = lower.substr(s, e - s); break; }
    }
    static const std::vector<std::string> blocks = {"p", "div", "br", "li", "tr", "h1", "h2", "h3", "h4", "h5", "h6", "section", "article",
                                                    "header", "pre", "blockquote", "table", "ul", "ol", "dd", "dt", "figcaption", "hr"};
    std::string out; out.reserve(html.size() / 2);
    for (std::size_t i = 0; i < html.size(); ++i) {
        if (html[i] != '<') { out += html[i]; continue; }
        const auto gt = tag_end(html, i);
        if (gt == std::string::npos) break;
        std::string name;
        std::size_t k = i + 1;
        const bool closing = k < gt && html[k] == '/';
        if (closing) ++k;
        while (k < gt && (std::isalnum(static_cast<unsigned char>(html[k])))) name += static_cast<char>(std::tolower(static_cast<unsigned char>(html[k++])));
        if (std::find(blocks.begin(), blocks.end(), name) != blocks.end()) {
            if (!closing && name == "li") out += "\n- ";
            else if (!closing && name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '3') out += "\n\n## ";
            else if (name == "td" || name == "th") out += " | ";
            else out += "\n";
        } else if (name == "td" || name == "th") out += " | ";
        i = gt;
    }
    out = decode_entities(out);
    // Collapse whitespace: spaces within lines, at most one blank line.
    std::string clean; clean.reserve(out.size());
    int newlines = 0; bool space = false;
    for (char c : out) {
        if (c == '\r') continue;
        if (c == '\n') { space = false; if (++newlines <= 2) { while (!clean.empty() && clean.back() == ' ') clean.pop_back(); clean += '\n'; } continue; }
        if (c == ' ' || c == '\t') { if (!space && newlines == 0 && !clean.empty()) { clean += ' '; } space = true; continue; }
        newlines = 0; space = false; clean += c;
    }
    // Drop menus before the article starts (language pickers, nav link lists): runs of 5+ short bullets.
    std::vector<std::string> lines;
    { std::istringstream in(clean); std::string line; while (std::getline(in, line)) lines.push_back(line); }
    std::size_t first_text = lines.size();
    for (std::size_t i = 0; i < lines.size(); ++i) if (lines[i].size() >= 120 && lines[i].rfind("- ", 0) != 0) { first_text = i; break; }
    std::vector<bool> drop(lines.size(), false);
    for (std::size_t i = 0; i < first_text;) {
        std::size_t j = i;
        while (j < first_text && (lines[j].empty() || (lines[j].rfind("- ", 0) == 0 && lines[j].size() < 48))) ++j;
        std::size_t bullets = 0;
        for (std::size_t k = i; k < j; ++k) if (!lines[k].empty()) ++bullets;
        if (bullets >= 5) for (std::size_t k = i; k < j; ++k) drop[k] = true;
        i = j == i ? i + 1 : j;
    }
    std::string result;
    for (std::size_t i = 0; i < lines.size(); ++i) if (!drop[i]) { result += lines[i]; result += "\n"; }
    return trim(result);
}

// ---------- web reader ----------
struct ReadResult { bool ok = false; std::string title, url, text, via, error; };
ReadResult web_read(const std::string& url_in) {
    ReadResult r;
    std::string url = trim(url_in);
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) url = "https://" + url;
    r.url = url;
    const FetchResult f = http_fetch(url, {"Accept: text/html,application/xhtml+xml,text/plain;q=0.9,*/*;q=0.5", "Accept-Language: en,fr;q=0.8,ar;q=0.6"}, nullptr, 20, 4'000'000);
    const bool html = f.content_type.find("html") != std::string::npos || (f.content_type.empty() && f.body.find("<html") != std::string::npos);
    const bool textual = html || f.content_type.find("text/") == 0 || f.content_type.find("json") != std::string::npos || f.content_type.find("xml") != std::string::npos;
    if (f.error.empty() && f.status >= 200 && f.status < 400 && textual) {
        r.text = html ? html_to_text(f.body, r.title) : f.body;
        r.via = "direct";
        if (!f.final_url.empty()) r.url = f.final_url;
    } else if (f.error.find("private") != std::string::npos) {
        r.error = "That address is private or blocked, so Astro AI will not open it.";
        return r;
    }
    // JavaScript-heavy pages, PDFs and blocked pages: ask the Jina reader (keyless, rate-limited).
    if (r.text.size() < 400) {
        const FetchResult j = http_fetch("https://r.jina.ai/" + url, {"Accept: text/plain", "X-Return-Format: markdown"}, nullptr, 40, 4'000'000);
        if (j.error.empty() && j.status == 200 && j.body.size() > r.text.size()) {
            std::string body = j.body;
            if (body.rfind("Title:", 0) == 0) { const auto nl = body.find('\n'); if (r.title.empty()) r.title = trim(body.substr(6, nl - 6)); }
            if (const auto mc = body.find("Markdown Content:"); mc != std::string::npos) body = body.substr(mc + 17);
            r.text = trim(body); r.via = "reader";
        }
    }
    if (r.text.empty()) {
        r.error = !f.error.empty() ? "Could not open the page: " + f.error
                  : f.status >= 400 ? "The site answered HTTP " + std::to_string(f.status) + "."
                  : "The page has no readable text (it may need a browser or a login).";
        return r;
    }
    if (r.text.size() > 60000) { r.text.resize(60000); r.text += "\n\n[...cut: the page is longer]"; }
    if (r.title.empty()) r.title = r.url;
    r.ok = true;
    return r;
}

// ---------- web search ----------
struct SearchHit { std::string title, url, snippet; };
std::string between(const std::string& s, std::size_t from, const std::string& a, const std::string& b, std::size_t limit, std::size_t* end_pos = nullptr) {
    const auto p = s.find(a, from);
    if (p == std::string::npos || p - from > limit) return "";
    const auto q = s.find(b, p + a.size());
    if (q == std::string::npos) return "";
    if (end_pos) *end_pos = q;
    return s.substr(p + a.size(), q - p - a.size());
}
std::vector<SearchHit> search_brave_html(const std::string& q, int n) {
    std::vector<SearchHit> hits;
    const FetchResult f = http_fetch("https://search.brave.com/search?q=" + url_escape(q) + "&source=web", {"Accept: text/html", "Accept-Language: en"}, nullptr, 15, 3'000'000);
    if (!f.error.empty() || f.status != 200) return hits;
    std::size_t pos = 0;
    while (static_cast<int>(hits.size()) < n && (pos = f.body.find("data-type=\"web\"", pos)) != std::string::npos) {
        const std::size_t start = pos; pos += 15;
        const std::string href = between(f.body, start, "href=\"", "\"", 3000);
        if (href.rfind("http", 0) != 0) continue;
        std::string title = between(f.body, start, "class=\"title", "</div>", 6000);
        if (auto gt = title.find('>'); gt != std::string::npos) title = title.substr(gt + 1);
        std::string desc;
        for (const char* marker : {"class=\"snippet-description", "class=\"generic-snippet", "class=\"content"}) {
            desc = between(f.body, start, marker, "</div>", 9000);
            if (!desc.empty()) break;
        }
        if (auto gt = desc.find('>'); gt != std::string::npos) desc = desc.substr(gt + 1);
        SearchHit h{truncate(strip_tags_inline(title), 200), decode_entities(href), truncate(strip_tags_inline(desc), 400)};
        if (!h.title.empty() && std::none_of(hits.begin(), hits.end(), [&](const SearchHit& x) { return x.url == h.url; })) hits.push_back(h);
    }
    return hits;
}
std::vector<SearchHit> search_ddg_html(const std::string& q, int n) {
    std::vector<SearchHit> hits;
    const std::string post = "q=" + url_escape(q);
    const FetchResult f = http_fetch("https://html.duckduckgo.com/html/", {"Content-Type: application/x-www-form-urlencoded", "Accept-Language: en"}, &post, 15, 2'000'000);
    if (!f.error.empty() || f.status != 200) return hits;
    std::size_t pos = 0;
    while (static_cast<int>(hits.size()) < n && (pos = f.body.find("class=\"result__a\"", pos)) != std::string::npos) {
        const std::size_t start = pos; pos += 17;
        std::string href = between(f.body, start, "href=\"", "\"", 400);
        std::size_t title_end = 0;
        const std::string title = between(f.body, start, ">", "</a>", 600, &title_end);
        const std::string snippet = between(f.body, start, "class=\"result__snippet\"", "</a>", 4000);
        if (const auto u = href.find("uddg="); u != std::string::npos) href = url_unescape(href.substr(u + 5, href.find('&', u) - u - 5));
        if (href.rfind("//", 0) == 0) href = "https:" + href;
        if (href.rfind("http", 0) != 0) continue;
        std::string snip = snippet; if (auto gt = snip.find('>'); gt != std::string::npos) snip = snip.substr(gt + 1);
        hits.push_back({truncate(strip_tags_inline(title), 200), href, truncate(strip_tags_inline(snip), 400)});
    }
    return hits;
}
std::vector<SearchHit> search_wikipedia(const std::string& q, int n, const std::string& lang = "en") {
    std::vector<SearchHit> hits;
    const FetchResult f = http_fetch("https://" + lang + ".wikipedia.org/w/api.php?action=query&list=search&format=json&srlimit=" + std::to_string(n) + "&srsearch=" + url_escape(q),
                                     {"Accept: application/json"}, nullptr, 12, 1'000'000);
    if (!f.error.empty() || f.status != 200) return hits;
    try {
        const Json j = JsonParser(f.body).parse();
        const Json* query = j.get("query");
        const Json* list = query ? query->get("search") : nullptr;
        if (list && list->array_ptr())
            for (const Json& item : *list->array_ptr()) {
                const std::string title = get_string(item, "title");
                std::string page = title; std::replace(page.begin(), page.end(), ' ', '_');
                hits.push_back({title + " (Wikipedia)", "https://" + lang + ".wikipedia.org/wiki/" + url_escape(page), truncate(strip_tags_inline(get_string(item, "snippet")), 400)});
            }
    } catch (...) {}
    return hits;
}
std::vector<SearchHit> search_tavily(const std::string& q, int n, const std::string& key) {
    std::vector<SearchHit> hits;
    Json::Object body; body["query"] = Json(q); body["max_results"] = Json(n); body["search_depth"] = Json(std::string("basic"));
    const std::string post = dump_json(Json(std::move(body)));
    const FetchResult f = http_fetch("https://api.tavily.com/search", {"Content-Type: application/json", "Authorization: Bearer " + key}, &post, 20, 2'000'000, false);
    if (!f.error.empty() || f.status != 200) return hits;
    try {
        const Json j = JsonParser(f.body).parse();
        if (const Json* rs = j.get("results"); rs && rs->array_ptr())
            for (const Json& r : *rs->array_ptr()) hits.push_back({get_string(r, "title"), get_string(r, "url"), truncate(get_string(r, "content"), 500)});
    } catch (...) {}
    return hits;
}
std::vector<SearchHit> search_brave_api(const std::string& q, int n, const std::string& key) {
    std::vector<SearchHit> hits;
    const FetchResult f = http_fetch("https://api.search.brave.com/res/v1/web/search?count=" + std::to_string(n) + "&q=" + url_escape(q),
                                     {"Accept: application/json", "X-Subscription-Token: " + key}, nullptr, 15, 2'000'000, false);
    if (!f.error.empty() || f.status != 200) return hits;
    try {
        const Json j = JsonParser(f.body).parse();
        const Json* web = j.get("web");
        const Json* rs = web ? web->get("results") : nullptr;
        if (rs && rs->array_ptr())
            for (const Json& r : *rs->array_ptr()) hits.push_back({strip_tags_inline(get_string(r, "title")), get_string(r, "url"), truncate(strip_tags_inline(get_string(r, "description")), 500)});
    } catch (...) {}
    return hits;
}
std::pair<std::string, std::vector<SearchHit>> web_search(const std::string& q, int n) {
    if (const std::string k = secret_value("TAVILY_API_KEY"); !k.empty()) if (auto h = search_tavily(q, n, k); !h.empty()) return {"Tavily", h};
    if (const std::string k = secret_value("BRAVE_API_KEY"); !k.empty()) if (auto h = search_brave_api(q, n, k); !h.empty()) return {"Brave Search API", h};
    if (auto h = search_brave_html(q, n); !h.empty()) return {"Brave Search", h};
    if (auto h = search_ddg_html(q, n); !h.empty()) return {"DuckDuckGo", h};
    return {"Wikipedia", search_wikipedia(q, n)};
}

// ---------------------------------------------------------------------------------------------
// Sync store: astro_data/store.json on this server, mirrored to the Supabase gateway (/store)
// when LLM_CREDENTIALS_URL + LLM_API_KEY are configured, so data survives a new server.
// ---------------------------------------------------------------------------------------------
std::mutex g_store_mutex;
std::string gateway_store_url() {
    // ASTRO_SYNC_MIRROR=off keeps sync on this server only (used by test instances so they never touch real data).
    if (const char* m = std::getenv("ASTRO_SYNC_MIRROR"); m && std::string(m) == "off") return "";
    const char* u = std::getenv("LLM_CREDENTIALS_URL");
    if (!u || !*u) return "";
    std::string url = u;
    const auto p = url.rfind("/credentials");
    return p == std::string::npos ? "" : url.substr(0, p) + "/store";
}
std::string gateway_token() { const char* t = std::getenv("LLM_API_KEY"); return t ? trim(t) : ""; }

// ---------------------------------------------------------------------------------------------
// Vision: describe an image with the first available vision model.
// ---------------------------------------------------------------------------------------------
struct VisionResult { bool ok = false; std::string text, model, error; };
VisionResult describe_image(const std::string& image, const std::string& prompt) {
    VisionResult out;
    std::vector<std::string> errors;
    int tries = 0;
    for (const auto& route : g_routes) {
        if (!route.vision || route.kind != "openai" || route_blocked(route)) continue;
        if (++tries > 4) break;
        Json::Array parts;
        { Json::Object t; t["type"] = Json(std::string("text")); t["text"] = Json(prompt); parts.emplace_back(std::move(t)); }
        { Json::Object u; u["url"] = Json(image); Json::Object p; p["type"] = Json(std::string("image_url")); p["image_url"] = Json(std::move(u)); parts.emplace_back(std::move(p)); }
        Json::Object msg; msg["role"] = Json(std::string("user")); msg["content"] = Json(std::move(parts));
        Json::Array msgs; msgs.emplace_back(std::move(msg));
        Json::Object body; body["model"] = Json(route.model); body["messages"] = Json(std::move(msgs));
        body["max_tokens"] = Json(900); body["temperature"] = Json(0.2); body["stream"] = Json(false);
        const std::string post = dump_json(Json(std::move(body)));
        std::vector<std::string> headers = {"Content-Type: application/json"};
        if (!route.api_key.empty()) headers.push_back("Authorization: Bearer " + route.api_key);
        const long long t0 = now_ms();
        const FetchResult f = http_fetch(route.base_url + "/chat/completions", headers, &post, 75, 2'000'000, false);
        std::string text;
        if (f.error.empty() && f.status >= 200 && f.status < 300) {
            try { if (auto c = extract_content(JsonParser(f.body).parse())) text = *c; } catch (...) {}
        }
        usage_record(route.provider, !text.empty(), f.status, f.error.empty() ? "HTTP " + std::to_string(f.status) : f.error, now_ms() - t0);
        if (!text.empty()) { out.ok = true; out.text = text; out.model = route.label; return out; }
        errors.push_back(route.label + ": " + (f.error.empty() ? "HTTP " + std::to_string(f.status) + " " + truncate(f.body, 120) : f.error));
    }
    out.error = errors.empty() ? "No vision model is available right now." : "Vision models failed: " + truncate(errors.front(), 300);
    return out;
}

// ---------------------------------------------------------------------------------------------
// Request handlers
// ---------------------------------------------------------------------------------------------
void send_keys_list(int fd) {
    Json::Array list;
    for (const auto& p : g_providers) {
        Json::Object o;
        o["id"] = Json(p.id); o["name"] = Json(p.name); o["key"] = Json(p.key); o["keyless"] = Json(p.keyless);
        o["connected"] = Json(p.connected); o["models"] = Json(p.models); o["status"] = Json(p.status);
        o["signup"] = Json(p.signup); o["limits"] = Json(p.limits); o["privacy"] = Json(p.privacy); o["trains"] = Json(p.trains);
        o["key_set"] = Json(!p.key.empty() && !secret_value(p.key).empty());
        o["key_source"] = Json(p.key.empty() ? std::string("") : g_app_keys.count(p.key) ? std::string("app") : g_keys.count(p.key) ? std::string("gateway") : std::getenv(p.key.c_str()) ? std::string("env") : std::string(""));
        Json::Array req; for (const auto& r : p.requires_keys) { Json::Object ro; ro["name"] = Json(r); ro["set"] = Json(!secret_value(r).empty()); req.emplace_back(std::move(ro)); }
        o["requires"] = Json(std::move(req));
        list.emplace_back(std::move(o));
    }
    Json::Array search;
    for (const char* k : {"TAVILY_API_KEY", "BRAVE_API_KEY"}) { Json::Object o; o["key"] = Json(std::string(k)); o["key_set"] = Json(!secret_value(k).empty()); search.emplace_back(std::move(o)); }
    Json::Object obj; obj["providers"] = Json(std::move(list)); obj["search"] = Json(std::move(search));
    send_json(fd, 200, Json(std::move(obj)));
}

// Returns true when the request was handled here.
bool handle_astro_api(int fd, const HttpRequest& request) {
    const std::string& path = request.path;
    if (path == "/api/usage" && request.method == "GET") {
        Json::Array out;
        {
            std::lock_guard<std::mutex> lock(g_usage_mutex);
            usage_load_locked();
            if (now_ms() / 86'400'000LL != g_usage_day) { g_usage.clear(); g_usage_day = now_ms() / 86'400'000LL; }
            for (const auto& p : g_providers) {
                const auto it = g_usage.find(p.id);
                Json::Object o; o["id"] = Json(p.id); o["name"] = Json(p.name); o["limits"] = Json(p.limits); o["connected"] = Json(p.connected);
                const UsageStats s = it == g_usage.end() ? UsageStats{} : it->second;
                o["requests"] = Json(static_cast<double>(s.requests)); o["ok"] = Json(static_cast<double>(s.ok));
                o["failed"] = Json(static_cast<double>(s.failed)); o["limited"] = Json(static_cast<double>(s.limited));
                o["avg_ms"] = Json(s.requests ? static_cast<double>(s.total_ms / s.requests) : 0.0); o["last_error"] = Json(s.last_error);
                out.emplace_back(std::move(o));
            }
        }
        Json::Object obj; obj["providers"] = Json(std::move(out)); obj["day_utc"] = Json(static_cast<double>(now_ms() / 86'400'000LL));
        send_json(fd, 200, Json(std::move(obj)));
        return true;
    }
    if (path == "/api/keys" && request.method == "GET") { send_keys_list(fd); return true; }
    if (path == "/api/sync" && request.method == "GET") {
        std::string text;
        { std::lock_guard<std::mutex> lock(g_store_mutex); text = read_file_text(data_path("astro_data/store.json")).value_or(""); }
        if (text.empty() && !gateway_store_url().empty()) {
            const FetchResult f = http_fetch(gateway_store_url(), {"Authorization: Bearer " + gateway_token()}, nullptr, 20, 12'000'000, false);
            if (f.error.empty() && f.status == 200 && f.body.size() > 2) text = f.body;
        }
        send_response(fd, 200, "application/json; charset=utf-8", text.empty() ? std::string("{\"updated\":0}") : text);
        return true;
    }
    if (request.method != "POST") return false;
    if (path != "/api/keys" && path != "/api/keys/test" && path != "/api/web/search" && path != "/api/web/read" && path != "/api/vision" && path != "/api/sync") return false;
    const auto body_opt = parse_request_json(request, fd);
    if (!body_opt) return true;
    const Json& body = *body_opt;

    if (path == "/api/keys") {
        const std::string name = trim(get_string(body, "name"));
        const std::string value = trim(get_string(body, "value"));
        const auto names = known_key_names();
        if (std::find(names.begin(), names.end(), name) == names.end()) { send_json(fd, 400, error_json("Unknown key name.")); return true; }
        if (value.size() > 600 || std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; })) { send_json(fd, 400, error_json("That key looks invalid.")); return true; }
        std::map<std::string, std::string> keys = g_app_keys;
        if (value.empty()) keys.erase(name); else keys[name] = value;
        std::string out = "# Astro AI provider keys saved from Settings -> Providers (never sent to browsers)\n";
        for (const auto& [k, v] : keys) out += k + "=" + v + "\n";
        if (!write_file_private(data_path(".astro_keys.env"), out)) { send_json(fd, 500, error_json("Could not save the key on the server.")); return true; }
        Json::Object obj; obj["ok"] = Json(true); obj["restarting"] = Json(true);
        send_json(fd, 200, Json(std::move(obj)));
        schedule_restart();
        return true;
    }
    if (path == "/api/keys/test") {
        const std::string provider = get_string(body, "provider");
        const std::string model = get_string(body, "model");
        for (const auto& route : g_routes) {
            if (route.provider != provider || (!model.empty() && route.id != model) || route.kind != "openai") continue;
            Json::Array msgs;
            { Json::Object m; m["role"] = Json(std::string("user")); m["content"] = Json(std::string("Reply with exactly: OK")); msgs.emplace_back(std::move(m)); }
            Json::Object b; b["model"] = Json(route.model); b["messages"] = Json(std::move(msgs)); b["max_tokens"] = Json(1500); b["stream"] = Json(false);
            const std::string post = dump_json(Json(std::move(b)));
            std::vector<std::string> headers = {"Content-Type: application/json"};
            if (!route.api_key.empty()) headers.push_back("Authorization: Bearer " + route.api_key);
            const long long t0 = now_ms();
            const FetchResult f = http_fetch(route.base_url + "/chat/completions", headers, &post, 45, 1'000'000, false);
            const long long ms = now_ms() - t0;
            std::string text;
            if (f.error.empty() && f.status >= 200 && f.status < 300) { try { if (auto c = extract_content(JsonParser(f.body).parse())) text = *c; } catch (...) {} }
            // Reasoning models can spend the whole budget thinking: a 200 with choices still proves the key and model work.
            if (text.empty() && f.error.empty() && f.status >= 200 && f.status < 300 && f.body.find("\"choices\"") != std::string::npos) text = "(reachable: the model was still reasoning when the test budget ran out)";
            usage_record(route.provider, !text.empty(), f.status, f.error.empty() ? "HTTP " + std::to_string(f.status) : f.error, ms);
            Json::Object obj; obj["model"] = Json(route.label); obj["ms"] = Json(static_cast<double>(ms)); obj["ok"] = Json(!text.empty());
            if (!text.empty()) obj["reply"] = Json(truncate(text, 200));
            else obj["error"] = Json(f.error.empty() ? "HTTP " + std::to_string(f.status) + ": " + truncate(f.body, 240) : f.error);
            send_json(fd, 200, Json(std::move(obj)));
            return true;
        }
        send_json(fd, 404, error_json("This provider has no connected model to test yet."));
        return true;
    }
    if (path == "/api/web/search") {
        const std::string q = truncate(trim(get_string(body, "q")), 400);
        if (q.empty()) { send_json(fd, 400, error_json("Type something to search for.")); return true; }
        const int n = std::clamp(json_int(body, "n", 6), 1, 10);
        const auto [engine, hits] = web_search(q, n);
        Json::Array results;
        for (const auto& h : hits) { Json::Object o; o["title"] = Json(h.title); o["url"] = Json(h.url); o["snippet"] = Json(h.snippet); results.emplace_back(std::move(o)); }
        Json::Object obj; obj["engine"] = Json(engine); obj["results"] = Json(std::move(results));
        send_json(fd, 200, Json(std::move(obj)));
        return true;
    }
    if (path == "/api/web/read") {
        const ReadResult r = web_read(get_string(body, "url"));
        if (!r.ok) { send_json(fd, 422, error_json(r.error)); return true; }
        Json::Object obj; obj["title"] = Json(r.title); obj["url"] = Json(r.url); obj["text"] = Json(r.text); obj["via"] = Json(r.via);
        send_json(fd, 200, Json(std::move(obj)));
        return true;
    }
    if (path == "/api/vision") {
        const std::string image = get_string(body, "image");
        if (image.rfind("data:image/", 0) != 0 || image.size() > 9'000'000) { send_json(fd, 400, error_json("Attach a PNG, JPEG, WebP or GIF image under 6 MB.")); return true; }
        std::string prompt = truncate(get_string(body, "prompt"), 2000);
        if (prompt.empty()) prompt = "Describe this image in detail. Transcribe any visible text exactly. Mention charts, numbers, people, objects and layout.";
        const VisionResult v = describe_image(image, prompt);
        if (!v.ok) { send_json(fd, 502, error_json(v.error)); return true; }
        Json::Object obj; obj["text"] = Json(v.text); obj["model"] = Json(v.model);
        send_json(fd, 200, Json(std::move(obj)));
        return true;
    }
    if (path == "/api/sync") {
        const Json* data = body.get("data");
        const Json* upd = body.get("updated");
        const double* updated = upd ? std::get_if<double>(&upd->value) : nullptr;
        if (!data || !data->object_ptr() || !updated) { send_json(fd, 400, error_json("Send {updated, data}.")); return true; }
        std::string stored;
        {
            std::lock_guard<std::mutex> lock(g_store_mutex);
            double current = 0;
            if (auto txt = read_file_text(data_path("astro_data/store.json"))) {
                try { const Json cur = JsonParser(*txt).parse(); const Json* u = cur.get("updated"); if (u) if (const double* d = std::get_if<double>(&u->value)) current = *d; } catch (...) {}
                // Conflict: another device saved after the copy this device last synced (base).
                const Json* b = body.get("base");
                const double* base = b ? std::get_if<double>(&b->value) : nullptr;
                if (current > (base ? *base : *updated) && !json_bool(body, "force", false)) { send_response(fd, 409, "application/json; charset=utf-8", *txt); return true; }
            }
            Json::Object doc; doc["updated"] = Json(*updated); doc["data"] = *data;
            stored = dump_json(Json(std::move(doc)));
            if (!write_file_private(data_path("astro_data/store.json"), stored)) { send_json(fd, 500, error_json("Could not save on the server.")); return true; }
        }
        if (const std::string url = gateway_store_url(); !url.empty()) {
            std::thread([url, stored] {
                http_fetch(url, {"Content-Type: application/json", "Authorization: Bearer " + gateway_token()}, &stored, 30, 100'000, false);
            }).detach();
        }
        Json::Object obj; obj["ok"] = Json(true); obj["updated"] = Json(*updated); obj["mirrored"] = Json(!gateway_store_url().empty());
        send_json(fd, 200, Json(std::move(obj)));
        return true;
    }
    return false;
}

// Static app files: anything directly inside web/ with a known extension.
void serve_static(int fd, const HttpRequest& request) {
    std::string name = request.path == "/" ? "index.html" : request.path.substr(1);
    if (const auto q = name.find('?'); q != std::string::npos) name.resize(q);
    // Flat files in web/, plus exactly one level under web/vendor/ (editor + terminal bundle).
    const bool vendor = name.rfind("vendor/", 0) == 0;
    const std::string leaf = vendor ? name.substr(7) : name;
    const bool safe = !leaf.empty() && leaf.size() < 80 && leaf[0] != '.' &&
                      std::all_of(leaf.begin(), leaf.end(), [](unsigned char c) { return std::isalnum(c) || c == '.' || c == '-' || c == '_'; });
    static const std::map<std::string, std::string> types = {
        {".html", "text/html; charset=utf-8"}, {".css", "text/css; charset=utf-8"}, {".js", "text/javascript; charset=utf-8"},
        {".json", "application/json; charset=utf-8"}, {".webmanifest", "application/manifest+json"}, {".svg", "image/svg+xml"},
        {".png", "image/png"}, {".ico", "image/x-icon"}, {".txt", "text/plain; charset=utf-8"}, {".woff2", "font/woff2"}};
    const auto dot = name.rfind('.');
    const auto type = dot == std::string::npos ? types.end() : types.find(name.substr(dot));
    if (!safe || type == types.end()) { send_response(fd, 404, "text/plain; charset=utf-8", "Not found."); return; }
    std::ifstream in("web/" + name, std::ios::binary);
    if (!in) { send_response(fd, 404, "text/plain; charset=utf-8", "Not found."); return; }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (vendor) {   // versioned by ?v=..., safe to cache
        std::ostringstream h;
        h << "HTTP/1.1 200 OK\r\nContent-Type: " << type->second << "\r\nContent-Length: " << (request.method == "HEAD" ? 0 : content.size())
          << "\r\nCache-Control: public, max-age=604800, immutable\r\nAccess-Control-Allow-Origin: *\r\nCross-Origin-Resource-Policy: cross-origin\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n";
        send_all(fd, h.str());
        if (request.method != "HEAD") send_all(fd, content);
        return;
    }
    if (request.method == "HEAD") content.clear();
    send_response(fd, 200, type->second, content, true);
}
