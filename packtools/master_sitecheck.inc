// master_sitecheck.inc - master-build/sitecheck: crawl a local folder or a live site (same origin) and report what is
// broken. A port of sitecheck.py (master-skills, MIT): the Crawler (breadth-first pages, then links, assets, anchors
// and the favicon, with a pool of --workers threads), the text and JSON reports, and folder / .html / file:// /
// bare-domain targets (folders are served in-process by master-build/serve, see master_serve.inc).
// Fetching is in master_sitecheck_net.inc, the per-page analysis in master_sitecheck_page.inc.
namespace packtools::master_sitecheck {

struct StartError { std::string text; };

struct Options {
    long long depth = 3, limit = 100, max_image_kb = 500, workers = 8;
    bool external = false, insecure = false, strict = false;
    double timeout = 10.0;
    std::vector<std::string> exclude;
    std::set<std::string> ignore;
};

template <class T> struct Ordered {   // an OrderedDict keyed by string
    std::vector<std::string> keys;
    std::map<std::string, T> map;
    T* get(const std::string& k) { const auto it = map.find(k); return it == map.end() ? nullptr : &it->second; }
    const T* get(const std::string& k) const { const auto it = map.find(k); return it == map.end() ? nullptr : &it->second; }
    T& setdefault(const std::string& k, T dflt = T()) {
        auto it = map.find(k);
        if (it == map.end()) { keys.push_back(k); it = map.emplace(k, std::move(dflt)).first; }
        return it->second;
    }
    std::size_t size() const { return keys.size(); }
};

struct PageInfo { std::string url; long status = 0; long depth = 0; std::string title; std::shared_ptr<PageAnalysis> analysis; };
struct PlaceRef { std::string page; long line; std::optional<std::size_t> img_index; };
struct AssetEntry { std::string kind; std::vector<PlaceRef> refs; };
struct AnchorRef { std::string target; std::optional<std::string> frag; std::string page; long line; };
using ResultP = std::shared_ptr<Result>;

// ThreadPoolExecutor.map: results in input order
template <class In, class F>
std::vector<Result> parallel(long long workers_opt, const std::vector<In>& items, F func) {
    std::vector<Result> out(items.size());
    if (items.empty()) return out;
    const std::size_t workers = static_cast<std::size_t>(std::max<long long>(1, std::min<long long>(workers_opt, static_cast<long long>(items.size()))));
    std::atomic<std::size_t> next{0};
    auto work = [&] {
        for (;;) {
            const std::size_t k = next.fetch_add(1);
            if (k >= items.size()) return;
            out[k] = func(items[k]);
        }
    };
    std::vector<std::thread> pool;
    for (std::size_t w = 0; w < workers; ++w) pool.emplace_back(work);
    for (auto& t : pool) t.join();
    return out;
}

class Crawler {
public:
    explicit Crawler(const Options& opts) : opts_(opts), fetcher_(opts.timeout, opts.insecure) {}

    Val run(const std::string& start_url) {
        const auto t0 = std::chrono::steady_clock::now();
        const auto start_n = normalize(start_url);
        if (!start_n) throw StartError{"not a valid http(s) URL: " + start_url};
        const std::string start = *start_n;
        auto first = std::make_shared<Result>(fetcher_.fetch(start, "GET", PAGE_CAP));
        if (!first->ok()) throw StartError{"could not load " + start_url + " (" + first->describe() + ")"};
        if (!is_html(*first)) throw StartError{start_url + " is not an HTML page (content-type: " + (first->content_type.empty() ? std::string("unknown") : first->content_type) + ")"};
        const std::string final_url = normalize(first->final_url.empty() ? start : first->final_url).value_or(start);
        origins_ = {origin_of(start), origin_of(final_url)};
        home_ = final_url;
        site_is_local_ = is_local_host(up::urlsplit(final_url).hostname());

        std::set<std::string> seen = {start, final_url};
        std::vector<std::pair<std::string, ResultP>> level = {{start, first}};
        long depth = 0;
        while (!level.empty()) {
            std::vector<std::string> next_urls;
            for (const auto& [url, res] : level) process_page(url, res, depth, next_urls, seen);
            depth += 1;
            const long long room = opts_.limit - static_cast<long long>(pages_.size());
            if (depth > opts_.depth || room <= 0 || next_urls.empty()) break;
            std::vector<std::string> batch(next_urls.begin(), next_urls.begin() + static_cast<std::ptrdiff_t>(std::min<long long>(room, static_cast<long long>(next_urls.size()))));
            auto fetched = parallel(opts_.workers, batch, [this](const std::string& u) { return fetcher_.fetch(u, "GET", PAGE_CAP); });
            level.clear();
            for (std::size_t k = 0; k < batch.size(); ++k) level.emplace_back(batch[k], std::make_shared<Result>(std::move(fetched[k])));
        }
        check_links();
        check_assets();
        check_anchors();
        check_favicon();
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return report(start_url, secs);
    }

private:
    Options opts_;
    Fetcher fetcher_;
    std::map<std::string, ResultP> results_;
    Ordered<PageInfo> pages_;
    std::map<std::string, std::string> alias_;
    Ordered<std::vector<PlaceRef>> link_refs_;
    std::vector<AnchorRef> anchor_refs_;
    Ordered<AssetEntry> asset_refs_;
    std::vector<Finding> findings_;
    std::set<std::string> origins_, external_skipped_;
    long excluded_ = 0;
    std::string home_;
    bool site_is_local_ = true;

