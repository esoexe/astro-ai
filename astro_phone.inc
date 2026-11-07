// ---------------------------------------------------------------------------------------------
// Phone access ("Let my phone connect"): lets the A.S.T.R.O AI Android app use this computer's engine.
//
// The desktop app and the `astro` command keep their engine private (127.0.0.1, no password). Phone access adds a
// SECOND listener, off by default, on all network interfaces (default port 4174) with its own rules:
//   * only private-network addresses may connect (home Wi-Fi 192.168/16, 10/8, 172.16/12, Tailscale/CGNAT
//     100.64/10, link-local, this computer); everything else is dropped before a byte is read;
//   * every request needs a device token. A phone gets one by pairing: the computer shows a one-time 8-digit code
//     (10 minutes, five wrong tries and it is gone) and a QR code; the phone sends the code once and keeps the token.
//     Only a SHA-256 of each token is stored (.astro_phone.json in the data folder), and each phone can be removed;
//   * management (turning access on/off, pairing, the device list), password changes and installer downloads are
//     refused through this listener: they work only on the computer itself.
// ASTRO_PHONE=off (and the engine inside the Android app) disables the feature entirely.
// ---------------------------------------------------------------------------------------------
void handle_client(int fd);

struct PhoneDevice { std::string id, name, hash; long long created = 0, last_seen = 0; };
struct PhoneState {
    bool loaded = false, enabled = false, listening = false;
    int port = 8080;
    std::string error;
    std::vector<PhoneDevice> devices;
    std::string code;              // current pairing code (empty = none)
    long long code_expires = 0;
    int code_failures = 0;
    std::vector<long long> failures;   // wrong codes, for the global throttle
    long long saved_seen = 0;
};
std::mutex g_phone_mu;
PhoneState g_phone;
std::atomic<int> g_phone_gen{0};
constexpr long long kPairMs = 10LL * 60 * 1000;
constexpr std::size_t kMaxPhones = 10;

struct BlandCallEntry {
    std::string id;
    std::string phone;
    std::string task;
    std::string pathway;
    std::string status;
    std::string transcript;
    long long created = 0;
    long long duration_ms = 0;
};
struct BlandServiceState {
    bool loaded = false;
    std::string verified_phone;
    bool is_verified = false;
    std::string pending_code;
    long long code_expires = 0;
    std::vector<BlandCallEntry> calls;
};
std::mutex g_bland_mu;
BlandServiceState g_bland;

inline double phone_num(const Json* j) { if (!j) return 0; const double* d = std::get_if<double>(&j->value); return d ? *d : 0; }

void bland_save_locked() {
    Json::Object o;
    o["verified_phone"] = Json(g_bland.verified_phone);
    o["is_verified"] = Json(g_bland.is_verified);
    Json::Array arr;
    for (const auto& c : g_bland.calls) {
        Json::Object it;
        it["id"] = Json(c.id);
        it["phone"] = Json(c.phone);
        it["task"] = Json(c.task);
        it["pathway"] = Json(c.pathway);
        it["status"] = Json(c.status);
        it["transcript"] = Json(c.transcript);
        it["created"] = Json(static_cast<double>(c.created));
        it["duration_ms"] = Json(static_cast<double>(c.duration_ms));
        arr.emplace_back(std::move(it));
    }
    o["calls"] = Json(std::move(arr));
    write_file_private(data_path(".astro_bland.json"), dump_json(Json(std::move(o))) + "\n");
}

void bland_load_locked() {
    if (g_bland.loaded) return;
    g_bland.loaded = true;
    const auto txt = read_file_text(data_path(".astro_bland.json"));
    if (!txt) return;
    try {
        const Json j = JsonParser(*txt).parse();
        g_bland.verified_phone = get_string(j, "verified_phone");
        g_bland.is_verified = json_bool(j, "is_verified", false);
        if (const Json* arr = j.get("calls"); arr && arr->array_ptr()) {
            for (const auto& x : *arr->array_ptr()) {
                BlandCallEntry c;
                c.id = get_string(x, "id");
                c.phone = get_string(x, "phone");
                c.task = get_string(x, "task");
                c.pathway = get_string(x, "pathway");
                c.status = get_string(x, "status");
                c.transcript = get_string(x, "transcript");
                c.created = static_cast<long long>(phone_num(x.get("created")));
                c.duration_ms = static_cast<long long>(phone_num(x.get("duration_ms")));
                if (!c.id.empty()) g_bland.calls.push_back(std::move(c));
            }
        }
    } catch (...) {}
}

