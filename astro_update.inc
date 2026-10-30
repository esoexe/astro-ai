// ---------- Updates (1.7) ----------
// The website publishes latest.json next to the CLEAN downloads:
//   {"version":"1.7.0","date":"...","released":"YYYY-MM-DD","notes":"<changelog url>","highlights":[...],"highlights_i18n":{"fr":[...],"ar":[...]},
//    "assets":{"windows-setup":{"name":..., "url":..., "sha256":..., "bytes":N}, ...}}
// GET  /api/update/check[?force=1]  -> current vs latest (cached for 6 hours; nothing is sent about the user)
// POST /api/update/download {"asset":"windows-setup"} -> fetches that file from the official site only,
//      checks its SHA-256 against latest.json and keeps it in <data>/updates/. Running it is the desktop
//      app's job (it has to outlive this server), or `astro update` for terminal installs.

const char* kUpdateFeed = "https://esoexe.github.io/astro-ai/latest.json";

std::string update_feed_url() {
    if (const char* u = std::getenv("ASTRO_UPDATE_URL"); u && *u) return u;   // tests and mirrors
    return kUpdateFeed;
}
bool update_feed_custom() { const char* u = std::getenv("ASTRO_UPDATE_URL"); return u && *u; }

// 1.10.0 > 1.9.3; anything after a '-' (1.7.0-rc1) sorts before the plain release.
int version_compare(const std::string& a, const std::string& b) {
    auto split = [](const std::string& v, std::string& tail) {
        std::vector<long long> out; std::string cur; std::size_t i = 0;
        std::string s = v; if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.erase(0, 1);
        for (; i < s.size() && s[i] != '-' && s[i] != '+'; ++i) {
            if (s[i] == '.') { out.push_back(cur.empty() ? 0 : std::atoll(cur.c_str())); cur.clear(); }
            else if (std::isdigit(static_cast<unsigned char>(s[i]))) cur += s[i];
        }
        out.push_back(cur.empty() ? 0 : std::atoll(cur.c_str()));
        tail = i < s.size() && s[i] == '-' ? s.substr(i + 1) : "";
        return out;
    };
    std::string ta, tb;
    const auto va = split(a, ta), vb = split(b, tb);
    for (std::size_t i = 0; i < std::max(va.size(), vb.size()); ++i) {
        const long long x = i < va.size() ? va[i] : 0, y = i < vb.size() ? vb[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    if (ta == tb) return 0;
    if (ta.empty()) return 1;
    if (tb.empty()) return -1;
    return ta < tb ? -1 : 1;
}

std::mutex g_update_mu;
Json g_update_feed;            // last good latest.json
long long g_update_at = 0;     // when it was fetched (ms)
std::string g_update_err;

// Files may only come from the official site / repo (or the test feed's own host).
bool update_url_ok(const std::string& url) {
    if (update_feed_custom()) {
        const std::string feed = update_feed_url();
        const auto host_of = [](const std::string& u) { const auto p = u.find("://"); if (p == std::string::npos) return std::string(); const auto e = u.find('/', p + 3); return u.substr(0, e); };
        return !host_of(url).empty() && host_of(url) == host_of(feed);
    }
    return url.rfind("https://esoexe.github.io/astro-ai/", 0) == 0 || url.rfind("https://github.com/esoexe/astro-ai/releases/download/", 0) == 0;
}

bool update_refresh(bool force, std::string& error) {
    std::lock_guard lk(g_update_mu);
    if (!force && g_update_at && now_ms() - g_update_at < 6LL * 3600 * 1000) { error = g_update_err; return g_update_feed.is_object(); }
    const FetchResult r = http_fetch(update_feed_url() + (update_feed_url().find('?') == std::string::npos ? "?t=" : "&t=") + std::to_string(now_ms() / 60000),
                                     {"Accept: application/json"}, nullptr, 15, 200'000, !update_feed_custom());
    g_update_at = now_ms();
    if (!r.error.empty() || r.status != 200) {
        g_update_err = !r.error.empty() ? r.error : "update feed answered HTTP " + std::to_string(r.status);
        error = g_update_err;
        return g_update_feed.is_object();
    }
    try {
        Json j = JsonParser(r.body).parse();
        if (!j.is_object() || get_string(j, "version").empty()) throw std::runtime_error("latest.json has no version");
        g_update_feed = std::move(j); g_update_err.clear();
    } catch (const std::exception& e) { g_update_err = std::string("bad update feed: ") + e.what(); error = g_update_err; }
    return g_update_feed.is_object();
}

// Installed by a package manager? Then it updates the app, not our installer (a second copy would be confusing).
std::string install_manager() {
    if (const char* m = std::getenv("ASTRO_INSTALL_MANAGER"); m && *m) return m;   // tests
    std::string p;
#ifdef _WIN32
    wchar_t b[MAX_PATH * 2]; const DWORD n = GetModuleFileNameW(nullptr, b, MAX_PATH * 2);
    for (DWORD i = 0; i < n; ++i) { const wchar_t c = b[i]; p += c == L'\\' ? '/' : (c < 128 ? static_cast<char>(std::tolower(static_cast<int>(c))) : '?'); }
#elif defined(__linux__)
    char b[4096]; const ssize_t n = readlink("/proc/self/exe", b, sizeof b - 1);
    if (n > 0) { p.assign(b, static_cast<std::size_t>(n)); for (auto& c : p) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
#endif
    if (p.find("/scoop/apps/") != std::string::npos) return "scoop";
    if (p.find("/cellar/") != std::string::npos || p.find("linuxbrew") != std::string::npos) return "homebrew";
    return "";
}

Json update_status_json(bool force) {
    std::string error;
    const bool have = update_refresh(force, error);
    Json::Object o;
    o["current"] = Json(std::string(ASTRO_APP_VERSION));
    if (const std::string m = install_manager(); !m.empty()) o["manager"] = Json(m);
    if (!error.empty()) o["error"] = Json(error);
    if (have) {
        std::lock_guard lk(g_update_mu);
        const std::string latest = get_string(g_update_feed, "version");
        o["latest"] = Json(latest);
        o["available"] = Json(version_compare(latest, ASTRO_APP_VERSION) > 0);
        o["date"] = Json(get_string(g_update_feed, "date"));
        o["notes"] = Json(get_string(g_update_feed, "notes"));
        if (const Json* h = g_update_feed.get("highlights"); h && h->is_array()) o["highlights"] = *h;
        if (const Json* h = g_update_feed.get("highlights_i18n"); h && h->is_object()) o["highlights_i18n"] = *h;   /* {"fr":[...],"ar":[...]} */
        if (const std::string r = get_string(g_update_feed, "released"); !r.empty()) o["released"] = Json(r);
        if (const Json* a = g_update_feed.get("assets"); a && a->is_object()) o["assets"] = *a;
    } else o["available"] = Json(false);
    o["checked_at"] = Json(static_cast<double>(g_update_at));
    return Json(std::move(o));
}

void update_download(int fd, const Json& body) {
    const std::string key = get_string(body, "asset");
    std::string error;
    if (!update_refresh(false, error)) { send_json(fd, 502, error_json(error.empty() ? "Could not read the update feed." : error)); return; }
    std::string name, url, want;
    {
        std::lock_guard lk(g_update_mu);
        const Json* assets = g_update_feed.get("assets");
        const Json* a = assets ? assets->get(key) : nullptr;
        if (!a || !a->is_object()) { send_json(fd, 404, error_json("No download for this system in the update feed.")); return; }
        name = get_string(*a, "name"); url = get_string(*a, "url"); want = lower_copy(get_string(*a, "sha256"));
    }
    // A plain file name, nothing that can climb out of the updates folder.
    if (name.empty() || name.find_first_of("/\\:") != std::string::npos || name[0] == '.' || want.size() != 64 || !update_url_ok(url)) {
        send_json(fd, 400, error_json("The update feed entry is not valid; refusing to download it.")); return;
    }
    const FetchResult r = http_fetch(url, {"Accept: application/octet-stream"}, nullptr, 600, 400'000'000, !update_feed_custom());
    if (!r.error.empty() || r.status != 200 || r.truncated) {
        send_json(fd, 502, error_json(!r.error.empty() ? r.error : r.truncated ? "download is too large" : "download answered HTTP " + std::to_string(r.status))); return;
    }
    if (!update_url_ok(r.final_url.empty() ? url : r.final_url) && r.final_url.find("githubusercontent.com/") == std::string::npos) {
        send_json(fd, 502, error_json("The download was redirected somewhere unexpected; refusing it.")); return;
    }
    const std::string got = sha256_hex(r.body);
    if (got != want) { send_json(fd, 502, error_json("Checksum mismatch: the downloaded file is not the published one. Nothing was installed.")); return; }
    const fs::path dir = fs::path(data_dir()) / "updates";
    std::error_code ec; fs::create_directories(dir, ec);
    for (const auto& e : fs::directory_iterator(dir, ec)) if (e.path().filename().string() != name) fs::remove(e.path(), ec);   // keep only the newest
    const fs::path out = dir / name, tmp = dir / (name + ".part");
    { std::ofstream f(tmp, std::ios::binary | std::ios::trunc); f.write(r.body.data(), static_cast<std::streamsize>(r.body.size())); if (!f) { send_json(fd, 500, error_json("Could not save the download.")); return; } }
    fs::rename(tmp, out, ec);
    if (ec) { send_json(fd, 500, error_json("Could not save the download: " + ec.message())); return; }
    Json::Object o; o["ok"] = Json(true); o["name"] = Json(name); o["path"] = Json(fs::absolute(out, ec).string());
    o["sha256"] = Json(got); o["bytes"] = Json(static_cast<double>(r.body.size()));
    send_json(fd, 200, Json(std::move(o)));
}

bool handle_update_api(int fd, const HttpRequest& request) {
    if (request.path != "/api/update/check" && request.path != "/api/update/download") return false;
    if (request.headers.find("x-astro-client") == request.headers.end()) { send_json(fd, 403, error_json("Missing client header.")); return true; }
    try {
        if (request.path == "/api/update/check") { send_json(fd, 200, update_status_json(request.query.find("force=1") != std::string::npos)); return true; }
        if (request.method != "POST") { send_json(fd, 405, error_json("Use POST.")); return true; }
        const auto body = parse_request_json(request, fd);
        if (!body) return true;
        update_download(fd, *body);
    } catch (const std::exception& e) { send_json(fd, 500, error_json(e.what())); }
    return true;
}
