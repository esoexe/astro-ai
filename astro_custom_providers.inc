// ---------------------------------------------------------------------------------------------
// Custom Providers: dynamically adds, manages, tests, and routes custom LLM endpoints
// (Local Ollama, LM Studio, vLLM, DeepSeek, OpenAI-compatible proxies, custom API keys).
// ---------------------------------------------------------------------------------------------
#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

struct CustomProvider {
    std::string id;
    std::string name;
    std::string kind = "openai"; // "openai" or "ollama"
    std::string base_url;
    std::string api_key;
    std::string model;
    std::string vision_model;
    std::vector<std::string> fallback_models;
    bool enabled = true;
    int wait_seconds = 60;
    bool tools = true;
};

std::mutex g_custom_mu;
std::vector<CustomProvider> g_custom_providers;
bool g_custom_loaded = false;

void sync_custom_routes_locked();

void custom_providers_load_locked() {
    g_custom_providers.clear();
    const std::string path = data_path("astro_data/custom_providers.json");
    const auto text = read_file_text(path);
    if (!text || text->empty()) {
        g_custom_loaded = true;
        return;
    }
    try {
        Json j = JsonParser(*text).parse();
        if (auto* arr = j.array_ptr()) {
            for (const auto& item : *arr) {
                CustomProvider p;
                p.id = get_string(item, "id");
                p.name = get_string(item, "name");
                p.kind = get_string(item, "kind", "openai");
                p.base_url = get_string(item, "base_url");
                p.api_key = get_string(item, "api_key");
                p.model = get_string(item, "model");
                p.vision_model = get_string(item, "vision_model");
                p.fallback_models = json_strings(item.get("fallback_models"));
                p.enabled = json_bool(item, "enabled", true);
                p.wait_seconds = json_int(item, "wait_seconds", 60);
                p.tools = json_bool(item, "tools", true);
                if (!p.id.empty() && !p.base_url.empty() && !p.model.empty()) {
                    g_custom_providers.push_back(std::move(p));
                }
            }
        }
    } catch (...) {}
    g_custom_loaded = true;
    sync_custom_routes_locked();
}

void custom_providers_save_locked() {
    Json::Array arr;
    for (const auto& p : g_custom_providers) {
        Json::Object o;
        o["id"] = Json(p.id);
        o["name"] = Json(p.name);
        o["kind"] = Json(p.kind);
        o["base_url"] = Json(p.base_url);
        o["api_key"] = Json(p.api_key);
        o["model"] = Json(p.model);
        o["vision_model"] = Json(p.vision_model);
        Json::Array fbs;
        for (const auto& m : p.fallback_models) fbs.push_back(Json(m));
        o["fallback_models"] = Json(std::move(fbs));
        o["enabled"] = Json(p.enabled);
        o["wait_seconds"] = Json(static_cast<double>(p.wait_seconds));
        o["tools"] = Json(p.tools);
        arr.push_back(Json(std::move(o)));
    }
    const std::string dir = data_path("astro_data");
    std::error_code ec;
    fs::create_directories(dir, ec);
    write_file_private(data_path("astro_data/custom_providers.json"), dump_json(Json(std::move(arr))));
}

void sync_custom_routes_locked() {
    // Erase previously added custom routes and providers
    std::erase_if(g_routes, [](const Route& r) {
        return r.provider.rfind("custom_", 0) == 0;
    });
    std::erase_if(g_providers, [](const ProviderInfo& pi) {
        return pi.id.rfind("custom_", 0) == 0;
    });

    for (const auto& cp : g_custom_providers) {
        if (!cp.enabled) continue;
        Route r;
        r.id = "custom_" + cp.id + ":" + cp.model;
        r.kind = cp.kind;
        r.model = cp.model;
        r.provider = "custom_" + cp.id;
        r.provider_name = cp.name.empty() ? ("Custom (" + cp.model + ")") : cp.name;
        r.name = short_model_name(cp.model);
        r.label = r.name + " (" + (cp.name.empty() ? "Custom" : cp.name) + ")";
        r.base_url = cp.base_url;
        r.api_key = cp.api_key;
        r.shared_quota = false;
        r.in_auto = true;
        r.wait_seconds = cp.wait_seconds;
        r.paid = false;
        r.local = (cp.base_url.find("127.0.0.1") != std::string::npos || cp.base_url.find("localhost") != std::string::npos);
        r.vision = !cp.vision_model.empty();
        r.trains = false;
        g_routes.insert(g_routes.begin(), std::move(r));

        ProviderInfo pi;
        pi.id = "custom_" + cp.id;
        pi.name = cp.name.empty() ? ("Custom (" + cp.model + ")") : cp.name;
        pi.connected = true;
        pi.models = 1;
        pi.models_total = 1;
        pi.status = "Connected: " + cp.model;
        pi.keyless = cp.api_key.empty();
        pi.base_url_default = cp.base_url;
        pi.local = r.local;
        g_providers.push_back(std::move(pi));
    }
}