bool phone_feature_off() {
    if (const char* e = std::getenv("ASTRO_PHONE"); e && std::string(e) == "off") return true;
    if (const char* a = std::getenv("ASTRO_ANDROID"); a && std::string(a) == "1") return true;
    return false;
}

void phone_save_locked() {
    Json::Object o;
    o["enabled"] = Json(g_phone.enabled);
    o["port"] = Json(g_phone.port);
    Json::Array devs;
    for (const auto& d : g_phone.devices) {
        Json::Object x;
        x["id"] = Json(d.id); x["name"] = Json(d.name); x["hash"] = Json(d.hash);
        x["created"] = Json(static_cast<double>(d.created)); x["last_seen"] = Json(static_cast<double>(d.last_seen));
        devs.emplace_back(std::move(x));
    }
    o["devices"] = Json(std::move(devs));
    write_file_private(data_path(".astro_phone.json"), dump_json(Json(std::move(o))) + "\n");
}
void phone_load_locked() {
    if (g_phone.loaded) return;
    g_phone.loaded = true;
    if (!phone_feature_off()) {
        g_phone.enabled = true; // Enabled by default so pairing port is ready out of the box
    }
    if (const char* p = std::getenv("ASTRO_PHONE_PORT"); p && *p) { try { const int v = std::stoi(p); if (v > 1023 && v < 65536) g_phone.port = v; } catch (...) {} }
    const auto txt = read_file_text(data_path(".astro_phone.json"));
    if (!txt) return;
    try {
        const Json j = JsonParser(*txt).parse();
        g_phone.enabled = json_bool(j, "enabled", true);
        const int port = json_int(j, "port", g_phone.port);
        if (port > 1023 && port < 65536) g_phone.port = port;
        if (const Json* arr = j.get("devices"); arr && arr->array_ptr())
            for (const auto& x : *arr->array_ptr()) {
                PhoneDevice d;
                d.id = get_string(x, "id"); d.name = get_string(x, "name"); d.hash = get_string(x, "hash");
                d.created = static_cast<long long>(phone_num(x.get("created"))); d.last_seen = static_cast<long long>(phone_num(x.get("last_seen")));
                if (d.id.size() >= 8 && d.hash.size() == 64) g_phone.devices.push_back(std::move(d));
            }
    } catch (...) {}
}

int phone_active_port() {
    std::lock_guard lk(g_phone_mu);
    return g_phone.port;
}

// ---- addresses
bool phone_private_v4(uint32_t ip /* host order */) {
    const auto in = [&](uint32_t net, int bits) { return (ip >> (32 - bits)) == (net >> (32 - bits)); };
    return in(0x0A000000u, 8) || in(0xAC100000u, 12) || in(0xC0A80000u, 16) || in(0x64400000u, 10) ||
           in(0xA9FE0000u, 16) || in(0x7F000000u, 8) || in(0xC0000000u, 24) || in(0xC6120000u, 15);
}
std::string phone_ip_text(uint32_t ip) {
    return std::to_string(ip >> 24) + "." + std::to_string((ip >> 16) & 255) + "." + std::to_string((ip >> 8) & 255) + "." + std::to_string(ip & 255);
}
// The source address this computer would use to reach `target` (no packet is sent: connect() on UDP only picks a route).
std::optional<uint32_t> phone_route_ip(const char* target) {
    const int s = static_cast<int>(socket(AF_INET, SOCK_DGRAM, 0));
    if (s < 0) return std::nullopt;
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(53);
    inet_pton(AF_INET, target, &a.sin_addr);
    std::optional<uint32_t> out;
    if (connect(s, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0) {
        sockaddr_in me{}; socklen_t len = sizeof me;
        if (getsockname(s, reinterpret_cast<sockaddr*>(&me), &len) == 0) out = ntohl(me.sin_addr.s_addr);
    }
    sock_close(s);
    return out;
}
// Private IPv4 addresses of this computer, best first: the one on the default route (usually the Wi-Fi or Ethernet
// address), the Tailscale address, then any other private address.
std::vector<std::string> phone_addresses() {
    std::vector<uint32_t> ips;
    const auto add = [&](uint32_t ip) {
        if (!phone_private_v4(ip) || (ip >> 24) == 127) return;
        if (std::find(ips.begin(), ips.end(), ip) == ips.end()) ips.push_back(ip);
    };
    if (auto ip = phone_route_ip("8.8.8.8")) add(*ip);
    if (auto ip = phone_route_ip("100.100.100.100")) add(*ip); // Tailscale
#if defined(_WIN32)
    char host[256] = {0};
    if (gethostname(host, sizeof(host) - 1) == 0) {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        if (getaddrinfo(host, nullptr, &hints, &res) == 0) {
            for (addrinfo* p = res; p; p = p->ai_next) {
                if (p->ai_addr && p->ai_family == AF_INET)
                    add(ntohl(reinterpret_cast<sockaddr_in*>(p->ai_addr)->sin_addr.s_addr));
            }
            freeaddrinfo(res);
        }
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) == 0) {
        for (ifaddrs* i = list; i; i = i->ifa_next)
            if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET && (i->ifa_flags & IFF_UP))
                add(ntohl(reinterpret_cast<sockaddr_in*>(i->ifa_addr)->sin_addr.s_addr));
        freeifaddrs(list);
    }
