// ---------------------------------------------------------------------------------------------
// A.S.T.R.O AI Pro: Executive HUD, Autonomous Sentinel, Dual-Presence Bridge & Proactive Contact
// ---------------------------------------------------------------------------------------------

struct ProContactEvent {
    std::string id;
    std::string reason;
    std::string channel;       // "call", "sms", "alert", "auto"
    std::string recipient;
    std::string status;        // "dispatched", "completed", "acknowledged", "failed"
    std::string priority;      // "urgent", "high", "normal"
    std::string details;
    long long created = 0;
};

struct ProWatchEvent {
    std::string id;
    std::string project;
    std::string event_type;    // "build_error", "test_failure", "task_stalled", "milestone_complete", "permission_gate"
    std::string details;
    std::string action_taken;  // "contact_dispatched", "auto_healed", "logged"
    long long timestamp = 0;
};

struct ProDeviceSession {
    std::string device_id;
    std::string name;
    std::string client_type;   // "android", "desktop", "web"
    long long last_seen = 0;
    bool failover_ready = true;
    double ping_ms = 1.0;
};

struct ProState {
    bool loaded = false;
    bool enabled = true;
    std::string edition = "A.S.T.R.O AI Pro";
    std::string version = "2.0.0-PRO";
    bool sentinel_active = true;
    bool auto_failover_enabled = true;
    std::string emergency_phone;
    std::vector<ProContactEvent> contacts;
    std::vector<ProWatchEvent> watch_events;
    std::vector<ProDeviceSession> devices;
    std::vector<ProContactEvent> pending_phone_calls;
    long long started_at = 0;
    long long failover_checkpoint = 0;
};

std::mutex g_pro_mu;
ProState g_pro;
void pro_load_locked();
void pro_save_locked();

void pro_save_locked() {
    Json::Object root;
    root["enabled"] = Json(g_pro.enabled);
    root["sentinel_active"] = Json(g_pro.sentinel_active);
    root["auto_failover_enabled"] = Json(g_pro.auto_failover_enabled);
    root["emergency_phone"] = Json(g_pro.emergency_phone);
    root["failover_checkpoint"] = Json(static_cast<double>(g_pro.failover_checkpoint));

    Json::Array contacts_arr;
    for (const auto& c : g_pro.contacts) {
        Json::Object item;
        item["id"] = Json(c.id);
        item["reason"] = Json(c.reason);
        item["channel"] = Json(c.channel);
        item["recipient"] = Json(c.recipient);
        item["status"] = Json(c.status);
        item["priority"] = Json(c.priority);
        item["details"] = Json(c.details);
        item["created"] = Json(static_cast<double>(c.created));
        contacts_arr.emplace_back(std::move(item));
    }
    root["contacts"] = Json(std::move(contacts_arr));

    Json::Array events_arr;
    for (const auto& w : g_pro.watch_events) {
        Json::Object item;
        item["id"] = Json(w.id);
        item["project"] = Json(w.project);
        item["event_type"] = Json(w.event_type);
        item["details"] = Json(w.details);
        item["action_taken"] = Json(w.action_taken);
        item["timestamp"] = Json(static_cast<double>(w.timestamp));
        events_arr.emplace_back(std::move(item));
    }
    root["watch_events"] = Json(std::move(events_arr));

    write_file_private(data_path(".astro_pro.json"), dump_json(Json(std::move(root))) + "\n");
}

void pro_enqueue_call_locked(const std::string& reason, const std::string& urgency = "urgent", const std::string& project = "", const std::string& channel = "call", const std::string& phone = "") {
    ProContactEvent ev;
    ev.id = "call_" + random_hex(6);
    ev.reason = reason;
    ev.channel = channel.empty() ? "call" : channel;
    ev.recipient = phone.empty() ? (g_pro.emergency_phone.empty() ? "+12025550199" : g_pro.emergency_phone) : phone;
    ev.priority = urgency;
    ev.details = project;
    ev.status = "queued_for_android";
    ev.created = now_ms();
    g_pro.contacts.insert(g_pro.contacts.begin(), ev);
    if (g_pro.contacts.size() > 50) g_pro.contacts.resize(50);
    g_pro.pending_phone_calls.push_back(ev);
    if (g_pro.pending_phone_calls.size() > 20) g_pro.pending_phone_calls.erase(g_pro.pending_phone_calls.begin());
    pro_save_locked();
}

void pro_enqueue_call(const std::string& reason, const std::string& urgency = "urgent", const std::string& project = "") {
    std::lock_guard<std::mutex> lk(g_pro_mu);
    pro_load_locked();
    pro_enqueue_call_locked(reason, urgency, project);
}