    bool internal(const std::string& url) const { return origins_.count(origin_of(url)) > 0; }
    bool excluded_url(const std::string& url) const {
        for (const auto& x : opts_.exclude)
            if (re::compile(x).search(url).ok) return true;
        return false;
    }

    void process_page(const std::string& url, const ResultP& res, long depth, std::vector<std::string>& next_urls, std::set<std::string>& seen) {
        results_[url] = res;
        if (!res->ok()) return;
        const std::string final_url = normalize(res->final_url.empty() ? url : res->final_url).value_or(url);
        alias_[url] = final_url;
        results_.emplace(final_url, res);
        if (!internal(final_url) || pages_.get(final_url) || !is_html(*res)) return;
        auto analysis = std::make_shared<PageAnalysis>(analyze(decode_html(res->body, res->charset), final_url, res->charset, res->headers,
                                                               final_url == home_, site_is_local_));
        res->body.clear();   // free memory; the analysis holds what we need
        pages_.setdefault(final_url, PageInfo{final_url, res->status, depth, analysis->title, analysis});
        findings_.insert(findings_.end(), analysis->findings.begin(), analysis->findings.end());
        for (const auto& l : analysis->links) {
            if (excluded_url(l.target)) { excluded_ += 1; continue; }
            link_refs_.setdefault(l.target).push_back({final_url, l.line, std::nullopt});
            if (l.frag) anchor_refs_.push_back({l.target, l.frag, final_url, l.line});
            if (internal(l.target) && looks_like_page(l.target) && !seen.count(l.target)) {
                seen.insert(l.target);
                next_urls.push_back(l.target);
            }
        }
        for (const auto& a : analysis->assets) add_asset(a.target, a.kind, final_url, a.line, a.img_index);
    }

    void add_asset(const std::string& target, const std::string& kind, const std::string& page, long line, std::optional<std::size_t> img_index = std::nullopt) {
        if (excluded_url(target)) { excluded_ += 1; return; }
        AssetEntry* entry = asset_refs_.get(target);
        if (!entry) entry = &asset_refs_.setdefault(target, AssetEntry{kind, {}});
        else if ((kind == "img" || kind == "css") && entry->kind != "img" && entry->kind != "css") entry->kind = kind;   // these kinds need the body, so they win
        entry->refs.push_back({page, line, img_index});
    }

    struct Group { std::string page; long line; long also; };
    static Group group(const std::vector<PlaceRef>& refs) {
        std::vector<std::string> pages;
        for (const auto& r : refs)
            if (std::find(pages.begin(), pages.end(), r.page) == pages.end()) pages.push_back(r.page);
        return {refs[0].page, refs[0].line, std::max(0L, static_cast<long>(pages.size()) - 1)};
    }

    static std::optional<std::string> classify(const Result& res, bool is_internal, const std::string& cat) {
        if (res.ok()) return std::nullopt;
        if (is_internal) return std::string(cat == "link" ? "broken-link" : "broken-asset");
        if (res.status == 404 || res.status == 410 || res.dns_fail) return std::string(cat == "link" ? "external-link" : "external-asset");
        return std::string("external-unverified");
    }