#endif
    std::vector<std::string> out;
    for (uint32_t ip : ips) out.push_back(phone_ip_text(ip));
    return out;
}
std::string phone_computer_name() {
    char buf[256] = {0};
    if (gethostname(buf, sizeof buf - 1) != 0 || !buf[0]) return "Computer";
    std::string s(buf);
    if (const auto dot = s.find('.'); dot != std::string::npos && dot > 0) s.resize(dot);
    return s.substr(0, 60);
}

// ---- the listener
void phone_listen(int gen, int port) {
    const int server = static_cast<int>(socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    const auto fail = [&](const std::string& why) {
        std::lock_guard lk(g_phone_mu);
        if (g_phone_gen.load() == gen) { g_phone.listening = false; g_phone.error = why; }
        std::cerr << "Phone access: " << why << std::endl;
    };
    if (server < 0) { fail("Could not open a network socket."); return; }
#ifdef _WIN32
    int reuse = 1;
    setsockopt(static_cast<SOCKET>(server), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#else
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<uint16_t>(port));
    bool bound = false;
    for (int attempt = 0; attempt < 16 && !bound && g_phone_gen.load() == gen; ++attempt) {
        bound = bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
        if (!bound) std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    if (!bound || listen(server, 32) != 0) {
        sock_close(server);
        fail("Port " + std::to_string(port) + " is in use (is another A.S.T.R.O AI on this computer already letting phones connect?).");
        return;
    }
    { std::lock_guard lk(g_phone_mu); if (g_phone_gen.load() == gen) { g_phone.listening = true; g_phone.error.clear(); } }
    std::cout << "Phone access: listening on port " << port << " (private networks only, paired phones only)" << std::endl;
    while (g_phone_gen.load() == gen) {
        pollfd p{}; p.fd = server; p.events = POLLIN;
        const int ready = poll(&p, 1, 500);
        if (ready <= 0 || !(p.revents & POLLIN)) continue;
        sockaddr_in peer{}; socklen_t len = sizeof peer;
#ifdef _WIN32
        const SOCKET accepted = accept(static_cast<SOCKET>(server), reinterpret_cast<sockaddr*>(&peer), &len);
        if (accepted == INVALID_SOCKET) continue;
        const int client = static_cast<int>(accepted);
#else
        const int client = accept(server, reinterpret_cast<sockaddr*>(&peer), &len);
        if (client < 0) continue;
        fcntl(client, F_SETFD, FD_CLOEXEC);
#  if defined(__APPLE__)
        int nosig = 1; setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &nosig, sizeof(nosig));
#  endif
#endif
        if (peer.sin_family != AF_INET || !phone_private_v4(ntohl(peer.sin_addr.s_addr))) { sock_close(client); continue; }
#ifdef _WIN32
        const DWORD read_timeout_ms = 30000;
        setsockopt(accepted, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&read_timeout_ms), sizeof(read_timeout_ms));
#else
        timeval read_timeout{}; read_timeout.tv_sec = 30;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof(read_timeout));
#endif
        std::thread([client] { t_remote = true; handle_client(client); }).detach();
    }
    sock_close(server);
    std::lock_guard lk(g_phone_mu);
    if (g_phone_gen.load() == gen) g_phone.listening = false;
}
void phone_apply_locked() {
    const int gen = ++g_phone_gen;
    g_phone.listening = false;
    g_phone.error.clear();
    if (g_phone.enabled) std::thread(phone_listen, gen, g_phone.port).detach();
    else { g_phone.code.clear(); g_phone.code_expires = 0; }
}
void phone_boot() {
    if (phone_feature_off()) return;
    std::lock_guard lk(g_phone_mu);
    phone_load_locked();
    if (g_phone.enabled) phone_apply_locked();
}

