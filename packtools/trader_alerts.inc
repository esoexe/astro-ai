// trader-alerts/alerts - price alerts and the NYSE clock: is the market open, next open/close (2026 and 2027 holidays
// and 1:00 p.m. early closes as published by NYSE), delayed quotes (Yahoo chart JSON or Stooq CSV) into
// trader/prices.json, and rules (above / below / stop / pct_move / opening_bell) that ring once. An alert is
// information, never an order. C++ port of trader-skill/trader-alerts/alerts.py (Jake Schincariol, MIT); same
// calendar, rules, files, output and exit codes. --offline DIR reads <source>-<TICKER>.json|csv instead of the network.
namespace packtools::trader_alerts {
using namespace pyrt;
struct AlertError : std::runtime_error { using std::runtime_error::runtime_error; };
static const char* UA = "Mozilla/5.0 (compatible; trader-skill/1.0; +https://github.com/Jakeschincariol/trader-skill)";
static const double PAUSE = 0.5;
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
    if (!us) throw AlertError("--now wants an ISO time like 2026-10-13T09:31 (New York) or ...Z (UTC)");
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
inline std::string yahoo_url(const std::string& sym) { return "https://query1.finance.yahoo.com/v8/finance/chart/" + replace_all(upper(sym), ".", "-") + "?range=1d&interval=1d"; }
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
    Val last_open; for (const auto& x : iter(orv(dget(q, "open"), Val::list()))) if (!x.is_none()) last_open = x;
    Val o = Val::dict();
    o.d->set("symbol", Val::str(upper(sym))); o.d->set("price", pyfloat(price)); o.d->set("open", last_open);
    o.d->set("high", dget(m, "regularMarketDayHigh")); o.d->set("low", dget(m, "regularMarketDayLow"));
    o.d->set("prev_close", orv(dget(m, "previousClose"), dget(m, "chartPreviousClose"))); o.d->set("time", Val::str(when));
    o.d->set("currency", dget(m, "currency")); o.d->set("source", Val::str("yahoo"));
    return o;
}
inline std::string fetch(const std::string& url, int timeout = 20) {
    const HttpResp r = http_get(url, {std::string("User-Agent: ") + UA, "Accept: */*"}, timeout);
    if (r.status >= 400) throw AlertError(split(url, "?")[0] + " answered HTTP " + std::to_string(r.status));
    if (!r.error.empty() || r.status == 0) { const auto parts = split(url, "/"); throw AlertError("could not reach " + (parts.size() > 2 ? parts[2] : url) + ": " + (r.error.empty() ? std::string("no response") : r.error)); }
    return r.body;
}
struct Quotes { std::vector<std::pair<std::string, Val>> q; std::vector<std::string> errors; };
inline const Val* qget(const Quotes& qs, const std::string& s) { for (const auto& kv : qs.q) if (kv.first == s) return &kv.second; return nullptr; }
inline Quotes get_quotes(const std::vector<std::string>& symbols, const std::string& source, const std::string& offline, double pause = PAUSE) {
    Quotes out;
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        const std::string sym = upper(symbols[i]);
        try {
            std::string text;
            if (!offline.empty()) {
                const std::string path = path_join(offline, source + "-" + sym + "." + (source == "stooq" ? "csv" : "json"));
                if (!is_file(path)) throw AlertError("no offline quote file " + path);
                text = read_text(path);
            } else {
                if (i && pause > 0) std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(pause * 1000)));
                text = fetch(source == "stooq" ? stooq_url(sym) : yahoo_url(sym));
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
    DumpOpts o; o.indent = 2; write_text(path, dumps(doc, o) + "\n");
}
inline Val write_prices(const std::string& path, const Quotes& quotes, long long now_utc, const std::string& source) {
    Val old = Val::dict();
    if (is_file(path)) { try { old = loads(read_text(path)); } catch (const std::exception&) { old = Val::dict(); } }
    Val prices = Val::dict(), detail = Val::dict();
    for (const auto& [k, v] : dget(old, "prices", Val::dict()).d->items) prices.d->set(k, v);
    for (const auto& [k, v] : dget(old, "detail", Val::dict()).d->items) detail.d->set(k, v);
    for (const auto& [s, q] : quotes.q) {
        prices.d->set(s, get(q, "price"));
        Val dt = Val::dict(); dt.d->set("prev_close", dget(q, "prev_close")); dt.d->set("time", dget(q, "time")); dt.d->set("source", dget(q, "source")); detail.d->set(s, dt);
    }
    Val doc = Val::dict();
    doc.d->set("as_of", Val::str(strf(now_utc, true))); doc.d->set("source", Val::str(source));
    doc.d->set("note", Val::str("Delayed quotes for alerts and sizing checks. Your broker's price is the one that fills."));
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
inline void validate(const Val& rule) {
    const Val t = dget(rule, "type"); const std::string ts = t.t == Val::STR ? t.s : std::string("\x01");
    static const std::set<std::string> TYPES = {"above", "below", "stop", "target", "pct_move", "opening_bell"};
    if (!TYPES.count(ts)) throw AlertError("rule " + pystr(dget(rule, "id")) + ": unknown type " + repr(t));
    if (ts != "opening_bell" && !truthy(dget(rule, "symbol"))) throw AlertError("rule " + pystr(dget(rule, "id")) + " needs a symbol");
    if ((ts == "above" || ts == "below" || ts == "stop" || ts == "target") && !is_num(dget(rule, "price"))) throw AlertError("rule " + pystr(dget(rule, "id")) + " needs a numeric price");
    if (ts == "pct_move" && !is_num(dget(rule, "pct"))) throw AlertError("rule " + pystr(dget(rule, "id")) + " needs a numeric pct");
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
        const std::string typ = type == "stop" ? "below" : type == "target" ? "above" : type;
        if (typ == "opening_bell") {
            if (open && day10(dget(rung, rid, Val::str(""))) != today) {
                ring(rid, "", "Opening bell: the NYSE is open (" + get(status, "now_new_york").s + " New York)" + (truthy(dget(status, "early_close")) ? ", early close at 1:00 p.m. today, " + pystr(get(status, "early_close")) : std::string()) + ".");
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
                const std::string label = type == "stop" || type == "target" ? type : typ;
                ring(rid, sym, sym + " " + (open ? "is" : "last") + " $" + fmt_f(p, 2) + " is " + (typ == "above" ? "at or above" : "at or below") + " your " + label + " $" + n_(get(r, "price")) +
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
            if (hit && day10(dget(rung, rid, Val::str(""))) != today) {
                ring(rid, sym, sym + " moved " + fmt_fs(move, 2) + "% to $" + fmt_f(p, 2) + " (from $" + fmt_f(rf, 2) + ").");
                rung.d->set(rid, Val::str(stamp));
            }
        }
    }
    state.d->set("last_check", Val::str(stamp));
    return {rings, state};
}
// ---- commands
inline std::vector<std::string> dedupe(const std::vector<std::string>& items) { std::vector<std::string> o; for (const auto& s : items) if (!s.empty() && std::find(o.begin(), o.end(), s) == o.end()) o.push_back(s); return o; }
inline std::vector<std::string> holdings(const Val& a) {
    std::vector<std::string> syms;
    const Val pf = read_json(S(a, "portfolio"), Val::dict());
    for (const auto& p : iter(dget(pf, "positions", Val::list()))) syms.push_back(upper(pystr(orv(orv(dget(p, "ticker"), dget(p, "symbol")), Val::str("")))));
    const Val tg = read_json(S(a, "targets"), Val::dict());
    if (tg.t == Val::DICT) for (const auto& w : iter(dget(tg, "weights", Val::list()))) syms.push_back(upper(pystr(orv(orv(dget(w, "ticker"), dget(w, "symbol")), Val::str("")))));
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
    if (syms.empty()) throw AlertError("which tickers? alerts quote AAPL KO, or --holdings");
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
        print("Quotes are delayed and for alerts only. Your broker's price is the one that fills.");
    }
    return qs.q.empty() ? 1 : 0;
}
inline int cmd_check(const Val& a) {
    const Val doc = read_json(S(a, "rules"), Val());
    if (!truthy(doc) || !truthy(dget(doc, "rules"))) throw AlertError("no rules in " + S(a, "rules") + ": add one with alerts add --symbol AAPL --above 350");
    const List rules = iter(get(doc, "rules"));
    const long long now = parse_now(get(a, "now"));
    const Val status = market_status(now);
    std::vector<std::string> want; for (const auto& r : rules) if (!(dget(r, "type").t == Val::STR && get(r, "type").s == "opening_bell")) want.push_back(upper(pystr(dget(r, "symbol", Val::str("")))));
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
        if (rings.l->empty()) print("Nothing hit. " + std::to_string(rules.size()) + " rule(s) watched.");
        for (const auto& e : qs.errors) print("error: " + e);
        if (!rings.l->empty()) print("An alert is information, not an order. To act on one, size it (trader-budget or trader-rebalance), check it (trader-risk) and say yes to it (trader-broker).");
    }
    return rings.l->empty() ? 1 : 0;
}
inline int cmd_add(const Val& a) {
    Val init = Val::dict(); init.d->set("rules", Val::list());
    Val doc = read_json(S(a, "rules"), init);
    if (!has(doc, "rules")) doc.d->set("rules", Val::list());
    Val rule = Val::dict();
    if (B(a, "opening_bell")) { rule.d->set("id", Val::str(truthy(get(a, "id")) ? S(a, "id") : "opening-bell")); rule.d->set("type", Val::str("opening_bell")); }
    else {
        if (!truthy(get(a, "symbol"))) throw AlertError("--symbol is required (or --opening-bell)");
        const std::string sym = upper(S(a, "symbol"));
        std::string type, key; Val val;
        for (const char* k : {"above", "below", "stop"}) if (type.empty() && !get(a, k).is_none()) { type = k; key = "price"; val = get(a, k); }
        if (type.empty() && !get(a, "pct").is_none()) { type = "pct_move"; key = "pct"; val = get(a, "pct"); }
        if (type.empty()) throw AlertError("give one of --above, --below, --stop or --pct");
        rule.d->set("id", Val::str(truthy(get(a, "id")) ? S(a, "id") : lower(sym) + "-" + type + "-" + n_(val)));
        rule.d->set("symbol", Val::str(sym)); rule.d->set("type", Val::str(type)); rule.d->set(key, val);
        if (type == "pct_move" && truthy(get(a, "direction"))) rule.d->set("direction", get(a, "direction"));
        if (B(a, "rearm")) rule.d->set("rearm", Val::boolean(true));
        if (truthy(get(a, "note"))) rule.d->set("note", get(a, "note"));
    }
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
        s.opt({"--now"}).help("pretend it is this time (ISO; no offset = New York time)"); s.opt({"--json"}).store_true();
    };
    { Parser& s = p.sub("clock"); s.help_line = "is the NYSE open?"; s.opt({"--now"}); s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("quote"); s.help_line = "latest delayed quotes"; net(s); s.pos("symbols").nargs("*");
      s.opt({"--holdings"}).store_true().help("add tickers from your portfolio and targets"); s.opt({"--portfolio"}).def("trader/portfolio.json"); s.opt({"--targets"}).def("trader/targets.json");
      s.opt({"--write"}).nargs("?").cnst(Val::str("trader/prices.json")).metavar("PATH").help("save to prices.json (default trader/prices.json)"); }
    { Parser& s = p.sub("check"); s.help_line = "check every rule, ring what hit"; net(s); s.opt({"--rules"}).def("trader/alerts.json"); s.opt({"--state"}).def("trader/state/alerts.json");
      s.opt({"--prices-out"}).def("trader/prices.json"); s.opt({"--no-write"}).store_true(); }
    { Parser& s = p.sub("add"); s.help_line = "add a rule"; s.opt({"--rules"}).def("trader/alerts.json"); s.opt({"--symbol"});
      s.opt({"--above"}).number(); s.opt({"--below"}).number(); s.opt({"--stop"}).number(); s.opt({"--pct"}).number(); s.opt({"--opening-bell"}).store_true();
      s.exclusive({"above", "below", "stop", "pct", "opening_bell"});
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
PACKTOOL("trader-alerts/alerts", main);
}  // namespace packtools::trader_alerts
