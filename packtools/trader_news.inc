// trader-news/news - headlines (Yahoo Finance RSS) and SEC filings (EDGAR Atom) for the stocks you hold or plan to
// copy; prints only what is new since the last check, newest first, flagging what touches your positions.
// C++ port of trader-skill/trader-news/news.py (Jake Schincariol, MIT); same sources, state file, output and exit
// codes (0 = something new, 1 = nothing new, 2 = no tickers). --offline DIR reads saved feeds (the tests use it).
// Network goes through the engine's client (public addresses only).
namespace packtools::trader_news {
using namespace pyrt;
struct NewsError : std::runtime_error { using std::runtime_error::runtime_error; };
static const char* YAHOO = "https://feeds.finance.yahoo.com/rss/2.0/headline?s=";
static const char* TICKERS_URL = "https://www.sec.gov/files/company_tickers.json";
static const char* PLACEHOLDER = "you@example.com";
static const char* BROWSER_UA = "Mozilla/5.0 (compatible; trader-skill/1.0; +https://github.com/Jakeschincariol/trader-skill)";
constexpr int KEEP_DAYS = 30;
static const std::map<std::string, std::string> ITEMS_8K = {
    {"1.01", "material agreement"}, {"1.02", "agreement ended"}, {"1.03", "bankruptcy"}, {"1.05", "cybersecurity incident"}, {"2.01", "acquisition or sale of assets"},
    {"2.02", "earnings results"}, {"2.03", "new debt"}, {"2.04", "debt acceleration"}, {"2.05", "restructuring costs"}, {"2.06", "impairment"}, {"3.01", "delisting notice"},
    {"3.02", "unregistered share sale"}, {"3.03", "shareholder rights change"}, {"4.01", "auditor change"}, {"4.02", "past financials no longer reliable"},
    {"5.01", "change in control"}, {"5.02", "director or officer change"}, {"5.03", "bylaws or fiscal year change"}, {"5.07", "shareholder vote results"},
    {"7.01", "Regulation FD disclosure"}, {"8.01", "other events"}, {"9.01", "exhibits"}};
static const std::set<std::string> IMPORTANT_ITEMS = {"1.03", "1.05", "2.02", "2.04", "2.06", "3.01", "4.02", "5.01", "5.02"};
inline const std::vector<std::pair<std::string, std::regex>>& keywords() {
    static const auto rx = [](const char* p) { return std::regex(p, std::regex::ECMAScript | std::regex::icase); };
    static const std::vector<std::pair<std::string, std::regex>> K = {
        {"earnings", rx(R"(\bearnings\b|\bresults\b|\bEPS\b|\bguidance\b|\bforecast\b)")},
        {"analyst", rx(R"(\bupgrade[sd]?\b|\bdowngrade[sd]?\b|price target|\boutperform\b|\bunderperform\b)")},
        {"legal", rx(R"(\blawsuit\b|\bsued\b|\bprobe\b|\binvestigation\b|\bSEC charges\b|\bantitrust\b|\bjudge\b|\bcourt\b)")},
        {"deal", rx(R"(\bacqui(?:re|res|red|sition)\b|\bmerger\b|\bbuyout\b|\btakeover\b|\bdeal\b)")},
        {"capital", rx(R"(\bdividend\b|\bbuyback\b|\brepurchase\b|\bstock split\b|\boffering\b)")},
        {"people", rx(R"(\bCEO\b|\bCFO\b|\bresign|\bsteps down\b|\bappoint)")},
        {"risk", rx(R"(\brecall\b|\bbankrupt|\bdefault\b|\bdelist|\bhack|\bbreach\b|\boutage\b)")}};
    return K;
}
// std::regex sees bytes: map each non-ASCII code point to one ASCII stand-in with the same \w / \s class Python gives it
inline std::string wordify(const std::string& s) {
    std::string o; o.reserve(s.size());
    for (const auto& c : cps(s)) { if (c.size() == 1) o += c; else { const uint32_t u = cp_of(c); o += py_space(u) ? ' ' : py_word(u) ? '_' : '\x01'; } }
    return o;
}
using Watch = std::vector<std::pair<std::string, Val>>;   // {ticker: name or None}, insertion order
inline Val watch_get(const Watch& w, const std::string& t) { for (const auto& kv : w) if (kv.first == t) return kv.second; return Val(); }
inline bool watch_has(const Watch& w, const std::string& t) { for (const auto& kv : w) if (kv.first == t) return true; return false; }
inline void watch_set(Watch& w, const std::string& t, const Val& v) { for (auto& kv : w) if (kv.first == t) { kv.second = v; return; } w.push_back({t, v}); }
inline Val load_json(const std::string& path) { if (!path.empty() && is_file(path)) return loads(read_text(path)); return Val(); }
inline Watch watch_from_files(const std::string& portfolio, const std::string& targets) {
    Watch out;
    for (const auto& p : iter(dget(orv(load_json(portfolio), Val::dict()), "positions", Val::list()))) if (truthy(dget(p, "ticker"))) watch_set(out, upper(pystr(get(p, "ticker"))), dget(p, "name"));
    if (out.empty()) for (const auto& w : iter(dget(orv(load_json(targets), Val::dict()), "weights", Val::list()))) if (truthy(dget(w, "ticker"))) watch_set(out, upper(pystr(get(w, "ticker"))), dget(w, "name"));
    return out;
}
inline std::optional<std::string> sec_user_agent(const Val& cli, const std::string& config) {
    const char* env = std::getenv("SEC_USER_AGENT");
    std::string ua = strip(truthy(cli) ? pystr(cli) : (env && *env ? std::string(env) : std::string()));
    if (ua.empty()) ua = strip(pystr(orv(dget(orv(load_json(config), Val::dict()), "sec_user_agent"), Val::str(""))));
    if (ua.empty() || ua.find('@') == std::string::npos || ua.find(PLACEHOLDER) != std::string::npos) return std::nullopt;
    return ua;
}
struct Fetcher {
    std::optional<std::string> sec_ua; std::string offline; double pause = 0.15; long long last = 0;
    std::string get(const std::string& url, const std::string& name, bool sec = false) {
        if (!offline.empty()) {
            const std::string path = path_join(offline, name);
            if (!is_file(path)) throw NewsError("offline: no file " + path);
            return read_bytes(path);
        }
        if (sec && !sec_ua) throw NewsError("no SEC User-Agent set");
        const long long gap = now_us() - last, want = static_cast<long long>(pause * 1e6);
        if (gap < want) std::this_thread::sleep_for(std::chrono::microseconds(want - gap));
        last = now_us();
        const HttpResp r = http_get(url, {"User-Agent: " + (sec ? *sec_ua : std::string(BROWSER_UA)), "Accept-Encoding: gzip"}, 30);
        if (r.status >= 400) throw NewsError("HTTP " + std::to_string(r.status) + " for " + url);
        if (!r.error.empty() || r.status == 0) throw NewsError("could not reach " + url + ": " + (r.error.empty() ? std::string("no response") : r.error));
        return r.body;
    }
};
inline std::string yahoo_symbol(const std::string& t) { return replace_all(upper(t), ".", "-"); }
inline Val cik_map(Fetcher& f, Val& state) {
    if (!state.d->has("ciks")) state.d->set("ciks", Val::dict());
    Val cached = state.d->at("ciks");
    if (truthy(cached)) return cached;
    const Val data = loads(f.get(TICKERS_URL, "company_tickers.json", true));
    for (const auto& [_, v] : data.d->items) {
        const std::string t = replace_all(upper(pystr(get(v, "ticker"))), "-", ".");
        if (!cached.d->has(t)) { std::string c = pystr(get(v, "cik_str")); while (cplen(c) < 10) c = "0" + c; cached.d->set(t, Val::str(c)); }
    }
    return cached;
}
// ---- parsing
struct Item { Val d; std::optional<long long> time; };
inline std::string text_of(const xml::Node& el, const std::string& name) {
    std::string out; bool found = false;
    el.iter([&](const xml::Node& c) { if (!found && xml::local(c.tag) == name) { out = strip(c.text); found = true; } });
    return out;
}
// RFC 822 (RSS) or ISO 8601 (Atom) -> UTC microseconds
inline std::optional<long long> parse_time(const std::string& value0) {
    const std::string value = strip(value0);
    if (value.empty()) return std::nullopt;
    if (auto t = parsedate_us(value)) return t;
    return fromiso_us(replace_all(value, "Z", "+00:00"));
}
inline std::shared_ptr<xml::Node> parse_xml(const std::string& data, const std::string& what) {
    try { return xml::fromstring(data); } catch (const xml::ParseError& e) { throw NewsError(what + " is not valid XML: " + e.what()); }
}
inline std::vector<Item> parse_rss(const std::string& data, const std::string& ticker) {
    const auto root = parse_xml(data, "RSS for " + ticker);
    std::vector<Item> out;
    root->iter([&](const xml::Node& it) {
        if (it.tag != "item") return;
        const std::string title = text_of(it, "title"), link = text_of(it, "link"), g = text_of(it, "guid"), guid = g.empty() ? link : g;
        Item x; x.d = Val::dict();
        x.d.d->set("id", Val::str("rss:" + guid)); x.d.d->set("source", Val::str("Yahoo Finance")); x.d.d->set("kind", Val::str("headline")); x.d.d->set("ticker", Val::str(ticker));
        x.d.d->set("title", Val::str(title)); x.d.d->set("summary", Val::str(text_of(it, "description"))); x.d.d->set("link", Val::str(link)); x.d.d->set("time", Val());
        x.time = parse_time(text_of(it, "pubDate"));
        out.push_back(x);
    });
    return out;
}
inline std::vector<std::string> item_numbers(const std::string& s) {   // re.findall(r"\d+\.\d+", s)
    std::vector<std::string> o; std::size_t i = 0; const auto dg = [&](std::size_t k) { return k < s.size() && s[k] >= '0' && s[k] <= '9'; };
    while (i < s.size()) {
        if (!dg(i)) { i++; continue; }
        std::size_t j = i; while (dg(j)) j++;
        if (j < s.size() && s[j] == '.' && dg(j + 1)) { std::size_t k = j + 1; while (dg(k)) k++; o.push_back(s.substr(i, k - i)); i = k; } else i = j;
    }
    return o;
}
inline std::vector<Item> parse_sec_atom(const std::string& data, const std::string& ticker) {
    const auto root = parse_xml(data, "SEC feed for " + ticker);
    std::string company; bool got = false;
    root->iter([&](const xml::Node& el) { if (!got && xml::local(el.tag) == "conformed-name") { company = strip(el.text); got = true; } });
    std::vector<Item> out;
    root->iter([&](const xml::Node& entry) {
        if (xml::local(entry.tag) != "entry") return;
        const std::string form = text_of(entry, "filing-type"), acc = text_of(entry, "accession-number");
        const auto items = item_numbers(text_of(entry, "items-desc"));
        std::vector<std::string> labels; for (const auto& i : items) if (i != "9.01") { const auto it = ITEMS_8K.find(i); labels.push_back(it == ITEMS_8K.end() ? "item " + i : it->second); }
        std::string link; entry.iter([&](const xml::Node& c) { if (xml::local(c.tag) == "link") link = c.get("href", ""); });
        std::string title = (company.empty() ? ticker : company) + " filed a " + form;
        if (!labels.empty()) title += ": " + join(labels, ", ");
        bool important = false; Val il = Val::list(); for (const auto& i : items) { il.l->push_back(Val::str(i)); if (IMPORTANT_ITEMS.count(i)) important = true; }
        Item x; x.d = Val::dict();
        x.d.d->set("id", Val::str("sec:" + acc)); x.d.d->set("source", Val::str("SEC EDGAR")); x.d.d->set("kind", Val::str("filing")); x.d.d->set("form", Val::str(form));
        x.d.d->set("ticker", Val::str(ticker)); x.d.d->set("title", Val::str(title)); x.d.d->set("summary", Val::str(text_of(entry, "form-name"))); x.d.d->set("items", il);
        x.d.d->set("important", Val::boolean(important)); x.d.d->set("link", Val::str(link)); x.d.d->set("time", Val());
        x.time = parse_time(text_of(entry, "updated"));
        out.push_back(x);
    });
    return out;
}
inline std::string rx_escape(const std::string& s) {   // re.escape, then "\ " -> "[\s-]", for std::regex
    std::string o;
    for (const auto& c : cps(s)) {
        if (c == " ") o += "[\\s-]";
        else if (c.size() == 1 && std::strchr("\\^$.|?*+()[]{}/", c[0])) { o += '\\'; o += c; }
        else o += wordify(c);
    }
    return o;
}
inline std::vector<std::pair<std::string, std::regex>> mention_patterns(const Watch& watch) {
    std::vector<std::pair<std::string, std::regex>> pats;
    for (const auto& [t, name] : watch) {
        const std::string e = rx_escape(t);
        std::string alts = "\\(" + e + "\\)|\\$" + e + "\\b|\\b(?:NYSE|NASDAQ|Nasdaq)\\s*:\\s*" + e + "\\b";
        if (truthy(name) && cplen(pystr(name)) >= 4) alts += "|\\b" + rx_escape(pystr(name)) + "\\b";
        pats.push_back({t, std::regex(alts, std::regex::ECMAScript)});
    }
    return pats;
}
// which of your tickers each item touches, and why it matters
inline void tag(std::vector<Item>& items, const Watch& watch) {
    const auto pats = mention_patterns(watch);
    for (auto& it : items) {
        const std::string hay = wordify(get(it.d, "title").s + " " + pystr(orv(dget(it.d, "summary"), Val::str(""))));
        const std::string tk = get(it.d, "ticker").s;
        std::set<std::string> touches; if (watch_has(watch, tk)) touches.insert(tk);
        for (const auto& [t, rx] : pats) if (std::regex_search(hay, rx)) touches.insert(t);
        Val tl = Val::list(); for (const auto& t : touches) tl.l->push_back(Val::str(t));
        it.d.d->set("touches", tl);
        Val tags = Val::list(); for (const auto& [label, rx] : keywords()) if (std::regex_search(hay, rx)) tags.l->push_back(Val::str(label));
        if (get(it.d, "kind").s == "filing") {
            bool has202 = false, earn = false; for (const auto& x : iter(dget(it.d, "items", Val::list()))) if (x.t == Val::STR && x.s == "2.02") has202 = true;
            for (const auto& x : *tags.l) if (x.s == "earnings") earn = true;
            if (has202 && !earn) tags.l->insert(tags.l->begin(), Val::str("earnings"));
        }
        it.d.d->set("tags", tags);
    }
}
inline std::string title_key(const std::string& title) {   // re.sub(r"\W+", " ", title.lower()).strip()
    std::string o; bool gap = false;
    for (const auto& c : cps(lower(title))) { if (py_word(cp_of(c))) { if (gap) o += ' '; gap = false; o += c; } else gap = true; }
    if (gap && !o.empty()) o += ' ';
    return strip(o);
}
// one item per story: same id, or same link, or same title the same day
inline std::vector<Item> dedupe(const std::vector<Item>& items) {
    std::map<std::string, std::size_t> seen; std::vector<Item> out;
    for (const auto& it : items) {
        const std::vector<std::string> keys = {get(it.d, "id").s, get(it.d, "link").s, title_key(get(it.d, "title").s) + "|" + (it.time ? utc_date(*it.time) : std::string())};
        std::optional<std::size_t> hit;
        for (const auto& k : keys) if (!k.empty() && seen.count(k)) { hit = seen[k]; break; }
        if (hit) {
            Val ht = get(out[*hit].d, "touches");
            for (const auto& t : iter(get(it.d, "touches"))) { bool in = false; for (const auto& x : *ht.l) if (x.s == t.s) in = true; if (!in) ht.l->push_back(t); }
            std::stable_sort(ht.l->begin(), ht.l->end(), [](const Val& a, const Val& b) { return a.s < b.s; });
            continue;
        }
        for (const auto& k : keys) if (!k.empty()) seen[k] = out.size();
        out.push_back(it);
    }
    return out;
}
inline std::string fingerprint(const Item& it) { return sha1_hex(get(it.d, "id").s).substr(0, 16); }
// ---- the check
inline std::vector<Item> collect(Fetcher& f, const Watch& watch, Val& state, bool sec, const std::vector<std::string>& forms, int count, std::vector<std::string>& errors) {
    std::vector<Item> items;
    std::vector<std::string> tickers; for (const auto& kv : watch) tickers.push_back(kv.first); std::sort(tickers.begin(), tickers.end());
    for (const auto& t : tickers) {
        try { auto v = parse_rss(f.get(YAHOO + yahoo_symbol(t) + "&region=US&lang=en-US", "yahoo-" + t + ".xml"), t); items.insert(items.end(), v.begin(), v.end()); }
        catch (const NewsError& e) { errors.push_back("headlines for " + t + ": " + e.what()); }
    }
    if (sec) {
        Val ciks;
        try { ciks = cik_map(f, state); } catch (const NewsError& e) { errors.push_back(std::string("SEC ticker list: ") + e.what()); ciks = Val::dict(); }
        for (const auto& t : tickers) {
            const Val cik = dget(ciks, t);
            if (!truthy(cik)) { if (truthy(ciks)) errors.push_back("no SEC CIK for " + t); continue; }
            for (const auto& form : forms) {
                try {
                    const std::string url = "https://www.sec.gov/cgi-bin/browse-edgar?action=getcompany&CIK=" + pystr(cik) + "&type=" + form + "&dateb=&owner=include&count=" + std::to_string(count) + "&output=atom";
                    auto v = parse_sec_atom(f.get(url, "sec-" + form + "-" + pystr(cik) + ".xml", true), t); items.insert(items.end(), v.begin(), v.end());
                } catch (const NewsError& e) { errors.push_back("SEC " + form + " for " + t + ": " + e.what()); }
            }
        }
    }
    return items;
}
inline Val& seen_of(Val& state) { if (!state.d->has("seen")) state.d->set("seen", Val::dict()); static Val s; s = state.d->at("seen"); return s; }
// items within the window; only unseen ones unless show_all
inline std::vector<Item> select(std::vector<Item>& items, Val& state, long long now, double hours, bool show_all) {
    const Val seen = seen_of(state);
    const long long cutoff = now - static_cast<long long>(std::nearbyint(hours * 3600e6));
    std::vector<Item> out;
    for (auto& it : items) {
        if (it.time && *it.time < cutoff) continue;
        const bool fresh = !(seen.t == Val::DICT && seen.d->has(fingerprint(it)));
        it.d.d->set("new", Val::boolean(fresh));
        if (fresh || show_all) out.push_back(it);
    }
    std::stable_sort(out.begin(), out.end(), [&](const Item& a, const Item& b) { return a.time.value_or(now) > b.time.value_or(now); });
    return out;
}
inline void remember(const std::vector<Item>& items, Val& state, long long now) {
    Val seen = seen_of(state);
    const std::string stamp = iso_utc(now);
    for (const auto& it : items) if (!seen.d->has(fingerprint(it))) seen.d->set(fingerprint(it), Val::str(stamp));
    const std::string cutoff = iso_utc(now - static_cast<long long>(KEEP_DAYS) * 86400 * 1000000);
    Val kept = Val::dict(); for (const auto& [k, v] : seen.d->items) if (pystr(v) >= cutoff) kept.d->set(k, v);
    state.d->set("seen", kept); state.d->set("last_check", Val::str(stamp));
}
inline std::string render(const std::vector<Item>& items, const std::vector<std::string>& errors, long long now) {
    std::vector<std::string> out;
    if (items.empty()) out.push_back("Nothing new.");
    for (const auto& it : items) {
        const std::string when = it.time ? utc_hm(*it.time) : "time unknown";
        const std::string mark = truthy(dget(it.d, "important")) ? "!" : " ";
        std::vector<std::string> tv; for (const auto& t : iter(get(it.d, "touches"))) tv.push_back(t.s);
        const std::string who = tv.empty() ? get(it.d, "ticker").s : join(tv, ",");
        out.push_back(mark + " " + when + "  " + ljust(who, 11) + " " + ljust(get(it.d, "source").s, 13) + " " + get(it.d, "title").s);
        std::vector<std::string> extra; std::vector<std::string> tg; for (const auto& t : iter(get(it.d, "tags"))) tg.push_back(t.s);
        if (!tg.empty()) extra.push_back("tags: " + join(tg, ", "));
        extra.push_back(get(it.d, "link").s);
        out.push_back("      " + join(extra, "  "));
    }
    for (const auto& e : errors) out.push_back("note: " + e);
    out.push_back("Checked " + utc_hm(now) + ". Headlines are leads, not facts; open the story before acting. Not financial advice.");
    return join(out, "\n") + "\n";
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("news", "News monitor for trader-news. Built into A.S.T.R.O AI.");
    p.opt({"--tickers"}).nargs("*").help("default: trader/portfolio.json, else targets.json");
    p.opt({"--portfolio"}).def("trader/portfolio.json"); p.opt({"--targets"}).def("trader/targets.json"); p.opt({"--state"}).def("trader/state/news.json");
    p.opt({"--hours"}).number().def(48).help("ignore items older than this"); p.opt({"--all"}).store_true().help("show seen items too");
    p.opt({"--forms"}).def("8-K").help("SEC forms to watch, comma separated"); p.opt({"--no-sec"}).store_true().help("headlines only");
    p.opt({"--user-agent"}).help("SEC User-Agent: \"Your Name you@example.com\""); p.opt({"--config"}).def("trader/config.json");
    p.opt({"--offline"}).metavar("DIR"); p.opt({"--now"}).help("ISO time, for reproducible runs"); p.opt({"--dry-run"}).store_true().help("do not update the state file");
    p.opt({"--json"}).store_true();
    const Val a = parse_args(p, argv);
    Watch watch;
    if (truthy(get(a, "tickers"))) {
        for (const auto& t : *get(a, "tickers").l) watch_set(watch, upper(t.s), Val());
        const Watch known = watch_from_files(S(a, "portfolio"), S(a, "targets"));
        for (auto& [t, v] : watch) if (truthy(watch_get(known, t))) v = watch_get(known, t);
    } else watch = watch_from_files(S(a, "portfolio"), S(a, "targets"));
    if (watch.empty()) { eprint("news: no tickers. Pass --tickers, or create trader/portfolio.json or trader/targets.json first (/trader-copycat writes targets)."); return 2; }
    std::optional<long long> pn = truthy(get(a, "now")) ? parse_time(S(a, "now")) : std::optional<long long>(now_us());
    if (!pn) throw PyErr("TypeError", "unsupported operand type(s) for -: 'NoneType' and 'datetime.timedelta'");
    const long long now = *pn;
    Val state = orv(load_json(S(a, "state")), Val::dict());
    std::vector<std::string> errors;
    const auto ua = sec_user_agent(get(a, "user_agent"), S(a, "config"));
    bool sec = !B(a, "no_sec");
    if (sec && !ua && !truthy(get(a, "offline"))) { errors.push_back("SEC filings skipped: set sec_user_agent in trader/config.json or SEC_USER_AGENT (\"Your Name you@example.com\") to include them"); sec = false; }
    Fetcher f; f.sec_ua = ua; f.offline = truthy(get(a, "offline")) ? S(a, "offline") : "";
    std::vector<std::string> forms; for (const auto& x : split(S(a, "forms"), ",")) if (!strip(x).empty()) forms.push_back(upper(strip(x)));
    std::vector<Item> items = collect(f, watch, state, sec, forms, 10, errors);
    tag(items, watch);
    items = dedupe(items);
    const std::vector<Item> shown = select(items, state, now, get(a, "hours").num(), B(a, "all"));
    if (B(a, "json")) {
        Val o = Val::dict(), il = Val::list(), nl = Val::list();
        for (const auto& it : shown) { Val d = Val::dict(); for (const auto& [k, v] : it.d.d->items) d.d->set(k, k == "time" ? (it.time ? Val::str(iso_utc(*it.time)) : Val()) : v); il.l->push_back(d); }
        for (const auto& e : errors) nl.l->push_back(Val::str(e));
        o.d->set("checked", Val::str(iso_utc(now))); o.d->set("items", il); o.d->set("notes", nl);
        DumpOpts d; d.indent = 2; print(dumps(o, d));
    } else out(render(shown, errors, now));
    if (!B(a, "dry_run")) {
        remember(items, state, now);
        const std::string folder = dirname(S(a, "state")); if (!folder.empty()) makedirs(folder);
        DumpOpts d; d.indent = 2; write_text(S(a, "state"), dumps(state, d) + "\n");
    }
    for (const auto& it : shown) if (get(it.d, "new").b) return 0;
    return 1;
}
PACKTOOL("trader-news/news", main);
}  // namespace packtools::trader_news
