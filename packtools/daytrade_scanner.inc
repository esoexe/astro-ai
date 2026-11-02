// daytrade-scanner/scan - the Chart Scanner: universe (Nasdaq Trader's symbol directories -> US common stocks, ordinary
// shares and ADRs), daily (pre-market setups from Yahoo spark daily closes: 20-day closing-high breakout, bull flag,
// reclaimed 20-day average, oversold, oversold bounce) and orb (opening-range breakouts on 5-minute bars: spark closes as a
// necessary condition, then v8 highs and lows, the most traded candidates first). A scan is a list of charts that match a
// rule, not a list of stocks to buy. C++ port of day-trader-skill/trader-scanner/scan.py (Jake Schincariol, MIT); same
// rules, files, output and exit codes. Python's two lookbehinds ((?<!the ), (?<!american )(?<!global )) are checked by hand.
namespace packtools::daytrade_scanner {
using namespace pyrt;
struct ScanError : std::runtime_error { using std::runtime_error::runtime_error; };
namespace da = packtools::daytrade_alerts;
namespace dx = packtools::daytrade_exit;
namespace dy = packtools::daytrade_yahoo;
using da::to_eastern; using da::day_of; using da::day_iso; using da::session; using da::trading_day;
static const long long US = 1000000, MIN_US = 60 * US, DAY_US = 86400 * US, BAR_MIN = 5, MIN_DAILY_BARS = 30;
static const std::size_t SPARK_MAX = 20;          // Yahoo's spark answers 400 above 20 symbols
static const double PAUSE = 0.3, BACKOFF = 5.0;
static const int OPEN_M = 9 * 60 + 30, CLOSE_M = 16 * 60, MAX_CONFIRM = 200;
static const char* DEFAULT_UNIVERSE = "trader/universe.txt";
static const char* NASDAQ_URL = "https://www.nasdaqtrader.com/dynamic/SymDir/";
static const char* const NASDAQ_FILES[] = {"nasdaqlisted.txt", "otherlisted.txt"};
static const char* NOTE = "A scan is a list of charts that match a rule, not a list of stocks to buy.";
static const std::vector<std::pair<std::string, std::string>> SETUPS = {
    {"breakout_20d", "20-day closing-high breakout"}, {"bull_flag", "bull flag"}, {"reclaim_ma20", "reclaimed the 20-day average"},
    {"oversold", "oversold (RSI under 30)"}, {"oversold_bounce", "oversold bounce"}};
using Col = std::optional<std::vector<std::optional<double>>>;
struct Series { std::vector<long long> t; std::vector<std::optional<double>> c; Col o, h, l, v; Val meta; };
using Data = std::map<std::string, Series>;
using Errs = std::map<std::string, std::string>;

// ---------------------------------------------------------------- time
inline long long parse_now(const Val& text) { try { return da::parse_now(text); } catch (const da::AlertError& e) { throw ScanError(e.what()); } }
inline int parse_hhmm(const std::string& text, const std::string& flag = "--at") {
    static const std::regex R(R"(^\s*([0-9]{1,2}):([0-9]{2})\s*$)"); std::smatch m;
    if (!std::regex_match(text, m, R) || std::stoi(m[1].str()) > 23 || std::stoi(m[2].str()) > 59) throw ScanError(flag + " wants a New York time like 10:15");
    return std::stoi(m[1].str()) * 60 + std::stoi(m[2].str());
}
inline long long ts_to_eastern(long long ts) { return to_eastern(ts * US); }
inline int mod_(long long local) { return static_cast<int>((local - day_of(local) * DAY_US) / MIN_US); }
inline std::string clock_label(long long local) { return dx::ampm(mod_(local)); }   // 10:15 AM
inline std::string hm_(long long local) { return dx::hm(mod_(local)); }
inline std::string day_label(long long day) { return dx::strf_day(day); }
inline long long slot(long long day, long long k) { return day * DAY_US + OPEN_M * MIN_US + BAR_MIN * k * MIN_US; }

// ---------------------------------------------------------------- network
// query2 first, query1 on a 429 or 5xx, one more round after BACKOFF seconds if a 429 was seen; a 4xx other than 429 is
// final (Yahoo's own JSON error comes back to the caller)
inline std::string yahoo_get(const std::string& path) {
    std::string last = "no answer";
    const std::string bare = split(path, "?")[0];
    for (int attempt = 0; attempt < 2; ++attempt) {
        bool saw_429 = false;
        for (const char* host : {"https://query2.finance.yahoo.com", "https://query1.finance.yahoo.com"}) {
            const HttpResp r = http_get(std::string(host) + path, {std::string("User-Agent: ") + dy::UA, "Accept: */*"}, 20);
            if (r.status == 0 || (r.status < 400 && !r.error.empty())) { last = r.error.empty() ? std::string("no response") : r.error; continue; }
            if (r.status < 400) return decode_utf8_replace(r.body);
            last = "HTTP " + std::to_string(r.status);
            if (r.status == 429) { saw_429 = true; continue; }
            if (r.status >= 500) continue;
            const std::string body = decode_utf8_replace(r.body);
            if (starts(strip(body), "{")) return body;
            throw ScanError("Yahoo answered " + last + " for " + bare);
        }
        if (attempt == 0 && saw_429) { dy::pause(BACKOFF); continue; }
        break;
    }
    throw ScanError("Yahoo answered " + last + " for " + bare);
}
inline std::string to_yahoo(const std::string& sym) { return replace_all(upper(sym), ".", "-"); }
inline std::string spark_path(const std::vector<std::string>& symbols, const std::string& rng, const std::string& interval) {
    std::vector<std::string> y; for (const auto& s : symbols) y.push_back(to_yahoo(s));
    return "/v7/finance/spark?symbols=" + join(y, ",") + "&range=" + rng + "&interval=" + interval;
}
inline std::string chart_path(const std::string& sym, const std::string& rng, const std::string& interval) {
    return "/v8/finance/chart/" + to_yahoo(sym) + "?range=" + rng + "&interval=" + interval;
}

// ---------------------------------------------------------------- parsing
inline std::optional<double> optf(const Val& v) { if (v.is_none()) return std::nullopt; return to_float(v); }
inline Series series_from_result(const Val& res) {   // a Yahoo chart result (v8 or one spark response) -> plain lists
    const Val quotes = dget(orv(dget(res, "indicators"), Val::dict()), "quote");
    const Val q = truthy(quotes) && quotes.t == Val::LIST ? orv((*quotes.l)[0], Val::dict()) : Val::dict();
    const Val t = orv(dget(res, "timestamp"), Val::list());
    Series s; if (t.t == Val::LIST) for (const Val& x : *t.l) s.t.push_back(to_int(x));
    const std::size_t n = s.t.size();
    const auto col = [&](const char* k) -> Col {
        const Val v = dget(q, k);
        if (v.t != Val::LIST) return std::nullopt;
        std::vector<std::optional<double>> out; for (const Val& x : *v.l) out.push_back(optf(x));
        while (out.size() < n) out.push_back(std::nullopt);
        return out;
    };
    const Col c = col("close");
    s.c = c && !c->empty() ? *c : std::vector<std::optional<double>>(n);
    s.o = col("open"); s.h = col("high"); s.l = col("low"); s.v = col("volume"); s.meta = orv(dget(res, "meta"), Val::dict());
    return s;
}
inline bool has_col(const Col& c) { return c && !c->empty(); }   // a non-empty list (Python truthiness)
// spark JSON -> {SYM: series}; symbols Yahoo does not know are missing; wanted maps Yahoo symbols back (BRK-B -> BRK.B)
inline Data parse_spark(const Val& doc, const std::map<std::string, std::string>& wanted = {}) {
    const Val sp = doc.t == Val::DICT ? dget(doc, "spark") : Val();
    if (sp.t != Val::DICT) throw ScanError("Yahoo's spark answer is not the shape this tool knows");
    const Val err = dget(sp, "error");
    Data out;
    const Val items = orv(dget(sp, "result"), Val::list());
    if (items.t == Val::LIST)
        for (const Val& item : *items.l) {
            const Val resp = orv(dget(item, "response"), Val::list());
            if (!truthy(resp) || resp.t != Val::LIST) continue;
            const std::string ysym = upper(pystr(orv(dget(item, "symbol"), orv(dget(orv(dget((*resp.l)[0], "meta"), Val::dict()), "symbol"), Val::str("")))));
            const auto w = wanted.find(ysym);
            out[w != wanted.end() ? w->second : replace_all(ysym, "-", ".")] = series_from_result((*resp.l)[0]);
        }
    if (truthy(err) && out.empty()) throw ScanError("Yahoo spark: " + pystr(orv(dget(err, "description"), orv(dget(err, "code"), err))));
    return out;
}
inline Data parse_spark(const std::string& text, const std::map<std::string, std::string>& wanted = {}) { return parse_spark(loads(text), wanted); }
inline Series parse_chart(const Val& doc, const std::string& sym = "") {   // v8 chart JSON -> series
    if (doc.t != Val::DICT || !has(doc, "chart")) throw ScanError("Yahoo returned something unexpected for " + sym);
    const Val chart = get(doc, "chart"), res = dget(chart, "result");
    if (!truthy(res)) {
        const Val err = orv(dget(chart, "error"), Val::dict());
        throw ScanError("no data for " + sym + ": " + (has(err, "description") ? pystr(get(err, "description")) : std::string("unknown ticker")));
    }
    return series_from_result(res.t == Val::LIST ? (*res.l)[0] : Val::dict());
}
inline Series parse_chart(const std::string& text, const std::string& sym) {
    Val doc; try { doc = loads(text); } catch (const PyErr&) { throw ScanError("Yahoo returned something unexpected for " + sym); }
    return parse_chart(doc, sym);
}
inline std::string stem(const std::string& name) {
    for (const char* ext : {".json.gz", ".json"}) if (ends(name, ext)) return name.substr(0, name.size() - std::strlen(ext));
    return name;
}
inline void absorb(const Val& doc, const std::string& name_hint, Data& data, Errs& errors) {   // one parsed offline doc
    if (doc.t == Val::DICT && has(doc, "spark")) {
        for (auto& kv : parse_spark(doc)) data[kv.first] = kv.second;
    } else if (doc.t == Val::DICT && has(doc, "chart")) {
        std::string sym = name_hint;
        if (sym.empty()) {
            const Val res = orv(dget(get(doc, "chart"), "result"), Val());
            const Val first = res.t == Val::LIST && !res.l->empty() ? (*res.l)[0] : Val::dict();
            const Val s = has(first, "meta") && has(get(first, "meta"), "symbol") ? get(get(first, "meta"), "symbol") : Val();
            if (s.t != Val::STR) throw ScanError("an offline v8 chart without a symbol: name the file <TICKER>.json");
            sym = s.s;
        }
        sym = replace_all(upper(sym), "-", ".");
        try { data[sym] = parse_chart(doc, sym); } catch (const ScanError& e) { errors[sym] = e.what(); }
    } else if (doc.t == Val::DICT) {
        for (const auto& kv : doc.d->items) absorb(kv.second, kv.first, data, errors);
    } else {
        throw ScanError("offline file " + name_hint + " is not a spark answer, a v8 chart or a bundle");
    }
}
inline Val read_doc(const std::string& path, const std::string& label) {
    std::string text;
    try { text = dy::read_text_any(path); } catch (const std::exception& e) { throw ScanError(label + " is not valid JSON: " + e.what()); }
    try { return loads(text); } catch (const PyErr& e) { throw ScanError(label + " is not valid JSON: " + e.what()); }
}
// a folder of <T>.json(.gz) files or one file -> ({SYM: series}, {SYM: error})
inline std::pair<Data, Errs> load_offline(const std::string& path) {
    Data data; Errs errors;
    if (is_dir(path)) {
        std::vector<std::string> names;
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(fspath(path), ec)) {
            const std::u8string f8 = e.path().filename().u8string(); const std::string f(f8.begin(), f8.end());
            if (ends(f, ".json") || ends(f, ".json.gz")) names.push_back(f);
        }
        std::sort(names.begin(), names.end());
        if (names.empty()) throw ScanError("no .json files in " + path);
        for (const auto& f : names) {
            const Val doc = read_doc(path_join(path, f), f);
            if (doc.t == Val::DICT && (has(doc, "chart") || has(doc, "spark"))) absorb(doc, has(doc, "spark") ? "" : stem(f), data, errors);
            else absorb(doc, "", data, errors);
        }
    } else if (is_file(path)) {
        absorb(read_doc(path, path), "", data, errors);
    } else {
        throw ScanError("no offline file or folder at " + path);
    }
    return {data, errors};
}