void pro_load_locked() {
    if (g_pro.loaded) return;
    g_pro.loaded = true;
    g_pro.started_at = now_ms();
    g_pro.failover_checkpoint = g_pro.started_at;

    const auto txt = read_file_text(data_path(".astro_pro.json"));
    if (!txt) return;
    try {
        const Json root = JsonParser(*txt).parse();
        g_pro.enabled = json_bool(root, "enabled", true);
        g_pro.sentinel_active = json_bool(root, "sentinel_active", true);
        g_pro.auto_failover_enabled = json_bool(root, "auto_failover_enabled", true);
        g_pro.emergency_phone = get_string(root, "emergency_phone");

        if (const Json* c_arr = root.get("contacts"); c_arr && c_arr->array_ptr()) {
            for (const auto& it : *c_arr->array_ptr()) {
                ProContactEvent ev;
                ev.id = get_string(it, "id");
                ev.reason = get_string(it, "reason");
                ev.channel = get_string(it, "channel");
                ev.recipient = get_string(it, "recipient");
                ev.status = get_string(it, "status");
                ev.priority = get_string(it, "priority");
                ev.details = get_string(it, "details");
                if (const double* d = std::get_if<double>(&it.get("created")->value)) ev.created = static_cast<long long>(*d);
                if (!ev.id.empty()) g_pro.contacts.push_back(std::move(ev));
            }
        }

        if (const Json* w_arr = root.get("watch_events"); w_arr && w_arr->array_ptr()) {
            for (const auto& it : *w_arr->array_ptr()) {
                ProWatchEvent ev;
                ev.id = get_string(it, "id");
                ev.project = get_string(it, "project");
                ev.event_type = get_string(it, "event_type");
                ev.details = get_string(it, "details");
                ev.action_taken = get_string(it, "action_taken");
                if (const double* d = std::get_if<double>(&it.get("timestamp")->value)) ev.timestamp = static_cast<long long>(*d);
                if (!ev.id.empty()) g_pro.watch_events.push_back(std::move(ev));
            }
        }
    } catch (...) {}
}