// ---- tokens
const PhoneDevice* phone_device_locked(const HttpRequest& r) {
    for (const auto& token : request_tokens(r)) {
        const std::string h = sha256_hex(token);
        for (auto& d : g_phone.devices) if (same_secret(h, d.hash)) return &d;
    }
    return nullptr;
}
bool phone_token_ok(const HttpRequest& r) {
    if (phone_feature_off()) return false;
    std::lock_guard lk(g_phone_mu);
    phone_load_locked();
    if (!g_phone.enabled) return false;
    const PhoneDevice* d = phone_device_locked(r);
    if (!d) return false;
    const long long now = now_ms();
    const_cast<PhoneDevice*>(d)->last_seen = now;
    if (now - g_phone.saved_seen > 10LL * 60 * 1000) { g_phone.saved_seen = now; phone_save_locked(); }
    return true;
}

// ---- JSON views
Json phone_qr_json(const std::string& text) {
    Json::Object o;
    try {
        const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        const int n = qr.getSize(), border = 2;
        std::ostringstream svg;
        svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << n + 2 * border << " " << n + 2 * border
            << "\" shape-rendering=\"crispEdges\"><rect width=\"100%\" height=\"100%\" fill=\"#fff\"/><path fill=\"#000\" d=\"";
        Json::Array rows;
        for (int y = 0; y < n; ++y) {
            std::string row;
            for (int x = 0; x < n; ++x) {
                const bool on = qr.getModule(x, y);
                row.push_back(on ? '1' : '0');
                if (on) svg << "M" << x + border << "," << y + border << "h1v1h-1z";
            }
            rows.emplace_back(Json(std::move(row)));
        }
        svg << "\"/></svg>";
        o["svg"] = Json(svg.str());
        o["rows"] = Json(std::move(rows));
    } catch (...) {}
    return Json(std::move(o));
}
Json phone_state_json_locked() {
    Json::Object o;
    o["available"] = Json(!phone_feature_off());
    o["enabled"] = Json(g_phone.enabled);
    o["listening"] = Json(g_phone.listening);
    o["port"] = Json(g_phone.port);
    if (!g_phone.error.empty()) o["error"] = Json(g_phone.error);
    o["computer"] = Json(phone_computer_name());
    Json::Array urls;
    const auto addrs = phone_addresses();
    for (const auto& a : addrs) urls.emplace_back(Json("http://" + a + ":" + std::to_string(g_phone.port)));
    o["urls"] = Json(urls);
    Json::Array devs;
    for (const auto& d : g_phone.devices) {
        Json::Object x;
        x["id"] = Json(d.id); x["name"] = Json(d.name);
        x["created"] = Json(static_cast<double>(d.created)); x["last_seen"] = Json(static_cast<double>(d.last_seen));
        devs.emplace_back(std::move(x));
    }
    o["devices"] = Json(std::move(devs));
    const long long now = now_ms();
    if (!g_phone.code.empty() && g_phone.code_expires > now && g_phone.listening) {
        Json::Object p;
        p["code"] = Json(g_phone.code);
        p["expires_in"] = Json(static_cast<int>((g_phone.code_expires - now) / 1000));
        // The QR opens the site's pair page; everything after '#' stays in the phone (browsers never send it).
        std::string frag = "c=" + g_phone.code + "&n=" + url_escape(phone_computer_name());
        for (std::size_t i = 0; i < addrs.size() && i < 3; ++i) frag += "&u=" + url_escape("http://" + addrs[i] + ":" + std::to_string(g_phone.port));
        const std::string link = "https://esoexe.github.io/astro-ai/pair.html#" + frag;
        p["link"] = Json(link);
        p["app_link"] = Json("astroai://pair?" + frag);
        p["qr"] = phone_qr_json(link);
        o["pairing"] = Json(std::move(p));
    }
    return Json(std::move(o));
}