// ---------------------------------------------------------------- universe
using Row = std::map<std::string, std::string>;
inline std::string rget(const Row& r, const std::string& k) { const auto it = r.find(k); return it == r.end() ? "" : it->second; }
// rx matches somewhere in s with none of `nots` right before the match (Python's negative lookbehinds)
inline bool search_not_after(const std::string& s, const std::regex& rx, std::initializer_list<const char*> nots) {
    for (auto it = std::sregex_iterator(s.begin(), s.end(), rx); it != std::sregex_iterator(); ++it) {
        const std::size_t p = static_cast<std::size_t>(it->position(0));
        bool blocked = false;
        for (const char* n : nots) { const std::size_t k = std::strlen(n); if (p >= k && s.compare(p - k, k, n) == 0) blocked = true; }
        if (!blocked) return true;
    }
    return false;
}
inline std::string classify(const Row& row) {   // one symbol-directory row -> "keep" or the rule that drops it
    static const std::regex TICKER(R"(^[A-Z]{1,5}$|^[A-Z]{1,4}\.[A-Z]$)"), SUFFIX(R"(\.[UWR]$)");
    static const std::regex WARRANT(R"(\bwarrants?\b)"), UNIT1(R"(\b(units?|rights?),? (each|that|entitling)\b)"), RIGHT_TO(R"(\brights? to\b)");
    static const std::regex NOTE_(R"(\b(notes?|etns?|debentures?)\b)"), DEPOSITARY(R"(\bdepositary shares?\b)");
    static const std::regex COMMON(R"(\bcommon (stock|shares?|units?)\b|\bord(inary)? shares?\b|\bshares? of beneficial interest\b)"
                                   R"(|\b(american|global) deposit[ao]ry\b|\bads[s]?\b|\badrs?\b)"
                                   R"(|\b(limited partnership|partnership|trust) units?\b|\bl\.?p\.? units?\b)"
                                   R"(|\bunits? (of|representing)\b.*\b(limited partner|partnership|beneficial interest|limited liability))");
    static const std::vector<std::pair<const char*, std::regex>> SOFT = {
        {"unit", std::regex(R"(\bunits?\b)")}, {"right", std::regex(R"(\brights?\b)")}, {"bond", std::regex(R"(\bbonds?\b)")}, {"preferred", std::regex(R"(\bpreferred\b|\bpreference\b)")}};
    std::string sym = rget(row, "Symbol"); if (sym.empty()) sym = rget(row, "ACT Symbol"); sym = strip(sym);
    if (strip(rget(row, "Test Issue")) != "N") return "test issue";
    if (strip(rget(row, "ETF")) != "N") return "ETF";
    { std::string ns = rget(row, "NextShares"); if (ns.empty()) ns = "N"; if (strip(ns) != "N") return "NextShares"; }
    if (!std::regex_search(sym, TICKER)) return "not a plain ticker";
    if (std::regex_search(sym, SUFFIX)) return "unit / warrant / right suffix";
    const std::string name = join(split_ws(lower(rget(row, "Security Name"))), " ");
    if (std::regex_search(name, WARRANT)) return "warrant";
    if (std::regex_search(name, UNIT1) || search_not_after(name, RIGHT_TO, {"the "})) return "unit or right";
    if (std::regex_search(name, NOTE_)) return "note";
    if (name.find('%') != std::string::npos) return "coupon";
    if (search_not_after(name, DEPOSITARY, {"american ", "global "})) return "depositary";
    if (std::regex_search(name, COMMON)) return "keep";
    for (const auto& [tag, rx] : SOFT) if (std::regex_search(name, rx)) return tag;
    return "keep";
}
// Nasdaq Trader pipe file -> (rows, file_created); the last line is "File Creation Time: ..."
inline std::pair<std::vector<Row>, std::optional<std::string>> parse_symdir(const std::string& text) {
    std::vector<std::string> lines;
    for (const auto& ln : split(replace_all(text, "\r", ""), "\n")) if (!strip(ln).empty()) lines.push_back(ln);
    if (lines.empty() || lines[0].find('|') == std::string::npos) throw ScanError("not a Nasdaq Trader symbol directory");
    std::vector<std::string> hdr; for (const auto& h : split(lines[0], "|")) hdr.push_back(strip(h));
    const auto in = [&](const char* k) { return std::find(hdr.begin(), hdr.end(), k) != hdr.end(); };
    if (!in("Security Name") || !(in("Symbol") || in("ACT Symbol"))) throw ScanError("not a Nasdaq Trader symbol directory (header " + cpslice(lines[0], 0, 60) + ")");
    std::vector<Row> rows; std::optional<std::string> created;
    static const std::regex FC(R"(([0-9]{2})([0-9]{2})([0-9]{4})([0-9]{2}):([0-9]{2}))");
    for (std::size_t i = 1; i < lines.size(); ++i) {
        const std::string& ln = lines[i];
        if (starts(ln, "File Creation Time")) {
            std::smatch m;
            if (std::regex_search(ln, m, FC)) created = m[3].str() + "-" + m[1].str() + "-" + m[2].str() + " " + m[4].str() + ":" + m[5].str();
            continue;
        }
        const auto parts = split(ln, "|"); Row r;
        for (std::size_t k = 0; k < hdr.size() && k < parts.size(); ++k) r[hdr[k]] = parts[k];
        rows.push_back(r);
    }
    return {rows, created};
}
using Counts = std::vector<std::pair<std::string, long long>>;   // an ordered dict of counters
inline long long& bump(Counts& c, const std::string& k) { for (auto& kv : c) if (kv.first == k) return kv.second; c.emplace_back(k, 0); return c.back().second; }
inline long long cget(const Counts& c, const std::string& k) { for (const auto& kv : c) if (kv.first == k) return kv.second; return 0; }
struct Universe { std::vector<std::pair<std::string, std::string>> kept; Counts counts; std::optional<std::string> created; };
inline Universe build_universe(const std::map<std::string, std::string>& texts) {
    std::map<std::string, std::string> kept; Universe u;
    for (const char* fname : NASDAQ_FILES) {
        const auto it = texts.find(fname);
        if (it == texts.end()) throw ScanError(std::string("missing ") + fname);
        const auto [rows, c] = parse_symdir(it->second);
        if (!u.created) u.created = c;
        bump(u.counts, "rows") += static_cast<long long>(rows.size());
        for (const Row& r : rows) {
            const std::string why = classify(r);
            bump(u.counts, why) += 1;
            if (why == "keep") {
                std::string sym = rget(r, "Symbol"); if (sym.empty()) sym = rget(r, "ACT Symbol");
                kept[strip(sym)] = join(split_ws(rget(r, "Security Name")), " ");
            }
        }
    }
    u.kept.assign(kept.begin(), kept.end());
    return u;
}

