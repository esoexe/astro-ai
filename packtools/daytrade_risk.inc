// daytrade-risk/check - the day-trader edition of trader-risk: every proposed order gets PASS or BLOCK with the reasons,
// checked in file order as if each PASS had filled. An ENTRY (opens or adds) also needs a stop, a max loss under the
// per-trade cap, enough reward for the risk, room in the daily loss and trade limits, and the entry window (New York
// time, NYSE calendar); an EXIT (only reduces what you hold) is checked for format, asset and short sales only, so
// getting out is always possible. Verdicts go to trader/state/risk.json with a fingerprint of each exact order.
// C++ port of day-trader-skill/trader-risk/check.py (Jake Schincariol, MIT); same output, same exit codes:
// 0 when every order passes, 1 when any is blocked (or on an error). No network.
namespace packtools::daytrade_risk {
using namespace pyrt;
using trader_risk::RiskError; using trader_risk::CENT; using trader_risk::TYPES; using trader_risk::TIFS; using trader_risk::occ; using trader_risk::ticker;
using trader_risk::read_json; using trader_risk::fingerprint; using trader_risk::money; using trader_risk::pct_; using trader_risk::num_; using trader_risk::Acct;
using trader_risk::account; using trader_risk::placed_today;
using trader_alerts::to_eastern; using trader_alerts::eastern_to_utc; using trader_alerts::day_of; using trader_alerts::day_iso; using trader_alerts::weekday; using trader_alerts::strf;
using trader_alerts::trading_day; using trader_alerts::lookup; using trader_alerts::NYSE_EARLY_CLOSE; using trader_alerts::CAL_Y0; using trader_alerts::CAL_Y1;
constexpr long long MIN_US = 60LL * 1000000, DAY_US = 86400LL * 1000000;
constexpr int OPEN_M = 9 * 60 + 30, CLOSE_M = 16 * 60, EARLY_M = 13 * 60, FRESH_MINUTES = 15;
static const char* const PRICE_FIELDS[] = {"qty", "notional", "limit_price", "stop_price", "take_profit_limit_price", "stop_loss_stop_price", "stop_loss_limit_price"};
inline std::string ref_words(const std::string& k) {
    static const std::map<std::string, std::string> m = {{"plan", "the plan's entry"}, {"price", "the latest price"}, {"limit", "the limit price"}, {"stop price", "the stop price"}, {"stale price", "an old price"}};
    const auto it = m.find(k); return it == m.end() ? k : it->second;
}
inline Val defaults() {
    Val d = Val::dict();
    d.d->set("budget", Val()); d.d->set("cash_buffer_pct", Val::integer(10)); d.d->set("max_position_pct", Val::integer(75)); d.d->set("max_order_usd", Val::integer(4000));
    d.d->set("min_order_usd", Val::integer(5)); d.d->set("max_loss_per_trade_usd", Val::integer(50)); d.d->set("max_daily_loss_usd", Val::integer(150)); d.d->set("max_trades_per_day", Val::integer(3));
    d.d->set("max_orders_per_day", Val::integer(10)); d.d->set("min_reward_risk", Val::flt(1.0)); d.d->set("require_stop", Val::boolean(true));
    d.d->set("no_entries_before", Val::str("09:45")); d.d->set("no_entries_after", Val::str("15:30")); d.d->set("flat_by", Val::str("15:55"));
    d.d->set("paper", Val::boolean(true)); d.d->set("blocked", Val::list()); d.d->set("allow_options", Val::boolean(false)); d.d->set("allow_margin", Val::boolean(false));
    d.d->set("allow_short", Val::boolean(false)); d.d->set("pdt_guard", Val::boolean(false));
    return d;
}
// ISO time -> naive UTC; a time without an offset is New York time
inline long long parse_now(const Val& text) {
    if (!truthy(text)) return now_us() / 1000000 * 1000000;
    const std::string t = replace_all(strip(pystr(text)), "Z", "+00:00");
    const auto us = fromiso_us(t);
    if (!us) throw RiskError("--now wants a time like \"2026-10-19 10:15\" (New York) or ...Z (UTC)");
    const std::size_t base = t.size() >= 10 && t[4] == '-' ? 10 : 8;
    const bool aware = t.size() > base + 1 && t.find_first_of("+-", base + 1) != std::string::npos;
    return aware ? *us : eastern_to_utc(*us);
}
inline std::optional<long long> parse_utc(const Val& text) {   // aware -> UTC, naive taken as UTC
    if (!truthy(text)) return std::nullopt;
    return fromiso_us(replace_all(strip(pystr(text)), "Z", "+00:00"));
}
inline std::string hm(int m) { char b[16]; std::snprintf(b, sizeof b, "%02d:%02d", m / 60, m % 60); return b; }
// "09:45" -> minutes; None, "", False (or 0) -> off
inline std::optional<int> hhmm(const Val& value, const char* key) {
    if (value.is_none() || (value.t == Val::STR && value.s.empty()) || ((value.t == Val::BOOL || value.is_num()) && value.num() == 0)) return std::nullopt;
    static const std::regex HHMM(R"(^([01]?[0-9]|2[0-3]):([0-5][0-9])$)");
    std::smatch m; const std::string s = strip(pystr(value));
    if (!std::regex_match(s, m, HHMM)) throw RiskError(std::string(key) + " must be a New York time like \"09:45\", got " + repr(value));
    return std::stoi(m[1].str()) * 60 + std::stoi(m[2].str());
}
// reasons an entry cannot be placed now (New York time); empty when the window is off
inline std::pair<std::vector<std::string>, std::vector<std::string>> window_check(long long now_et, const Val& c) {
    const auto before = hhmm(dget(c, "no_entries_before"), "no_entries_before");
    auto after = hhmm(dget(c, "no_entries_after"), "no_entries_after");
    if (!before && !after) return {};
    std::vector<std::string> warns;
    const long long d = day_of(now_et), tod = now_et - d * DAY_US; const int t = static_cast<int>(tod / MIN_US);
    long long y; unsigned mo, dd; civil_from(d, y, mo, dd);
    if (y != CAL_Y0 && y != CAL_Y1) warns.push_back("the holiday list covers " + std::to_string(CAL_Y0) + " and " + std::to_string(CAL_Y1) + " only: check nyse.com for " + day_iso(d));
    const auto [ok, why] = trading_day(d);
    static const char* DAYS[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    const std::string stamp = std::string(DAYS[weekday(d)]) + " " + strf(now_et, false);
    if (!ok) return {{"window: the market is closed today (" + why + ", " + stamp + " New York)"}, warns};
    const std::string* early = lookup(NYSE_EARLY_CLOSE, day_iso(d));
    const int close = early ? EARLY_M : CLOSE_M;
    if (t < OPEN_M || tod >= close * MIN_US) return {{"window: the market is closed (" + stamp + " New York; the session is " + hm(OPEN_M) + " to " + hm(close) + ")"}, warns};
    if (early && after) { after = ((*after - 180) % 1440 + 1440) % 1440; warns.push_back("early close at 1:00 p.m. today (" + *early + "): no new entries after " + hm(*after)); }
    if (before && t < *before) return {{"window: no new entries before " + hm(*before) + " New York (it is " + hm(t) + ")"}, warns};
    if (after && t > *after) return {{"window: no new entries after " + hm(*after) + " New York (it is " + hm(t) + ")"}, warns};
    return {{}, warns};
}
// the last n NYSE trading days up to and including today, newest first
inline std::vector<std::string> business_days(long long today, int n = 5) {
    std::vector<std::string> out; long long d = today;
    for (int k = 0; k < 40; ++k) { if (trading_day(d).first) { out.push_back(day_iso(d)); if (static_cast<int>(out.size()) == n) break; } d -= 1; }
    return out;
}
inline std::string rec_day(const Val& rec) { const Val d = dget(rec, "date"); return truthy(d) ? pystr(d) : cpslice(pystr(dget(rec, "at", Val::str(""))), 0, 10); }
// trader-broker logs the kind trader-risk gave; older records: a buy is an entry
inline bool rec_is_entry(const Val& rec) {
    const Val k = dget(rec, "kind");
    if (truthy(k)) return k.t == Val::STR && k.s == "entry";
    return lower(pystr(dget(rec, "side", Val::str("")))) == "buy";
}
inline long long iso_day(const std::string& s) {   // date.fromisoformat
    const auto us = s.size() == 10 ? fromiso_us(s) : std::nullopt;
    if (!us) throw PyErr("ValueError", "Invalid isoformat string: " + str_repr(s));
    return day_of(*us);
}
// day trades in the last 5 business days: a (day, symbol) with both a buy and a sell placed or filled that day; an
// entry placed today counts too (it is meant to be flat by the close)
inline std::pair<long long, std::vector<std::string>> day_trades(const Val& state, const std::string& today) {
    const auto bd = business_days(iso_day(today)); const std::set<std::string> days(bd.begin(), bd.end());
    std::map<std::pair<std::string, std::string>, std::set<std::string>> sides;
    for (const auto& rec : iter(dget(orv(state, Val::dict()), "orders", Val::list()))) {
        const Val st = dget(rec, "status");
        if (!(st.t == Val::STR && (st.s == "placed" || st.s == "filled"))) continue;
        const std::string d = rec_day(rec);
        if (!days.count(d)) continue;
        const std::pair<std::string, std::string> key{d, upper(pystr(dget(rec, "symbol", Val::str(""))))};
        sides[key].insert(lower(pystr(dget(rec, "side", Val::str("")))));
        if (d == today && st.s == "placed" && rec_is_entry(rec)) sides[key].insert("pending");
    }
    std::vector<std::string> hits;
    for (const auto& [k, s] : sides) if ((s.count("buy") && s.count("sell")) || (k.first == today && s.count("pending"))) hits.push_back(k.first + " " + k.second);
    std::sort(hits.begin(), hits.end());
    return {static_cast<long long>(hits.size()), hits};
}
inline double cent(double x) { return std::trunc(x * 100 + 0.5 + 1e-9) / 100.0; }   // to the cent, half up (366.475 -> 366.48)
inline std::string p2(double x) { return fmt_f(x, 2); }
inline Val opt(const std::optional<double>& x) { return x ? Val::flt(*x) : Val(); }
using Times = std::optional<std::map<std::string, std::optional<long long>>>;   // None = every price counts as fresh
// now: naive UTC; its New York date is "today"
inline Val check(const List& orders, const Val& cfg, const Val& portfolio, const ODict& prices_in, const Val& state, std::string today, long long now_utc, const Times& price_times) {
    Val c = defaults();
    if (truthy(cfg)) { if (cfg.t != Val::DICT) throw PyErr("ValueError", "dictionary update sequence element #0 has the wrong length"); for (const auto& [k, v] : cfg.d->items) c.d->set(k, v); }
    ODict prices; for (const auto& [k, v] : prices_in.items) if (v != 0) prices[upper(k)] = v;
    const long long now_et = to_eastern(now_utc);
    if (today.empty()) today = day_iso(day_of(now_et));
    std::vector<std::string> top_warn;
    Acct acct = account(c, portfolio, prices, top_warn);
    std::set<std::string> blocked; for (const auto& b : iter(orv(c.d->at("blocked"), Val::list()))) blocked.insert(upper(pystr(b)));
    const List done_today = placed_today(state, today);
    const long long count_today = static_cast<long long>(done_today.size());
    List entries_today; for (const auto& r : done_today) if (rec_is_entry(r)) entries_today.push_back(r);
    const long long n_entries = static_cast<long long>(entries_today.size());
    double loss_today = 0.0;
    for (const auto& r : entries_today) {
        const auto ml = f_(dget(r, "max_loss"));
        if (!ml) top_warn.push_back("an entry placed today (" + pystr(dget(r, "side")) + " " + pystr(dget(r, "symbol")) + ") has no max loss in the log; not counted in the daily loss");
        else loss_today += *ml;
    }
    std::set<std::pair<std::string, std::string>> placed_keys;
    for (const auto& r : done_today) { const Val sd = dget(r, "side"); if (sd.t == Val::STR) placed_keys.insert({upper(pystr(dget(r, "symbol", Val::str("")))), sd.s}); }
    const auto [win_reasons, win_warns] = window_check(now_et, c);
    const bool pdt_guard = truthy(dget(c, "pdt_guard"));
    long long pdt_n = 0; std::vector<std::string> pdt_list;
    if (pdt_guard) std::tie(pdt_n, pdt_list) = day_trades(state, today);
    std::set<std::pair<std::string, std::string>> seen;
    Val results = Val::list(); long long passed_n = 0, batch_entries = 0; double batch_loss = 0.0;
    if (!truthy(c.d->at("paper", Val::boolean(true)))) top_warn.push_back("LIVE account (\"paper\": false): these orders use real money");
    for (const auto& w : win_warns) top_warn.push_back(w);
    const auto fresh = [&](const std::string& sym) -> std::optional<double> {
        if (!prices.has(sym)) return std::nullopt;
        if (!price_times) return prices.get(sym);
        const auto it = price_times->find(sym);
        if (it == price_times->end() || !it->second) return std::nullopt;
        const double age = static_cast<double>(now_utc - *it->second) / 1e6;
        return (-60 <= age && age <= FRESH_MINUTES * 60) ? std::optional<double>(prices.get(sym)) : std::nullopt;
    };
    long long i = 0;
    for (const auto& o : orders) {
        ++i;
        std::vector<std::string> reasons, warns, info, later;   // later: entry-only reasons, warnings for an exit
        const std::string sym = upper(strip(pystr(dget(o, "symbol", Val::str("")))));
        const std::string side = lower(strip(pystr(dget(o, "side", Val::str("")))));
        const std::string typ = lower(strip(pystr(dget(o, "type", Val::str("market")))));
        const std::string tif = lower(strip(pystr(dget(o, "time_in_force", Val::str("day")))));
        const Val qty = dget(o, "qty"), notional = dget(o, "notional");
        const Val plan = dget(o, "plan").t == Val::DICT ? get(o, "plan") : Val::dict();
        const std::string oc = lower(strip(pystr(orv(dget(o, "order_class"), Val::str("")))));
        const bool legs = !dget(o, "take_profit_limit_price").is_none() || !dget(o, "stop_loss_stop_price").is_none();
        const bool bracket = oc == "bracket" || (oc.empty() && legs);
        std::optional<double> value;
        // format
        if (sym.empty()) reasons.push_back("format: no symbol");
        if (side != "buy" && side != "sell") reasons.push_back("format: side must be buy or sell, got " + repr(dget(o, "side")));
        if (qty.is_none() == notional.is_none()) reasons.push_back("format: give exactly one of qty or notional");
        for (const char* k : PRICE_FIELDS) {
            const Val x = dget(o, k); if (x.is_none()) continue;
            const auto v = f_(x);
            if (!v || *v <= 0) reasons.push_back(std::string("format: ") + k + " must be a positive number, got " + repr(x));
            else if (x.t != Val::STR) warns.push_back(std::string("send ") + k + " as a string (\"" + pystr(x) + "\"), the way place_stock_order types it");
        }
        if (!TYPES.count(typ)) reasons.push_back("format: unknown order type " + str_repr(typ));
        if (!TIFS.count(tif)) reasons.push_back("format: unknown time_in_force " + str_repr(tif));
        if (!notional.is_none() && (typ != "market" || tif != "day")) reasons.push_back("format: a notional (dollar) order must be a market order with time_in_force day");
        if ((typ == "limit" || typ == "stop_limit") && dget(o, "limit_price").is_none()) reasons.push_back("format: a " + typ + " order needs a limit_price");
        if ((typ == "stop" || typ == "stop_limit") && dget(o, "stop_price").is_none()) reasons.push_back("format: a " + typ + " order needs a stop_price");
        if (truthy(dget(o, "extended_hours")) && !(typ == "limit" && (tif == "day" || tif == "gtc"))) reasons.push_back("format: extended hours needs a limit order with time_in_force day or gtc");
        if (!dget(o, "stop_loss_limit_price").is_none() && dget(o, "stop_loss_stop_price").is_none()) reasons.push_back("format: stop_loss_limit_price needs a stop_loss_stop_price");
        if (bracket) {
            const auto fq0 = f_(qty);
            if (!notional.is_none()) reasons.push_back("format: a bracket order needs a whole-share qty, not a notional amount");
            else if (fq0 && std::fabs(*fq0 - std::nearbyint(*fq0)) > 1e-9) reasons.push_back("format: a bracket order takes whole shares only, got qty " + repr(qty));
            if (tif != "day" && tif != "gtc") reasons.push_back("format: a bracket order needs time_in_force day or gtc, got " + str_repr(tif));
            if (dget(o, "take_profit_limit_price").is_none() || dget(o, "stop_loss_stop_price").is_none()) reasons.push_back("format: a bracket order needs both take_profit_limit_price and stop_loss_stop_price");
        } else if (!oc.empty() || truthy(dget(o, "advanced_instructions")))
            warns.push_back("advanced order (" + (oc.empty() ? std::string("advanced_instructions") : oc) + "): check every leg before you say yes");
        // asset
        if (!sym.empty() && occ(replace_all(sym, " ", ""))) {
            if (!truthy(c.d->at("allow_options"))) reasons.push_back("asset: " + sym + " is an option contract; options are off (\"allow_options\": false)");
            else warns.push_back("option order: options can lose 100%% of what you pay");   // sic: the original prints the doubled %
        } else if (!sym.empty() && (sym.find('/') != std::string::npos || !ticker(sym)))
            reasons.push_back("asset: " + sym + " does not look like a US stock or ETF ticker; this pack trades stocks and ETFs only");
        // the reference price: what one share costs to get in
        const auto plan_entry = f_(dget(plan, "entry"));
        const auto live = fresh(sym);
        std::optional<double> ref; Val ref_from;
        if (typ == "limit" || typ == "stop_limit") { ref = f_(dget(o, "limit_price")); ref_from = Val::str("limit"); }
        else if (typ == "stop") { ref = f_(dget(o, "stop_price")); ref_from = Val::str("stop price"); }
        if (!ref) {
            std::vector<std::pair<double, const char*>> cands;
            if (plan_entry && *plan_entry != 0) cands.push_back({*plan_entry, "plan"});
            if (live && *live != 0) cands.push_back({*live, "price"});
            if (!cands.empty()) {
                auto best = cands[0]; for (const auto& cd : cands) if (cd.first > best.first) best = cd;
                ref = cent(best.first); ref_from = Val::str(best.second);
                if (live && *live != 0 && ref_from.s == "price" && plan_entry && *plan_entry != 0 && *live > *plan_entry + CENT)
                    info.push_back(sym + " last " + p2(*ref) + " is above the plan's entry " + p2(*plan_entry) + ": the risk is measured from " + p2(*ref));
            } else if (prices.has(sym)) { ref = prices.get(sym); ref_from = Val::str("stale price"); }
        }
        if (plan_entry && *plan_entry != 0 && !live && prices.has(sym) && price_times && typ == "market")
            warns.push_back("the " + sym + " price in trader/prices.json is older than " + std::to_string(FRESH_MINUTES) + " minutes: not used");
        const auto q = f_(qty), fn = f_(notional);
        const bool ref_t = ref && *ref != 0;
        if (!notional.is_none() && fn && *fn != 0) value = fn;
        else if (q && *q != 0) { if (ref_t) value = *q * *ref; }
        // entry or exit
        const double held = acct.shares.get(sym, 0.0), held_value = acct.values.get(sym, 0.0);
        bool exiting = false;
        if (side == "sell" && held > 1e-9) { if (q) exiting = *q <= held + 1e-9; else if (value) exiting = *value <= held_value + CENT; }
        else if (side == "buy" && held < -1e-9 && q) exiting = *q <= -held + 1e-9;
        const std::string kind = exiting ? "exit" : "entry";
        if (kind == "entry" && !value && q && *q != 0)
            later.push_back("price: no price for " + sym + " to size a " + pystr(qty) + "-share order; add it to trader/prices.json (trader-alerts: alerts quote " + sym + " --write)");
        // blocked list and size
        if (blocked.count(sym)) later.push_back("blocked: " + sym + " is on your blocked list");
        if (value) {
            if (*value < to_float(orv(c.d->at("min_order_usd"), Val::integer(0))) - CENT) later.push_back("size: " + money(*value) + " is under your " + money(to_float(c.d->at("min_order_usd"))) + " minimum order");
            if (truthy(dget(c, "max_order_usd")) && *value > to_float(c.d->at("max_order_usd")) + CENT) later.push_back("size: " + money(*value) + " is over your " + money(to_float(c.d->at("max_order_usd"))) + " max order");
        }
        // account effects, simulated in order
        const double cash = acct.cash;
        const double total = cash + fsum(acct.values.values());
        const double buffer_usd = total * to_float(orv(dget(c, "cash_buffer_pct"), Val::integer(0))) / 100.0;
        if (value && side == "buy" && kind == "entry") {
            const double after_cash = cash - *value;
            if (after_cash < -CENT && !truthy(dget(c, "allow_margin"))) later.push_back("cash: needs " + money(*value) + " but only " + money(cash) + " cash; that would be margin (borrowed money)");
            else if (after_cash < buffer_usd - CENT) later.push_back("cash: leaves " + money(std::max(after_cash, 0.0)) + " cash, under your " + pct_(to_float(c.d->at("cash_buffer_pct"))) + "% buffer of " + money(buffer_usd));
            const double pos_after = held_value + *value;
            const double limit = total * to_float(orv(dget(c, "max_position_pct"), Val::integer(100))) / 100.0;
            if (total > 0 && pos_after > limit + CENT)
                later.push_back("position: " + sym + " would be " + money(pos_after) + ", " + fmt_f(100.0 * pos_after / total, 1) + "% of the " + money(total) + " account, over your " + pct_(to_float(c.d->at("max_position_pct"))) + "% cap");
        }
        if (side == "sell" && !sym.empty() && kind == "entry") {
            bool over;
            if (q) over = *q > held + 1e-9; else if (value) over = *value > held_value + CENT; else over = held <= 0;
            if (over && !truthy(dget(c, "allow_short"))) reasons.push_back("short: selling more " + sym + " than you hold (" + num_(held) + " shares); short selling is off. If you bought it, record the fill first (ticket fill)");
        }
        // day-trade checks: stop, loss, reward:risk, daily loss, trades, window, pdt
        std::optional<double> max_loss, risk_ps, rr, stop, target;
        if (kind == "entry" && (side == "buy" || side == "sell")) {
            const bool lng = side == "buy";
            const auto leg_stop = f_(dget(o, "stop_loss_stop_price")), plan_stop = f_(dget(plan, "stop"));
            const bool leg_t = leg_stop && *leg_stop != 0;
            stop = leg_t ? leg_stop : plan_stop;
            { const auto tp = f_(dget(o, "take_profit_limit_price")); target = tp && *tp != 0 ? tp : f_(dget(plan, "target")); }
            const bool stop_t = stop && *stop != 0, target_t = target && *target != 0;
            if (leg_t && plan_stop && *plan_stop != 0 && std::fabs(*leg_stop - *plan_stop) > CENT)
                warns.push_back("the bracket stop " + p2(*leg_stop) + " differs from the plan's stop " + p2(*plan_stop) + ": the bracket's is the one the broker uses");
            if (stop_t && !leg_t) warns.push_back("the stop " + p2(*stop) + " is in the plan only, not at the broker: set a stop alert (trader-alerts: alerts add --from-plan)");
            if (!stop_t) {
                if (truthy(c.d->at("require_stop", Val::boolean(true)))) later.push_back("stop: no stop. Every entry needs one (\"require_stop\": true): a bracket stop_loss_stop_price, from trader-exit and trader-budget");
                else warns.push_back("no stop: the max loss is not capped, so it is not counted in the daily loss");
            } else if (ref_t) {
                if ((lng && *stop >= *ref - CENT) || (!lng && *stop <= *ref + CENT))
                    later.push_back("stop: the stop " + p2(*stop) + " must be " + (lng ? "below" : "above") + " the entry " + p2(*ref) + " for a " + side);
                else {
                    const long long risk_c = static_cast<long long>(std::nearbyint(std::fabs(*ref - *stop) * 100));
                    risk_ps = static_cast<double>(risk_c) / 100.0;
                    const double shares = q ? *q : value.value_or(0) / *ref;
                    max_loss = round_n(shares * *risk_ps, 2);
                    const auto cap = f_(dget(c, "max_loss_per_trade_usd"));
                    if (cap && *cap != 0 && *max_loss > *cap + CENT)
                        later.push_back("loss: " + num_(shares) + " shares x " + p2(*risk_ps) + " risk a share = " + money(*max_loss) + " at the stop, over your " + money(*cap) + " max loss per trade");
                    const auto min_rr = f_(dget(c, "min_reward_risk"));
                    const bool min_t = min_rr && *min_rr != 0;
                    if (target_t) {
                        if ((lng && *target <= *ref + CENT) || (!lng && *target >= *ref - CENT))
                            later.push_back("target: the target " + p2(*target) + " must be " + (lng ? "above" : "below") + " the entry " + p2(*ref));
                        else {
                            if (risk_c == 0) throw PyErr("ZeroDivisionError", "float division by zero");
                            rr = round_n(static_cast<double>(static_cast<long long>(std::nearbyint(std::fabs(*target - *ref) * 100))) / static_cast<double>(risk_c), 2);
                            if (min_t && *rr < *min_rr - 1e-9)
                                later.push_back("reward:risk: " + fmt_f(*rr, 2) + " (target " + p2(*target) + ", entry " + p2(*ref) + ", stop " + p2(*stop) + ") is under your minimum " + num_(*min_rr));
                        }
                    } else if (min_t) later.push_back("reward:risk: no target to measure it against (min_reward_risk " + num_(*min_rr) + ")");
                }
            }
            const auto daily = f_(dget(c, "max_daily_loss_usd"));
            const bool daily_t = daily && *daily != 0;
            if (daily_t && max_loss) {
                const double used = loss_today + batch_loss;
                if (used + *max_loss > *daily + CENT)
                    later.push_back("daily loss: " + money(used) + " already at risk today plus " + money(*max_loss) + " for this one = " + money(used + *max_loss) + ", over your " + money(*daily) + " daily loss limit");
            }
            const long long cap_t = to_int(orv(dget(c, "max_trades_per_day"), Val::integer(0)));
            if (cap_t && n_entries + batch_entries >= cap_t)
                later.push_back("trades: " + std::to_string(n_entries) + " entries placed today plus " + std::to_string(batch_entries) + " passed earlier in this batch, your limit is " + std::to_string(cap_t) + " a day");
            for (const auto& w : win_reasons) later.push_back(w);
            if (pdt_guard && pdt_n >= 3)
                later.push_back("pdt: " + std::to_string(pdt_n) + " day trades in the last 5 business days (" + join(pdt_list, ", ") + "); a 4th trips the pattern-day-trader rule your broker applies (\"pdt_guard\": true)");
            if (stop_t && ref_t && risk_ps && *risk_ps != 0) {
                info.push_back("entry " + p2(*ref) + " (" + ref_words(pystr(ref_from)) + "), stop " + p2(*stop) + ", target " + (target_t ? p2(*target) : std::string("none")) + ": risk " + p2(*risk_ps) + " a share");
                const auto capv = f_(dget(c, "max_loss_per_trade_usd")), mrr = f_(dget(c, "min_reward_risk"));
                if (!max_loss) throw PyErr("TypeError", "float() argument must be a string or a real number, not 'NoneType'");
                info.push_back("max loss " + money(*max_loss) + (capv && *capv != 0 ? " (limit " + money(*capv) + ")" : std::string()) + ", reward:risk " + (rr ? fmt_f(*rr, 2) : std::string("n/a")) +
                               (mrr && *mrr != 0 ? " (min " + num_(*mrr) + ")" : std::string()));
            }
            if (daily_t || cap_t)
                info.push_back("today before this: " + std::to_string(n_entries + batch_entries) + " of " + (cap_t ? std::to_string(cap_t) : std::string("unlimited")) + " trades, " + money(loss_today + batch_loss) + " of " + (daily_t ? money(*daily) : std::string("no limit on")) + " daily loss");
        }
        // duplicates and the daily order limit
        const std::pair<std::string, std::string> key{sym, side};
        if (seen.count(key)) later.push_back("duplicate: a second " + side + " " + sym + " in this batch");
        if (placed_keys.count(key)) later.push_back("duplicate: a " + side + " " + sym + " was already placed today");
        const long long cap_n = to_int(orv(dget(c, "max_orders_per_day"), Val::integer(0)));
        if (cap_n && count_today + passed_n >= cap_n)
            later.push_back("daily limit: " + std::to_string(count_today) + " placed today plus " + std::to_string(passed_n) + " passed earlier in this batch, your limit is " + std::to_string(cap_n) + " a day");
        seen.insert(key);
        if (kind == "entry") for (const auto& x : later) reasons.push_back(x);
        else for (const auto& x : later) warns.push_back("exit, not blocked: " + x);
        const bool pass = reasons.empty();
        if (pass) {
            if (kind == "entry" && (side == "buy" || side == "sell")) { batch_entries++; batch_loss += max_loss.value_or(0.0); }
            if (side == "buy") {
                if (value) { acct.cash -= *value; acct.values[sym] = acct.values.get(sym, 0.0) + *value; }
                if (q) acct.shares[sym] = held + *q; else if (ref_t && value) acct.shares[sym] = held + *value / *ref;
            } else if (side == "sell") {
                if (value) { acct.cash += *value; acct.values[sym] = std::max(acct.values.get(sym, 0.0) - *value, 0.0); }
                if (q) acct.shares[sym] = held - *q; else if (ref_t && value) acct.shares[sym] = held - *value / *ref;
            }
            passed_n++;
        }
        Val r = Val::dict();
        r.d->set("index", Val::integer(i)); r.d->set("symbol", Val::str(sym)); r.d->set("side", Val::str(side)); r.d->set("kind", Val::str(kind));
        r.d->set("amount", Val::str(!notional.is_none() ? money(fn && *fn != 0 ? *fn : 0) : pystr(qty) + " shares"));
        r.d->set("value_usd", value ? Val::flt(round_n(*value, 2)) : Val());
        r.d->set("entry_ref", ref_t ? Val::flt(round_n(*ref, 2)) : Val()); r.d->set("entry_ref_from", ref_from);
        r.d->set("stop", opt(stop)); r.d->set("target", opt(target)); r.d->set("risk_per_share", opt(risk_ps)); r.d->set("max_loss", opt(max_loss)); r.d->set("reward_risk", opt(rr));
        r.d->set("fingerprint", Val::str(fingerprint(o))); r.d->set("verdict", Val::str(pass ? "PASS" : "BLOCK"));
        const auto L = [](const std::vector<std::string>& xs) { Val l = Val::list(); for (const auto& x : xs) l.l->push_back(Val::str(x)); return l; };
        r.d->set("reasons", L(reasons)); r.d->set("warnings", L(warns)); r.d->set("info", L(info));
        results.l->push_back(r);
    }
    Val out = Val::dict();
    out.d->set("date", Val::str(today)); out.d->set("now_new_york", Val::str(strf(now_et, false))); out.d->set("paper", Val::boolean(truthy(c.d->at("paper", Val::boolean(true)))));
    out.d->set("results", results); out.d->set("pass", Val::integer(passed_n)); out.d->set("block", Val::integer(static_cast<long long>(results.l->size()) - passed_n));
    Val tw = Val::list(); for (const auto& x : top_warn) tw.l->push_back(Val::str(x));
    out.d->set("warnings", tw); out.d->set("cash_after", Val::flt(round_n(acct.cash, 2)));
    Val td = Val::dict();
    td.d->set("orders_placed", Val::integer(count_today)); td.d->set("entries_placed", Val::integer(n_entries)); td.d->set("loss_at_risk", Val::flt(round_n(loss_today, 2)));
    td.d->set("day_trades_5d", pdt_guard ? Val::integer(pdt_n) : Val());
    out.d->set("today", td);
    return out;
}
inline std::string report(const Val& res) {
    std::vector<std::string> lines;
    for (const auto& w : *get(res, "warnings").l) lines.push_back("WARNING: " + w.s);
    if (!get(res, "warnings").l->empty()) lines.emplace_back();
    for (const auto& r : *get(res, "results").l) {
        lines.push_back(ljust(get(r, "verdict").s, 5) + " #" + std::to_string(get(r, "index").i) + " " + get(r, "side").s + " " + get(r, "symbol").s + " " + get(r, "amount").s + ", " + get(r, "kind").s);
        for (const auto& x : *get(r, "info").l) lines.push_back("          " + x.s);
        for (const auto& x : *get(r, "reasons").l) lines.push_back("        - " + x.s);
        for (const auto& x : *get(r, "warnings").l) lines.push_back("        ~ " + x.s);
    }
    lines.emplace_back();
    lines.push_back(std::to_string(get(res, "pass").i) + " passed, " + std::to_string(get(res, "block").i) + " blocked at " + get(res, "now_new_york").s + " New York. Cash after the passed orders: " +
                    money(get(res, "cash_after").num()) + " (" + (get(res, "paper").b ? "paper" : "LIVE") + " account).");
    if (get(res, "block").i) lines.push_back("Blocked orders do not go to the broker. Fix the order or your rules, then check again.");
    else lines.push_back("Every order passed. trader-broker still asks you yes or no for each one.");
    lines.push_back("Education and tooling, not financial advice.");
    return join(lines, "\n");
}
// prices.json -> prices and each price's time (the quote's own time, else when it was fetched, else the file's as_of)
inline std::pair<ODict, std::map<std::string, std::optional<long long>>> load_prices(const Val& pdoc) {
    ODict out; std::map<std::string, std::optional<long long>> times;
    if (pdoc.t != Val::DICT) return {out, times};
    const Val prices = pdoc.d->at("prices", pdoc);
    if (prices.t != Val::DICT) return {out, times};
    const auto as_of = parse_utc(dget(pdoc, "as_of"));
    const Val detail = orv(dget(pdoc, "detail"), Val::dict());
    for (const auto& [s0, p] : prices.d->items) {
        const auto f = f_(p); if (!f) continue;
        const std::string s = upper(s0);
        out[s] = *f;
        const Val d = orv(dget(detail, s), Val::dict());
        auto t = parse_utc(dget(d, "time")); if (!t) t = parse_utc(dget(d, "fetched")); if (!t) t = as_of;
        times[s] = t;
    }
    return {out, times};
}
inline bool is_ymd(const std::string& s) { static const std::regex R(R"(^[0-9]{4}-[0-9]{2}-[0-9]{2}\n?$)"); return std::regex_match(s, R); }
inline int main(const std::vector<std::string>& argv) {
    Parser p("check", "Check proposed orders against your limits (trader-risk).");
    p.opt({"--orders"}).def("trader/orders.json");
    p.opt({"--config"}).def("trader/config.json");
    p.opt({"--portfolio"}).def("trader/portfolio.json").help("your account; if missing, a fresh account funded with the budget");
    p.opt({"--prices"}).def("trader/prices.json");
    p.opt({"--state"}).def("trader/state/broker.json").help("what trader-broker placed today");
    p.opt({"--now"}).help("pretend it is this New York time, e.g. \"2026-10-19 10:15\"");
    p.opt({"--today"}).help("YYYY-MM-DD (default: today in New York)");
    p.opt({"--out"}).def("trader/state/risk.json").help("where the verdicts are recorded");
    p.opt({"--no-write"}).store_true().help("do not record the verdicts");
    p.opt({"--json"}).store_true();
    const Val a = parse_args(p, argv);
    Val res;
    try {
        const long long now = parse_now(get(a, "now"));
        if (truthy(get(a, "today")) && !is_ymd(S(a, "today"))) throw RiskError("--today wants a date like 2026-10-19");
        const std::string orders_path = S(a, "orders");
        const Val doc = read_json(orders_path, "orders file (run trader-budget first)");
        const Val orders = doc.t == Val::DICT ? doc.d->at("orders", doc) : doc;
        if (orders.t != Val::LIST || orders.l->empty()) throw RiskError("no orders in " + orders_path);
        const Val cfg = orv(read_json(S(a, "config"), "config", false), Val::dict());
        const Val portfolio = read_json(S(a, "portfolio"), "portfolio", false);
        const Val pdoc = orv(read_json(S(a, "prices"), "prices", false), Val::dict());
        const auto [prices, times] = load_prices(pdoc);
        const Val state = read_json(S(a, "state"), "broker state", false);
        res = check(*orders.l, cfg, portfolio, prices, state, truthy(get(a, "today")) ? S(a, "today") : "", now, times);
        if (truthy(dget(pdoc, "as_of"))) res.d->set("prices_as_of", dget(pdoc, "as_of"));
        if (!B(a, "no_write")) {
            const std::string outp = S(a, "out"); const std::string d = dirname(outp); if (!d.empty()) makedirs(d);
            Val rec = Val::dict(); rec.d->items = res.d->items;
            rec.d->set("checked_at", Val::str(strf(now_us() / 1000000 * 1000000, true))); rec.d->set("orders_file", Val::str(orders_path));
            DumpOpts o; o.indent = 2; const std::string tmp = outp + ".tmp"; write_text(tmp, dumps(rec, o) + "\n");
            std::error_code ec; std::filesystem::rename(fspath(tmp), fspath(outp), ec);
            if (ec) { std::filesystem::remove(fspath(outp), ec); std::filesystem::rename(fspath(tmp), fspath(outp), ec); if (ec) throw OSError(ec.value(), outp); }
        }
    } catch (const RiskError& e) { eprint(std::string("check: ") + e.what()); return 1; }
    if (B(a, "json")) { DumpOpts o; o.indent = 2; print(dumps(res, o)); } else print(report(res));
    return get(res, "block").i ? 1 : 0;
}
PACKTOOL("daytrade-risk/check", main);
}  // namespace packtools::daytrade_risk