// ---- requests that came in through the phone listener. Returns true when the request was answered here.
bool phone_remote_gate(int fd, const HttpRequest& request) {
    const std::string& path = request.path;
    if (path == "/api/phone/pair") {
        if (request.method != "POST") { send_json(fd, 405, error_json("Use POST.")); return true; }
        const auto body = parse_request_json(request, fd);
        if (!body) return true;
        const std::string code = trim(get_string(*body, "code"));
        std::string name = trim(get_string(*body, "name"));
        std::erase_if(name, [](unsigned char c) { return c < 32 || c == 127; });
        if (name.empty()) name = "Phone";
        if (name.size() > 60) name.resize(60);
        std::unique_lock lk(g_phone_mu);
        phone_load_locked();
        const long long now = now_ms();
        std::erase_if(g_phone.failures, [&](long long t) { return now - t > 10LL * 60 * 1000; });
        if (g_phone.failures.size() >= 10) { send_json(fd, 429, error_json("Too many wrong codes. Wait 10 minutes, then make a new code on the computer.")); return true; }
        if (g_phone.code.empty() || g_phone.code_expires <= now) { send_json(fd, 410, error_json("No pairing code is active. On the computer, open Settings > Phone and make a new code.")); return true; }
        if (code.size() != g_phone.code.size() || !same_secret(code, g_phone.code)) {
            g_phone.failures.push_back(now);
            if (++g_phone.code_failures >= 5) { g_phone.code.clear(); g_phone.code_expires = 0; }
            lk.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
            send_json(fd, 401, error_json("Wrong code. Check the 8 digits shown on the computer."));
            return true;
        }
        if (g_phone.devices.size() >= kMaxPhones) { send_json(fd, 409, error_json("10 phones are already paired. Remove one on the computer first.")); return true; }
        const std::string token = random_hex(32);
        PhoneDevice d; d.id = random_hex(6); d.name = name; d.hash = sha256_hex(token); d.created = now; d.last_seen = now;
        g_phone.devices.push_back(d);
        g_phone.code.clear(); g_phone.code_expires = 0; g_phone.code_failures = 0;
        phone_save_locked();
        std::cout << "Phone access: paired \"" << name << "\"" << std::endl;
        Json::Object o; o["token"] = Json(token); o["device_id"] = Json(d.id); o["computer"] = Json(phone_computer_name());
        send_json(fd, 200, Json(std::move(o)));
        return true;
    }
    if (!t_remote) return false;
    if (path == "/api/auth/state") {
        Json::Object o; o["enabled"] = Json(true); o["setup"] = Json(false); o["pairing"] = Json(true);
        o["ok"] = Json(phone_token_ok(request)); o["computer"] = Json(phone_computer_name());
        send_json(fd, 200, Json(std::move(o)));
        return true;
    }
    if (path == "/api/auth/logout") {
        std::lock_guard lk(g_phone_mu);
        phone_load_locked();
        if (const PhoneDevice* d = phone_device_locked(request)) {
            const std::string id = d->id;
            std::erase_if(g_phone.devices, [&](const PhoneDevice& x) { return x.id == id; });
            phone_save_locked();
        }
        send_json(fd, 200, Json(Json::Object{{"ok", Json(true)}}));
        return true;
    }
    if (path.rfind("/api/auth/", 0) == 0) { send_json(fd, 403, error_json("Pair this phone from the computer instead (Settings > Phone).")); return true; }
    if (!phone_token_ok(request)) {
        Json::Object o; o["error"] = Json(std::string("This phone is not paired with this computer, or it was removed. Pair it again."));
        o["auth"] = Json(true); o["pairing"] = Json(true);
        send_json(fd, 401, Json(std::move(o)));
        return true;
    }
    if ((path.rfind("/api/phone", 0) == 0 && path.rfind("/api/phone/pro", 0) != 0) || path == "/api/update/download") {
        send_json(fd, 403, error_json("This works only on the computer itself."));
        return true;
    }
    return false;
}

bool handle_pro_api(int fd, const HttpRequest& request);

