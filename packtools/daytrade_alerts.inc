// daytrade-alerts/alerts - price alerts and the NYSE clock for a day trade: is the market open, next open/close (2026
// and 2027 holidays and 1:00 p.m. early closes as published by NYSE), delayed quotes (Yahoo chart JSON, query2 then query1,
// or Stooq CSV) into trader/prices.json, and rules that ring once: above / target, below / stop, pct_move, opening_bell and
// time ("flat by close", 3 hours earlier on an early close if before_close). add --from-plan turns trader/plan.json into a
// stop, a target and a flat-by alert for the plan's day. An alert is information, never an order. C++ port of
// day-trader-skill/trader-alerts/alerts.py (Jake Schincariol, MIT); same calendar, rules, files, output and exit codes.
// --offline DIR reads <source>-<TICKER>.json|csv (or .json.gz) instead of the network.
namespace packtools::daytrade_alerts {
using namespace pyrt;
struct AlertError : std::runtime_error { using std::runtime_error::runtime_error; };
static const char* UA = "Mozilla/5.0 (compatible; day-trader-skill/2.0; +https://github.com/Jakeschincariol/day-trader-skill)";
static const double PAUSE = 0.5;     // seconds between requests: be polite to free endpoints
static const double BACKOFF = 2.0;   // seconds to wait before the one retry after a 429
static const char* const YAHOO_HOSTS[] = {"query2.finance.yahoo.com", "query1.finance.yahoo.com"};
static const std::vector<std::pair<std::string, std::string>> NYSE_HOLIDAYS = {
    {"2026-01-01", "New Year's Day"}, {"2026-01-19", "Martin Luther King, Jr. Day"}, {"2026-02-16", "Washington's Birthday"},
    {"2026-04-03", "Good Friday"}, {"2026-05-25", "Memorial Day"}, {"2026-06-19", "Juneteenth National Independence Day"},
    {"2026-07-03", "Independence Day (observed)"}, {"2026-09-07", "Labor Day"}, {"2026-11-26", "Thanksgiving Day"}, {"2026-12-25", "Christmas Day"},
    {"2027-01-01", "New Year's Day"}, {"2027-01-18", "Martin Luther King, Jr. Day"}, {"2027-02-15", "Washington's Birthday"},
    {"2027-03-26", "Good Friday"}, {"2027-05-31", "Memorial Day"}, {"2027-06-18", "Juneteenth National Independence Day (observed)"},
    {"2027-07-05", "Independence Day (observed)"}, {"2027-09-06", "Labor Day"}, {"2027-11-25", "Thanksgiving Day"}, {"2027-12-24", "Christmas Day (observed)"}};
static const std::vector<std::pair<std::string, std::string>> NYSE_EARLY_CLOSE = {
    {"2026-11-27", "day after Thanksgiving"}, {"2026-12-24", "Christmas Eve"}, {"2027-11-26", "day after Thanksgiving"}};
static const int CAL_Y0 = 2026, CAL_Y1 = 2027;
static const long long MIN_US = 60LL * 1000000, OPEN_M = 9 * 60 + 30, CLOSE_M = 16 * 60, EARLY_M = 13 * 60;
inline const std::string* lookup(const std::vector<std::pair<std::string, std::string>>& t, const std::string& k) { for (const auto& kv : t) if (kv.first == k) return &kv.second; return nullptr; }
// ---- time: naive UTC/local datetimes as microseconds since 1970 (no zone attached)
inline long long epoch_days(long long y, unsigned m, unsigned d) { return civil_days(y, m, d) - 719468; }
inline long long floordiv(long long a, long long b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
inline long long day_of(long long t) { return floordiv(t, 86400LL * 1000000); }
inline int weekday(long long eday) { return static_cast<int>(((eday + 3) % 7 + 7) % 7); }   // Monday = 0
inline std::string day_iso(long long eday) { long long y; unsigned m, d; civil_from(eday, y, m, d); char b[48]; std::snprintf(b, sizeof b, "%04lld-%02u-%02u", y, m, d); return b; }
inline long long year_of(long long t) { long long y; unsigned m, d; civil_from(day_of(t), y, m, d); return y; }
inline std::string strf(long long t, bool z) {   // %Y-%m-%dT%H:%M:%SZ  or  %Y-%m-%d %H:%M
    long long y, us; unsigned mo, d; int h, mi, s; split_us(t, y, mo, d, h, mi, s, us); char b[64];
    if (z) std::snprintf(b, sizeof b, "%04lld-%02u-%02uT%02d:%02d:%02dZ", y, mo, d, h, mi, s); else std::snprintf(b, sizeof b, "%04lld-%02u-%02u %02d:%02d", y, mo, d, h, mi);
    return b;
}
inline long long nth_sunday(long long y, unsigned m, int n) { const long long d = epoch_days(y, m, 1); return d + (6 - weekday(d)) % 7 + 7 * (n - 1); }
// hours from UTC for New York: -4 from 2:00 a.m. on the second Sunday of March to 2:00 a.m. on the first Sunday of November
inline int eastern_offset(long long utc) {
    const long long y = year_of(utc), H = 3600LL * 1000000;
    const long long start = nth_sunday(y, 3, 2) * 24 * H + 7 * H, end = nth_sunday(y, 11, 1) * 24 * H + 6 * H;
    return start <= utc && utc < end ? -4 : -5;
}
inline long long to_eastern(long long utc) { return utc + eastern_offset(utc) * 3600LL * 1000000; }
inline long long eastern_to_utc(long long local) {
    for (int off : {-4, -5}) { const long long utc = local - off * 3600LL * 1000000; if (eastern_offset(utc) == off) return utc; }
    return local + 5 * 3600LL * 1000000;
}
// ISO time -> naive UTC; a time without an offset is New York time
inline long long parse_now(const Val& text) {
    if (!truthy(text)) return now_us() / 1000000 * 1000000;
    const std::string t = replace_all(strip(pystr(text)), "Z", "+00:00");
    const auto us = fromiso_us(t);
    if (!us) throw AlertError("--now wants a time like \"2026-10-19 10:15\" (New York) or ...Z (UTC)");
    const std::size_t base = t.size() >= 10 && t[4] == '-' ? 10 : 8;
    const bool aware = t.size() > base + 1 && t.find_first_of("+-", base + 1) != std::string::npos;
    return aware ? *us : eastern_to_utc(*us);
}
inline std::pair<bool, std::string> trading_day(long long eday) {
    if (weekday(eday) >= 5) return {false, "weekend"};
    if (const std::string* n = lookup(NYSE_HOLIDAYS, day_iso(eday))) return {false, "NYSE holiday: " + *n};
    return {true, ""};
}
inline std::optional<std::pair<long long, long long>> session(long long eday) {   // minutes of the day, New York
    if (!trading_day(eday).first) return std::nullopt;
    return std::make_pair(OPEN_M, lookup(NYSE_EARLY_CLOSE, day_iso(eday)) ? EARLY_M : CLOSE_M);
}
inline std::string hm(long long m) { char b[48]; std::snprintf(b, sizeof b, "%02lld:%02lld", m / 60, m % 60); return b; }
inline Val market_status(long long now_utc) {
    static const char* DAYS[] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
    const long long local = to_eastern(now_utc), d = day_of(local), tod = local - d * 86400LL * 1000000;
    const auto [ok, reason] = trading_day(d); (void)ok;
    const std::string* early = lookup(NYSE_EARLY_CLOSE, day_iso(d));
    long long y; unsigned m_, dd; civil_from(d, y, m_, dd);
    Val st = Val::dict();
    st.d->set("now_new_york", Val::str(strf(local, false))); st.d->set("date", Val::str(day_iso(d))); st.d->set("weekday", Val::str(DAYS[weekday(d)]));
    st.d->set("calendar_known", Val::boolean(y == CAL_Y0 || y == CAL_Y1)); st.d->set("open", Val::boolean(false)); st.d->set("reason", Val::str(reason));
    st.d->set("early_close", early ? Val::str(*early) : Val());
    const auto s = session(d);
    if (s) {
        st.d->set("opens", Val::str(hm(s->first))); st.d->set("closes", Val::str(hm(s->second)));
        if (s->first * MIN_US <= tod && tod < s->second * MIN_US) { st.d->set("open", Val::boolean(true)); st.d->set("reason", Val::str("regular session")); }
        else if (tod < s->first * MIN_US) st.d->set("reason", Val::str("before the open"));
        else st.d->set("reason", Val::str("after the close"));
    }
    long long nd = (s && tod < s->first * MIN_US) ? d : d + 1;
    for (int k = 0; k < 15; ++k, ++nd) if (session(nd)) { st.d->set("next_open", Val::str(day_iso(nd) + " 09:30")); break; }
    return st;
}
// ---- quotes
inline std::string stooq_url(const std::string& sym) { return "https://stooq.com/q/l/?s=" + replace_all(lower(sym), ".", "-") + ".us&f=sd2t2ohlcv&h&e=csv"; }
inline std::string yahoo_url(const std::string& sym, const std::string& host = YAHOO_HOSTS[0]) { return "https://" + host + "/v8/finance/chart/" + replace_all(upper(sym), ".", "-") + "?range=1d&interval=1d"; }
inline Val pyfloat(const Val& v) { return Val::flt(to_float(v)); }
inline Val parse_stooq(const std::string& text, const std::string& sym) {
    const CsvDict cd = csv_dicts(strip(text));
    if (cd.rows.empty()) throw AlertError("Stooq returned no rows for " + sym);
    std::vector<std::pair<std::string, std::string>> r;
    for (const auto& [k, v] : cd.rows[0]) {
        if (!k || k->empty()) continue;
        const std::string key = lower(strip(*k)), val = strip(v.t == Val::STR ? v.s : std::string());
        bool put = false; for (auto& kv : r) if (kv.first == key) { kv.second = val; put = true; }
        if (!put) r.push_back({key, val});
    }
    const auto g = [&](const std::string& k) { for (const auto& kv : r) if (kv.first == k) return kv.second; return std::string(); };
    const std::string close = g("close");
    if (close.empty() || upper(close) == "N/D") throw AlertError("Stooq has no data for " + sym + " (check the ticker)");
    const auto f = [&](const std::string& k) { const auto x = parse_float(g(k)); return x ? Val::flt(*x) : Val(); };
    Val q = Val::dict();
    q.d->set("symbol", Val::str(upper(sym))); q.d->set("price", pyfloat(Val::str(close))); q.d->set("open", f("open")); q.d->set("high", f("high"));
    q.d->set("low", f("low")); q.d->set("prev_close", Val()); q.d->set("time", Val::str(strip(g("date") + " " + g("time")))); q.d->set("source", Val::str("stooq"));
    return q;
}
inline Val parse_yahoo(const std::string& text, const std::string& sym) {
    Val data, res;
    const auto bad = [&]() { return AlertError("Yahoo returned something unexpected for " + sym); };
    try { data = loads(text); } catch (const PyErr&) { throw bad(); }
    if (data.t != Val::DICT || !has(data, "chart")) throw bad();
    const Val chart = get(data, "chart");
    if (chart.t == Val::LIST || chart.t == Val::STR) throw bad();   // indexing a list/str with a str key: TypeError
    if (chart.t != Val::DICT || !has(chart, "result")) throw bad();
    res = get(chart, "result");
    if (!truthy(res)) {
        const Val e = orv(dget(orv(dget(data, "chart"), Val::dict()), "error"), Val::dict());
        throw AlertError("Yahoo has no data for " + sym + ": " + pystr(dget(e, "description", Val::str("unknown ticker"))));
    }
    const Val r0 = res.t == Val::LIST ? res.l->at(0) : Val::dict();
    const Val m = dget(r0, "meta", Val::dict());
    const Val price = dget(m, "regularMarketPrice");
    if (price.is_none()) throw AlertError("Yahoo has no price for " + sym);
    const Val t = dget(m, "regularMarketTime");
    std::string when;
    if (truthy(t)) { const double x = to_float(t); when = strf(static_cast<long long>(std::floor(std::nearbyint(x * 1e6) / 1e6)) * 1000000, true); }
    const Val ql = dget(dget(r0, "indicators", Val::dict()), "quote");
    const Val q = truthy(ql) ? ql.l->at(0) : Val::dict();
    Val first_open; for (const auto& x : iter(orv(dget(q, "open"), Val::list()))) if (!x.is_none()) { first_open = x; break; }
    Val o = Val::dict();
    o.d->set("symbol", Val::str(upper(sym))); o.d->set("price", pyfloat(price)); o.d->set("open", first_open);
    o.d->set("high", dget(m, "regularMarketDayHigh")); o.d->set("low", dget(m, "regularMarketDayLow"));
    o.d->set("prev_close", orv(dget(m, "previousClose"), dget(m, "chartPreviousClose"))); o.d->set("time", Val::str(when));
    o.d->set("currency", dget(m, "currency")); o.d->set("source", Val::str("yahoo"));
    return o;
}
// (HTTP status, body); a network failure is an AlertError
inline std::pair<long, std::string> get_(const std::string& url, int timeout = 20) {
    const HttpResp r = http_get(url, {std::string("User-Agent: ") + UA, "Accept: */*"}, timeout);
    if (r.status >= 400) return {r.status, r.body};
    if (!r.error.empty() || r.status == 0) { const auto parts = split(url, "/"); throw AlertError("could not reach " + (parts.size() > 2 ? parts[2] : url) + ": " + (r.error.empty() ? std::string("no response") : r.error)); }
    return {200, r.body};
}
inline void sleep_s(double sec) { std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(sec * 1000))); }
inline std::string fetch(const std::string& url, int timeout = 20) {
    const auto [code, text] = get_(url, timeout);
    if (code != 200) throw AlertError(split(url, "?")[0] + " answered HTTP " + std::to_string(code));
    return text;
}
// query2 first. A 429 waits BACKOFF seconds and tries once more; a 429 or a 5xx after that moves on to query1.
// A 404 comes back as Yahoo's own error JSON.
inline std::string fetch_yahoo(const std::string& sym, int timeout = 20) {
    std::string last_url; long last_code = 0;
    for (const char* host : YAHOO_HOSTS) {
        const std::string url = yahoo_url(sym, host);
        auto [code, text] = get_(url, timeout);
        if (code == 429) { sleep_s(BACKOFF); std::tie(code, text) = get_(url, timeout); }
        if (code == 200) return text;
        if (code == 404 && starts(strip(text), "{")) return text;
        last_url = url; last_code = code;
        if (code != 429 && code < 500) break;
    }
    throw AlertError(split(last_url, "?")[0] + " answered HTTP " + std::to_string(last_code));
}
inline std::string offline_text(const std::string& folder, const std::string& source, const std::string& sym) {
    const std::string base = path_join(folder, source + "-" + sym + "." + (source == "stooq" ? "csv" : "json"));
    if (is_file(base)) return read_text(base);
    if (is_file(base + ".gz")) {
        std::string t;
        try { t = zip::gunzip(read_bytes(base + ".gz")); } catch (const std::exception& e) { throw AlertError(base + ".gz: " + e.what()); }
        std::string o; o.reserve(t.size());   // text mode: universal newlines
        for (std::size_t k = 0; k < t.size(); ++k) { if (t[k] == '\r') { o += '\n'; if (k + 1 < t.size() && t[k + 1] == '\n') k++; } else o += t[k]; }
        return o;
    }
    throw AlertError("no offline quote file " + base);
}
struct Quotes { std::vector<std::pair<std::string, Val>> q; std::vector<std::string> errors; };
inline const Val* qget(const Quotes& qs, const std::string& s) { for (const auto& kv : qs.q) if (kv.first == s) return &kv.second; return nullptr; }
inline Quotes get_quotes(const std::vector<std::string>& symbols, const std::string& source, const std::string& offline, double pause = PAUSE) {
    Quotes out;
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        const std::string sym = upper(symbols[i]);
        try {
            std::string text;
            if (!offline.empty()) text = offline_text(offline, source, sym);
            else {
                if (i && pause > 0) sleep_s(pause);
                text = source == "stooq" ? fetch(stooq_url(sym)) : fetch_yahoo(sym);
            }
            Val q = source == "stooq" ? parse_stooq(text, sym) : parse_yahoo(text, sym);
            bool put = false; for (auto& kv : out.q) if (kv.first == sym) { kv.second = q; put = true; }
            if (!put) out.q.push_back({sym, q});
        } catch (const AlertError& e) { out.errors.push_back(e.what()); }
    }
    return out;
}
inline void write_json(const std::string& path, const Val& doc) {
    const std::string d = dirname(path); if (!d.empty()) makedirs(d);
    const std::string tmp = path + ".tmp";
    DumpOpts o; o.indent = 2; write_text(tmp, dumps(doc, o) + "\n");
    std::error_code ec; std::filesystem::rename(fspath(tmp), fspath(path), ec);   // os.replace
    if (ec) { std::filesystem::remove(fspath(path), ec); std::filesystem::rename(fspath(tmp), fspath(path), ec); if (ec) throw OSError(ec.value(), path); }
}
inline Val write_prices(const std::string& path, const Quotes& quotes, long long now_utc, const std::string& source) {
    Val old = Val::dict();
    if (is_file(path)) { try { old = loads(read_text(path)); } catch (const std::exception&) { old = Val::dict(); } }
    Val prices = Val::dict(), detail = Val::dict();
    for (const auto& [k, v] : dget(old, "prices", Val::dict()).d->items) prices.d->set(k, v);
    for (const auto& [k, v] : dget(old, "detail", Val::dict()).d->items) detail.d->set(k, v);
    for (const auto& [s, q] : quotes.q) {
        prices.d->set(s, get(q, "price"));
        Val dt = Val::dict(); dt.d->set("prev_close", dget(q, "prev_close")); dt.d->set("time", dget(q, "time")); dt.d->set("source", dget(q, "source")); dt.d->set("fetched", Val::str(strf(now_utc, true))); detail.d->set(s, dt);
    }
    Val doc = Val::dict();
    doc.d->set("as_of", Val::str(strf(now_utc, true))); doc.d->set("source", Val::str(source));
    doc.d->set("note", Val::str("Delayed quotes for alerts and sizing checks. Your broker's price is the one that fills. detail.fetched is when each price was read."));
    doc.d->set("prices", prices); doc.d->set("detail", detail);
    write_json(path, doc);
    return doc;
}
inline Val read_json(const std::string& path, const Val& dflt) {
    if (path.empty() || !is_file(path)) return dflt;
    try { return loads(read_text(path)); } catch (const JSONDecodeError& e) { throw AlertError(path + " is not valid JSON: " + e.what()); }
}
// ---- rules
inline bool is_num(const Val& v) { return v.t == Val::INT || v.t == Val::FLOAT || v.t == Val::BOOL; }
inline bool no_quote(const Val& t) { return t.t == Val::STR && (t.s == "opening_bell" || t.s == "time"); }
// ^([01]?\d|2[0-3]):([0-5]\d)$ on str(text or "").strip() -> minutes of the day
inline std::optional<long long> hhmm(const Val& text) {
    const std::string t = strip(truthy(text) ? pystr(text) : std::string());
    const std::size_t c = t.find(':'); if (c == std::string::npos) return std::nullopt;
    const std::string h = t.substr(0, c), m = t.substr(c + 1);
    const auto digits = [](const std::string& x) { if (x.empty()) return false; for (char ch : x) if (ch < '0' || ch > '9') return false; return true; };
    if (!digits(h) || !digits(m) || h.size() > 2 || m.size() != 2 || m[0] > '5') return std::nullopt;
    if (h.size() == 2 && !(h[0] <= '1' || (h[0] == '2' && h[1] <= '3'))) return std::nullopt;
    return std::stoll(h) * 60 + std::stoll(m);
}
inline bool ymd_text(std::string d) {   // ^\d{4}-\d{2}-\d{2}$
    if (!d.empty() && d.back() == '\n') d.pop_back();
    if (d.size() != 10 || d[4] != '-' || d[7] != '-') return false;
    for (int k : {0, 1, 2, 3, 5, 6, 8, 9}) if (d[k] < '0' || d[k] > '9') return false;
    return true;
}
// a rule with a "date" is watched on that New York date only
inline bool active(const Val& rule, const std::string& today) { return !truthy(dget(rule, "date")) || pystr(get(rule, "date")) == today; }
// did a rule that rang at this UTC stamp ring on this New York date?
inline bool rang_today(const Val& stamp, const std::string& today) {
    if (!truthy(stamp)) return false;
    const std::string st = pystr(stamp), head = cpslice(st, 0, 19);
    std::optional<long long> us;
    if (head.size() == 19 && head[10] == 'T') us = fromiso_us(head);
    if (!us) return cpslice(st, 0, 10) == today;
    return day_iso(day_of(to_eastern(*us))) == today;
}
// the time a time rule rings today, moved 3 hours earlier on an early close if "before_close"
inline long long ring_time(const Val& rule, const Val& status) {
    long long at = *hhmm(dget(rule, "at"));
    if (truthy(dget(rule, "before_close")) && truthy(dget(status, "early_close"))) at = ((at - 180) % 1440 + 1440) % 1440;
    return at;
}
inline void validate(const Val& rule) {
    const Val t = dget(rule, "type"); const std::string ts = t.t == Val::STR ? t.s : std::string("\x01");
    static const std::set<std::string> TYPES = {"above", "below", "stop", "target", "pct_move", "opening_bell", "time"};
    if (!TYPES.count(ts)) throw AlertError("rule " + pystr(dget(rule, "id")) + ": unknown type " + repr(t));
    if (!no_quote(t) && !truthy(dget(rule, "symbol"))) throw AlertError("rule " + pystr(dget(rule, "id")) + " needs a symbol");
    if ((ts == "above" || ts == "below" || ts == "stop" || ts == "target") && !is_num(dget(rule, "price"))) throw AlertError("rule " + pystr(dget(rule, "id")) + " needs a numeric price");
    if (ts == "pct_move" && !is_num(dget(rule, "pct"))) throw AlertError("rule " + pystr(dget(rule, "id")) + " needs a numeric pct");
    if (ts == "time" && !hhmm(dget(rule, "at"))) throw AlertError("rule " + pystr(dget(rule, "id")) + " needs \"at\" as a New York time like \"15:55\"");
    if (truthy(dget(rule, "date")) && !ymd_text(pystr(get(rule, "date")))) throw AlertError("rule " + pystr(dget(rule, "id")) + ": date must be YYYY-MM-DD");
}
inline std::string n_(const Val& x) { const double v = to_float(x); return std::isfinite(v) && v == std::trunc(v) ? fmt_f(v, 0) : fmt_f(v, 2); }
inline std::string day10(const Val& v) { return v.t == Val::STR ? cpslice(v.s, 0, 10) : std::string(); }
// pure: (rings, new state); each ring {id, symbol, message}
inline std::pair<Val, Val> evaluate(const List& rules, const Quotes& quotes, const Val& status, const Val& state0, long long now_utc) {
    Val state = loads(dumps(orv(state0, Val::dict())));
    if (!has(state, "rung")) state.d->set("rung", Val::dict());
    Val rung = get(state, "rung");   // shared handle: edits land in state
    const std::string today = get(status, "date").s, stamp = strf(now_utc, true);
    const bool open = get(status, "open").b;
    Val rings = Val::list();
    const auto ring = [&](const std::string& id, const std::string& sym, const std::string& msg) { Val r = Val::dict(); r.d->set("id", Val::str(id)); r.d->set("symbol", Val::str(sym)); r.d->set("message", Val::str(msg)); rings.l->push_back(r); };
    for (const auto& r : rules) {
        validate(r);
        const std::string type = get(r, "type").s;
        const std::string rid = truthy(dget(r, "id")) ? pystr(get(r, "id")) : pystr(dget(r, "symbol", Val::str("market"))) + "-" + type;
        if (!active(r, today)) continue;
        const std::string typ = type == "stop" ? "below" : type == "target" ? "above" : type;
        if (typ == "opening_bell") {
            if (open && !rang_today(dget(rung, rid), today)) {
                ring(rid, "", "Opening bell: the NYSE is open (" + get(status, "now_new_york").s + " New York)" + (truthy(dget(status, "early_close")) ? ", early close at 1:00 p.m. today, " + pystr(get(status, "early_close")) : std::string()) + ".");
                rung.d->set(rid, Val::str(stamp));
            }
            continue;
        }
        if (typ == "time") {
            const long long at = ring_time(r, status), local = to_eastern(now_utc), tod = local - day_of(local) * 86400LL * 1000000;
            if (truthy(dget(status, "opens")) && tod >= at * MIN_US && !rang_today(dget(rung, rid), today)) {
                const std::string label = truthy(dget(r, "label")) ? pystr(get(r, "label")) : std::string("time alert");
                std::string msg = upper(cpslice(label, 0, 1)) + cpslice(label, 1) + ": it is " + hm(tod / MIN_US) + " New York (set for " + hm(at) + ")";
                msg += open ? ", the market closes at " + get(status, "closes").s + "." : std::string(", the market has closed.");
                const std::string sym = upper(truthy(dget(r, "symbol")) ? pystr(get(r, "symbol")) : std::string());
                if (!sym.empty()) msg += " If " + sym + " is still open, close it: trader-broker flatten --symbol " + sym + " builds the sell ticket, then trader-risk and your yes.";
                ring(rid, sym, msg);
                rung.d->set(rid, Val::str(stamp));
            }
            continue;
        }
        const std::string sym = upper(pystr(get(r, "symbol")));
        const Val* q = qget(quotes, sym);
        if (!q || !truthy(*q)) continue;
        const double p = to_float(get(*q, "price"));
        if (typ == "above" || typ == "below") {
            const double lim = to_float(get(r, "price"));
            const bool hit = typ == "above" ? p >= lim : p <= lim;
            if (hit && !rung.d->has(rid)) {
                const std::string label = type == "stop" || type == "target" ? type : std::string("alert level");
                ring(rid, sym, sym + " " + (open ? "is" : "last") + " $" + fmt_f(p, 2) + ": " + (typ == "above" ? "at or above" : "at or below") + " your " + label + " $" + n_(get(r, "price")) +
                                   (truthy(dget(r, "note")) ? " (" + pystr(get(r, "note")) + ")" : std::string()) + ".");
                rung.d->set(rid, Val::str(stamp));
            } else if (!hit && rung.d->has(rid) && truthy(dget(r, "rearm"))) {
                auto& it = rung.d->items; it.erase(std::remove_if(it.begin(), it.end(), [&](const auto& kv) { return kv.first == rid; }), it.end());
            }
        } else if (typ == "pct_move") {
            const Val ref = orv(orv(dget(r, "ref"), dget(*q, "prev_close")), dget(*q, "open"));
            if (!truthy(ref)) continue;
            const double rf = to_float(ref), move = 100.0 * (p / rf - 1);
            const Val want = dget(r, "direction");
            const bool hit = std::fabs(move) >= to_float(get(r, "pct")) && (!truthy(want) || ((want.t == Val::STR && want.s == "up") == (move > 0)));
            if (hit && !rang_today(dget(rung, rid), today)) {
                ring(rid, sym, sym + " moved " + fmt_fs(move, 2) + "% to $" + fmt_f(p, 2) + " (from $" + fmt_f(rf, 2) + ").");
                rung.d->set(rid, Val::str(stamp));
            }
        }
    }
    state.d->set("last_check", Val::str(stamp));
    return {rings, state};
}
// trader/plan.json -> a stop alert, a target alert and a flat-by-close time alert, for the plan's day
inline List rules_from_plan(const Val& plan, const Val& cfg_flat_by) {
    if (plan.t != Val::DICT || !truthy(dget(plan, "symbol"))) throw AlertError("the plan has no symbol (run trader-exit first)");
    const std::string sym = upper(pystr(get(plan, "symbol")));
    List out;
    for (const std::string key : {"stop", "target"}) {
        const Val v = dget(plan, key);
        if (!is_num(v) || to_float(v) <= 0) throw AlertError("the plan has no " + key + " (run trader-exit)");
        const Val label = dget(plan, key + "_label");
        Val r = Val::dict();
        r.d->set("id", Val::str(lower(sym) + "-plan-" + key)); r.d->set("symbol", Val::str(sym)); r.d->set("type", Val::str(key));
        r.d->set("price", Val::flt(to_float(v))); r.d->set("note", Val::str("plan " + key + (truthy(label) ? ": " + pystr(label) : std::string())));
        out.push_back(r);
    }
    const Val flat = orv(orv(dget(plan, "flat_by"), cfg_flat_by), Val::str("15:55"));
    if (!hhmm(flat)) throw AlertError("the plan's flat_by " + repr(flat) + " is not a time like 15:55");
    Val f = Val::dict();
    f.d->set("id", Val::str(lower(sym) + "-flat-by")); f.d->set("symbol", Val::str(sym)); f.d->set("type", Val::str("time")); f.d->set("at", flat);
    f.d->set("label", Val::str("flat by close")); f.d->set("before_close", Val::boolean(true));
    out.push_back(f);
    if (truthy(dget(plan, "date"))) for (auto& r : out) r.d->set("date", Val::str(pystr(get(plan, "date"))));
    return out;
}
// ---- commands
inline std::vector<std::string> dedupe(const std::vector<std::string>& items) { std::vector<std::string> o; for (const auto& s : items) if (!s.empty() && std::find(o.begin(), o.end(), s) == o.end()) o.push_back(s); return o; }
inline std::vector<std::string> holdings(const Val& a) {
    std::vector<std::string> syms;
    const Val pf = read_json(S(a, "portfolio"), Val::dict());
    if (pf.t == Val::DICT) for (const auto& p : iter(dget(pf, "positions", Val::list()))) syms.push_back(upper(pystr(orv(orv(dget(p, "ticker"), dget(p, "symbol")), Val::str("")))));
    const Val plan = read_json(S(a, "plan"), Val::dict());
    if (plan.t == Val::DICT && truthy(dget(plan, "symbol"))) syms.push_back(upper(pystr(get(plan, "symbol"))));
    std::vector<std::string> o; for (const auto& s : syms) if (!s.empty()) o.push_back(s); return o;
}
inline std::string offline_of(const Val& a) { return truthy(get(a, "offline")) ? S(a, "offline") : std::string(); }
inline int cmd_clock(const Val& a) {
    const Val st = market_status(parse_now(get(a, "now")));
    if (B(a, "json")) { DumpOpts o; o.indent = 2; print(dumps(st, o)); }
    else {
        print("New York " + get(st, "now_new_york").s + " (" + get(st, "weekday").s + "): NYSE " + (get(st, "open").b ? "OPEN" : "closed") + ", " + get(st, "reason").s + ".");
        if (truthy(dget(st, "opens"))) print("Today's session: " + get(st, "opens").s + " to " + get(st, "closes").s + (truthy(dget(st, "early_close")) ? " (early close: " + get(st, "early_close").s + ")" : std::string()) + ".");
        if (!get(st, "open").b && truthy(dget(st, "next_open"))) print("Next open: " + get(st, "next_open").s + " New York.");
        if (!get(st, "calendar_known").b) print("Holiday list covers " + std::to_string(CAL_Y0) + " and " + std::to_string(CAL_Y1) + " only: check nyse.com for this date.");
    }
    return get(st, "open").b ? 0 : 1;
}
inline int cmd_quote(const Val& a) {
    std::vector<std::string> all; for (const auto& s : iter(get(a, "symbols"))) all.push_back(upper(s.s));
    if (B(a, "holdings")) for (const auto& s : holdings(a)) all.push_back(s);
    const auto syms = dedupe(all);
    if (syms.empty()) throw AlertError("which tickers? alerts quote TSLA KO, or --holdings");
    const long long now = parse_now(get(a, "now"));
    const Quotes qs = get_quotes(syms, S(a, "source"), offline_of(a));
    const bool write = truthy(get(a, "write"));
    if (write && !qs.q.empty()) write_prices(S(a, "write"), qs, now, S(a, "source"));
    if (B(a, "json")) {
        Val qd = Val::dict(), errs = Val::list(); for (const auto& [k, v] : qs.q) qd.d->set(k, v); for (const auto& e : qs.errors) errs.l->push_back(Val::str(e));
        Val doc = Val::dict(); doc.d->set("quotes", qd); doc.d->set("errors", errs); DumpOpts o; o.indent = 2; print(dumps(doc, o));
    } else {
        for (const auto& s : syms) {
            const Val* q = qget(qs, s); if (!q) continue;
            std::string chg; const double price = to_float(get(*q, "price"));
            if (truthy(dget(*q, "prev_close"))) { const double pc = to_float(get(*q, "prev_close")); chg = " (" + fmt_fs(100.0 * (price / pc - 1), 2) + "% vs prev close $" + fmt_f(pc, 2) + ")"; }
            print(ljust(s, 6) + " $" + fmt_f(price, 2) + chg + "  " + pystr(dget(*q, "time", Val::str(""))) + "  [" + pystr(get(*q, "source")) + "]");
        }
        for (const auto& e : qs.errors) print("error: " + e);
        if (write && !qs.q.empty()) print("Wrote " + std::to_string(qs.q.size()) + " price(s) to " + S(a, "write") + ".");
        print("Quotes are free, delayed and unofficial: for alerts only. Your broker's price is the one that fills.");
    }
    return qs.q.empty() ? 1 : 0;
}
inline int cmd_check(const Val& a) {
    const Val doc = read_json(S(a, "rules"), Val());
    if (!truthy(doc) || !truthy(dget(doc, "rules"))) throw AlertError("no rules in " + S(a, "rules") + ": add some with alerts add --from-plan");
    const List rules = iter(get(doc, "rules"));
    const long long now = parse_now(get(a, "now"));
    const Val status = market_status(now);
    std::size_t watched = 0; std::vector<std::string> want;
    for (const auto& r : rules) { if (!active(r, get(status, "date").s)) continue; ++watched; if (!no_quote(dget(r, "type"))) want.push_back(upper(pystr(dget(r, "symbol", Val::str(""))))); }
    const auto syms = dedupe(want);
    const Quotes qs = syms.empty() ? Quotes{} : get_quotes(syms, S(a, "source"), offline_of(a));
    const Val state0 = read_json(S(a, "state"), Val::dict());
    auto [rings, state] = evaluate(rules, qs, status, state0, now);
    if (!B(a, "no_write")) { write_json(S(a, "state"), state); if (!qs.q.empty()) write_prices(S(a, "prices_out"), qs, now, S(a, "source")); }
    if (B(a, "json")) {
        Val errs = Val::list(), qp = Val::dict(); for (const auto& e : qs.errors) errs.l->push_back(Val::str(e)); for (const auto& [k, v] : qs.q) qp.d->set(k, get(v, "price"));
        Val o = Val::dict(); o.d->set("market", status); o.d->set("rings", rings); o.d->set("errors", errs); o.d->set("quotes", qp); DumpOpts d; d.indent = 2; print(dumps(o, d));
    } else {
        print(std::string("NYSE ") + (get(status, "open").b ? "OPEN" : "closed") + " (" + get(status, "now_new_york").s + " New York, " + get(status, "reason").s + ").");
        for (const auto& r : *rings.l) print("ALERT " + get(r, "message").s);
        if (rings.l->empty()) {
            std::string seen;
            if (!qs.q.empty()) { std::vector<std::string> parts; for (const auto& s : syms) if (const Val* q = qget(qs, s)) parts.push_back(s + " $" + fmt_f(to_float(get(*q, "price")), 2)); seen = ", " + join(parts, ", "); }
            print("Nothing hit. " + std::to_string(watched) + " rule(s) watched today" + seen + ".");
        }
        for (const auto& e : qs.errors) print("error: " + e);
        if (!rings.l->empty()) print("An alert is information, not an order. A bracket's stop-loss and take-profit already sit at the broker: check get_open_position and record any fill (ticket fill). To close a position yourself: ticket flatten, then trader-risk and your yes.");
    }
    return rings.l->empty() ? 1 : 0;
}
inline int cmd_add(const Val& a) {
    Val init = Val::dict(); init.d->set("rules", Val::list());
    Val doc = read_json(S(a, "rules"), init);
    if (!has(doc, "rules")) doc.d->set("rules", Val::list());
    if (B(a, "from_plan")) {
        const Val plan = read_json(S(a, "plan"), Val());
        if (plan.is_none()) throw AlertError("no plan at " + S(a, "plan") + " (run trader-exit first)");
        const Val cfg = truthy(get(a, "config")) ? read_json(S(a, "config"), Val::dict()) : Val::dict();
        const List fresh = rules_from_plan(plan, dget(orv(cfg, Val::dict()), "flat_by"));
        std::set<std::string> ids; for (const auto& r : fresh) ids.insert(get(r, "id").s);
        Val kept = Val::list();
        for (const auto& r : iter(get(doc, "rules"))) { const Val id = dget(r, "id"); if (!(id.t == Val::STR && ids.count(id.s))) kept.l->push_back(r); }
        for (const auto& r : fresh) kept.l->push_back(r);
        doc.d->set("rules", kept);
        Val state = read_json(S(a, "state"), Val::dict());
        if (truthy(dget(state, "rung"))) {
            auto& it = get(state, "rung").d->items;
            it.erase(std::remove_if(it.begin(), it.end(), [&](const auto& kv) { return ids.count(kv.first) > 0; }), it.end());
            write_json(S(a, "state"), state);
        }
        write_json(S(a, "rules"), doc);
        for (const auto& r : fresh) print("Added " + get(r, "id").s + ": " + dumps(r));
        print("Run alerts check every few minutes while the trade is open (re-run it, or ask the agent to; A.S.T.R.O AI does not schedule it for you). Free quotes are delayed; the broker's bracket orders are what actually stop you out.");
        return 0;
    }
    Val rule = Val::dict();
    if (B(a, "opening_bell")) { rule.d->set("id", Val::str(truthy(get(a, "id")) ? S(a, "id") : "opening-bell")); rule.d->set("type", Val::str("opening_bell")); }
    else if (truthy(get(a, "at"))) {
        if (!hhmm(get(a, "at"))) throw AlertError("--at wants a New York time like 15:55");
        rule.d->set("id", Val::str(truthy(get(a, "id")) ? S(a, "id") : "time-" + replace_all(S(a, "at"), ":", "")));
        rule.d->set("type", Val::str("time")); rule.d->set("at", get(a, "at"));
        rule.d->set("label", Val::str(truthy(get(a, "label")) ? S(a, "label") : std::string("time alert")));
        if (truthy(get(a, "symbol"))) rule.d->set("symbol", Val::str(upper(S(a, "symbol"))));
        if (B(a, "before_close")) rule.d->set("before_close", Val::boolean(true));
    } else {
        if (!truthy(get(a, "symbol"))) throw AlertError("--symbol is required (or --from-plan, --at, --opening-bell)");
        const std::string sym = upper(S(a, "symbol"));
        std::string type, key; Val val;
        for (const char* k : {"above", "below", "stop", "target"}) if (type.empty() && !get(a, k).is_none()) { type = k; key = "price"; val = get(a, k); }
        if (type.empty() && !get(a, "pct").is_none()) { type = "pct_move"; key = "pct"; val = get(a, "pct"); }
        if (type.empty()) throw AlertError("give one of --above, --below, --stop, --target or --pct");
        rule.d->set("id", Val::str(truthy(get(a, "id")) ? S(a, "id") : lower(sym) + "-" + type + "-" + n_(val)));
        rule.d->set("symbol", Val::str(sym)); rule.d->set("type", Val::str(type)); rule.d->set(key, val);
        if (type == "pct_move" && truthy(get(a, "direction"))) rule.d->set("direction", get(a, "direction"));
        if (B(a, "rearm")) rule.d->set("rearm", Val::boolean(true));
        if (truthy(get(a, "note"))) rule.d->set("note", get(a, "note"));
    }
    if (truthy(get(a, "date"))) rule.d->set("date", get(a, "date"));
    validate(rule);
    for (const auto& r : iter(get(doc, "rules"))) if (dget(r, "id").t == Val::STR && get(r, "id").s == get(rule, "id").s) throw AlertError("a rule with id " + get(rule, "id").s + " already exists");
    get(doc, "rules").l->push_back(rule);
    write_json(S(a, "rules"), doc);
    print("Added " + get(rule, "id").s + ": " + dumps(rule));
    return 0;
}
inline int cmd_list(const Val& a) {
    Val init = Val::dict(); init.d->set("rules", Val::list());
    const Val doc = read_json(S(a, "rules"), init), state = read_json(S(a, "state"), Val::dict());
    if (!truthy(dget(doc, "rules"))) { print("No rules yet."); return 1; }
    for (const auto& r : iter(get(doc, "rules"))) {
        const Val rid = dget(r, "id");
        const Val rung = rid.t == Val::STR ? dget(dget(state, "rung", Val::dict()), rid.s) : Val();
        Val rest = Val::dict(); for (const auto& [k, v] : r.d->items) if (k != "id") rest.d->set(k, v);
        print(ljust(pystr(rid), 24) + " " + dumps(rest) + (truthy(rung) ? "  (rang " + pystr(rung) + ")" : std::string()));
    }
    return 0;
}
inline int cmd_reset(const Val& a) {
    Val state = read_json(S(a, "state"), Val::dict());
    const std::string id = S(a, "id");
    const Val rung = dget(state, "rung", Val::dict());
    if (!has(rung, id)) { print(id + " has not rung (nothing to reset)."); return 1; }
    auto& it = rung.d->items; it.erase(std::remove_if(it.begin(), it.end(), [&](const auto& kv) { return kv.first == id; }), it.end());
    write_json(S(a, "state"), state);
    print("Re-armed " + id + ".");
    return 0;
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("alerts", "Price alerts and the NYSE clock (trader-alerts).");
    p.subcommands("cmd", false);
    const auto net = [](Parser& s) {
        s.opt({"--source"}).def("yahoo").choices({"yahoo", "stooq"}); s.opt({"--offline"}).metavar("DIR").help("read <source>-<TICKER>.json|csv files, no network");
        s.opt({"--now"}).help("pretend it is this time (no offset = New York time)"); s.opt({"--json"}).store_true();
    };
    { Parser& s = p.sub("clock"); s.help_line = "is the NYSE open?"; s.opt({"--now"}); s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("quote"); s.help_line = "latest delayed quotes"; net(s); s.pos("symbols").nargs("*");
      s.opt({"--holdings"}).store_true().help("add tickers from your portfolio and plan"); s.opt({"--portfolio"}).def("trader/portfolio.json"); s.opt({"--plan"}).def("trader/plan.json");
      s.opt({"--write"}).nargs("?").cnst(Val::str("trader/prices.json")).metavar("PATH").help("save to prices.json (default trader/prices.json)"); }
    { Parser& s = p.sub("check"); s.help_line = "check every rule, ring what hit"; net(s); s.opt({"--rules"}).def("trader/alerts.json"); s.opt({"--state"}).def("trader/state/alerts.json");
      s.opt({"--prices-out"}).def("trader/prices.json"); s.opt({"--no-write"}).store_true(); }
    { Parser& s = p.sub("add"); s.help_line = "add a rule"; s.opt({"--rules"}).def("trader/alerts.json"); s.opt({"--state"}).def("trader/state/alerts.json"); s.opt({"--symbol"});
      s.opt({"--above"}).number(); s.opt({"--below"}).number(); s.opt({"--stop"}).number(); s.opt({"--target"}).number(); s.opt({"--pct"}).number(); s.opt({"--opening-bell"}).store_true();
      s.opt({"--at"}).metavar("HH:MM").help("a time alert, New York time"); s.opt({"--from-plan"}).store_true().help("stop, target and flat-by-close from the plan");
      s.exclusive({"above", "below", "stop", "target", "pct", "opening_bell", "at", "from_plan"});
      s.opt({"--plan"}).def("trader/plan.json"); s.opt({"--config"}).def("trader/config.json").help("flat_by when the plan has none");
      s.opt({"--label"}).help("what a time alert says, e.g. \"flat by close\""); s.opt({"--before-close"}).store_true().help("a time alert that moves 3 hours earlier on a 1:00 p.m. early close");
      s.opt({"--date"}).help("watch this rule on this New York date only (YYYY-MM-DD)");
      s.opt({"--direction"}).choices({"up", "down"}); s.opt({"--rearm"}).store_true(); s.opt({"--note"}); s.opt({"--id"}); }
    { Parser& s = p.sub("list"); s.help_line = "list the rules"; s.opt({"--rules"}).def("trader/alerts.json"); s.opt({"--state"}).def("trader/state/alerts.json"); }
    { Parser& s = p.sub("reset"); s.help_line = "re-arm a rule that rang"; s.pos("id"); s.opt({"--state"}).def("trader/state/alerts.json"); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { out(ap::help_text(ap::norm(p, p.prog))); return 1; }
    try {
        if (cmd == "clock") return cmd_clock(a);
        if (cmd == "quote") return cmd_quote(a);
        if (cmd == "check") return cmd_check(a);
        if (cmd == "add") return cmd_add(a);
        if (cmd == "list") return cmd_list(a);
        return cmd_reset(a);
    } catch (const AlertError& e) { eprint(std::string("alerts: ") + e.what()); return 1; }
}
PACKTOOL("daytrade-alerts/alerts", main);
}  // namespace packtools::daytrade_alerts