// ---------------------------------------------------------------- daily setups
inline std::optional<double> rsi(const std::vector<double>& c, std::size_t n = 14) {   // Wilder's RSI on closes
    if (c.size() < n + 1) return std::nullopt;
    std::vector<double> g, lo;
    for (std::size_t i = 1; i < c.size(); ++i) { g.push_back(std::max(0.0, c[i] - c[i - 1])); lo.push_back(std::max(0.0, c[i - 1] - c[i])); }
    double ag = fsum(std::vector<double>(g.begin(), g.begin() + static_cast<long>(n))) / static_cast<double>(n);
    double al = fsum(std::vector<double>(lo.begin(), lo.begin() + static_cast<long>(n))) / static_cast<double>(n);
    for (std::size_t i = n; i < g.size(); ++i) {
        ag = (ag * static_cast<double>(n - 1) + g[i]) / static_cast<double>(n);
        al = (al * static_cast<double>(n - 1) + lo[i]) / static_cast<double>(n);
    }
    return al != 0.0 ? 100 - 100 / (1 + ag / al) : 100.0;
}
// [(day, close)] of COMPLETE daily bars: null closes dropped, today's bar dropped until the session closes, anything after
// the clock dropped; one bar a day (a late duplicate, the live tick, wins)
inline std::vector<std::pair<long long, double>> daily_closes(const Series& s, long long now_utc) {
    const long long now = to_eastern(now_utc), today = day_of(now);
    const auto ses = session(today);
    const bool today_done = !ses || now - today * DAY_US >= static_cast<long long>(ses->second) * MIN_US;
    std::map<long long, double> out;
    for (std::size_t i = 0; i < s.t.size() && i < s.c.size(); ++i) {
        if (!s.c[i]) continue;
        const long long d = day_of(ts_to_eastern(s.t[i]));
        if (d > today || (d == today && !today_done)) continue;
        out[d] = *s.c[i];
    }
    return {out.begin(), out.end()};
}
inline double vmax(const std::vector<double>& v, long a, long b) { double m = v[static_cast<std::size_t>(a)]; for (long k = a + 1; k < b; ++k) if (v[static_cast<std::size_t>(k)] > m) m = v[static_cast<std::size_t>(k)]; return m; }
inline double vmin(const std::vector<double>& v, long a, long b) { double m = v[static_cast<std::size_t>(a)]; for (long k = a + 1; k < b; ++k) if (v[static_cast<std::size_t>(k)] < m) m = v[static_cast<std::size_t>(k)]; return m; }
inline double vsum(const std::vector<double>& v, long a, long b) { return fsum(std::vector<double>(v.begin() + a, v.begin() + b)); }
struct Setups { std::vector<std::string> tags; std::optional<double> rsi, rsi_prev; };
inline Setups find_setups(const std::vector<double>& c) {   // closes oldest first, >= 30
    const long n = static_cast<long>(c.size());
    const auto at = [&](long k) { return c[static_cast<std::size_t>(n + k)]; };   // c[k] with k < 0
    const double last = at(-1), hi20 = vmax(c, n - 21, n - 1), ma20 = vsum(c, n - 20, n) / 20.0, ma20_prev = vsum(c, n - 21, n - 1) / 20.0;
    const double run = at(-6) / at(-16) - 1, rng5 = (vmax(c, n - 5, n) - vmin(c, n - 5, n)) / last;
    Setups s; s.rsi = rsi(c); s.rsi_prev = rsi(std::vector<double>(c.begin(), c.end() - 1));
    if (last > hi20) s.tags.push_back("breakout_20d");
    if (run > 0.10 && rng5 < 0.05) s.tags.push_back("bull_flag");
    if (at(-2) < ma20_prev && last > ma20) s.tags.push_back("reclaim_ma20");
    if (s.rsi && *s.rsi < 30) s.tags.push_back("oversold");
    if (s.rsi_prev && *s.rsi_prev < 30 && last > at(-2)) s.tags.push_back("oversold_bounce");
    return s;
}
struct DailyOut { std::vector<Val> hits; Counts counts; std::optional<long long> last_date; };
inline DailyOut scan_daily(const Data& data, const std::vector<std::string>& symbols, long long now_utc, double min_price, long long min_volume) {
    DailyOut o; std::map<long long, long long> dates;
    o.counts = {{"requested", static_cast<long long>(symbols.size())}, {"fetched", 0}, {"illiquid", 0}, {"short", 0}, {"scanned", 0}};
    struct H { Val h; double key; std::string sym; };
    std::vector<H> hits;
    for (const auto& sym : symbols) {
        const auto it = data.find(sym);
        if (it == data.end()) continue;
        const Series& s = it->second;
        bump(o.counts, "fetched") += 1;
        const auto bars = daily_closes(s, now_utc);
        Val price = dget(s.meta, "regularMarketPrice");
        if (price.is_none() && !bars.empty()) price = Val::flt(bars.back().second);
        const Val vol = dget(s.meta, "regularMarketVolume");
        if (price.is_none() || to_float(price) < min_price || (min_volume > 0 && (vol.is_none() || to_float(vol) < static_cast<double>(min_volume)))) {
            bump(o.counts, "illiquid") += 1;
            continue;
        }
        if (static_cast<long long>(bars.size()) < MIN_DAILY_BARS) { bump(o.counts, "short") += 1; continue; }
        bump(o.counts, "scanned") += 1;
        const long long d_last = bars.back().first;
        dates[d_last] += 1;
        std::vector<double> closes; for (const auto& b : bars) closes.push_back(b.second);
        const Setups st = find_setups(closes);
        if (st.tags.empty()) continue;
        Val h = Val::dict(), tags = Val::list();
        for (const auto& t : st.tags) tags.l->push_back(Val::str(t));
        const double p = round_n(closes.back(), 2);
        h.d->set("symbol", Val::str(sym)); h.d->set("setups", tags); h.d->set("price", Val::flt(p));
        h.d->set("close_date", Val::str(day_iso(d_last))); h.d->set("rsi", Val::flt(round_n(*st.rsi, 1)));
        const Val v = vol.is_none() ? Val() : Val::integer(vol.t == Val::INT ? vol.i : static_cast<long long>(to_float(vol)));
        h.d->set("volume", v);
        hits.push_back({h, -(p * (v.is_none() ? 0.0 : static_cast<double>(v.i))), sym});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const H& a, const H& b) { return a.key != b.key ? a.key < b.key : a.sym < b.sym; });
    for (auto& h : hits) o.hits.push_back(h.h);
    long long best = -1;
    for (const auto& [d, n] : dates) if (best < 0 || n > dates[best] || (n == dates[best] && d > best)) best = d;
    if (best >= 0) o.last_date = best;
    bump(o.counts, "hits") = static_cast<long long>(o.hits.size());
    return o;
}

