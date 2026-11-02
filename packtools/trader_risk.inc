// trader-risk/check - pre-trade risk check: every proposed order gets PASS or BLOCK with the reasons, checked in file
// order as if each PASS had filled (format, asset, blocked list, size, position cap, cash buffer, shorting,
// duplicates, daily limit). Verdicts go to trader/state/risk.json with a fingerprint of each exact order. No network.
// C++ port of trader-skill/trader-risk/check.py (Jake Schincariol, MIT); same output, same exit codes:
// 0 when every order passes, 1 when any is blocked (or on an error).
namespace packtools::trader_risk {
using namespace pyrt;
struct RiskError : std::runtime_error { using std::runtime_error::runtime_error; };
constexpr double CENT = 0.005;
inline const char* const ALPACA_FIELDS[] = {"symbol", "side", "qty", "notional", "type", "time_in_force", "limit_price", "stop_price", "trail_price", "trail_percent",
    "extended_hours", "client_order_id", "order_class", "take_profit_limit_price", "stop_loss_stop_price", "stop_loss_limit_price", "advanced_instructions"};
static const std::set<std::string> TYPES = {"market", "limit", "stop", "stop_limit", "trailing_stop"};
static const std::set<std::string> TIFS = {"day", "gtc", "opg", "cls", "ioc", "fok"};
inline Val defaults() {
    Val d = Val::dict();
    d.d->set("budget", Val()); d.d->set("cash_buffer_pct", Val::integer(10)); d.d->set("max_position_pct", Val::integer(25)); d.d->set("max_order_usd", Val::integer(2000));
    d.d->set("min_order_usd", Val::integer(5)); d.d->set("max_orders_per_day", Val::integer(10)); d.d->set("paper", Val::boolean(true)); d.d->set("blocked", Val::list());
    d.d->set("allow_options", Val::boolean(false)); d.d->set("allow_margin", Val::boolean(false)); d.d->set("allow_short", Val::boolean(false));
    return d;
}
inline bool AZ(char c) { return c >= 'A' && c <= 'Z'; }
inline bool D9(char c) { return c >= '0' && c <= '9'; }
// ^[A-Z]{1,6}\d{6}[CP]\d{8}$
inline bool occ(const std::string& s) {
    std::size_t i = 0; while (i < s.size() && AZ(s[i])) i++;
    if (i < 1 || s.size() < i + 15) return false;
    // the letters may give some back: the tail is exactly 6 digits + C/P + 8 digits
    const std::size_t L = s.size() - 15; if (L < 1 || L > 6 || L > i) return false;
    for (std::size_t k = 0; k < 6; ++k) if (!D9(s[L + k])) return false;
    if (s[L + 6] != 'C' && s[L + 6] != 'P') return false;
    for (std::size_t k = 7; k < 15; ++k) if (!D9(s[L + k])) return false;
    return true;
}
// ^[A-Z]{1,5}([.\-][A-Z]{1,2})?$
inline bool ticker(const std::string& s) {
    std::size_t i = 0; while (i < s.size() && AZ(s[i])) i++;
    if (i < 1 || i > 5) return false;
    if (i == s.size()) return true;
    if (s[i] != '.' && s[i] != '-') return false;
    const std::size_t r = s.size() - i - 1; if (r < 1 || r > 2) return false;
    for (std::size_t k = i + 1; k < s.size(); ++k) if (!AZ(s[k])) return false;
    return true;
}
inline Val read_json(const std::string& path, const std::string& what, bool required = true) {
    if (path.empty() || !exists(path)) { if (required) throw RiskError("no " + what + " at " + path); return Val(); }
    try { return loads(read_text(path)); } catch (const JSONDecodeError& e) { throw RiskError(path + " is not valid JSON: " + e.what()); }
}
// Same function in trader-broker/ticket: the exact order that was checked.
inline std::string fingerprint(const Val& order) {
    Val core = Val::dict();
    for (const char* k : ALPACA_FIELDS) if (has(order, k) && !get(order, k).is_none()) core.d->set(k, get(order, k));
    DumpOpts o; o.sort_keys = true; o.compact = true;
    return sha256_hex(dumps(core, o)).substr(0, 16);
}
inline std::string money(double x) { return "$" + replace_all(fmt_fc(x, 2), ".00", ""); }
inline std::string pct_(double x) { return x == std::trunc(x) ? std::to_string(static_cast<long long>(x)) : fmt_f(x, 2); }
inline std::string num_(double x) {
    if (x == std::trunc(x)) return std::to_string(static_cast<long long>(x));
    std::string s = fmt_f(x, 4); while (!s.empty() && s.back() == '0') s.pop_back(); return s;
}
inline double fsum_or0(const List& lots, const char* key) { std::vector<double> xs; for (const auto& l : lots) xs.push_back(f_(dget(l, key)).value_or(0)); return fsum(xs); }

struct Acct { double cash = 0; ODict shares, values; };
inline Acct account(const Val& cfg, const Val& portfolio, const ODict& prices, std::vector<std::string>& warnings) {
    Acct a;
    if (portfolio.is_none()) {
        const auto budget = f_(dget(cfg, "budget"));
        if (!budget || *budget == 0) throw RiskError("no trader/portfolio.json and no budget in the config: nothing to check the orders against");
        a.cash = *budget; return a;
    }
    const auto cash = f_(dget(portfolio, "cash"));
    if (!cash) throw RiskError("portfolio.json needs a \"cash\" number");
    a.cash = *cash;
    for (const auto& p : iter(dget(portfolio, "positions", Val::list()))) {
        const std::string t = upper(pystr(orv(orv(dget(p, "ticker"), dget(p, "symbol")), Val::str(""))));
        if (t.empty()) continue;
        auto n = f_(dget(p, "shares", dget(p, "qty")));
        const List lots = iter(dget(p, "lots", Val::list()));
        if (!n) n = fsum_or0(lots, "shares");
        a.shares[t] = a.shares.get(t, 0) + *n;
        if (prices.get(t)) a.values[t] = a.shares.get(t) * prices.get(t);
        else { const double basis = fsum_or0(lots, "cost"); a.values[t] = basis; warnings.push_back("no price for " + t + ": valued at its cost basis " + money(basis)); }
    }
    return a;
}
// Orders recorded as placed today by trader-broker.
inline List placed_today(const Val& state, const std::string& today) {
    List out;
    for (const auto& rec : iter(dget(orv(state, Val::dict()), "orders", Val::list()))) {
        if (!(dget(rec, "status").t == Val::STR && dget(rec, "status").s == "placed")) continue;
        const Val d = dget(rec, "date");
        const std::string day = truthy(d) ? pystr(d) : cpslice(pystr(dget(rec, "at", Val::str(""))), 0, 10);
        if (day == today) out.push_back(rec);
    }
    return out;
}

inline Val check(const List& orders, const Val& cfg, const Val& portfolio, const Val& prices_in, const Val& state, std::string today) {
    Val c = defaults();
    if (truthy(cfg)) { if (cfg.t != Val::DICT) throw PyErr("ValueError", "dictionary update sequence element #0 has the wrong length"); for (const auto& [k, v] : cfg.d->items) c.d->set(k, v); }
    ODict prices;
    { const Val pv = orv(prices_in, Val::dict()); if (pv.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(pv) + "' object has no attribute 'items'");
      for (const auto& [k, v] : pv.d->items) { const auto f = f_(v); if (f && *f != 0) prices[upper(k)] = *f; } }
    if (today.empty()) today = today_iso();
    std::vector<std::string> top_warn;
    Acct acct = account(c, portfolio, prices, top_warn);
    std::set<std::string> blocked; for (const auto& b : iter(orv(c.d->at("blocked"), Val::list()))) blocked.insert(upper(pystr(b)));
    const List done_today = placed_today(state, today);
    const long long count_today = static_cast<long long>(done_today.size());
    std::set<std::pair<std::string, std::string>> placed_keys;
    for (const auto& r : done_today) { const Val sd = dget(r, "side"); if (sd.t == Val::STR) placed_keys.insert({upper(pystr(dget(r, "symbol", Val::str("")))), sd.s}); }
    std::set<std::pair<std::string, std::string>> seen;
    Val results = Val::list(); long long passed_n = 0;
    if (!truthy(c.d->at("paper", Val::boolean(true)))) top_warn.push_back("LIVE account (\"paper\": false): these orders use real money");

    long long i = 0;
    for (const auto& o : orders) {
        ++i;
        std::vector<std::string> reasons, warns;
        const std::string sym = upper(strip(pystr(dget(o, "symbol", Val::str("")))));
        const std::string side = lower(strip(pystr(dget(o, "side", Val::str("")))));
        const std::string typ = lower(strip(pystr(dget(o, "type", Val::str("market")))));
        const std::string tif = lower(strip(pystr(dget(o, "time_in_force", Val::str("day")))));
        const Val qty = dget(o, "qty"), notional = dget(o, "notional");
        std::optional<double> value;
        // format
        if (sym.empty()) reasons.push_back("format: no symbol");
        if (side != "buy" && side != "sell") reasons.push_back("format: side must be buy or sell, got " + repr(dget(o, "side")));
        if (qty.is_none() == notional.is_none()) reasons.push_back("format: give exactly one of qty or notional");
        for (const char* k : {"qty", "notional", "limit_price", "stop_price"}) {
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
        if (truthy(dget(o, "extended_hours")) && !(typ == "limit" && tif == "day")) reasons.push_back("format: extended hours needs a limit order with time_in_force day");
        if (truthy(dget(o, "order_class")) || truthy(dget(o, "advanced_instructions")))
            warns.push_back("advanced order (" + pystr(orv(dget(o, "order_class"), Val::str("advanced_instructions"))) + "): check every leg before you say yes");
        // asset
        if (!sym.empty() && occ(replace_all(sym, " ", ""))) {
            if (!truthy(c.d->at("allow_options"))) reasons.push_back("asset: " + sym + " is an option contract; options are off (\"allow_options\": false)");
            else warns.push_back("option order: options can lose 100%% of what you pay");   // sic: the original prints the doubled %
        } else if (!sym.empty() && (sym.find('/') != std::string::npos || !ticker(sym)))
            reasons.push_back("asset: " + sym + " does not look like a US stock or ETF ticker; this pack trades stocks and ETFs only");
        // blocked list
        if (blocked.count(sym)) reasons.push_back("blocked: " + sym + " is on your blocked list");
        // dollar value of the order
        std::optional<double> ref;
        if (typ == "limit" || typ == "stop_limit") ref = f_(dget(o, "limit_price"));
        else if (typ == "stop") ref = f_(dget(o, "stop_price"));
        if (!ref && prices.has(sym)) ref = prices.get(sym);
        const auto fn = f_(notional), fq = f_(qty);
        if (!notional.is_none() && fn && *fn != 0) value = fn;
        else if (!qty.is_none() && fq && *fq != 0) {
            if (ref && *ref != 0) value = *fq * *ref;
            else reasons.push_back("price: no price for " + sym + " to size a " + pystr(qty) + "-share order; add it to trader/prices.json (trader-alerts: alerts quote " + sym + " --write)");
        }
        if (value) {
            if (*value < to_float(orv(c.d->at("min_order_usd"), Val::integer(0))) - CENT) reasons.push_back("size: " + money(*value) + " is under your " + money(to_float(c.d->at("min_order_usd"))) + " minimum order");
            if (truthy(c.d->at("max_order_usd")) && *value > to_float(c.d->at("max_order_usd")) + CENT) reasons.push_back("size: " + money(*value) + " is over your " + money(to_float(c.d->at("max_order_usd"))) + " max order");
        }
        // account effects, simulated in order
        const double cash = acct.cash, held = acct.shares.get(sym, 0.0), held_value = acct.values.get(sym, 0.0);
        const double total = cash + fsum(acct.values.values());
        const double buffer_usd = total * to_float(orv(c.d->at("cash_buffer_pct"), Val::integer(0))) / 100.0;
        if (value && side == "buy") {
            const double after_cash = cash - *value;
            if (after_cash < -CENT && !truthy(c.d->at("allow_margin"))) reasons.push_back("cash: needs " + money(*value) + " but only " + money(cash) + " cash; that would be margin (borrowed money)");
            else if (after_cash < buffer_usd - CENT) reasons.push_back("cash: leaves " + money(std::max(after_cash, 0.0)) + " cash, under your " + pct_(to_float(c.d->at("cash_buffer_pct"))) + "% buffer of " + money(buffer_usd));
            const double pos_after = held_value + *value;
            const double limit = total * to_float(orv(c.d->at("max_position_pct"), Val::integer(100))) / 100.0;
            if (total > 0 && pos_after > limit + CENT)
                reasons.push_back("position: " + sym + " would be " + money(pos_after) + ", " + fmt_f(100.0 * pos_after / total, 1) + "% of the " + money(total) + " account, over your " + pct_(to_float(c.d->at("max_position_pct"))) + "% cap");
        }
        if (side == "sell" && !sym.empty()) {
            bool over;
            if (!qty.is_none() && fq && *fq != 0) over = *fq > held + 1e-9;
            else if (value) over = *value > held_value + CENT;
            else over = held <= 0;
            if (over && !truthy(c.d->at("allow_short"))) reasons.push_back("short: selling more " + sym + " than you hold (" + num_(held) + " shares); short selling is off");
        }
        // duplicates and the daily limit
        const std::pair<std::string, std::string> key{sym, side};
        if (seen.count(key)) reasons.push_back("duplicate: a second " + side + " " + sym + " in this batch");
        if (placed_keys.count(key)) reasons.push_back("duplicate: a " + side + " " + sym + " was already placed today");
        const long long cap_n = to_int(orv(c.d->at("max_orders_per_day"), Val::integer(0)));
        if (cap_n && count_today + passed_n >= cap_n)
            reasons.push_back("daily limit: " + std::to_string(count_today) + " placed today plus " + std::to_string(passed_n) + " passed earlier in this batch, your limit is " + std::to_string(cap_n) + " a day");
        seen.insert(key);

        const bool pass = reasons.empty();
        if (pass && value) {
            if (side == "buy") {
                acct.cash -= *value; acct.values[sym] = acct.values.get(sym, 0.0) + *value;
                if (ref && *ref != 0) acct.shares[sym] = held + *value / *ref;
            } else {
                acct.cash += *value; acct.values[sym] = std::max(acct.values.get(sym, 0.0) - *value, 0.0);
                if (!qty.is_none()) { if (!fq) throw PyErr("TypeError", "unsupported operand type(s) for -: 'float' and 'NoneType'"); acct.shares[sym] = held - *fq; }
                else if (ref && *ref != 0) acct.shares[sym] = held - *value / *ref;
            }
        }
        if (pass) passed_n++;
        Val r = Val::dict();
        r.d->set("index", Val::integer(i)); r.d->set("symbol", Val::str(sym)); r.d->set("side", Val::str(side));
        r.d->set("amount", Val::str(!notional.is_none() ? money(fn && *fn != 0 ? *fn : 0) : pystr(qty) + " shares"));
        r.d->set("value_usd", value ? Val::flt(round_n(*value, 2)) : Val());
        r.d->set("fingerprint", Val::str(fingerprint(o))); r.d->set("verdict", Val::str(pass ? "PASS" : "BLOCK"));
        Val rl = Val::list(); for (const auto& x : reasons) rl.l->push_back(Val::str(x));
        Val wl = Val::list(); for (const auto& x : warns) wl.l->push_back(Val::str(x));
        r.d->set("reasons", rl); r.d->set("warnings", wl);
        results.l->push_back(r);
    }
    Val out = Val::dict();
    out.d->set("date", Val::str(today)); out.d->set("paper", Val::boolean(truthy(c.d->at("paper", Val::boolean(true)))));
    out.d->set("results", results); out.d->set("pass", Val::integer(passed_n)); out.d->set("block", Val::integer(static_cast<long long>(results.size()) - passed_n));
    Val tw = Val::list(); for (const auto& x : top_warn) tw.l->push_back(Val::str(x));
    out.d->set("warnings", tw); out.d->set("cash_after", Val::flt(round_n(acct.cash, 2)));
    return out;
}
inline std::string report(const Val& res) {
    std::vector<std::string> lines;
    for (const auto& w : *get(res, "warnings").l) lines.push_back("WARNING: " + w.s);
    if (!get(res, "warnings").l->empty()) lines.emplace_back();
    for (const auto& r : *get(res, "results").l) {
        lines.push_back(ljust(get(r, "verdict").s, 5) + " #" + std::to_string(get(r, "index").i) + " " + get(r, "side").s + " " + get(r, "symbol").s + " " + get(r, "amount").s);
        for (const auto& x : *get(r, "reasons").l) lines.push_back("        - " + x.s);
        for (const auto& x : *get(r, "warnings").l) lines.push_back("        ~ " + x.s);
    }
    lines.emplace_back();
    lines.push_back(std::to_string(get(res, "pass").i) + " passed, " + std::to_string(get(res, "block").i) + " blocked. Cash after the passed orders: " + money(get(res, "cash_after").num()) + " (" + (get(res, "paper").b ? "paper" : "LIVE") + " account).");
    if (get(res, "block").i) lines.push_back("Blocked orders do not go to the broker. Fix the order or your rules, then check again.");
    else lines.push_back("Every order passed. trader-broker still asks you yes or no for each one.");
    lines.push_back("Education and tooling, not financial advice.");
    return join(lines, "\n");
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("check", "Check proposed orders against your limits (trader-risk).");
    p.opt({"--orders"}).def("trader/orders.json");
    p.opt({"--config"}).def("trader/config.json");
    p.opt({"--portfolio"}).def("trader/portfolio.json").help("your account; if missing, a fresh account funded with the budget");
    p.opt({"--prices"}).def("trader/prices.json");
    p.opt({"--state"}).def("trader/state/broker.json").help("what trader-broker placed today");
    p.opt({"--today"}).help("YYYY-MM-DD (default: today)");
    p.opt({"--out"}).def("trader/state/risk.json").help("where the verdicts are recorded");
    p.opt({"--no-write"}).store_true().help("do not record the verdicts");
    p.opt({"--json"}).store_true();
    const Val a = parse_args(p, argv);
    Val res;
    try {
        const std::string orders_path = S(a, "orders");
        const Val doc = read_json(orders_path, "orders file (run trader-budget or trader-rebalance first)");
        const Val orders = doc.t == Val::DICT ? doc.d->at("orders", doc) : doc;
        if (orders.t != Val::LIST || orders.l->empty()) throw RiskError("no orders in " + orders_path);
        const Val cfg = orv(read_json(S(a, "config"), "config", false), Val::dict());
        const Val portfolio = read_json(S(a, "portfolio"), "portfolio", false);
        const Val pdoc = orv(read_json(S(a, "prices"), "prices", false), Val::dict());
        const Val prices = pdoc.t == Val::DICT ? pdoc.d->at("prices", pdoc) : Val::dict();
        const Val state = read_json(S(a, "state"), "broker state", false);
        res = check(*orders.l, cfg, portfolio, prices, state, S(a, "today"));
        if (truthy(dget(pdoc, "as_of"))) res.d->set("prices_as_of", dget(pdoc, "as_of"));
        if (!B(a, "no_write")) {
            const std::string outp = S(a, "out"); const std::string d = dirname(outp); if (!d.empty()) makedirs(d);
            Val rec = Val::dict(); rec.d->items = res.d->items;
            rec.d->set("checked_at", Val::str(now_iso())); rec.d->set("orders_file", Val::str(orders_path));
            DumpOpts o; o.indent = 2; write_text(outp, dumps(rec, o) + "\n");
        }
    } catch (const RiskError& e) { eprint(std::string("check: ") + e.what()); return 1; }
    if (B(a, "json")) { DumpOpts o; o.indent = 2; print(dumps(res, o)); } else print(report(res));
    return get(res, "block").i ? 1 : 0;
}
PACKTOOL("trader-risk/check", main);
}  // namespace packtools::trader_risk