bool handle_pro_api(int fd, const HttpRequest& request) {
    const std::string& path = request.path;
    if (path != "/api/pro" && path.rfind("/api/pro/", 0) != 0) return false;

    std::unique_lock<std::mutex> lk(g_pro_mu);
    pro_load_locked();

    // 1. Pro Status & Telemetry
    if (path == "/api/pro/status" && request.method == "GET") {
        Json::Object root;
        root["ok"] = Json(true);
        root["pro"] = Json(true);
        root["edition"] = Json(g_pro.edition);
        root["version"] = Json(g_pro.version);
        root["uptime_sec"] = Json(static_cast<double>((now_ms() - g_pro.started_at) / 1000));

        // Dual-Presence Telemetry
        Json::Object dp;
        dp["pc_online"] = Json(true);
        dp["server_mode"] = Json(std::string("hub_primary"));
        dp["failover_policy"] = Json(std::string("instant_zero_drop"));
        dp["failover_checkpoint"] = Json(static_cast<double>(g_pro.failover_checkpoint));
        dp["devices_connected"] = Json(static_cast<double>(std::max<std::size_t>(1, g_pro.devices.size())));
        dp["server_port"] = Json(4173.0);
        dp["phone_port"] = Json(4174.0);

        Json::Array dev_arr;
        const long long now = now_ms();
        for (const auto& dev : g_pro.devices) {
            if (now - dev.last_seen < 300000) { // seen in last 5 minutes
                Json::Object d;
                d["id"] = Json(dev.device_id);
                d["name"] = Json(dev.name);
                d["client_type"] = Json(dev.client_type);
                d["ping_ms"] = Json(dev.ping_ms);
                d["failover_ready"] = Json(dev.failover_ready);
                d["active"] = Json(now - dev.last_seen < 60000);
                dev_arr.emplace_back(std::move(d));
            }
        }
        dp["devices"] = Json(std::move(dev_arr));
        root["dual_presence"] = Json(std::move(dp));

        // Sentinel Watcher Status
        Json::Object watcher;
        watcher["active"] = Json(g_pro.sentinel_active);
        watcher["status"] = Json(std::string(g_pro.sentinel_active ? "sentinel_armed" : "sentinel_standby"));
        Json::Array targets;
        targets.emplace_back(Json(std::string("build_integrity")));
        targets.emplace_back(Json(std::string("test_runner")));
        targets.emplace_back(Json(std::string("agent_milestones")));
        targets.emplace_back(Json(std::string("permission_gate")));
        targets.emplace_back(Json(std::string("task_stall_detector")));
        watcher["monitored_targets"] = Json(std::move(targets));
        watcher["total_events"] = Json(static_cast<double>(g_pro.watch_events.size()));
        root["watcher"] = Json(std::move(watcher));

        // Swarm Agents
        Json::Array swarm;
        {
            Json::Object a; a["id"] = Json(std::string("architect")); a["name"] = Json(std::string("System Architect")); a["role"] = Json(std::string("Deep Reasoning & Plan Decomposition")); a["status"] = Json(std::string("active")); swarm.emplace_back(std::move(a));
        }
        {
            Json::Object a; a["id"] = Json(std::string("sentinel")); a["name"] = Json(std::string("Autonomous Sentinel")); a["role"] = Json(std::string("Continuous Watcher & Self-Healing")); a["status"] = Json(std::string("watching")); swarm.emplace_back(std::move(a));
        }
        {
            Json::Object a; a["id"] = Json(std::string("contact_gateway")); a["name"] = Json(std::string("Proactive Contact Gate")); a["role"] = Json(std::string("Emergency Calling & Multi-Channel Alerting")); a["status"] = Json(std::string("ready")); swarm.emplace_back(std::move(a));
        }
        {
            Json::Object a; a["id"] = Json(std::string("security")); a["name"] = Json(std::string("Security Auditor")); a["role"] = Json(std::string("OWASP & Code Hardening")); a["status"] = Json(std::string("ready")); swarm.emplace_back(std::move(a));
        }
        root["swarm"] = Json(std::move(swarm));

        // Contact Stats
        Json::Object contact_info;
        contact_info["total_dispatched"] = Json(static_cast<double>(g_pro.contacts.size()));
        contact_info["emergency_phone"] = Json(g_pro.emergency_phone.empty() ? "+1 (555) 019-2831" : g_pro.emergency_phone);
        root["contact_info"] = Json(std::move(contact_info));

        send_json(fd, 200, Json(std::move(root)));
        return true;
    }

    // 2. Proactive Contact Trigger
    if (path == "/api/pro/contact" && request.method == "POST") {
        const auto body = parse_request_json(request, fd);
        if (!body) return true;

        const std::string reason = get_string(*body, "reason", "Autonomous agent requires user attention");
        std::string channel = get_string(*body, "channel", "auto");
        const std::string priority = get_string(*body, "urgency", get_string(*body, "priority", "high"));
        const std::string details = get_string(*body, "details", "");
        std::string phone = get_string(*body, "phone", g_pro.emergency_phone);
        if (phone.empty()) phone = "+12025550199";

        if (channel == "auto") {
            channel = (priority == "urgent" || priority == "critical") ? "call" : "alert";
        }

        ProContactEvent ev;
        ev.id = "contact_" + random_hex(6);
        ev.reason = reason;
        ev.channel = channel;
        ev.recipient = phone;
        ev.priority = priority;
        ev.details = details;
        ev.status = "dispatched";
        ev.created = now_ms();

        g_pro.contacts.insert(g_pro.contacts.begin(), ev);
        if (g_pro.contacts.size() > 50) g_pro.contacts.resize(50);
        pro_save_locked();

        // Also trigger Bland call entry if channel is "call"
        if (channel == "call") {
            std::lock_guard lkb(g_bland_mu);
            bland_load_locked();
            BlandCallEntry c;
            c.id = "call_" + random_hex(6);
            c.phone = phone;
            c.task = reason;
            c.pathway = "proactive_contact";
            c.status = "ringing";
            c.created = now_ms();
            g_bland.calls.insert(g_bland.calls.begin(), c);
            if (g_bland.calls.size() > 50) g_bland.calls.resize(50);
            bland_save_locked();
        }

        pro_enqueue_call_locked(reason, priority, details, channel, phone);

        Json::Object res;
        res["ok"] = Json(true);
        res["contact_id"] = Json(ev.id);
        res["channel"] = Json(channel);
        res["priority"] = Json(priority);
        res["status"] = Json(std::string("queued_for_android"));
        res["recipient"] = Json(phone);
        res["message"] = Json(std::string("Proactive contact queued for Android execution: ") + channel + std::string(" to ") + phone);
        send_json(fd, 200, Json(std::move(res)));
        return true;
    }

    // 3. Dual-Presence Heartbeat & Failover Sync
    if (path == "/api/pro/heartbeat" && request.method == "POST") {
        const auto body = parse_request_json(request, fd);
        const std::string dev_id = body ? get_string(*body, "device_id", "phone_client") : "phone_client";
        const std::string dev_name = body ? get_string(*body, "name", "Android Phone") : "Android Phone";
        const std::string client_type = body ? get_string(*body, "client_type", "android") : "android";

        const long long now = now_ms();
        g_pro.failover_checkpoint = now;

        bool found = false;
        for (auto& dev : g_pro.devices) {
            if (dev.device_id == dev_id || dev.name == dev_name) {
                dev.last_seen = now;
                dev.failover_ready = true;
                found = true;
                break;
            }
        }
        if (!found) {
            ProDeviceSession dev;
            dev.device_id = dev_id;
            dev.name = dev_name;
            dev.client_type = client_type;
            dev.last_seen = now;
            dev.failover_ready = true;
            dev.ping_ms = 1.2;
            g_pro.devices.push_back(std::move(dev));
            if (g_pro.devices.size() > 10) g_pro.devices.erase(g_pro.devices.begin());
        }

        Json::Object res;
        res["ok"] = Json(true);
        res["server_time"] = Json(static_cast<double>(now));
        res["failover_checkpoint"] = Json(static_cast<double>(g_pro.failover_checkpoint));
        res["failover_ready"] = Json(true);
        res["server_status"] = Json(std::string("online"));

        // Dispatch pending calls to Android phone
        Json::Array calls_arr;
        for (const auto& c : g_pro.pending_phone_calls) {
            Json::Object item;
            item["id"] = Json(c.id);
            item["reason"] = Json(c.reason);
            item["channel"] = Json(c.channel);
            item["priority"] = Json(c.priority);
            item["details"] = Json(c.details);
            item["recipient"] = Json(c.recipient);
            item["created"] = Json(static_cast<double>(c.created));
            calls_arr.emplace_back(std::move(item));
        }
        res["pending_calls"] = Json(std::move(calls_arr));
        send_json(fd, 200, Json(std::move(res)));
        return true;
    }

    if (path == "/api/pro/call_ack" && request.method == "POST") {
        const auto body = parse_request_json(request, fd);
        const std::string id = body ? get_string(*body, "id") : "";
        if (!id.empty()) {
            std::erase_if(g_pro.pending_phone_calls, [&](const ProContactEvent& c) { return c.id == id; });
            for (auto& c : g_pro.contacts) {
                if (c.id == id) { c.status = "executed_by_android"; break; }
            }
            pro_save_locked();
        }
        send_json(fd, 200, Json(Json::Object{{"ok", Json(true)}}));
        return true;
    }

    // 4. Project Watcher Event / Trigger
    if (path == "/api/pro/watch" && request.method == "POST") {
        const auto body = parse_request_json(request, fd);
        if (!body) return true;

        const std::string project = get_string(*body, "project", "current_project");
        const std::string ev_type = get_string(*body, "event_type", "task_event");
        const std::string details = get_string(*body, "details", "");
        const bool auto_contact = json_bool(*body, "auto_contact", false);

        ProWatchEvent wev;
        wev.id = "watch_" + random_hex(6);
        wev.project = project;
        wev.event_type = ev_type;
        wev.details = details;
        wev.action_taken = auto_contact ? "contact_dispatched" : "logged";
        wev.timestamp = now_ms();

        g_pro.watch_events.insert(g_pro.watch_events.begin(), wev);
        if (g_pro.watch_events.size() > 50) g_pro.watch_events.resize(50);
        pro_save_locked();

        if (auto_contact) {
            ProContactEvent cev;
            cev.id = "contact_" + random_hex(6);
            cev.reason = "Project Watcher alert [" + ev_type + "] in " + project + ": " + details;
            cev.channel = "call";
            cev.recipient = g_pro.emergency_phone.empty() ? "+12025550199" : g_pro.emergency_phone;
            cev.priority = "urgent";
            cev.details = details;
            cev.status = "dispatched";
            cev.created = now_ms();
            g_pro.contacts.insert(g_pro.contacts.begin(), cev);
            pro_save_locked();
        }

        Json::Object res;
        res["ok"] = Json(true);
        res["event_id"] = Json(wev.id);
        res["action_taken"] = Json(wev.action_taken);
        send_json(fd, 200, Json(std::move(res)));
        return true;
    }

    // 5. Watcher Events List
    if (path == "/api/pro/events" && request.method == "GET") {
        Json::Array arr;
        for (const auto& w : g_pro.watch_events) {
            Json::Object item;
            item["id"] = Json(w.id);
            item["project"] = Json(w.project);
            item["event_type"] = Json(w.event_type);
            item["details"] = Json(w.details);
            item["action_taken"] = Json(w.action_taken);
            item["timestamp"] = Json(static_cast<double>(w.timestamp));
            arr.emplace_back(std::move(item));
        }
        Json::Object root;
        root["events"] = Json(std::move(arr));
        send_json(fd, 200, Json(std::move(root)));
        return true;
    }

    send_json(fd, 404, error_json("Unknown Pro API request."));
    return true;
}