// ---------------------------------------------------------------- opening-range breakout
struct Bar { long long start; double c; std::optional<double> h, l; double v; };
// 5-minute bars of `day` complete at `cutoff` (New York time), on the grid, with a close; sorted, one per slot
inline std::vector<Bar> session_bars(const Series& s, long long day, long long cutoff) {
    const auto ses = session(day);
    const long long close_m = ses ? ses->second : CLOSE_M;
    std::map<long long, Bar> out;
    const auto pick = [](const Col& c, std::size_t i) -> std::optional<double> { if (!has_col(c) || i >= c->size()) return std::nullopt; return (*c)[i]; };
    for (std::size_t i = 0; i < s.t.size(); ++i) {
        if (i >= s.c.size() || !s.c[i]) continue;
        const long long et = ts_to_eastern(s.t[i]);
        const long long tod = et - day_of(et) * DAY_US;
        if (day_of(et) != day || tod % MIN_US || (tod / MIN_US) % BAR_MIN) continue;   // another day, or the live tick off the grid
        if (tod < OPEN_M * MIN_US || tod >= close_m * MIN_US) continue;              // pre-market, after hours, the closing print
        if (et + BAR_MIN * MIN_US > cutoff) continue;                                 // still in progress at the cutoff
        const auto v = pick(s.v, i);
        out[et] = Bar{et, *s.c[i], pick(s.h, i), pick(s.l, i), has_col(s.v) ? (v ? *v : 0.0) : 0.0};
    }
    std::vector<Bar> r; for (auto& kv : out) r.push_back(kv.second);
    return r;
}
inline long long floordiv(long long a, long long b) { long long q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) --q; return q; }
inline bool usable(const std::vector<Bar>& bars, long long day, long long cutoff, long long n_range) {   // every slot 9:30..cutoff, at least the range
    long long want = floordiv(cutoff - slot(day, 0), BAR_MIN * MIN_US);
    if (const auto ses = session(day)) want = std::min(want, floordiv(day * DAY_US + ses->second * MIN_US - slot(day, 0), BAR_MIN * MIN_US));
    if (want < n_range || static_cast<long long>(bars.size()) != want) return false;
    for (std::size_t k = 0; k < bars.size(); ++k) if (bars[k].start != slot(day, static_cast<long long>(k))) return false;
    return true;
}
inline bool orb_candidate(const std::vector<Bar>& bars, std::size_t n_range) {   // stage 1, closes only (necessary for a breakout)
    double top = bars[0].c; for (std::size_t k = 1; k < n_range; ++k) top = std::max(top, bars[k].c);
    for (std::size_t k = n_range; k < bars.size(); ++k) if (bars[k].c >= top) return true;
    return false;
}
struct Hit { Val h; long long t; std::string sym; };
inline std::optional<Hit> orb_hit(const std::vector<Bar>& bars, std::size_t n_range, const std::string& sym) {   // stage 2: first close above the range high
    for (std::size_t k = 0; k < n_range; ++k) if (!bars[k].h || !bars[k].l) return std::nullopt;
    double oh = *bars[0].h, ol = *bars[0].l;
    for (std::size_t k = 1; k < n_range; ++k) { oh = std::max(oh, *bars[k].h); ol = std::min(ol, *bars[k].l); }
    for (std::size_t k = n_range; k < bars.size(); ++k) {
        const Bar& b = bars[k];
        if (!(b.c > oh)) continue;
        const long long closes_at = b.start + BAR_MIN * MIN_US;
        Val h = Val::dict(), setups = Val::list({Val::str("orb")});
        h.d->set("symbol", Val::str(sym)); h.d->set("setups", setups);
        h.d->set("price", Val::flt(round_n(b.c, 2))); h.d->set("at", Val::str(clock_label(closes_at)));
        h.d->set("bar_start", Val::str(hm_(b.start))); h.d->set("bar_index", Val::integer(static_cast<long long>(k)));
        h.d->set("or_high", Val::flt(round_n(oh, 2))); h.d->set("or_low", Val::flt(round_n(ol, 2)));
        h.d->set("pct_over", Val::flt(round_n((b.c / oh - 1) * 100, 2)));
        return Hit{h, closes_at, sym};
    }
    return std::nullopt;
}
// -> (day, cutoff New York time) or a ScanError with the plain message
inline std::pair<long long, long long> orb_window(long long now_utc, const std::optional<int>& at, long long or_minutes, std::optional<int> until, std::optional<long long> explicit_day) {
    const long long now = to_eastern(now_utc);
    const long long day = explicit_day ? *explicit_day : day_of(now);
    if (or_minutes < BAR_MIN || or_minutes % BAR_MIN || or_minutes > 120) throw ScanError("--or-minutes wants a multiple of 5 from 5 to 120 (15 is the classic)");
    const auto [ok, why] = trading_day(day);
    if (!ok) throw ScanError("No session on " + day_label(day) + " (" + why + "): the market is closed. Run orb on a trading day after the opening range, or replay a past day with --offline.");
    const auto ses = session(day);
    const long long range_end = slot(day, or_minutes / BAR_MIN), first_close = range_end + BAR_MIN * MIN_US;
    const long long close_dt = day * DAY_US + static_cast<long long>(ses->second) * MIN_US;
    long long cutoff;
    if (at) {
        const int at_min = at.value_or(0);
        cutoff = day * DAY_US + static_cast<long long>(at_min) * MIN_US;
        if (!explicit_day && cutoff > now + 59 * US) throw ScanError("--at " + dx::hm(at_min) + " is later than now (" + clock_label(now) + " ET).");
        if (cutoff > close_dt) throw ScanError("--at " + dx::hm(at_min) + " is after the " + clock_label(close_dt) + " close on " + day_label(day) + ".");
    } else {
        cutoff = floordiv(now, MIN_US) * MIN_US;
        if (cutoff >= close_dt) throw ScanError("The market closed at " + clock_label(close_dt) + " ET on " + day_label(day) + ". Replay the session with --at 10:15.");
    }
    if (cutoff < first_close)
        throw ScanError("It is " + clock_label(cutoff) + " ET: the " + std::to_string(or_minutes) + "-minute opening range runs 9:30 to " + clock_label(range_end) +
                        " and the first bar after it closes at " + clock_label(first_close) + ". Run orb again after " + clock_label(first_close) + ".");
    if (until && mod_(cutoff) > *until)
        throw ScanError("It is " + clock_label(cutoff) + " ET, after " + dx::hm(*until) + ", your last time for new entries (no_entries_after): an opening-range breakout found now is expired. Run orb tomorrow after " +
                        clock_label(first_close) + ", or replay today with --at 10:15.");
    return {day, cutoff};
}
using Confirm = std::function<Series(const std::string&)>;
struct OrbOut { std::vector<Val> hits; Counts counts; Errs failed; };
inline OrbOut scan_orb(const Data& stage1, const std::vector<std::string>& symbols, long long day, long long cutoff, long long or_minutes,
                       double min_price, long long min_volume, const Confirm& confirm, long long max_confirm) {
    const std::size_t n_range = static_cast<std::size_t>(or_minutes / BAR_MIN);
    OrbOut o;
    o.counts = {{"requested", static_cast<long long>(symbols.size())}, {"fetched", 0}, {"unusable", 0}, {"illiquid", 0}, {"scanned", 0},
                {"candidates", 0}, {"confirmed", 0}, {"unconfirmed", 0}};
    struct C { std::string sym; std::vector<Bar> bars; double traded; };
    std::vector<C> cands;
    for (const auto& sym : symbols) {
        const auto it = stage1.find(sym);
        if (it == stage1.end()) continue;
        const Series& s = it->second;
        bump(o.counts, "fetched") += 1;
        auto bars = session_bars(s, day, cutoff);
        if (!usable(bars, day, cutoff, static_cast<long long>(n_range))) { bump(o.counts, "unusable") += 1; continue; }
        double vol = 0;
        if (has_col(s.v)) { std::vector<double> vs; for (const auto& b : bars) vs.push_back(b.v); vol = fsum(vs); }
        else vol = to_float(orv(dget(s.meta, "regularMarketVolume"), Val::integer(0)));
        if (bars.back().c < min_price || vol < static_cast<double>(min_volume)) { bump(o.counts, "illiquid") += 1; continue; }
        bump(o.counts, "scanned") += 1;
        if (orb_candidate(bars, n_range)) cands.push_back({sym, bars, bars.back().c * vol});
    }
    bump(o.counts, "candidates") = static_cast<long long>(cands.size());
    if (confirm && max_confirm && static_cast<long long>(cands.size()) > max_confirm) {
        std::stable_sort(cands.begin(), cands.end(), [](const C& a, const C& b) { return -a.traded < -b.traded; });
        bump(o.counts, "unconfirmed") = static_cast<long long>(cands.size()) - max_confirm;
        cands.resize(static_cast<std::size_t>(max_confirm));
    }
    std::vector<Hit> hits;
    for (const auto& cd : cands) {
        std::vector<Bar> full = cd.bars;
        if (confirm) {
            Series s2;
            try { s2 = confirm(cd.sym); } catch (const ScanError& e) { o.failed[cd.sym] = e.what(); continue; }
            full = session_bars(s2, day, cutoff);
            if (!usable(full, day, cutoff, static_cast<long long>(n_range))) { o.failed[cd.sym] = "the v8 chart has a missing bar"; continue; }
        }
        bump(o.counts, "confirmed") += 1;
        if (auto h = orb_hit(full, n_range, cd.sym)) hits.push_back(*h);
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.t != b.t ? a.t < b.t : a.sym < b.sym; });
    for (auto& h : hits) o.hits.push_back(h.h);
    bump(o.counts, "hits") = static_cast<long long>(o.hits.size());
    return o;
}