    void check_links() {
        std::vector<std::string> todo;
        for (const auto& target : link_refs_.keys) {
            if (results_.count(target)) continue;
            if (internal(target) || opts_.external) todo.push_back(target);
            else external_skipped_.insert(target);
        }
        auto fetched = parallel(opts_.workers, todo, [this](const std::string& u) { return fetcher_.check(u, "page"); });
        for (std::size_t k = 0; k < todo.size(); ++k) results_[todo[k]] = std::make_shared<Result>(std::move(fetched[k]));
        for (const auto& target : link_refs_.keys) {
            const auto it = results_.find(target);
            if (it == results_.end()) continue;
            const auto rule = classify(*it->second, internal(target), "link");
            if (!rule) continue;
            const Group g = group(*link_refs_.get(target));
            findings_.push_back({*rule, g.page, target, g.line, it->second->describe() + ", linked from " + display(g.page, origins_) + " line " + std::to_string(g.line), g.also});
        }
    }

    void check_assets() {
        std::set<std::string> checked;
        for (int round = 0; round < 4; ++round) {   // extra rounds pick up fonts/images found inside CSS
            std::vector<std::pair<std::string, std::string>> todo;
            const std::vector<std::string> keys = asset_refs_.keys;
            for (const auto& target : keys) {
                if (checked.count(target)) continue;
                checked.insert(target);
                if (internal(target) || opts_.external) todo.emplace_back(target, asset_refs_.get(target)->kind);
                else external_skipped_.insert(target);
            }
            if (todo.empty()) break;
            auto fetched = parallel(opts_.workers, todo, [this](const std::pair<std::string, std::string>& tk) { return fetcher_.check(tk.first, tk.second); });
            for (std::size_t k = 0; k < todo.size(); ++k) judge_asset(todo[k].first, fetched[k]);
        }
    }

    void judge_asset(const std::string& target, Result& res) {
        const AssetEntry entry = *asset_refs_.get(target);
        const Group g = group(entry.refs);
        const std::string where = g.line ? display(g.page, origins_) + " line " + std::to_string(g.line) : display(g.page, origins_);
        if (const auto rule = classify(res, internal(target), "asset")) {
            findings_.push_back({*rule, g.page, target, g.line, res.describe() + ", used on " + where, g.also});
            res.body.clear();
            return;
        }
        if (entry.kind == "css" && internal(target) && !res.body.empty()) {
            const std::string css = decode_utf8_replace(res.body);
            const std::string base = normalize(res.final_url.empty() ? target : res.final_url).value_or(target);
            for (const auto& cu : css_urls(css)) {
                const Resolved r = resolve(cu.url, base);
                if (r.kind != "http") continue;
                const long line_no = cu.newlines + 1;
                if (base.rfind("https:", 0) == 0 && r.url.rfind("http:", 0) == 0)
                    findings_.push_back({"mixed-content", target, r.url, line_no, "stylesheet loads it over http at line " + std::to_string(line_no), 0});
                add_asset(r.url, cu.kind, target, line_no);
            }
        }
        if (entry.kind == "img") judge_image(target, res, entry.refs, g, where);
        res.body.clear();
    }

    void judge_image(const std::string& target, const Result& res, const std::vector<PlaceRef>& refs, const Group& g, const std::string& where) {
        const auto dims = res.body.empty() ? std::nullopt : image_size(res.body.substr(0, 262144));
        const std::string dim_text = dims ? " (" + std::to_string(dims->first) + "x" + std::to_string(dims->second) + ")" : "";
        if (res.size && *res.size > opts_.max_image_kb * 1024)
            findings_.push_back({"image-heavy", g.page, target, g.line,
                                 human_bytes(res.size) + dim_text + ", over the " + std::to_string(opts_.max_image_kb) + " KB budget, used on " + where, g.also});
        if (!dims) return;
        for (const auto& r : refs) {
            if (!r.img_index) continue;
            const PageInfo* info = pages_.get(r.page);
            if (!info || !info->analysis || *r.img_index >= info->analysis->imgs.size()) continue;
            const ImgInfo& img = info->analysis->imgs[*r.img_index];
            if (!img.srcset.value_or("").empty()) continue;
            const std::string wpat = "^\\s*(\\d+)";
            const auto m = re::match(wpat, img.width.value_or(""));
            if (!m.ok) continue;
            const auto shown_v = py_int(m.group(1));
            if (!shown_v || *shown_v <= 0) continue;
            const long long shown = *shown_v;
            if (dims->first >= 800 && dims->first >= 3 * shown) {
                findings_.push_back({"image-oversized", r.page, target, r.line,
                                     std::to_string(dims->first) + "x" + std::to_string(dims->second) + " pixels shown at width=" + std::to_string(shown) + " on " +
                                         display(r.page, origins_) + " line " + std::to_string(r.line) + " (about " + std::to_string(dims->first / shown) + "x too large)",
                                     0});
                break;
            }
        }
    }