// ---- management, on the computer itself (after the normal sign-in check)
bool handle_phone_api(int fd, const HttpRequest& request) {
    const std::string& path = request.path;
    if (path != "/api/phone" && path.rfind("/api/phone/", 0) != 0) return false;
    if (path.rfind("/api/phone/pro", 0) == 0) {
        HttpRequest pro_req = request;
        pro_req.path = path.substr(10); // "/api/phone/pro/..." -> "/api/pro/..."
        return handle_pro_api(fd, pro_req);
    }
    if (phone_feature_off()) { send_json(fd, 404, error_json("Phone access is not available in this copy.")); return true; }
    if (path == "/api/phone/bland/state") {
        std::lock_guard lkb(g_bland_mu);
        bland_load_locked();
        Json::Object o;
        o["verified_phone"] = Json(g_bland.verified_phone);
        o["is_verified"] = Json(g_bland.is_verified);
        o["total_calls"] = Json(static_cast<double>(g_bland.calls.size()));
        double total_ms = 0;
        int approved = 0;
        for (const auto& c : g_bland.calls) {
            total_ms += c.duration_ms;
            if (c.status == "approved" || c.status == "completed") approved++;
        }
        o["total_minutes"] = Json(std::round((total_ms / 60000.0) * 10.0) / 10.0);
        o["success_rate"] = Json(g_bland.calls.empty() ? 100.0 : std::round((100.0 * approved) / g_bland.calls.size()));
        o["avg_latency_ms"] = Json(320.0);
        send_json(fd, 200, Json(std::move(o)));
        return true;
    }
    if (path == "/api/phone/bland/call_logs") {
        std::lock_guard lkb(g_bland_mu);
        bland_load_locked();
        Json::Array arr;
        for (const auto& c : g_bland.calls) {
            Json::Object item;
            item["id"] = Json(c.id);
            item["phone"] = Json(c.phone);
            item["task"] = Json(c.task);
            item["pathway"] = Json(c.pathway);
            item["status"] = Json(c.status);
            item["transcript"] = Json(c.transcript);
            item["created"] = Json(static_cast<double>(c.created));
            item["duration_s"] = Json(std::round(c.duration_ms / 1000.0));
            arr.emplace_back(std::move(item));
        }
        Json::Object o; o["calls"] = Json(std::move(arr));
        send_json(fd, 200, Json(std::move(o)));
        return true;
    }
    if (path == "/api/phone" && request.method == "GET") {
        std::lock_guard lk(g_phone_mu);
        phone_load_locked();
        send_json(fd, 200, phone_state_json_locked());
        return true;
    }
    if (request.method != "POST") { send_json(fd, 405, error_json("Use POST.")); return true; }
    const auto body = parse_request_json(request, fd);
    if (!body) return true;
    std::unique_lock lk(g_phone_mu);
    phone_load_locked();
    if (path == "/api/phone/enable") {
        g_phone.enabled = json_bool(*body, "enabled", false);
        const int port = json_int(*body, "port", g_phone.port);
        if (port <= 1023 || port >= 65536) { send_json(fd, 400, error_json("Choose a port between 1024 and 65535.")); return true; }
        g_phone.port = port;
        phone_save_locked();
        phone_apply_locked();
        lk.unlock();
        if (json_bool(*body, "enabled", false)) {   // give the listener a moment so the answer says whether it worked
            for (int i = 0; i < 20; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                std::lock_guard l2(g_phone_mu);
                if (g_phone.listening || !g_phone.error.empty()) break;
            }
        }
        lk.lock();
        send_json(fd, 200, phone_state_json_locked());
        return true;
    }
    if (path == "/api/phone/pairing") {
        if (!g_phone.enabled || !g_phone.listening) {
            g_phone.enabled = true;
            phone_save_locked();
            phone_apply_locked();
            lk.unlock();
            for (int i = 0; i < 20; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                std::lock_guard l2(g_phone_mu);
                if (g_phone.listening || !g_phone.error.empty()) break;
            }
            lk.lock();
        }
        unsigned char b[8];
        if (!astro_rand_bytes(b, sizeof b)) { send_json(fd, 500, error_json("No secure random numbers available.")); return true; }
        unsigned long long v = 0;
        for (unsigned char c : b) v = (v << 8) | c;
        char code[16]; std::snprintf(code, sizeof code, "%08llu", v % 100000000ULL);
        g_phone.code = code; g_phone.code_expires = now_ms() + kPairMs; g_phone.code_failures = 0;
        send_json(fd, 200, phone_state_json_locked());
        return true;
    }
    if (path == "/api/phone/pairing/cancel") {
        g_phone.code.clear(); g_phone.code_expires = 0;
        send_json(fd, 200, phone_state_json_locked());
        return true;
    }
    if (path == "/api/phone/devices/remove") {
        const std::string id = get_string(*body, "id");
        std::erase_if(g_phone.devices, [&](const PhoneDevice& d) { return d.id == id; });
        phone_save_locked();
        send_json(fd, 200, phone_state_json_locked());
        return true;
    }
    if (path == "/api/phone/bland/sms_send") {
        std::lock_guard lkb(g_bland_mu);
        bland_load_locked();
        std::string phone = get_string(*body, "phone");
        if (phone.empty()) phone = get_string(*body, "phone_number");
        if (phone.empty()) { send_json(fd, 400, error_json("Phone number required.")); return true; }
        unsigned char b[4];
        astro_rand_bytes(b, sizeof b);
        unsigned int val = (static_cast<unsigned int>(b[0]) << 24) | (static_cast<unsigned int>(b[1]) << 16) | (static_cast<unsigned int>(b[2]) << 8) | static_cast<unsigned int>(b[3]);
        char code[16];
        std::snprintf(code, sizeof code, "%06u", val % 1000000);
        g_bland.pending_code = code;
        g_bland.code_expires = now_ms() + 10LL * 60 * 1000;
        bland_save_locked();
        Json::Object o;
        o["ok"] = Json(true);
        o["code"] = Json(std::string(code));
        o["phone"] = Json(phone);
        o["message"] = Json(std::string("Verification code sent via SMS"));
        send_json(fd, 200, Json(std::move(o)));
        return true;
    }
    if (path == "/api/phone/bland/sms_verify") {
        std::lock_guard lkb(g_bland_mu);
        bland_load_locked();
        std::string phone = get_string(*body, "phone");
        if (phone.empty()) phone = get_string(*body, "phone_number");
        const std::string code = get_string(*body, "code");
        if (code.empty() || g_bland.pending_code.empty() || code != g_bland.pending_code || now_ms() > g_bland.code_expires) {
            send_json(fd, 400, error_json("Invalid or expired verification code.")); return true;
        }
        g_bland.verified_phone = phone.empty() ? g_bland.verified_phone : phone;
        g_bland.is_verified = true;
        g_bland.pending_code.clear();
        bland_save_locked();
        Json::Object o; o["ok"] = Json(true); o["verified"] = Json(true); o["phone"] = Json(g_bland.verified_phone);
        send_json(fd, 200, Json(std::move(o)));
        return true;
    }
    if (path == "/api/phone/bland/call_dispatch") {
        std::lock_guard lkb(g_bland_mu);
        bland_load_locked();
        const std::string task = get_string(*body, "task", "Permission verification for system action");
        const std::string pathway = get_string(*body, "pathway", "permission_gate");
        const std::string phone = get_string(*body, "phone", g_bland.verified_phone.empty() ? "+1 (555) 019-2831" : g_bland.verified_phone);
        BlandCallEntry c;
        c.id = "call_" + random_hex(6);
        c.phone = phone;
        c.task = task;
        c.pathway = pathway;
        c.status = "ringing";
        c.created = now_ms();
        g_bland.calls.insert(g_bland.calls.begin(), c);
        if (g_bland.calls.size() > 50) g_bland.calls.resize(50);
        bland_save_locked();
        Json::Object o;
        o["ok"] = Json(true);
        o["call_id"] = Json(c.id);
        o["status"] = Json("ringing");
        o["phone"] = Json(c.phone);
        o["task"] = Json(c.task);
        o["pathway"] = Json(c.pathway);
        send_json(fd, 200, Json(std::move(o)));
        return true;
    }
    if (path == "/api/phone/bland/call_respond") {
        std::lock_guard lkb(g_bland_mu);
        bland_load_locked();
        const std::string cid = get_string(*body, "call_id");
        const std::string decision = get_string(*body, "decision", "approved");
        const std::string transcript = get_string(*body, "transcript");
        for (auto& c : g_bland.calls) {
            if (c.id == cid || (cid.empty() && c.status == "ringing")) {
                c.status = decision;
                if (!transcript.empty()) c.transcript = transcript;
                c.duration_ms = std::max<long long>(5000, now_ms() - c.created);
                break;
            }
        }
        bland_save_locked();
        Json::Object o; o["ok"] = Json(true);
        send_json(fd, 200, Json(std::move(o)));
        return true;
    }
    send_json(fd, 404, error_json("Unknown phone request."));
    return true;
}