// ---------------------------------------------------------------- fetching and files
inline void progress(const Val& a, const std::string& msg) { if (!B(a, "json") && !(has(a, "quiet") && B(a, "quiet"))) eprint(msg); }
inline std::string commas_i(long long n) { return commas(std::to_string(n)); }
inline std::pair<Data, Errs> fetch_spark(const std::vector<std::string>& symbols, const std::string& rng, const std::string& interval, double pause, const Val* a) {
    Data data; Errs failed;
    std::vector<std::vector<std::string>> batches;
    for (std::size_t i = 0; i < symbols.size(); i += SPARK_MAX) batches.emplace_back(symbols.begin() + static_cast<long>(i), symbols.begin() + static_cast<long>(std::min(symbols.size(), i + SPARK_MAX)));
    for (std::size_t k = 0; k < batches.size(); ++k) {
        const auto& batch = batches[k];
        if (k && pause) dy::pause(pause);
        std::map<std::string, std::string> wanted; for (const auto& s : batch) wanted[to_yahoo(s)] = s;
        Data got;
        try { got = parse_spark(yahoo_get(spark_path(batch, rng, interval)), wanted); }
        catch (const ScanError& e) { for (const auto& s : batch) failed[s] = e.what(); continue; }
        catch (const PyErr& e) { for (const auto& s : batch) failed[s] = e.what(); continue; }
        for (const auto& s : batch) {
            const auto it = got.find(s);
            if (it != got.end()) data[s] = it->second;
            else failed[s] = "no data from Yahoo";
        }
        if (a && batches.size() > 25 && (k + 1) % 50 == 0) progress(*a, "  spark " + std::to_string(k + 1) + " of " + std::to_string(batches.size()) + " requests");
    }
    return {data, failed};
}
inline std::vector<std::string> symbols_from_file(const std::string& path) {
    if (!exists(path)) throw ScanError("no file at " + path);
    const std::string text = read_text(path);
    std::vector<std::string> out;
    const std::string ls = strip(text);
    if (starts(ls, "{") || starts(ls, "[")) {
        Val doc;
        try { doc = loads(text); } catch (const PyErr& e) { throw ScanError(path + " is not valid JSON: " + e.what()); }
        const Val items = doc.t == Val::DICT ? (has(doc, "hits") ? get(doc, "hits") : Val::list()) : doc;
        if (items.t == Val::LIST)
            for (const Val& x : *items.l) if (truthy(x)) out.push_back(upper(pystr(x.t == Val::DICT ? dget(x, "symbol") : x)));
        return out;
    }
    for (const auto& raw : splitlines(text)) {
        const std::string ln = strip(raw);
        if (!ln.empty() && ln[0] != '#') out.push_back(upper(strip(split(ln, "|")[0])));
    }
    return out;
}
inline std::vector<std::string> read_symbols(const Val& a, const std::optional<std::vector<std::string>>& offline_syms) {
    std::vector<std::string> syms;
    if (truthy(get(a, "symbols"))) {
        static const std::regex SEP(R"([,\s]+)");
        const std::string s = upper(S(a, "symbols"));
        for (auto it = std::sregex_token_iterator(s.begin(), s.end(), SEP, -1); it != std::sregex_token_iterator(); ++it) if (it->length()) syms.push_back(it->str());
    } else if (truthy(get(a, "watchlist"))) {
        syms = symbols_from_file(S(a, "watchlist"));
    } else if (offline_syms && !truthy(get(a, "universe"))) {
        syms = *offline_syms;   // offline: every chart in the file, unless a list was given
    } else {
        const std::string path = truthy(get(a, "universe")) ? S(a, "universe") : DEFAULT_UNIVERSE;
        if (!exists(path)) throw ScanError("no " + path + " yet: run `scan universe` first, or pass --symbols");
        syms = symbols_from_file(path);
    }
    std::vector<std::string> out;
    for (const auto& s : syms) if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
    const Val lim = get(a, "limit");
    if (truthy(lim)) {
        const long long n = lim.i, sz = static_cast<long long>(out.size());
        const long long end = n >= 0 ? std::min(n, sz) : std::max(0LL, sz + n);
        out.resize(static_cast<std::size_t>(end));
    }
    if (out.empty()) throw ScanError("no symbols to scan");
    return out;
}
inline Val read_config(const std::string& path) {
    if (path.empty() || !exists(path)) return Val::dict();
    Val cfg;
    try { cfg = loads(read_text(path)); } catch (const PyErr& e) { throw ScanError(path + " is not valid JSON: " + e.what()); }
    return cfg.t == Val::DICT ? cfg : Val::dict();
}
inline void write_json(const std::string& path, const Val& doc) {
    const std::string d = dirname(path);
    if (!d.empty()) makedirs(d);
    DumpOpts o; o.indent = 2;
    write_text(path, dumps(doc, o) + "\n");
}
inline std::string G(const Val& d, const char* k) { const Val v = get(d, k); return v.t == Val::STR ? v.s : pystr(v); }
inline std::string money(double x) { return "$" + fmt_fc(x, 2); }
inline std::string n_(double x) { return x == std::trunc(x) ? std::to_string(static_cast<long long>(x)) : fmt_f(x, 2); }
inline std::string label_of(const std::string& k) { for (const auto& s : SETUPS) if (s.first == k) return s.second; return k; }
inline std::string utc_z(long long t) { long long y, us; unsigned mo, d; int h, mi, s; split_us(t, y, mo, d, h, mi, s, us); char b[40]; std::snprintf(b, sizeof b, "%04lld-%02u-%02uT%02d:%02d:%02dZ", y, mo, d, h, mi, s); return b; }
inline Val counts_val(const Counts& c) { Val v = Val::dict(); for (const auto& kv : c) v.d->set(kv.first, Val::integer(kv.second)); return v; }
inline Val seconds_since(std::chrono::steady_clock::time_point t0) {
    return Val::flt(round_n(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), 1));
}
inline std::string rpad(const std::string& s, std::size_t w) { const std::size_t n = cplen(s); return n >= w ? s : s + std::string(w - n, ' '); }
inline std::string lpad(const std::string& s, std::size_t w) { const std::size_t n = cplen(s); return n >= w ? s : std::string(w - n, ' ') + s; }
inline std::string plural(std::size_t n) { return n == 1 ? "" : "s"; }
inline std::string offline_name(const std::string& p) { std::string s = p; while (!s.empty() && s.back() == '/') s.pop_back(); return basename(s); }
inline std::vector<std::string> keys_of(const Data& d, const Errs& e) {
    std::set<std::string> k; for (const auto& kv : d) k.insert(kv.first); for (const auto& kv : e) k.insert(kv.first);
    return {k.begin(), k.end()};
}

