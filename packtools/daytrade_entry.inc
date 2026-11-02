// daytrade-entry/entry - the Entry Timer: watches a setup bar by bar and says the moment to get in. orb (the first
// completed bar after the opening range that CLOSES above the range high) or level (the first close above a price),
// optional volume confirmation, only on-grid bars that have closed (Yahoo's live tick is dropped, a bar Yahoo has not
// caught up on waits up to a minute), no entries before/after the config times (the cutoff moves up with a 1:00 p.m.
// early close). check, watch (simulated clock with --now/--offline) and watchlist. It never places an order. C++ port of
// day-trader-skill/trader-entry/entry.py (Jake Schincariol, MIT); same rules, files, output and exit codes.
namespace packtools::daytrade_entry {
using namespace pyrt;
struct EntryError : std::runtime_error { using std::runtime_error::runtime_error; };
namespace da = packtools::daytrade_alerts;
namespace dx = packtools::daytrade_exit;
namespace dy = packtools::daytrade_yahoo;
using da::to_eastern; using da::eastern_to_utc; using da::day_of; using da::day_iso; using da::session; using da::trading_day;
static const long long US = 1000000, MIN_US = 60 * US, DAY_US = 86400 * US;
static const double PAUSE = 0.5, BACKOFF = 5.0;
static const long long GRACE = 2, SETTLE = 60;
static const int OPEN_M = 9 * 60 + 30, CLOSE_M = 16 * 60;

// ---------------------------------------------------------------- time
inline long long parse_now(const Val& text) { try { return dx::parse_now(text); } catch (const dx::PlanError& e) { throw EntryError(e.what()); } }
inline int parse_hhmm(const Val& text, const std::string& what) {
    try { return dx::parse_hhmm(text, what); } catch (const dx::PlanError&) { throw EntryError(what + " wants a New York time like 15:30, not " + repr(text)); }
}
inline int mod_(long long local) { return static_cast<int>((local - day_of(local) * DAY_US) / MIN_US); }   // minute of the day
inline std::string hm(long long local) { return dx::hm(mod_(local)); }          // 10:10, 09:45
inline std::string short_(long long local) { const int m = mod_(local); char b[16]; std::snprintf(b, sizeof b, "%d:%02d", m / 60, m % 60); return b; }
inline std::string short_m(int m) { char b[16]; std::snprintf(b, sizeof b, "%d:%02d", m / 60, m % 60); return b; }
inline std::string ampm(long long local) { return dx::ampm(mod_(local)); }
inline std::string hms(long long local) {
    const long long s = (local - day_of(local) * DAY_US) / US; char b[64];
    std::snprintf(b, sizeof b, "%02lld:%02lld:%02lld", s / 3600, s / 60 % 60, s % 60); return b;
}
inline std::string ymd_hms(long long local) { return day_iso(day_of(local)) + " " + hms(local); }
inline long long ts_of(long long utc) { return utc >= 0 ? utc / US : -((-utc) / US); }   // int(total_seconds())
inline std::optional<std::string> next_open(long long d) {
    long long nd = d + 1;
    for (int k = 0; k < 15; ++k, ++nd) if (session(nd)) return day_iso(nd) + " 09:30";
    return std::nullopt;
}

// ---------------------------------------------------------------- Yahoo
struct Bar { long long t = 0; double o = 0, h = 0, l = 0, c = 0; long long v = 0; long long et = 0, end = 0, slot = 0; };
struct Chart { std::string symbol; Val meta; std::vector<Bar> bars; Val prev_close, data_time; };
inline std::string yahoo_symbol(const std::string& sym) { return replace_all(upper(sym), ".", "-"); }
inline std::string chart_path(const std::string& sym, const std::string& rng, const std::string& interval) {
    return "/v8/finance/chart/" + yahoo_symbol(sym) + "?range=" + rng + "&interval=" + interval;
}
inline std::string chart_range(long long day, long long today, const std::string& interval) {   // the smallest range that holds `day`
    const long long back = today - day;
    if (back <= 0) return "1d";
    if (back <= 6) return "5d";
    if (interval == "1m") throw EntryError("Yahoo keeps 1-minute bars for about a week: use --interval 5m for " + day_iso(day));
    if (back <= 28) return "1mo";
    if (back <= 58) return "60d";
    throw EntryError("Yahoo keeps 5-minute bars for about 60 days: " + day_iso(day) + " is too far back");
}
// Yahoo chart JSON v8 -> bars; bars with a null close are dropped here, off-grid ticks later
inline Chart parse_chart(const std::string& text, const std::string& sym) {
    Val data;
    try { data = loads(text); } catch (const std::exception&) { throw EntryError("Yahoo returned something unexpected for " + sym); }
    const Val chart = data.t == Val::DICT ? get(data, "chart") : Val();
    if (data.t != Val::DICT || !has(data, "chart") || chart.t != Val::DICT || !has(chart, "result")) throw EntryError("Yahoo returned something unexpected for " + sym);
    const Val res = get(chart, "result");
    if (!truthy(res)) {
        const Val err = orv(dget(chart, "error"), Val::dict());
        throw EntryError("Yahoo has no data for " + sym + ": " + pystr(orv(dget(err, "description"), Val::str("unknown ticker"))));
    }
    const Val r = res.t == Val::LIST && !res.l->empty() ? orv((*res.l)[0], Val::dict()) : Val::dict();
    const Val meta = orv(dget(r, "meta"), Val::dict());
    const Val quotes = dget(orv(dget(r, "indicators"), Val::dict()), "quote");
    const Val q = quotes.t == Val::LIST && !quotes.l->empty() ? orv((*quotes.l)[0], Val::dict()) : Val::dict();
    const auto at = [&](const char* key, std::size_t i) -> Val {
        const Val arr = orv(dget(q, key), Val::list());
        return arr.t == Val::LIST && i < arr.l->size() ? (*arr.l)[i] : Val();
    };
    Chart ch; ch.symbol = upper(pystr(orv(dget(meta, "symbol"), Val::str(sym)))); ch.meta = meta;
    ch.prev_close = orv(dget(meta, "previousClose"), dget(meta, "chartPreviousClose")); ch.data_time = dget(meta, "regularMarketTime");
    const Val ts = orv(dget(r, "timestamp"), Val::list());
    if (ts.t == Val::LIST)
        for (std::size_t i = 0; i < ts.l->size(); ++i) {
            const Val t = (*ts.l)[i], c = at("close", i);
            if (t.is_none() || c.is_none()) continue;
            const Val o = at("open", i), h = at("high", i), lo = at("low", i), v = at("volume", i);
            Bar b; b.t = to_int(t); b.o = to_float(o.is_none() ? c : o); b.h = to_float(h.is_none() ? c : h); b.l = to_float(lo.is_none() ? c : lo);
            b.c = to_float(c); b.v = truthy(v) ? to_int(v) : 0;
            ch.bars.push_back(b);
        }
    return ch;
}
inline std::string offline_file(const std::string& path, const std::string& sym) {
    if (is_dir(path)) {
        for (const std::string& name : {sym + ".json", sym + ".json.gz", yahoo_symbol(sym) + ".json", yahoo_symbol(sym) + ".json.gz"}) {
            const std::string p = path_join(path, name);
            if (exists(p)) return p;
        }
        throw EntryError("no offline file for " + sym + " in " + path + " (want " + sym + ".json)");
    }
    if (!exists(path)) throw EntryError("no offline file " + path);
    return path;
}
inline long long today_et() { return day_of(to_eastern(dx::utc_now())); }
// bars for one ticker, from Yahoo (range=1d for today) or a saved file
inline Chart get_chart(std::string sym, const std::string& interval, std::optional<long long> day, const std::string& offline, std::optional<long long> today = std::nullopt) {
    sym = upper(sym);
    std::string text;
    if (!offline.empty()) {
        const std::string f = offline_file(offline, sym);
        try { text = dy::read_text_any(f); } catch (const std::exception& e) { throw EntryError("could not read " + f + ": " + e.what()); }
    } else {
        const long long td = today ? *today : today_et();
        text = dy::get<EntryError>(chart_path(sym, chart_range(day ? *day : td, td, interval), interval), BACKOFF);
    }
    Chart ch = parse_chart(text, sym);
    if (ch.symbol != yahoo_symbol(sym)) throw EntryError("the chart file holds " + ch.symbol + ", not " + sym);
    const Val gran = dget(ch.meta, "dataGranularity");
    if (truthy(gran) && !(gran.t == Val::STR && gran.s == interval))
        throw EntryError(sym + " has " + pystr(gran) + " bars, not " + interval + ": use --interval " + pystr(gran));
    return ch;
}

// ---------------------------------------------------------------- the rule
inline Val read_config(const std::string& path) {
    Val cfg = Val::dict(); cfg.d->set("no_entries_before", Val::str("09:45")); cfg.d->set("no_entries_after", Val::str("15:30"));
    if (!path.empty() && exists(path)) {
        Val doc;
        try { doc = loads(read_text(path)); } catch (const PyErr& e) { throw EntryError(path + " is not valid JSON: " + e.what()); }
        for (const char* k : {"no_entries_before", "no_entries_after"}) if (truthy(dget(doc, k))) cfg.d->set(k, dget(doc, k));
    }
    for (const char* k : {"no_entries_before", "no_entries_after"}) parse_hhmm(get(cfg, k), k);
    return cfg;
}
inline Val r2(double x) { return Val::flt(round_n(x, 2) + 0.0); }   // + 0.0 turns -0.0 into 0.0
inline Val r2(std::optional<double> x) { return x ? r2(*x) : Val(); }
// a cutoff on an early-close day moves up with the close; one already before the early close stays
inline long long early_cutoff(long long day, int t, long long close_dt) {
    const long long at = day * DAY_US + t * MIN_US;
    if (at < close_dt) return at;
    const long long moved = at - (day * DAY_US + CLOSE_M * MIN_US - close_dt);
    return moved - day_of(moved) * DAY_US > OPEN_M * MIN_US ? moved : close_dt;
}
struct Opts { std::string setup = "orb"; long long or_minutes = 15; std::string interval = "5m"; Val level; std::optional<double> min_volume_x; Val cfg; };
struct Res { Val d; std::vector<Bar> bars; };
inline Res evaluate(const Chart& chart, long long now_utc, const Opts& op) {
    const Val cfg = truthy(op.cfg) ? op.cfg : read_config("");
    if (op.interval != "5m" && op.interval != "1m") throw EntryError("--interval is 5m or 1m");
    const long long step = op.interval == "1m" ? 60 : 300;
    if (op.or_minutes <= 0 || (op.or_minutes * 60) % step) throw EntryError("--or-minutes must be a whole number of " + op.interval + " bars");
    if (op.setup != "orb" && op.setup != "level") throw EntryError("--setup is orb or level");
    if (op.setup == "level" && op.level.is_none()) throw EntryError("--setup level needs --level PRICE (for example a 20-day high)");
    const bool orb = op.setup == "orb";
    const long long n_or = op.or_minutes * 60 / step;
    const long long local = to_eastern(now_utc), day = day_of(local);
    long long y; unsigned mo, dd; civil_from(day, y, mo, dd);
    Res R; R.d = Val::dict(); auto& r = *R.d.d;
    r.set("symbol", Val::str(chart.symbol)); r.set("setup", Val::str(op.setup)); r.set("side", Val::str("buy")); r.set("date", Val::str(day_iso(day)));
    r.set("interval", Val::str(op.interval)); r.set("or_minutes", Val::integer(op.or_minutes)); r.set("now", Val::str(ymd_hms(local)));
    r.set("status", Val::str("waiting")); r.set("phase", Val::str("")); r.set("reason", Val::str("")); r.set("opening_range", Val()); r.set("level", Val());
    r.set("trigger", Val()); r.set("last", Val()); r.set("forming", Val()); r.set("min_volume_x", op.min_volume_x ? Val::flt(*op.min_volume_x) : Val());
    r.set("prev_close", chart.prev_close); r.set("calendar_known", Val::boolean(y == 2026 || y == 2027));
    const Val dt = chart.data_time;
    r.set("data_time", truthy(dt) ? Val::str(ymd_hms(to_eastern(to_int(dt) * US))) : Val());
    const auto [ok, why] = trading_day(day);
    if (!ok) {
        r.set("status", Val::str("no-data")); r.set("phase", Val::str("closed")); r.set("reason", Val::str("the NYSE is closed today (" + why + ")"));
        const auto no = next_open(day); r.set("next_open", no ? Val::str(*no) : Val());
        return R;
    }
    const auto s = session(day);
    const long long open_dt = day * DAY_US + s->first * MIN_US, close_dt = day * DAY_US + s->second * MIN_US;
    const bool early = s->second != CLOSE_M;
    const long long before = day * DAY_US + parse_hhmm(get(cfg, "no_entries_before"), "no_entries_before") * MIN_US;
    const long long cutoff = early_cutoff(day, parse_hhmm(get(cfg, "no_entries_after"), "no_entries_after"), close_dt);
    const long long or_end = open_dt + op.or_minutes * MIN_US;
    const std::string* early_name = da::lookup(da::NYSE_EARLY_CLOSE, day_iso(day));
    r.set("opens", Val::str(dx::hm(static_cast<int>(s->first)))); r.set("closes", Val::str(dx::hm(static_cast<int>(s->second))));
    r.set("early_close", early_name ? Val::str(*early_name) : Val()); r.set("no_entries_before", Val::str(hm(before))); r.set("no_entries_after", Val::str(hm(cutoff)));

    std::map<long long, Bar> by_slot;
    for (const Bar& b0 : chart.bars) {
        if (b0.t % step) continue;   // Yahoo's live tick (10:49:45) is not a bar
        const long long et = to_eastern(b0.t * US);
        if (day_of(et) != day || !(open_dt <= et && et < close_dt)) continue;
        Bar b = b0; b.slot = (et - open_dt) / US / step; b.et = et; b.end = et + step * US;
        by_slot[b.slot] = b;
    }
    std::vector<Bar> bars; std::optional<Bar> pending;
    const long long now_ts = ts_of(now_utc), top = by_slot.empty() ? -1 : by_slot.rbegin()->first;
    const auto forming = [&](const Bar& b) { Val f = Val::dict(); f.d->set("bar_start", Val::str(hm(b.et))); f.d->set("closes", Val::str(ampm(b.end))); f.d->set("price", r2(b.c)); return f; };
    for (const auto& [slot, b] : by_slot) {
        const long long end_ts = b.t + step;
        if (end_ts <= now_ts) {
            // closed by the clock; on live data it counts once Yahoo has caught up: its last trade is at or after the
            // close, a later bar exists, or a minute has passed
            if (dt.is_none() || to_float(dt) >= static_cast<double>(end_ts) || slot < top || now_ts >= end_ts + SETTLE) {
                bars.push_back(b);
            } else {
                pending = b; r.set("forming", forming(b));
                r.set("lag_note", Val::str("Yahoo's last trade is " + hms(to_eastern(to_int(dt) * US)) + ", before the " + short_(b.et) +
                                           " bar's close: it counts once Yahoo catches up (a minute at most)"));
            }
        } else if (b.et <= local) {
            r.set("forming", forming(b));
        }
    }
    R.bars = bars; r.set("bars_closed", Val::integer(static_cast<long long>(bars.size())));
    const auto set3 = [&](const char* st, const char* ph, const std::string& why_) { if (st) r.set("status", Val::str(st)); r.set("phase", Val::str(ph)); r.set("reason", Val::str(why_)); };
    if (local < open_dt) {
        set3(nullptr, "pre-open", "the NYSE opens at 9:30" + (orb ? " and the " + std::to_string(op.or_minutes) + "-minute range is set at " + short_(or_end) : std::string()));
        return R;
    }
    std::vector<Bar> or_bars; for (const Bar& b : bars) if (b.slot < n_or) or_bars.push_back(b);
    std::optional<double> avg_vol;
    if (!or_bars.empty()) {
        long long sv = 0; double hi = or_bars[0].h, lo = or_bars[0].l;
        for (const Bar& b : or_bars) { sv += b.v; hi = std::max(hi, b.h); lo = std::min(lo, b.l); }
        avg_vol = static_cast<double>(sv) / static_cast<double>(or_bars.size());
        Val o = Val::dict(); o.d->set("high", r2(hi)); o.d->set("low", r2(lo)); o.d->set("minutes", Val::integer(op.or_minutes));
        o.d->set("bars", Val::integer(static_cast<long long>(or_bars.size()))); o.d->set("of", Val::integer(n_or)); o.d->set("complete", Val::boolean(local >= or_end));
        o.d->set("avg_volume", Val::integer(static_cast<long long>(std::nearbyint(*avg_vol)))); r.set("opening_range", o);
    }
    double lvl;
    if (orb) {
        if (local < or_end) { set3(nullptr, "forming", std::to_string(or_bars.size()) + " of " + std::to_string(n_or) + " range bars closed; the range is set at " + short_(or_end)); return R; }
        if (or_bars.empty()) { set3("no-data", "open", "Yahoo sent no bars for the opening range"); return R; }
        lvl = or_bars[0].h; for (const Bar& b : or_bars) lvl = std::max(lvl, b.h);
    } else {
        lvl = to_float(op.level);
    }
    r.set("level", r2(lvl));
    const long long first_slot = orb ? n_or : 0;
    std::optional<Bar> trig; std::set<long long> thin;
    const bool mvx = op.min_volume_x && *op.min_volume_x != 0.0;
    for (const Bar& b : bars) {
        if (b.slot < first_slot || b.end < before || b.end > cutoff) continue;
        if (b.c > lvl) {
            std::optional<double> vx; if (avg_vol && *avg_vol != 0.0) vx = static_cast<double>(b.v) / *avg_vol;
            if (mvx && (!vx || *vx < *op.min_volume_x)) { thin.insert(b.slot); continue; }
            trig = b; break;
        }
    }
    const Bar* last = bars.empty() ? nullptr : &bars.back();
    if (last) {
        Val l = Val::dict(); l.d->set("bar_start", Val::str(hm(last->et))); l.d->set("close", r2(last->c)); l.d->set("vs_level_pct", r2(100.0 * (last->c / lvl - 1)));
        r.set("last", l);
    }
    const std::string lvl_words = orb ? "the range high" : "the level";
    if (trig) {
        std::optional<double> vx; if (avg_vol && *avg_vol != 0.0) vx = static_cast<double>(trig->v) / *avg_vol;
        set3("triggered", local < close_dt ? "open" : "after-close", "the " + short_(trig->et) + " bar closed above " + lvl_words);
        Val t = Val::dict(); auto& td = *t.d;
        td.set("time", Val::str(ampm(trig->end))); td.set("close_time", Val::str(hm(trig->end))); td.set("bar_start", Val::str(hm(trig->et)));
        td.set("bar_index", Val::integer(trig->slot)); td.set("price", r2(trig->c)); td.set("close", Val::flt(trig->c)); td.set("volume", Val::integer(trig->v));
        td.set("volume_x", r2(vx)); td.set("pct_over", r2(100.0 * (trig->c / lvl - 1)));
        r.set("trigger", t);
        r.set("minutes_since", Val::integer(da::floordiv(local - trig->end, MIN_US)));
        if (truthy(dt) && to_int(dt) * US < eastern_to_utc(trig->end))
            r.set("data_note", Val::str("Yahoo's last trade is " + hms(to_eastern(to_int(dt) * US)) + ", before this bar closed: its close can still move a cent or two"));
        return R;
    }
    if (bars.empty()) { set3("no-data", "open", "Yahoo sent no bars for " + day_iso(day) + " yet"); return R; }
    const std::string what = orb ? "close above the range high $" + fmt_f(lvl, 2) : "close above $" + fmt_f(lvl, 2);
    if (local >= cutoff && !(pending && pending->end <= cutoff)) {
        set3("expired", local >= close_dt ? "after-close" : "after-cutoff", "no " + what + " by " + short_(cutoff) + ", the last time for a new entry" + (early ? " (1:00 p.m. early close today)" : ""));
        return R;
    }
    r.set("phase", Val::str("open"));
    std::string why_;
    if (last->c > lvl && thin.count(last->slot)) {
        why_ = "the " + short_(last->et) + " bar closed above at $" + fmt_f(last->c, 2) + " but on " + fmt_f(avg_vol && *avg_vol != 0.0 ? static_cast<double>(last->v) / *avg_vol : 0.0, 2) +
               "x the range's volume, under your " + fmt_f(*op.min_volume_x, 2) + "x";
    } else if (last->c > lvl && last->end < before) {
        why_ = "the " + short_(last->et) + " bar closed above at $" + fmt_f(last->c, 2) + ", but no entries before " + short_(before) + " (config)";
    } else {
        const double gap = lvl - last->c;
        why_ = "last close $" + fmt_f(last->c, 2) + " (" + short_(last->et) + " bar), $" + fmt_f(std::fabs(gap), 2) + " " + (gap >= 0 ? "under" : "over") + " " +
               (orb ? "the range high $" : "the level $") + fmt_f(lvl, 2) + " (" + fmt_fs(100.0 * (last->c / lvl - 1), 2) + "%)";
    }
    r.set("reason", Val::str(why_));
    return R;
}

inline Val public_(const Res& r) { return r.d; }   // the bar list lives outside the dict
inline Val entry_doc(const Res& res, long long now_utc) {   // trader/entry.json, the contract shape (plus a reason)
    const Val t = orv(get(res.d, "trigger"), Val::dict()), orr = get(res.d, "opening_range");
    Val d = Val::dict(); auto& o = *d.d;
    o.set("created_by", Val::str("trader-entry")); o.set("symbol", get(res.d, "symbol")); o.set("setup", get(res.d, "setup")); o.set("side", Val::str("buy"));
    o.set("date", get(res.d, "date")); o.set("status", get(res.d, "status")); o.set("time", dget(t, "time")); o.set("bar_start", dget(t, "bar_start"));
    o.set("bar_index", dget(t, "bar_index")); o.set("price", dget(t, "price")); o.set("level", get(res.d, "level")); o.set("interval", get(res.d, "interval"));
    Val ob;
    if (truthy(orr)) { ob = Val::dict(); ob.d->set("high", get(orr, "high")); ob.d->set("low", get(orr, "low")); ob.d->set("minutes", get(orr, "minutes")); }
    o.set("opening_range", ob); o.set("source", Val::str("yahoo")); o.set("checked_at", Val::str(da::strf(now_utc, true))); o.set("reason", get(res.d, "reason"));
    return d;
}
inline void write_json(const std::string& path, const Val& doc) {
    const std::string d = dirname(path); if (!d.empty()) makedirs(d);
    DumpOpts o; o.indent = 2; write_text(path, dumps(doc, o) + "\n");
}

// ---------------------------------------------------------------- words
inline std::string G(const Val& d, const char* k) { const Val v = get(d, k); return v.t == Val::STR ? v.s : pystr(v); }
inline double Fv(const Val& d, const char* k) { return to_float(get(d, k)); }
inline std::string setup_name(const Val& r) {
    if (G(r, "setup") == "orb") return "opening-range breakout (" + std::to_string(to_int(get(r, "or_minutes"))) + "-minute range)";
    return !get(r, "level").is_none() ? "close above $" + fmt_f(Fv(r, "level"), 2) : "close above a level";
}
inline int hhmm_m(const std::string& s) { return std::stoi(s.substr(0, 2)) * 60 + std::stoi(s.substr(3, 2)); }
inline std::vector<std::string> describe(const Res& R) {
    const Val& r = R.d;
    const long long when = *fromiso_us(G(r, "now"));
    const std::string bars = G(r, "interval") == "5m" ? "5-minute" : "1-minute";
    std::string head = G(r, "symbol") + "  " + dx::strf_day(day_of(when)) + " " + hm(when) + " ET  " + setup_name(r) + " on " + bars + " bars (Yahoo, free, can lag)";
    const Val dtv = dget(r, "data_time");
    if (truthy(dtv) && dtv.s <= G(r, "now") && dtv.s.substr(0, 10) == G(r, "date")) head += ", data to " + dtv.s.substr(11);
    std::vector<std::string> out{head};
    const Val orr = dget(r, "opening_range");
    if (truthy(orr)) {
        const int start = hhmm_m(G(r, "opens")), end = start + static_cast<int>(to_int(get(orr, "minutes")));
        out.push_back("Opening range " + short_m(start) + "-" + short_m(end) + ": high $" + fmt_f(Fv(orr, "high"), 2) + ", low $" + fmt_f(Fv(orr, "low"), 2) + " (" +
                      std::to_string(to_int(get(orr, "bars"))) + " of " + std::to_string(to_int(get(orr, "of"))) + " bars" + (truthy(get(orr, "complete")) ? "" : ", still forming") + ").");
    }
    static const std::vector<std::pair<std::string, std::string>> LABEL = {{"triggered", "TRIGGERED"}, {"waiting", "WAITING"}, {"expired", "EXPIRED"}, {"no-data", "NO DATA"}};
    static const std::vector<std::pair<std::string, std::string>> PHASE = {{"pre-open", "NOT OPEN YET"}, {"forming", "RANGE FORMING"}, {"closed", "MARKET CLOSED"}};
    const std::string status = G(r, "status"), phase = G(r, "phase");
    const std::string* pl = da::lookup(PHASE, phase); const std::string* sl = da::lookup(LABEL, status);
    const std::string label = pl ? *pl : sl ? *sl : upper(status);
    const Val t = dget(r, "trigger");
    const long long step = G(r, "interval") == "1m" ? 1 : 5;
    const long long since = truthy(dget(r, "minutes_since")) ? to_int(get(r, "minutes_since")) : 0;
    if (truthy(t)) {
        out.push_back("TRIGGERED " + G(t, "time") + " at $" + fmt_f(Fv(t, "price"), 2) + ": the " + short_m(hhmm_m(G(t, "bar_start"))) + " bar (bar " + std::to_string(to_int(get(t, "bar_index"))) +
                      ") closed above " + (G(r, "setup") == "orb" ? "the range high" : "the level") + " $" + fmt_f(Fv(r, "level"), 2) + " (" + fmt_fs(Fv(t, "pct_over"), 2) + "%" +
                      (truthy(dget(r, "min_volume_x")) && truthy(dget(t, "volume_x")) ? ", " + fmt_f(Fv(t, "volume_x"), 2) + "x the range's volume" : "") + ").");
        const Val last = dget(r, "last");
        if (since > step && truthy(last))
            out.push_back("That was " + std::to_string(since) + " minutes ago; the last close is $" + fmt_f(Fv(last, "close"), 2) + " (" + G(last, "bar_start") +
                          " bar). A late entry is a different trade: the price, the stop distance and the odds have moved.");
        if (truthy(dget(r, "data_note"))) out.push_back(G(r, "data_note") + ".");
        out.push_back(since > step ? "Nothing was placed." : "This is the moment to get in, if you take the trade. Nothing was placed.");
        out.push_back("Next: set the stop and target BEFORE you buy: /trader-exit (exit plan --from trader/entry.json).");
    } else {
        out.push_back(label + "  " + G(r, "reason") + ".");
        const Val f = dget(r, "forming");
        if (truthy(dget(r, "lag_note")) && status == "waiting") out.push_back(G(r, "lag_note") + ".");
        else if (truthy(f) && status == "waiting" && (phase == "open" || phase == "forming")) {
            std::string bs = G(f, "bar_start"); std::size_t k = 0; while (k < bs.size() && bs[k] == '0') k++;
            out.push_back("The " + bs.substr(k) + " bar is still forming ($" + fmt_f(Fv(f, "price"), 2) + " so far, closes " + G(f, "closes") + "); it does not count until it closes.");
        }
        if (status == "expired") out.push_back("No entry on this setup today. Nothing was placed.");
        if (truthy(dget(r, "next_open"))) out.push_back("Next open: " + G(r, "next_open") + " New York.");
    }
    if (truthy(dget(r, "early_close"))) out.push_back("Early close today at 1:00 p.m. (" + G(r, "early_close") + "): no new entries after " + G(r, "no_entries_after") + ".");
    if (!truthy(dget(r, "calendar_known"))) out.push_back("Holiday list covers 2026 and 2027 only: check nyse.com for this date.");
    return out;
}
inline std::string bar_line(const Bar& b, const Res& R) {   // one watch line for a bar that just closed
    const Val& r = R.d;
    const Val orr = orv(dget(r, "opening_range"), Val::dict()), lvl = dget(r, "level"), t = dget(r, "trigger");
    const long long n_or = truthy(dget(orr, "of")) ? to_int(get(orr, "of")) : 0;
    const bool orb = G(r, "setup") == "orb";
    std::string note;
    if (orb && b.slot < n_or) {
        double lo = 0, hi = 0; bool any = false;
        for (const Bar& x : R.bars) if (x.slot <= b.slot) { lo = any ? std::min(lo, x.l) : x.l; hi = any ? std::max(hi, x.h) : x.h; any = true; }
        note = "range so far $" + fmt_f(lo, 2) + " to $" + fmt_f(hi, 2);
        if (b.slot == n_or - 1) note = "opening range set: high $" + fmt_f(Fv(orr, "high"), 2) + ", low $" + fmt_f(Fv(orr, "low"), 2);
    } else if (truthy(t) && to_int(get(t, "bar_index")) == b.slot) {
        note = "TRIGGERED: closed above $" + fmt_f(to_float(lvl), 2) + " (" + fmt_fs(Fv(t, "pct_over"), 2) + "%). Get in now if you take it: " + G(t, "time") + ", $" + fmt_f(Fv(t, "price"), 2);
    } else if (lvl.is_none()) {
        note = "";
    } else if (b.c > to_float(lvl)) {
        note = "above $" + fmt_f(to_float(lvl), 2);
        if (!orb || b.slot >= n_or) {
            const Val avg = dget(orr, "avg_volume");
            if (truthy(dget(r, "min_volume_x")) && truthy(avg))
                note += ", but volume " + fmt_f(static_cast<double>(b.v) / to_float(avg), 2) + "x is under " + fmt_f(Fv(r, "min_volume_x"), 2) + "x";
            else if (b.end < day_of(b.et) * DAY_US + parse_hhmm(get(r, "no_entries_before"), "x") * MIN_US)
                note += ", but no entries before " + G(r, "no_entries_before");
        }
    } else {
        note = "$" + fmt_f(to_float(lvl) - b.c, 2) + " under $" + fmt_f(to_float(lvl), 2);
    }
    std::string start = short_(b.et); if (start.size() < 5) start.insert(0, 5 - start.size(), ' ');
    return start + " bar  closed $" + fmt_f(b.c, 2) + "  " + note;
}

// ---------------------------------------------------------------- watch
// poll bar by bar until a trigger (0), expiry, the close or --until (1); sleeps in steps of at most `every` seconds and
// re-reads the clock each time, so a computer that dozed off catches up as soon as it wakes
inline std::pair<int, std::optional<Res>> watch_loop(const std::function<Chart(long long)>& provider, const std::function<long long()>& clock,
                                                      const std::function<void(double)>& sleep, const std::function<void(const std::string&)>& emit, const Opts& opts,
                                                      double every = 30, std::optional<long long> until = std::nullopt, int max_errors = 5,
                                                      const std::function<void(const Res&, long long)>& on_trigger = nullptr) {
    const long long step = opts.interval == "1m" ? 60 : 300;
    long long printed = -1; int errors = 0; bool said_preopen = false, first = true;
    std::optional<long long> next_fetch;
    while (true) {
        const long long now = clock();
        if (next_fetch && now < *next_fetch) { sleep(std::max(1.0, std::min(every, static_cast<double>(*next_fetch - now) / US))); continue; }
        const long long local = to_eastern(now), day = day_of(local);
        const auto s = session(day);
        if (!s) { emit(day_iso(day) + ": market closed today (" + trading_day(day).second + "). Nothing to watch."); return {1, std::nullopt}; }
        const long long open_dt = day * DAY_US + s->first * MIN_US, close_dt = day * DAY_US + s->second * MIN_US;
        if (local < open_dt + step * US) {
            if (!said_preopen) {
                emit(hm(local) + " ET  " + (local < open_dt ? "not open yet" : "open, no bar closed yet") + ": watching from the first bar's close at " + short_(open_dt + step * US) + ".");
                said_preopen = true;
            }
            next_fetch = eastern_to_utc(open_dt) + (step + GRACE) * US;
            continue;
        }
        Chart chart;
        try {
            chart = provider(now); errors = 0;
        } catch (const EntryError& e) {
            errors++;
            emit(hms(local) + " ET  could not read bars: " + e.what());
            if (errors >= max_errors) { emit("Stopping after " + std::to_string(errors) + " failed reads in a row."); return {1, std::nullopt}; }
            next_fetch = now + static_cast<long long>(std::nearbyint(every * US));
            continue;
        }
        const Res res = evaluate(chart, now, opts);
        const auto& bars = res.bars;
        const auto lines = describe(res);
        const bool fresh = first;
        const bool trig = truthy(dget(res.d, "trigger"));
        if (first) {
            emit(lines[0]);
            if (lines.size() > 1 && starts(lines[1], "Opening range")) emit(lines[1]);
            if (!bars.empty() && !trig) emit(bar_line(bars.back(), res));
            printed = bars.empty() ? -1 : bars.back().slot;
            first = false;
        } else {
            for (const Bar& b : bars) if (b.slot > printed) { emit(bar_line(b, res)); printed = b.slot; }
        }
        const std::string status = G(res.d, "status");
        if (status == "triggered") {
            for (std::size_t k = 1; k < lines.size(); ++k) {
                if (starts(lines[k], "Opening range") || (starts(lines[k], "TRIGGERED") && !fresh)) continue;
                emit(lines[k]);
            }
            if (on_trigger) on_trigger(res, now);
            return {0, res};
        }
        if (status == "expired" || local >= close_dt) {
            const std::string why = G(res.d, "reason");
            emit("EXPIRED  " + (why.empty() ? std::string("the market closed without a trigger") : why) + ".");
            return {1, res};
        }
        if (until && local >= *until) { emit("Stopped at --until " + short_(*until) + " without a trigger. " + G(res.d, "reason") + "."); return {1, res}; }
        // sleep until just after the next bar closes; poll every `every` seconds if Yahoo is behind
        const long long cur_slot = (local - open_dt) / US / step;
        const bool behind = bars.empty() || bars.back().slot < cur_slot - 1;
        double wait = behind ? every : static_cast<double>(open_dt + ((cur_slot + 1) * step + GRACE) * US - local) / US;
        const Val nea = dget(res.d, "no_entries_after");
        const long long cutoff = day * DAY_US + parse_hhmm(truthy(nea) ? nea : Val::str("15:30"), "cutoff") * MIN_US;
        for (const std::optional<long long>& stop_at : {until, std::optional<long long>(cutoff), std::optional<long long>(close_dt)})
            if (stop_at && *stop_at > local) wait = std::min(wait, static_cast<double>(*stop_at - local) / US);
        next_fetch = now + static_cast<long long>(std::nearbyint(std::max(1.0, wait) * US));
    }
}

// ---------------------------------------------------------------- commands
inline Opts opts_of(const Val& a, const Val& cfg) {
    const std::string side = S(a, "side");
    if (side == "sell" || side == "short") throw EntryError("v2 plans longs only: no short entries. Watch for a buy (--side buy) instead.");
    const Val level = get(a, "level"), mvx = get(a, "min_volume_x");
    if (S(a, "setup") == "orb" && !level.is_none()) throw EntryError("--level is for --setup level (an orb's level is its range high)");
    if (!mvx.is_none() && to_float(mvx) <= 0) throw EntryError("--min-volume-x must be above 0");
    if (!level.is_none() && to_float(level) <= 0) throw EntryError("--level must be a price above 0");
    Opts o; o.setup = S(a, "setup"); o.or_minutes = to_int(get(a, "or_minutes")); o.interval = S(a, "interval"); o.level = level;
    if (!mvx.is_none()) o.min_volume_x = to_float(mvx);
    o.cfg = cfg;
    return o;
}
// (chart, now); an offline file without --now is read as of its own time
inline std::pair<Chart, long long> load_(const Val& a, const std::string& sym, std::optional<long long> now) {
    const long long real_today = today_et();
    const std::string offline = S(a, "offline");
    if (!offline.empty() && !now) {
        Chart ch = get_chart(sym, S(a, "interval"), std::nullopt, offline);
        const long long when = truthy(ch.data_time) ? to_int(ch.data_time) * US : dx::utc_now();
        return {ch, when};
    }
    const long long n = now ? *now : dx::utc_now();
    return {get_chart(sym, S(a, "interval"), day_of(to_eastern(n)), offline, real_today), n};
}
inline std::optional<long long> now_arg(const Val& a) { if (truthy(get(a, "now"))) return parse_now(get(a, "now")); return std::nullopt; }
inline int cmd_check(const Val& a) {
    const Val cfg = read_config(S(a, "config"));
    const Opts opts = opts_of(a, cfg);
    const std::string sym = upper(S(a, "symbol"));
    const auto [chart, now] = load_(a, sym, now_arg(a));
    const Res res = evaluate(chart, now, opts);
    const std::string write = S(a, "write");
    if (!write.empty()) write_json(write, entry_doc(res, now));
    if (B(a, "json")) { DumpOpts o; o.indent = 2; print(dumps(public_(res), o)); }
    else {
        for (const auto& line : describe(res)) print(line);
        if (!write.empty()) print("Wrote " + write + " (status " + G(res.d, "status") + ").");
    }
    return G(res.d, "status") == "triggered" ? 0 : 1;
}
inline int cmd_watch(const Val& a) {
    const Val cfg = read_config(S(a, "config"));
    const Opts opts = opts_of(a, cfg);
    const std::string sym = upper(S(a, "symbol"));
    const double every = to_float(get(a, "every"));
    if (every <= 0) throw EntryError("--every must be above 0 seconds");
    std::optional<long long> start = now_arg(a);
    const std::string offline = S(a, "offline");
    auto sim = std::make_shared<long long>(0);
    std::function<long long()> now_fn; std::function<void(double)> sleep_fn;
    if (!offline.empty() || start) {
        if (!start) start = load_(a, sym, std::nullopt).second;
        *sim = *start;
        now_fn = [sim] { return *sim; };
        sleep_fn = [sim](double sec) { *sim += std::max(1LL, static_cast<long long>(std::nearbyint(sec))) * US; };
    } else {
        now_fn = [] { return dx::utc_now(); };
        sleep_fn = [](double sec) { dy::pause(sec); };
    }
    const long long real_today = today_et();
    const std::string interval = S(a, "interval");
    const auto provider = [&](long long now) { return get_chart(sym, interval, day_of(to_eastern(now)), offline, real_today); };
    std::optional<long long> until;
    if (truthy(get(a, "until"))) until = day_of(to_eastern(now_fn())) * DAY_US + parse_hhmm(get(a, "until"), "--until") * MIN_US;
    const std::string out_path = S(a, "out");
    const auto on_trigger = [&](const Res& res, long long now) { write_json(out_path, entry_doc(res, now)); print("Wrote " + out_path + "."); flush(); };
    const auto emit = [](const std::string& line) { print(line); flush(); };
    return watch_loop(provider, now_fn, sleep_fn, emit, opts, every, until, 5, on_trigger).first;
}
inline std::string lj(std::string s, std::size_t n) { const std::size_t w = cps(s).size(); if (w < n) s.append(n - w, ' '); return s; }
inline int cmd_watchlist(const Val& a) {
    const Val cfg = read_config(S(a, "config"));
    const Opts opts = opts_of(a, cfg);
    const std::string wl = S(a, "watchlist"), offline = S(a, "offline");
    if (!exists(wl)) throw EntryError("no watchlist at " + wl + ": run /trader-scanner first (it writes the hits there)");
    Val doc;
    try { doc = loads(read_text(wl)); } catch (const PyErr& e) { throw EntryError(wl + " is not valid JSON: " + e.what()); }
    std::vector<Val> hits;
    const Val hl = orv(dget(doc, "hits"), Val::list());
    if (hl.t == Val::LIST) for (const Val& h : *hl.l) if (h.t == Val::DICT && truthy(dget(h, "symbol"))) hits.push_back(h);
    if (hits.empty()) throw EntryError("no hits in " + wl + ": the scanner found nothing to watch");
    if (!offline.empty() && !is_dir(offline)) throw EntryError("watchlist --offline wants a folder of <TICKER>.json files");
    const auto now = now_arg(a);
    struct Row { std::string sym; std::optional<Res> res; std::string err; };
    std::vector<Row> rows;
    for (std::size_t i = 0; i < hits.size(); ++i) {
        const std::string sym = upper(pystr(get(hits[i], "symbol")));
        Opts o = opts;
        if (o.setup == "level" && o.level.is_none()) o.level = dget(hits[i], "level");
        try {
            if (o.setup == "level" && o.level.is_none()) throw EntryError("needs a level: --level PRICE or a \"level\" in the watchlist");
            if (i && offline.empty()) dy::pause(PAUSE);
            const auto [chart, when] = load_(a, sym, now);
            rows.push_back({sym, evaluate(chart, when, o), ""});
        } catch (const EntryError& e) { rows.push_back({sym, std::nullopt, e.what()}); }
    }
    int n = 0; for (const auto& row : rows) if (row.res && G(row.res->d, "status") == "triggered") n++;
    if (B(a, "json")) {
        Val l = Val::list();
        for (const auto& row : rows) {
            Val d = Val::dict(); d.d->set("symbol", Val::str(row.sym)); d.d->set("result", row.res ? public_(*row.res) : Val());
            d.d->set("error", row.res ? Val() : Val::str(row.err)); l.l->push_back(d);
        }
        DumpOpts o; o.indent = 2; print(dumps(l, o));
    } else {
        static const std::vector<std::pair<std::string, std::string>> SHORT = {{"pre-open", "not open"}, {"forming", "forming"}, {"closed", "closed"}};
        const auto line = [](const std::vector<std::string>& c) { return lj(c[0], 6) + "  " + lj(c[1], 9) + "  " + lj(c[2], 8) + "  " + lj(c[3], 8) + "  " + lj(c[4], 9) + "  " + lj(c[5], 9) + "  " + c[6]; };
        print(line({"SYMBOL", "STATUS", "TIME", "PRICE", "LEVEL", "LAST", "VS LEVEL"}));
        for (const auto& row : rows) {
            if (!row.res) { print(lj(row.sym, 6) + "  error: " + row.err); continue; }
            const Val& r = row.res->d;
            const Val t = orv(dget(r, "trigger"), Val::dict()), last = orv(dget(r, "last"), Val::dict());
            std::string status = G(r, "status");
            if (status != "triggered") { const std::string* sh = da::lookup(SHORT, G(r, "phase")); if (sh) status = *sh; }
            print(line({row.sym, cpslice(status, 0, 9), truthy(dget(t, "time")) ? G(t, "time") : "-", truthy(t) ? "$" + fmt_f(Fv(t, "price"), 2) : "-",
                        !dget(r, "level").is_none() ? "$" + fmt_f(Fv(r, "level"), 2) : "-", truthy(last) ? "$" + fmt_f(Fv(last, "close"), 2) : "-",
                        !dget(last, "vs_level_pct").is_none() ? fmt_fs(Fv(last, "vs_level_pct"), 2) + "%" : "-"}));
        }
        print(std::to_string(n) + " of " + std::to_string(rows.size()) + " triggered. Pick one, then: entry check <TICKER> --write, and /trader-exit.");
        print("Free Yahoo bars, unofficial and can lag. Nothing was placed.");
    }
    return n ? 0 : 1;
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("entry", "Entry Timer (trader-entry): watches a setup bar by bar.");
    p.subcommands("cmd", false);
    const auto common = [](Parser& s) {
        s.opt({"--setup"}).choices({"orb", "level"}).def("orb"); s.opt({"--or-minutes"}).integer().def(15).help("opening range length (default 15)");
        s.opt({"--interval"}).choices({"5m", "1m"}).def("5m"); s.opt({"--level"}).number().help("the price to close above (--setup level)");
        s.opt({"--min-volume-x"}).number().help("trigger bar volume vs the range bars' average");
        s.opt({"--side"}).def("buy").choices({"buy", "long", "sell", "short"}).help("v2: buy only");
        s.opt({"--now"}).help("pretend it is this New York time, e.g. \"2026-10-19 10:15\"");
        s.opt({"--offline"}).metavar("PATH").help("saved Yahoo chart JSON (.json/.json.gz) or a folder"); s.opt({"--config"}).def("trader/config.json");
    };
    { Parser& s = p.sub("check"); s.help_line = "the setup's status right now"; s.pos("symbol"); common(s);
      s.opt({"--write"}).nargs("?").cnst(Val::str("trader/entry.json")).metavar("PATH").help("save the result (default trader/entry.json)"); s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("watch"); s.help_line = "poll bar by bar until it triggers or expires"; s.pos("symbol"); common(s);
      s.opt({"--every"}).number().def(30).help("seconds between polls while Yahoo is behind"); s.opt({"--until"}).help("stop at this New York time (HH:MM)");
      s.opt({"--out"}).def("trader/entry.json").help("where a trigger is written"); }
    { Parser& s = p.sub("watchlist"); s.help_line = "check every hit in trader/watchlist.json"; common(s);
      s.opt({"--watchlist"}).def("trader/watchlist.json"); s.opt({"--json"}).store_true(); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { out(ap::help_text(ap::norm(p, p.prog))); return 1; }
    try {
        return cmd == "check" ? cmd_check(a) : cmd == "watch" ? cmd_watch(a) : cmd_watchlist(a);
    } catch (const EntryError& e) { eprint(std::string("entry: ") + e.what()); return 1; }
}
PACKTOOL("daytrade-entry/entry", main);
}  // namespace packtools::daytrade_entry