Json custom_provider_test(const std::string& base_url, const std::string& api_key, const std::string& model, const std::string& kind) {
    std::string endpoint = base_url;
    while (!endpoint.empty() && endpoint.back() == '/') endpoint.pop_back();
    if (endpoint.rfind("/chat/completions") == std::string::npos && kind != "ollama_raw") {
        endpoint += "/chat/completions";
    }

    std::vector<std::string> headers = {"Content-Type: application/json"};
    if (!api_key.empty()) headers.push_back("Authorization: Bearer " + api_key);

    Json::Object req;
    req["model"] = Json(model);
    req["max_tokens"] = Json(16.0);
    Json::Array msgs;
    Json::Object m; m["role"] = Json("user"); m["content"] = Json("Hi");
    msgs.push_back(Json(std::move(m)));
    req["messages"] = Json(std::move(msgs));
    std::string post = dump_json(Json(std::move(req)));

    const auto t0 = std::chrono::steady_clock::now();
    FetchResult f = http_fetch(endpoint, headers, &post, 20, 500'000, false);
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    if (f.status == 200) {
        std::string reply = "OK";
        try {
            Json res = JsonParser(f.body).parse();
            if (const Json* c = res.get("choices")) {
                if (auto* ca = c->array_ptr(); ca && !ca->empty()) {
                    if (const Json* msg = (*ca)[0].get("message")) {
                        if (const Json* cont = msg->get("content")) {
                            if (cont->string_ptr()) reply = *cont->string_ptr();
                        }
                    }
                }
            }
        } catch (...) {}
        Json::Object o;
        o["ok"] = Json(true);
        o["ms"] = Json(ms);
        o["model"] = Json(model);
        o["reply"] = Json(reply.substr(0, 100));
        return Json(std::move(o));
    }
    Json::Object o;
    o["ok"] = Json(false);
    o["error"] = Json(f.error.empty() ? ("HTTP " + std::to_string(f.status) + ": " + f.body.substr(0, 160)) : f.error);
    o["ms"] = Json(ms);
    return Json(std::move(o));
}

bool handle_custom_providers_api(int fd, const HttpRequest& request) {
    const std::string& path = request.path;
    if (path.rfind("/api/custom_providers", 0) != 0) return false;

    if (path == "/api/custom_providers" && request.method == "GET") {
        std::lock_guard<std::mutex> lock(g_custom_mu);
        if (!g_custom_loaded) custom_providers_load_locked();
        Json::Array list;
        for (const auto& p : g_custom_providers) {
            Json::Object o;
            o["id"] = Json(p.id);
            o["name"] = Json(p.name);
            o["kind"] = Json(p.kind);
            o["base_url"] = Json(p.base_url);
            o["model"] = Json(p.model);
            o["vision_model"] = Json(p.vision_model);
            o["enabled"] = Json(p.enabled);
            o["wait_seconds"] = Json(static_cast<double>(p.wait_seconds));
            o["tools"] = Json(p.tools);
            o["has_key"] = Json(!p.api_key.empty());
            o["api_key"] = Json(p.api_key.empty() ? std::string("") : (p.api_key.size() > 7 ? p.api_key.substr(0, 4) + "..." + p.api_key.substr(p.api_key.size() - 3) : "***"));
            list.push_back(Json(std::move(o)));
        }
        Json::Object resp;
        resp["ok"] = Json(true);
        resp["providers"] = Json(std::move(list));
        send_json(fd, 200, Json(std::move(resp)));
        return true;
    }

    if (path == "/api/custom_providers/test" && request.method == "POST") {
        const auto body_opt = parse_request_json(request, fd);
        if (!body_opt) return true;
        const Json& body = *body_opt;
        const std::string base_url = trim(get_string(body, "base_url"));
        std::string api_key = trim(get_string(body, "api_key"));
        const std::string model = trim(get_string(body, "model"));
        const std::string kind = trim(get_string(body, "kind", "openai"));

        if (api_key.find("...") != std::string::npos || api_key == "***") {
            const std::string id = trim(get_string(body, "id"));
            std::lock_guard<std::mutex> lock(g_custom_mu);
            for (const auto& p : g_custom_providers) {
                if (p.id == id) { api_key = p.api_key; break; }
            }
        }
        if (base_url.empty() || model.empty()) {
            send_json(fd, 400, error_json("Need both Base URL and Model name."));
            return true;
        }
        Json r = custom_provider_test(base_url, api_key, model, kind);
        send_json(fd, 200, r);
        return true;
    }

    if (path == "/api/custom_providers" && request.method == "POST") {
        const auto body_opt = parse_request_json(request, fd);
        if (!body_opt) return true;
        const Json& body = *body_opt;

        std::string id = trim(get_string(body, "id"));
        const std::string name = trim(get_string(body, "name"));
        const std::string kind = trim(get_string(body, "kind", "openai"));
        std::string base_url = trim(get_string(body, "base_url"));
        std::string api_key = trim(get_string(body, "api_key"));
        const std::string model = trim(get_string(body, "model"));
        const std::string vision_model = trim(get_string(body, "vision_model"));
        const bool enabled = json_bool(body, "enabled", true);
        const int wait_seconds = std::clamp(json_int(body, "wait_seconds", 60), 10, 600);
        const bool tools = json_bool(body, "tools", true);

        while (!base_url.empty() && base_url.back() == '/') base_url.pop_back();

        if (base_url.empty() || model.empty()) {
            send_json(fd, 400, error_json("Base URL and Model name cannot be empty."));
            return true;
        }

        std::lock_guard<std::mutex> lock(g_custom_mu);
        if (!g_custom_loaded) custom_providers_load_locked();

        if (id.empty()) {
            id = "c_" + random_hex(6);
        }

        bool found = false;
        for (auto& p : g_custom_providers) {
            if (p.id == id) {
                p.name = name.empty() ? ("Custom " + model) : name;
                p.kind = kind;
                p.base_url = base_url;
                if (!api_key.empty() && api_key.find("...") == std::string::npos && api_key != "***") {
                    p.api_key = api_key;
                }
                p.model = model;
                p.vision_model = vision_model;
                p.enabled = enabled;
                p.wait_seconds = wait_seconds;
                p.tools = tools;
                found = true;
                break;
            }
        }
        if (!found) {
            CustomProvider p;
            p.id = id;
            p.name = name.empty() ? ("Custom " + model) : name;
            p.kind = kind;
            p.base_url = base_url;
            p.api_key = api_key;
            p.model = model;
            p.vision_model = vision_model;
            p.enabled = enabled;
            p.wait_seconds = wait_seconds;
            p.tools = tools;
            g_custom_providers.push_back(std::move(p));
        }

        custom_providers_save_locked();
        sync_custom_routes_locked();

        Json::Object resp;
        resp["ok"] = Json(true);
        resp["id"] = Json(id);
        resp["models_count"] = Json(static_cast<double>(g_routes.size()));
        send_json(fd, 200, Json(std::move(resp)));
        return true;
    }

    if ((path == "/api/custom_providers" && request.method == "DELETE") ||
        (path == "/api/custom_providers/delete" && request.method == "POST")) {
        std::string id;
        std::string q = request.query;
        if (q.empty()) {
            if (auto qpos = request.path.find('?'); qpos != std::string::npos) q = request.path.substr(qpos + 1);
        }
        if (!q.empty()) {
            if (auto idpos = q.find("id="); idpos != std::string::npos) {
                const std::size_t start = idpos + 3;
                const std::size_t amp = q.find('&', start);
                id = trim(q.substr(start, amp == std::string::npos ? std::string::npos : amp - start));
            }
        }
        if (id.empty() && !request.body.empty()) {
            if (const auto body_opt = parse_request_json(request, fd)) {
                id = trim(get_string(*body_opt, "id"));
            }
        }
        if (id.empty()) {
            send_json(fd, 400, error_json("Need provider ID to delete."));
            return true;
        }

        std::lock_guard<std::mutex> lock(g_custom_mu);
        if (!g_custom_loaded) custom_providers_load_locked();

        std::erase_if(g_custom_providers, [&](const CustomProvider& p) { return p.id == id; });
        custom_providers_save_locked();
        sync_custom_routes_locked();

        Json::Object resp;
        resp["ok"] = Json(true);
        resp["deleted"] = Json(id);
        send_json(fd, 200, Json(std::move(resp)));
        return true;
    }

    return false;
}