// ---------------------------------------------------------------- commands
inline int cmd_universe(const Val& a) {
    const auto t0 = std::chrono::steady_clock::now();
    std::map<std::string, std::string> texts;
    int k = 0;
    for (const char* fname : NASDAQ_FILES) {
        if (truthy(get(a, "offline"))) {
            const std::string p = path_join(S(a, "offline"), fname);
            if (!exists(p)) throw ScanError(std::string("no ") + fname + " in " + S(a, "offline"));
            std::string t = decode_utf8_replace(read_bytes(p)), u; u.reserve(t.size());
            for (std::size_t i = 0; i < t.size(); ++i) {   // universal newlines, like open() in text mode
                if (t[i] != '\r') { u += t[i]; continue; }
                u += '\n';
                if (i + 1 < t.size() && t[i + 1] == '\n') ++i;
            }
            texts[fname] = u;
        } else {
            if (k && truthy(get(a, "pause"))) dy::pause(to_float(get(a, "pause")));
            const HttpResp r = http_get(std::string(NASDAQ_URL) + fname, {std::string("User-Agent: ") + dy::UA, "Accept: */*"}, 60);
            if (r.status >= 400) throw ScanError("nasdaqtrader.com answered HTTP " + std::to_string(r.status) + " for " + fname);
            if (r.status == 0 || !r.error.empty()) throw ScanError("could not reach nasdaqtrader.com: " + (r.error.empty() ? std::string("no response") : r.error));
            texts[fname] = decode_utf8_replace(r.body);
        }
        ++k;
    }
    const Universe u = build_universe(texts);
    const bool no_write = B(a, "no_write");
    const std::string outp = S(a, "out");
    if (!no_write) {
        const std::string d = dirname(outp);
        if (!d.empty()) makedirs(d);
        std::string body; for (const auto& [sym, name] : u.kept) body += sym + "|" + name + "\n";
        write_text(outp, body);
    }
    Counts dropped; for (const auto& kv : u.counts) if (kv.first != "keep" && kv.first != "rows") dropped.push_back(kv);
    if (B(a, "json")) {
        Val d = Val::dict();
        d.d->set("count", Val::integer(static_cast<long long>(u.kept.size()))); d.d->set("rows", Val::integer(cget(u.counts, "rows")));
        d.d->set("files_created", u.created ? Val::str(*u.created) : Val()); d.d->set("dropped", counts_val(dropped));
        d.d->set("out", no_write ? Val() : Val::str(outp)); d.d->set("seconds", seconds_since(t0));
        DumpOpts o; o.indent = 2; print(dumps(d, o));
    } else {
        print("Nasdaq Trader symbol directories" + (u.created ? ", files of " + *u.created + " ET" : std::string()) + ": " + commas_i(cget(u.counts, "rows")) + " rows.");
        print("Kept " + commas_i(static_cast<long long>(u.kept.size())) + " US-listed common stocks, ordinary shares and ADRs.");
        Counts sorted = dropped;
        std::stable_sort(sorted.begin(), sorted.end(), [](const auto& x, const auto& y) { return -x.second < -y.second; });
        std::vector<std::string> parts; for (const auto& kv : sorted) parts.push_back(commas_i(kv.second) + " " + kv.first);
        print("Dropped: " + join(parts, ", ") + ".");
        if (!no_write) print("Wrote " + outp + " (one SYMBOL|Name a line).");
    }
    return u.kept.empty() ? 1 : 0;
}
inline Val base_doc(const char* mode, long long now, const std::optional<std::string>& as_of, std::size_t universe, long long scanned, long long nfailed) {
    Val doc = Val::dict();
    doc.d->set("created_by", Val::str("trader-scanner")); doc.d->set("created_at", Val::str(utc_z(now))); doc.d->set("mode", Val::str(mode));
    doc.d->set("as_of", as_of ? Val::str(*as_of) : Val()); doc.d->set("universe", Val::integer(static_cast<long long>(universe)));
    doc.d->set("scanned", Val::integer(scanned)); doc.d->set("failed", Val::integer(nfailed));
    return doc;
}
inline Val filters(const Val& a) { Val f = Val::dict(); f.d->set("min_price", Val::flt(to_float(get(a, "min_price")))); f.d->set("min_volume", get(a, "min_volume")); return f; }
inline Val list_of(const std::vector<Val>& v) { Val l = Val::list(); for (const auto& x : v) l.l->push_back(x); return l; }
inline int cmd_daily(const Val& a) {
    const auto t0 = std::chrono::steady_clock::now();
    const long long now = parse_now(get(a, "now"));
    const double min_price = to_float(get(a, "min_price")); const long long min_volume = get(a, "min_volume").i;
    Data data; Errs failed; std::optional<std::vector<std::string>> offline_syms;
    const bool offline = truthy(get(a, "offline"));
    if (offline) { std::tie(data, failed) = load_offline(S(a, "offline")); offline_syms = keys_of(data, failed); }
    const auto symbols = read_symbols(a, offline_syms);
    if (!offline) {
        progress(a, "Fetching daily closes for " + commas_i(static_cast<long long>(symbols.size())) + " symbols (" + std::to_string((symbols.size() + SPARK_MAX - 1) / SPARK_MAX) + " spark requests)...");
        std::tie(data, failed) = fetch_spark(symbols, "3mo", "1d", to_float(get(a, "pause")), &a);
    }
    const DailyOut r = scan_daily(data, symbols, now, min_price, min_volume);
    const long long fetched = cget(r.counts, "fetched"), nfailed = static_cast<long long>(symbols.size()) - fetched;
    const std::optional<std::string> as_of = r.last_date ? std::optional<std::string>(day_iso(*r.last_date) + " close") : std::nullopt;
    Val doc = base_doc("daily", now, as_of, symbols.size(), cget(r.counts, "scanned"), nfailed);
    doc.d->set("source", Val::str("yahoo")); doc.d->set("filters", filters(a)); doc.d->set("note", Val::str(NOTE)); doc.d->set("hits", list_of(r.hits));
    if (offline) doc.d->set("offline", Val::str(offline_name(S(a, "offline"))));
    const bool no_write = B(a, "no_write"); const std::string outp = S(a, "out");
    if (!no_write) write_json(outp, doc);
    const int rc = r.hits.empty() ? 1 : 0;
    if (B(a, "json")) {
        Val d = doc;   /* doc is not used again */ d.d->set("counts", counts_val(r.counts)); d.d->set("seconds", seconds_since(t0));
        DumpOpts o; o.indent = 2; print(dumps(d, o)); return rc;
    }
    const long long local = to_eastern(now);
    print("Daily scan, " + day_label(day_of(local)) + " " + hm_(local) + " ET, closes through " + (r.last_date ? day_iso(*r.last_date) : std::string("nothing")) + " (Yahoo spark, daily closes only).");
    const long long shrt = cget(r.counts, "short");
    print("Fetched " + commas_i(fetched) + " of " + commas_i(static_cast<long long>(symbols.size())) + " charts" +
          (nfailed ? " (" + commas_i(nfailed) + " failed or had no data)" : std::string()) + ". " + commas_i(fetched - cget(r.counts, "illiquid")) +
          " passed the liquidity filter ($" + n_(min_price) + "+ and " + commas_i(min_volume) + "+ shares)" +
          (shrt ? ", " + std::to_string(shrt) + " had under " + std::to_string(MIN_DAILY_BARS) + " days" : std::string()) + "; " + commas_i(cget(r.counts, "scanned")) + " scanned.");
    if (!r.hits.empty()) {
        std::vector<std::string> per;
        for (const auto& [k, lbl] : SETUPS) {
            long long n = 0;
            for (const Val& h : r.hits) for (const Val& t : *get(h, "setups").l) if (t.s == k) ++n;
            if (n) per.push_back(std::to_string(n) + " " + lbl);
        }
        print(std::to_string(r.hits.size()) + " chart" + plural(r.hits.size()) + " matched a setup: " + join(per, ", ") + ".");
        print("Most traded first (price x volume):");
    } else {
        print("No setups.");
    }
    const long long show = get(a, "show").i;
    const long long nh = static_cast<long long>(r.hits.size());
    const long long upto = show >= 0 ? std::min(show, nh) : std::max(0LL, nh + show);
    for (long long i = 0; i < upto; ++i) {
        const Val& h = r.hits[static_cast<std::size_t>(i)];
        std::vector<std::string> lbls; for (const Val& t : *get(h, "setups").l) lbls.push_back(label_of(t.s));
        print("  " + rpad(G(h, "symbol"), 6) + " " + lpad(money(to_float(get(h, "price"))), 10) + "  RSI " + lpad(fmt_f(to_float(get(h, "rsi")), 1), 5) + "  " + join(lbls, ", "));
    }
    if (nh > show) print("  ... and " + std::to_string(nh - show) + " more in the watchlist (--show N for more).");
    if (!no_write) print("Wrote " + outp + " (mode daily, " + std::to_string(nh) + " hit" + plural(r.hits.size()) + ").");
    print(std::string(NOTE) + " Next: pick one chart, then /trader-news TICKER.");
    return rc;
}
inline int cmd_orb(const Val& a) {
    const auto t0 = std::chrono::steady_clock::now();
    const long long now = parse_now(get(a, "now"));
    const std::optional<int> at = truthy(get(a, "at")) ? std::optional<int>(parse_hhmm(S(a, "at"))) : std::nullopt;
    const Val cfg = read_config(S(a, "config"));
    const Val until_s = orv(get(a, "until"), orv(dget(cfg, "no_entries_after"), Val::str("15:30")));
    const int until = parse_hhmm(pystr(until_s), "--until");
    const long long or_minutes = get(a, "or_minutes").i, max_confirm = get(a, "max_confirm").i;
    const double min_price = to_float(get(a, "min_price")); const long long min_volume = get(a, "min_volume").i;
    Data data; Errs failed; std::optional<long long> explicit_day; std::optional<std::vector<std::string>> offline_syms;
    const bool offline = truthy(get(a, "offline"));
    if (offline) {
        std::tie(data, failed) = load_offline(S(a, "offline"));
        offline_syms = keys_of(data, failed);
        if (!truthy(get(a, "now"))) {
            std::optional<long long> mx;
            for (const auto& kv : data) if (!kv.second.t.empty()) { const long long d = day_of(ts_to_eastern(kv.second.t.back())); if (!mx || d > *mx) mx = d; }
            if (mx) {
                explicit_day = mx;
                if (!at) throw ScanError("offline replay: give --at HH:MM or --now \"YYYY-MM-DD HH:MM\"");
            }
        }
    }
    const auto [day, cutoff] = orb_window(now, at, or_minutes, until, explicit_day);
    const auto symbols = read_symbols(a, offline_syms);
    Confirm confirm;
    std::string rng;
    auto done = std::make_shared<long long>(0);
    if (!offline) {
        const long long today = day_of(to_eastern(now_us()));
        if (today - day > 6) throw ScanError("Yahoo's free 5-minute history is short: replay " + day_iso(day) + " with --offline");
        rng = day == today ? "1d" : "5d";
        progress(a, "Stage 1: 5-minute closes for " + commas_i(static_cast<long long>(symbols.size())) + " symbols (" + std::to_string((symbols.size() + SPARK_MAX - 1) / SPARK_MAX) + " spark requests)...");
        std::tie(data, failed) = fetch_spark(symbols, rng, "5m", to_float(get(a, "pause")), &a);
        const double pause = to_float(get(a, "pause"));
        confirm = [&a, rng, pause, done](const std::string& sym) {
            if (pause) dy::pause(pause);
            if (++*done % 50 == 0) progress(a, "  stage 2: " + std::to_string(*done) + " v8 charts so far");
            const std::string text = yahoo_get(chart_path(sym, rng, "5m"));
            return parse_chart(text, sym);
        };
    }
    const OrbOut r = scan_orb(data, symbols, day, cutoff, or_minutes, min_price, min_volume, confirm, max_confirm);
    for (const auto& kv : r.failed) failed[kv.first] = kv.second;
    long long nfailed = 0; for (const auto& s : symbols) if (failed.count(s)) ++nfailed;
    Val doc = base_doc("orb", now, day_iso(day) + " " + hm_(cutoff) + " ET", symbols.size(), cget(r.counts, "scanned"), nfailed);
    doc.d->set("unconfirmed", Val::integer(cget(r.counts, "unconfirmed"))); doc.d->set("source", Val::str("yahoo"));
    doc.d->set("or_minutes", Val::integer(or_minutes)); doc.d->set("interval", Val::str("5m"));
    doc.d->set("filters", filters(a)); doc.d->set("note", Val::str(NOTE)); doc.d->set("hits", list_of(r.hits));
    if (offline) doc.d->set("offline", Val::str(offline_name(S(a, "offline"))));
    const bool no_write = B(a, "no_write"); const std::string outp = S(a, "out");
    if (!no_write) write_json(outp, doc);
    const int rc = r.hits.empty() ? 1 : 0;
    if (B(a, "json")) {
        Val d = doc;   /* doc is not used again */ d.d->set("counts", counts_val(r.counts)); d.d->set("seconds", seconds_since(t0));
        DumpOpts o; o.indent = 2; print(dumps(d, o)); return rc;
    }
    const long long illiquid = cget(r.counts, "illiquid"), unconf = cget(r.counts, "unconfirmed");
    print("Opening-range breakout scan, " + day_label(day) + " " + clock_label(cutoff) + " ET: " + std::to_string(or_minutes) + "-minute range (9:30 to " +
          clock_label(slot(day, or_minutes / BAR_MIN)) + "), 5-minute bars, Yahoo.");
    print("Fetched " + commas_i(cget(r.counts, "fetched")) + " of " + commas_i(static_cast<long long>(symbols.size())) + " charts" +
          (nfailed ? " (" + commas_i(nfailed) + " failed or had no data)" : std::string()) + "; " + commas_i(cget(r.counts, "scanned")) + " usable" +
          (illiquid ? " (" + commas_i(illiquid) + " under the price / volume floor)" : std::string()) + "; " + commas_i(cget(r.counts, "candidates")) +
          " candidates, " + commas_i(cget(r.counts, "confirmed")) + " confirmed with real highs and lows.");
    if (unconf) print("PARTIAL: " + commas_i(unconf) + " more candidates were not checked (only the " + commas_i(max_confirm) + " most traded are; --max-confirm 0 checks all).");
    const long long show = get(a, "show").i, nh = static_cast<long long>(r.hits.size());
    if (nh) {
        print(std::to_string(nh) + " of " + (unconf ? "the " + commas_i(cget(r.counts, "confirmed")) + " checked" : commas_i(cget(r.counts, "scanned"))) +
              " charts closed above their opening-range high by " + clock_label(cutoff) + ":");
        const long long upto = show >= 0 ? std::min(show, nh) : std::max(0LL, nh + show);
        for (long long i = 0; i < upto; ++i) {
            const Val& h = r.hits[static_cast<std::size_t>(i)];
            print("  " + lpad(G(h, "at"), 8) + "  " + rpad(G(h, "symbol"), 6) + " " + lpad(money(to_float(get(h, "price"))), 10) + "  range " +
                  fmt_fc(to_float(get(h, "or_low")), 2) + " to " + fmt_fc(to_float(get(h, "or_high")), 2) + "  " + fmt_fs(to_float(get(h, "pct_over")), 2) + "% over");
        }
        if (nh > show) print("  ... and " + std::to_string(nh - show) + " more in the watchlist (--show N for more).");
    } else {
        print("No chart closed above its opening-range high by " + clock_label(cutoff) + ".");
    }
    if (!no_write) print("Wrote " + outp + " (mode orb, " + std::to_string(nh) + " hit" + plural(r.hits.size()) + ").");
    print(std::string(NOTE) + " Next: pick one chart, then /trader-news TICKER and /trader-entry TICKER.");
    return rc;
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("scan", "Chart Scanner (trader-scanner): daily setups and opening-range breakouts.");
    p.subcommands("cmd", false);
    { Parser& s = p.sub("universe"); s.help_line = "download Nasdaq Trader's lists -> trader/universe.txt";
      s.opt({"--out"}).def(DEFAULT_UNIVERSE); s.opt({"--offline"}).metavar("DIR").help("a folder with nasdaqlisted.txt and otherlisted.txt");
      s.opt({"--no-write"}).store_true(); s.opt({"--pause"}).number().def_float(PAUSE); s.opt({"--json"}).store_true(); }
    const auto common = [](Parser& s, double min_price, long long min_volume) {
        s.opt({"--symbols"}).help("comma-separated tickers instead of the universe");
        s.opt({"--watchlist"}).metavar("FILE").help("a watchlist.json (hits) or a text file of tickers");
        s.exclusive({"symbols", "watchlist"});
        s.opt({"--universe"}).help(std::string("symbol list, one SYMBOL|Name a line (default ") + DEFAULT_UNIVERSE + ")");
        s.opt({"--limit"}).integer().help("scan only the first N symbols");
        s.opt({"--min-price"}).number().def_float(min_price); s.opt({"--min-volume"}).integer().def(min_volume);
        s.opt({"--offline"}).metavar("PATH").help("fixture folder or file instead of the network");
        s.opt({"--now"}).help("pretend it is this time (\"2026-10-19 10:15\" New York, or ...Z)");
        s.opt({"--out"}).def("trader/watchlist.json"); s.opt({"--no-write"}).store_true();
        s.opt({"--pause"}).number().def_float(PAUSE).help("seconds between requests (0.3)");
        s.opt({"--show"}).integer().def(50).help("hits to print (all are written)");
        s.opt({"--quiet"}).store_true(); s.opt({"--json"}).store_true();
    };
    { Parser& s = p.sub("daily"); s.help_line = "pre-market setups from daily closes"; common(s, 5.0, 1000000); }
    { Parser& s = p.sub("orb"); s.help_line = "opening-range breakouts so far today"; common(s, 5.0, 0);
      s.opt({"--or-minutes"}).integer().def(15);
      s.opt({"--max-confirm"}).integer().def(MAX_CONFIRM).help("v8 charts to fetch in stage 2, most traded first (200; 0 = all)");
      s.opt({"--at"}).help("cut the clock at this New York time, HH:MM");
      s.opt({"--until"}).help("no new entries after HH:MM (default: config no_entries_after, 15:30)");
      s.opt({"--config"}).def("trader/config.json"); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { out(ap::help_text(ap::norm(p, p.prog))); return 1; }
    try {
        if (cmd == "universe") return cmd_universe(a);
        if (cmd == "daily") return cmd_daily(a);
        return cmd_orb(a);
    } catch (const ScanError& e) { eprint(std::string("scan: ") + e.what()); return 1; }
}
PACKTOOL("daytrade-scanner/scan", main);
}  // namespace packtools::daytrade_scanner
