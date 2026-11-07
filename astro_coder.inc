// astro_coder.inc - Developer suite: HTTP/API Client scratchpad, Database inspector (SQLite), Multi-port previews
#pragma once

#if __has_include(<sqlite3.h>)
#include <sqlite3.h>
#define ASTRO_HAS_SQLITE 1
#else
#define ASTRO_HAS_SQLITE 0
#endif

struct HttpScratchpadReq {
    std::string method;
    std::string url;
    std::vector<std::string> headers;
    std::string body;
    long timeout_s = 30;
};

struct HttpScratchpadResp {
    int status = 0;
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
    long long duration_ms = 0;
    std::string error;
};

inline HttpScratchpadResp dev_exec_http(const HttpScratchpadReq& req) {
    HttpScratchpadResp resp;
    CURL* curl = curl_easy_init();
    if (!curl) {
        resp.error = "Failed to initialize HTTP client.";
        return resp;
    }
    const auto start = std::chrono::steady_clock::now();
    curl_slist* hlist = nullptr;
    for (const auto& h : req.headers) hlist = curl_slist_append(hlist, h.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, req.url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, req.method.c_str());
    if (hlist) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hlist);
    if (!req.body.empty() && (req.method == "POST" || req.method == "PUT" || req.method == "PATCH")) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, req.timeout_s);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);

    std::string out_body;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
        auto* str = static_cast<std::string*>(userdata);
        str->append(ptr, size * nmemb);
        return size * nmemb;
    });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out_body);

    CURLcode res = curl_easy_perform(curl);
    const auto end = std::chrono::steady_clock::now();
    resp.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    if (res == CURLE_OK) {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        resp.status = static_cast<int>(code);
        resp.body = std::move(out_body);
    } else {
        resp.error = curl_easy_strerror(res);
    }

    if (hlist) curl_slist_free_all(hlist);
    curl_easy_cleanup(curl);
    return resp;
}

inline void dev_handle_http_client(int fd, const Json& body) {
    HttpScratchpadReq req;
    req.method = get_string(body, "method", "GET");
    req.url = get_string(body, "url");
    if (req.url.empty()) {
        send_json(fd, 400, error_json("URL cannot be empty."));
        return;
    }
    req.body = get_string(body, "body");
    if (const Json* hs = body.get("headers"); hs && hs->object_ptr()) {
        for (const auto& [k, v] : *hs->object_ptr()) {
            if (v.string_ptr()) req.headers.push_back(k + ": " + *v.string_ptr());
        }
    }
    req.timeout_s = std::clamp(json_int(body, "timeout", 30), 1, 120);

    const auto resp = dev_exec_http(req);
    Json::Object o;
    o["status"] = Json(static_cast<double>(resp.status));
    o["body"] = Json(resp.body);
    o["duration_ms"] = Json(static_cast<double>(resp.duration_ms));
    if (!resp.error.empty()) o["error"] = Json(resp.error);
    send_json(fd, 200, Json(std::move(o)));
}