    void check_anchors() {
        Ordered<std::vector<PlaceRef>> missing;
        std::map<std::string, std::pair<std::string, std::string>> key_parts;
        for (const auto& a : anchor_refs_) {
            if (!a.frag) continue;
            const std::string& frag = *a.frag;
            if (frag.empty() || frag == "top" || frag.rfind(":~:", 0) == 0 || frag.rfind("!", 0) == 0 || frag.rfind("/", 0) == 0) continue;
            const auto al = alias_.find(a.target);
            const std::string dest = al == alias_.end() ? a.target : al->second;
            const PageInfo* info = pages_.get(dest);
            if (!info) continue;
            const std::string wanted = up::unquote(frag);
            const auto& targets = info->analysis->targets;
            if (targets.count(wanted) || targets.count(frag)) continue;
            const std::string key = dest + std::string(1, '\0') + wanted;
            key_parts[key] = {dest, wanted};
            missing.setdefault(key).push_back({a.page, a.line, std::nullopt});
        }
        for (const auto& key : missing.keys) {
            const auto& [dest, wanted] = key_parts[key];
            const Group g = group(*missing.get(key));
            findings_.push_back({"broken-anchor", g.page, dest + "#" + wanted, g.line,
                                 "no id=\"" + wanted + "\" on " + display(dest, origins_) + ", linked from " + display(g.page, origins_) + " line " + std::to_string(g.line), g.also});
        }
    }

    void check_favicon() {
        const PageInfo* home = pages_.get(home_);
        if (!home || home->analysis->icon_links) return;
        const Result res = fetcher_.check(origin_of(home_) + "/favicon.ico", "img");
        if (!res.ok()) findings_.push_back({"missing-favicon", home_, home_, 0, "no <link rel=\"icon\"> and /favicon.ico is " + res.describe(), 0});
    }