inline void dev_handle_db_query(int fd, const Json& body) {
    const std::string project = get_string(body, "project");
    const std::string db_file = get_string(body, "file", "app.db");
    const std::string sql = get_string(body, "sql");
    if (sql.empty()) {
        send_json(fd, 400, error_json("SQL query cannot be empty."));
        return;
    }

    std::error_code ec;
    fs::path base = project.empty() ? fs::current_path() : fs::path(projects_root()) / project;
    fs::path full_path = base / db_file;

#if ASTRO_HAS_SQLITE
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(full_path.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        std::string err = db ? sqlite3_errmsg(db) : "Failed to open SQLite database.";
        if (db) sqlite3_close(db);
        send_json(fd, 400, error_json(err));
        return;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* tail = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, &tail) != SQLITE_OK) {
        std::string err = sqlite3_errmsg(db);
        sqlite3_close(db);
        send_json(fd, 400, error_json(err));
        return;
    }

    Json::Array cols;
    const int col_count = sqlite3_column_count(stmt);
    for (int i = 0; i < col_count; ++i) {
        const char* name = sqlite3_column_name(stmt, i);
        cols.push_back(Json(name ? std::string(name) : ("col" + std::to_string(i))));
    }

    Json::Array rows;
    int row_limit = 500;
    while (sqlite3_step(stmt) == SQLITE_ROW && row_limit-- > 0) {
        Json::Array r;
        for (int i = 0; i < col_count; ++i) {
            const int type = sqlite3_column_type(stmt, i);
            if (type == SQLITE_INTEGER) {
                r.push_back(Json(static_cast<double>(sqlite3_column_int64(stmt, i))));
            } else if (type == SQLITE_FLOAT) {
                r.push_back(Json(sqlite3_column_double(stmt, i)));
            } else if (type == SQLITE_TEXT) {
                const auto* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
                r.push_back(Json(txt ? std::string(txt) : ""));
            } else if (type == SQLITE_NULL) {
                r.push_back(Json());
            } else {
                r.push_back(Json("[BLOB]"));
            }
        }
        rows.emplace_back(std::move(r));
    }

    const int changes = sqlite3_changes(db);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    Json::Object out;
    out["columns"] = Json(std::move(cols));
    out["rows"] = Json(std::move(rows));
    out["changes"] = Json(static_cast<double>(changes));
    out["ok"] = Json(true);
    send_json(fd, 200, Json(std::move(out)));
#else
    Json::Object out;
    out["error"] = Json("SQLite inspect is only available on native Linux/macOS builds.");
    send_json(fd, 400, Json(std::move(out)));
#endif
}

inline void dev_handle_db_tables(int fd, const Json& body) {
    const std::string project = get_string(body, "project");
    const std::string db_file = get_string(body, "file", "app.db");

    fs::path base = project.empty() ? fs::current_path() : fs::path(projects_root()) / project;
    fs::path full_path = base / db_file;

#if ASTRO_HAS_SQLITE
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(full_path.string().c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        Json::Object empty;
        empty["tables"] = Json(Json::Array{});
        send_json(fd, 200, Json(std::move(empty)));
        return;
    }

    const char* sql = "SELECT name, sql FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%';";
    sqlite3_stmt* stmt = nullptr;
    Json::Array tables;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Json::Object t;
            const char* n = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            const char* s = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            t["name"] = Json(n ? std::string(n) : "");
            t["sql"] = Json(s ? std::string(s) : "");
            tables.emplace_back(std::move(t));
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);

    Json::Object out;
    out["tables"] = Json(std::move(tables));
    send_json(fd, 200, Json(std::move(out)));
#else
    Json::Object empty;
    empty["tables"] = Json(Json::Array{});
    send_json(fd, 200, Json(std::move(empty)));
#endif
}

inline void dev_handle_port_scan(int fd) {
    Json::Array ports;
    std::ifstream net_tcp("/proc/net/tcp");
    if (net_tcp.is_open()) {
        std::string line;
        std::getline(net_tcp, line); // header
        while (std::getline(net_tcp, line)) {
            std::istringstream iss(line);
            std::string sl, local_address, rem_address, st;
            if (iss >> sl >> local_address >> rem_address >> st) {
                if (st == "0A") { // TCP_LISTEN
                    const auto colon = local_address.find(':');
                    if (colon != std::string::npos) {
                        const std::string port_hex = local_address.substr(colon + 1);
                        int port = 0;
                        try { port = std::stoi(port_hex, nullptr, 16); } catch (...) {}
                        if (port > 0 && port < 65536) {
                            Json::Object p;
                            p["port"] = Json(static_cast<double>(port));
                            ports.emplace_back(std::move(p));
                        }
                    }
                }
            }
        }
    }
    Json::Object out;
    out["ports"] = Json(std::move(ports));
    send_json(fd, 200, Json(std::move(out)));
}