    Val report(const std::string& target, double seconds) {
        std::map<std::string, long> page_order;
        for (std::size_t k = 0; k < pages_.keys.size(); ++k) page_order[pages_.keys[k]] = static_cast<long>(k);
        std::vector<Finding> findings;
        for (const auto& f : findings_)
            if (!opts_.ignore.count(f.rule)) findings.push_back(f);
        auto pord = [&](const std::string& p) { const auto it = page_order.find(p); return it == page_order.end() ? 1000000L : it->second; };
        std::stable_sort(findings.begin(), findings.end(), [&](const Finding& a, const Finding& b) {
            const int ra = rule_index(a.rule), rb = rule_index(b.rule);
            if (ra != rb) return ra < rb;
            const long pa = pord(a.page), pb = pord(b.page);
            if (pa != pb) return pa < pb;
            if (a.line != b.line) return a.line < b.line;
            return a.url < b.url;
        });
        long errors = 0;
        for (const auto& f : findings)
            if (f.severity() == "error") errors++;
        const long warnings = static_cast<long>(findings.size()) - errors;
        const bool failed = errors > 0 || (opts_.strict && warnings > 0);

        Val r = Val::dict();
        auto& d = *r.d;
        d.set("tool", Val::str("sitecheck"));
        d.set("version", Val::str(VERSION));
        d.set("target", Val::str(target));
        d.set("start_url", Val::str(home_));
        List origins;
        for (const auto& o : origins_) origins.push_back(Val::str(o));
        d.set("origins", Val::list(origins));
        Val o = Val::dict();
        o.d->set("depth", Val::integer(opts_.depth));
        o.d->set("limit", Val::integer(opts_.limit));
        o.d->set("external", Val::boolean(opts_.external));
        o.d->set("max_image_kb", Val::integer(opts_.max_image_kb));
        o.d->set("strict", Val::boolean(opts_.strict));
        List ign;
        for (const auto& i : opts_.ignore) ign.push_back(Val::str(i));
        o.d->set("ignore", Val::list(ign));
        d.set("options", o);
        Val st = Val::dict();
        st.d->set("pages", Val::integer(static_cast<long long>(pages_.size())));
        st.d->set("links", Val::integer(static_cast<long long>(link_refs_.size())));
        st.d->set("assets", Val::integer(static_cast<long long>(asset_refs_.size())));
        st.d->set("external_skipped", Val::integer(static_cast<long long>(external_skipped_.size())));
        st.d->set("excluded", Val::integer(excluded_));
        st.d->set("requests", Val::integer(fetcher_.count));
        st.d->set("seconds", Val::flt(std::round(seconds * 100.0) / 100.0));
        d.set("stats", st);
        Val sm = Val::dict();
        sm.d->set("errors", Val::integer(errors));
        sm.d->set("warnings", Val::integer(warnings));
        sm.d->set("result", Val::str(failed ? "fail" : "pass"));
        d.set("summary", sm);
        List fl;
        for (const auto& f : findings) fl.push_back(f.to_dict());
        d.set("findings", Val::list(fl));
        List pl;
        for (const auto& k : pages_.keys) {
            const PageInfo& p = *pages_.get(k);
            Val pv = Val::dict();
            pv.d->set("url", Val::str(p.url));
            pv.d->set("status", Val::integer(p.status));
            pv.d->set("depth", Val::integer(p.depth));
            pv.d->set("title", Val::str(p.title));
            pl.push_back(pv);
        }
        d.set("pages", Val::list(pl));
        return r;
    }
};

// ---- output ---------------------------------------------------------------------------------------------------------
inline std::string plural(long long n, const char* s = "s") { return n == 1 ? "" : s; }
inline std::string format_text(const Val& report) {
    std::set<std::string> origins;
    for (const auto& o : *get(report, "origins").l) origins.insert(o.s);
    const Val st = get(report, "stats"), opts = get(report, "options");
    std::vector<std::string> lines;
    lines.push_back("sitecheck " + get(report, "version").s + "  " + get(report, "start_url").s);
    char secs[64];
    std::snprintf(secs, sizeof secs, "%.1f", get(st, "seconds").f);
    const long long pages = get(st, "pages").i;
    lines.push_back("Checked " + std::to_string(pages) + " page" + plural(pages) + " (depth " + std::to_string(get(opts, "depth").i) + ", limit " +
                    std::to_string(get(opts, "limit").i) + "), " + std::to_string(get(st, "links").i) + " links and " + std::to_string(get(st, "assets").i) +
                    " assets in " + secs + "s.");
    const long long skipped = get(st, "external_skipped").i;
    if (skipped) lines.push_back("Not checked: " + std::to_string(skipped) + " external URL" + plural(skipped) + " (add --external to include them).");
    for (const auto& [severity, heading] : std::vector<std::pair<std::string, std::string>>{{"error", "ERRORS"}, {"warn", "WARNINGS"}}) {
        std::vector<Val> grp;
        for (const auto& f : *get(report, "findings").l)
            if (get(f, "severity").s == severity) grp.push_back(f);
        if (grp.empty()) continue;
        lines.push_back("");
        lines.push_back(heading + " (" + std::to_string(grp.size()) + ")");
        std::string current;
        for (const auto& f : grp) {
            const std::string rule = get(f, "rule").s;
            if (rule != current) {
                if (!current.empty()) lines.push_back(std::string("    fix: ") + rule_of(current).fix);
                current = rule;
                lines.push_back("  " + current + ": " + rule_of(current).what);
            }
            const std::string url = get(f, "url").s;
            const std::string target = display(url.empty() ? get(f, "page").s : url, origins);
            const long long also = get(f, "also_on").i;
            const std::string more = also ? " (+" + std::to_string(also) + " more page" + plural(also) + ")" : "";
            lines.push_back("    " + target + "  " + get(f, "detail").s + more);
        }
        lines.push_back(std::string("    fix: ") + rule_of(current).fix);
    }
    const Val s = get(report, "summary");
    const long long errors = get(s, "errors").i, warnings = get(s, "warnings").i;
    lines.push_back("");
    if (get(s, "result").s == "pass") {
        const std::string tail = warnings ? " Review the warnings before you ship." : "";
        lines.push_back("PASS: " + std::to_string(errors) + " errors, " + std::to_string(warnings) + " warnings." + tail);
    } else {
        const std::string why = errors ? "Fix every error" : "Fix the warnings (--strict)";
        lines.push_back("FAIL: " + std::to_string(errors) + " errors, " + std::to_string(warnings) + " warnings. " + why + ", then run sitecheck again.");
    }
    return join(lines, "\n");
}

inline std::string list_rules() {
    std::size_t width = 0;
    for (const auto& r : rules()) width = std::max(width, std::strlen(r.id));
    std::vector<std::string> out = {"Rules (error = fails the check, warn = review before shipping):"};
    auto pad = [](const std::string& s, std::size_t w) { return s.size() >= w ? s : s + std::string(w - s.size(), ' '); };
    for (const auto& r : rules()) {
        out.push_back("  " + pad(r.id, width) + "  " + pad(r.severity, 5) + "  " + r.what);
        out.push_back("  " + std::string(width, ' ') + "         fix: " + r.fix);
    }
    return join(out, "\n");
}

// ---- entry points ---------------------------------------------------------------------------------------------------
inline std::string expanduser(const std::string& p) {   // os.path.expanduser for "~" and "~/..."
    if (p.empty() || p[0] != '~' || (p.size() > 1 && p[1] != '/' && p[1] != '\\')) return p;
#if defined(_WIN32)
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    if (!home) return p;
    std::string h = home;
    while (h.size() > 1 && (h.back() == '/' || h.back() == '\\')) h.pop_back();
    return h + p.substr(1);
}
inline std::string dirname_of(const std::string& p) {
    const auto k = p.find_last_of("/\\");
    if (k == std::string::npos) return "";
    std::string d = p.substr(0, k);
    return d.empty() ? std::string(1, p[0]) : d;
}
inline std::string basename_of(const std::string& p) {
    const auto k = p.find_last_of("/\\");
    return k == std::string::npos ? p : p.substr(k + 1);
}

// check a URL, a folder or an .html file; throws StartError when it cannot run
inline Val check_site(const std::string& target, const Options& opts, bool spa, bool clean_urls) {
    const std::string http_pat = "^https?://";
    if (re::match(http_pat, target, re::I).ok) return Crawler(opts).run(target);
    std::string path = lower(target).rfind("file://", 0) == 0 ? cpslice(target, 7) : target;
    path = sv::abspath(expanduser(up::unquote(path)));
    if (!exists(path)) {
        const std::string domain_pat = "^[\\w.-]+\\.[a-z]{2,}(:\\d+)?(/.*)?$";
        if (re::match(domain_pat, target, re::I).ok) return Crawler(opts).run("https://" + target);
        throw StartError{"no such file, folder or URL: " + target};
    }
    std::string root = path, start_path = "/";
    if (is_file(path)) { root = dirname_of(path); start_path = "/" + basename_of(path); }
    const auto server = master_serve::start(root, spa, clean_urls, true);
    try {
        std::string base = server->base;
        while (!base.empty() && base.back() == '/') base.pop_back();
        Val report = Crawler(opts).run(base + up::quote(start_path));
        report.d->set("served_from", Val::str(root));
        master_serve::stop(server);
        return report;
    } catch (...) {
        master_serve::stop(server);
        throw;
    }
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("sitecheck",
             "Crawl a local folder or a live site (same origin) and report broken links and assets, missing page basics, accessibility basics, "
             "mixed content, duplicate ids and heavy images.");
    p.epilog = "Examples: sitecheck site/ | sitecheck https://example.com --external | sitecheck --list-rules";
    p.pos("target").nargs("?").help("URL, folder, or .html file (folders are served for you)");
    p.opt({"--depth"}).integer().def(3).help("link hops from the start page to crawl (default 3)");
    p.opt({"--limit"}).integer().def(100).help("maximum pages to crawl (default 100)");
    p.opt({"--external"}).store_true().help("also check links and assets on other sites");
    p.opt({"--max-image-kb"}).integer().def(500).help("image weight budget in KB (default 500)");
    p.opt({"--timeout"}).number().def(Val::flt(10.0)).help("seconds per request (default 10)");
    p.opt({"--workers"}).integer().def(8).help("parallel requests (default 8)");
    p.opt({"--insecure"}).store_true().help("accept self-signed TLS certificates");
    p.opt({"--exclude"}).append().def(Val::list()).metavar("REGEX").help("skip URLs matching this regex (repeatable)");
    p.opt({"--ignore"}).append().def(Val::list()).metavar("RULE").help("drop findings for a rule, e.g. --ignore missing-og-image (repeatable)");
    p.opt({"--strict"}).store_true().help("warnings also fail (exit 1)");
    p.opt({"--spa"}).store_true().help("folder mode: unknown routes serve index.html");
    p.opt({"--clean-urls"}).store_true().help("folder mode: /about serves about.html");
    p.opt({"--json"}).store_true().help("print the JSON report instead of text");
    p.opt({"--report"}).metavar("FILE").help("also write the JSON report to FILE");
    p.opt({"--list-rules"}).store_true().help("list every rule with its fix and exit");
    p.opt({"--version"}).version(std::string("sitecheck ") + VERSION);
    const Val a = parse_args(p, argv);

    if (B(a, "list_rules")) {
        const std::string t = list_rules() + "\n";
        std::fwrite(t.data(), 1, t.size(), stdout);
        return 0;
    }
    if (S(a, "target").empty()) {
        const std::string u = ap::usage(ap::norm(p, p.prog));
        std::fwrite(u.data(), 1, u.size(), stderr);
        eprint("sitecheck: give a URL, a folder or an .html file (or --list-rules)");
        return 2;
    }
    std::set<std::string> ignore;
    for (const auto& item : *get(a, "ignore").l)
        for (const auto& r : split(item.s, ","))
            if (!strip(r).empty()) ignore.insert(strip(r));
    std::vector<std::string> unknown;
    for (const auto& r : ignore)
        if (rule_index(r) < 0) unknown.push_back(r);
    if (!unknown.empty()) {
        eprint("sitecheck: unknown rule(s): " + join(unknown, ", ") + " (see --list-rules)");
        return 2;
    }
    std::vector<std::string> excludes;
    try {
        for (const auto& x : *get(a, "exclude").l) {
            re::compile(x.s);
            excludes.push_back(x.s);
        }
    } catch (const PyErr& e) {
        eprint(std::string("sitecheck: bad --exclude regex: ") + e.what());
        return 2;
    }
    Options opts;
    opts.depth = get(a, "depth").i; opts.limit = get(a, "limit").i; opts.workers = get(a, "workers").i;
    if (opts.depth < 0 || opts.limit < 1 || opts.workers < 1) {
        eprint("sitecheck: --depth must be >= 0, --limit and --workers >= 1");
        return 2;
    }
    opts.external = B(a, "external"); opts.max_image_kb = get(a, "max_image_kb").i; opts.timeout = get(a, "timeout").num();
    opts.insecure = B(a, "insecure"); opts.exclude = excludes; opts.ignore = ignore; opts.strict = B(a, "strict");
#if defined(_WIN32)
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    Val report;
    try {
        report = check_site(S(a, "target"), opts, B(a, "spa"), B(a, "clean_urls"));
    } catch (const StartError& e) {
        eprint("sitecheck: " + e.text);
        return 2;
    }
    DumpOpts dj; dj.indent = 2;
    if (!S(a, "report").empty()) {
        const std::string rp = S(a, "report");
        makedirs(dirname_of(sv::abspath(rp)));
        write_text(rp, dumps(report, dj));
    }
    std::string out = B(a, "json") ? dumps(report, dj) : format_text(report);
    out += "\n";
    std::fwrite(out.data(), 1, out.size(), stdout);
    std::fflush(stdout);
    return get(get(report, "summary"), "result").s == "fail" ? 1 : 0;
}
PACKTOOL("master-build/sitecheck", main);

}  // namespace packtools::master_sitecheck
