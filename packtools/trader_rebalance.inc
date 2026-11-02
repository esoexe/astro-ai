// trader-rebalance/rebalance - compares what you hold with your target weights and proposes the trades that bring
// every stock outside the band back to its target (sells first, cash buffer kept, min/max order respected).
// Proposals only: nothing is placed. No network.
// C++ port of trader-skill/trader-rebalance/rebalance.py (Jake Schincariol, MIT); same output, same exit codes:
// 0 when there are trades to propose, 1 when nothing is outside the band (or on an error).
namespace packtools::trader_rebalance {
using namespace pyrt;
struct RebalanceError : std::runtime_error { using std::runtime_error::runtime_error; };
constexpr double EPS = 1e-9;
inline Val defaults() {
    Val d = Val::dict();
    d.d->set("cash_buffer_pct", Val::integer(10)); d.d->set("max_position_pct", Val::integer(25)); d.d->set("max_order_usd", Val::integer(2000));
    d.d->set("min_order_usd", Val::integer(5)); d.d->set("fractional", Val::boolean(true)); d.d->set("blocked", Val::list());
    d.d->set("rebalance_band_pct", Val::integer(5)); d.d->set("paper", Val::boolean(true));
    return d;
}

inline Val read_json(const std::string& path, const std::string& what, bool required = true) {
    if (path.empty() || !exists(path)) { if (required) throw RebalanceError("no " + what + " at " + path); return Val(); }
    try { return loads(read_text(path)); } catch (const JSONDecodeError& e) { throw RebalanceError(path + " is not valid JSON: " + e.what()); }
}
inline std::string money(double x) {
    const std::string sign = x < -0.004 ? "-" : "";
    std::string s = fmt_fc(std::fabs(x), 2);
    if (ends(s, ".00")) s = s.substr(0, s.size() - 3);
    return sign + "$" + s;
}
inline std::string fmt_qty(double x) {
    std::string s = fmt_f(x, 4);
    while (!s.empty() && s.back() == '0') s.pop_back();
    while (!s.empty() && s.back() == '.') s.pop_back();
    return s.empty() ? "0" : s;
}
inline std::string n_(double x) { return x == std::trunc(x) ? std::to_string(static_cast<long long>(x)) : fmt_f(x, 2); }

inline ODict parse_targets(const Val& doc, const Val& blocked_v) {
    Val rows = doc.t == Val::DICT ? doc.d->at("weights", doc) : doc;
    if (rows.t == Val::DICT) {
        Val l = Val::list();
        for (const auto& [k, v] : rows.d->items) { Val r = Val::dict(); r.d->set("ticker", Val::str(k)); r.d->set("weight", v); l.l->push_back(r); }
        rows = l;
    }
    std::set<std::string> blocked; for (const auto& b : iter(orv(blocked_v, Val::list()))) blocked.insert(upper(pystr(b)));
    ODict out;
    for (const auto& r : iter(orv(rows, Val::list()))) {
        const std::string t = upper(pystr(orv(orv(dget(r, "ticker"), dget(r, "symbol")), Val::str(""))));
        const double w = to_float(orv(dget(r, "weight", Val::integer(0)), Val::integer(0)));
        if (!t.empty() && w > 0 && !blocked.count(t)) out[t] = out.get(t, 0.0) + w;
    }
    const double tot = fsum(out.values());
    if (tot == 0) throw RebalanceError("no target weights (run trader-copycat, or write trader/targets.json)");
    ODict pct; for (const auto& [t, w] : out.items) pct[t] = 100.0 * w / tot;
    return pct;
}
// Dollar target per ticker, proportional to weight, none above the cap.
inline ODict capped_targets(const ODict& weights, double sleeve, double cap) {
    ODict alloc, active = weights; double money_left = sleeve;
    while (!active.empty()) {
        const double wsum = fsum(active.values());
        ODict trial; for (const auto& [t, w] : active.items) trial[t] = money_left * w / wsum;
        std::vector<std::string> over; for (const auto& [t, v] : trial.items) if (v > cap + EPS) over.push_back(t);
        if (over.empty()) { for (const auto& [t, v] : trial.items) alloc[t] = v; break; }
        for (const auto& t : over) { alloc[t] = cap; money_left -= cap; active.erase(t); }
        if (money_left <= EPS) { for (const auto& [t, w] : active.items) alloc[t] = 0.0; break; }
    }
    return alloc;
}
inline ODict holdings_of(const Val& portfolio) {
    ODict out;
    for (const auto& p : iter(dget(orv(portfolio, Val::dict()), "positions", Val::list()))) {
        const std::string t = upper(pystr(orv(orv(dget(p, "ticker"), dget(p, "symbol")), Val::str(""))));
        if (t.empty()) continue;
        Val n = dget(p, "shares", dget(p, "qty"));
        double nv;
        if (n.is_none()) { std::vector<double> xs; for (const auto& l : iter(dget(p, "lots", Val::list()))) xs.push_back(to_float(dget(l, "shares", Val::integer(0)))); nv = fsum(xs); }
        else nv = to_float(n);
        out[t] = out.get(t, 0.0) + nv;
    }
    return out;
}

struct Sell { std::string ticker; double usd; double qty; bool all; std::string why; };
struct Buy { std::string ticker; double usd; std::string why; };
struct Order { Val o; std::string side; double usd; };
struct Plan { double account_value, cash, cash_buffer, cap, band, cash_after; Val rows; std::vector<Order> orders; std::vector<std::string> notes; };

inline Plan plan(const Val& portfolio, const ODict& prices, const ODict& weights, const Val& cfg, std::optional<double> band_arg, bool sell_untracked) {
    Val c = defaults();
    if (truthy(cfg)) { if (cfg.t != Val::DICT) throw PyErr("ValueError", "dictionary update sequence element #0 has the wrong length"); for (const auto& [k, v] : cfg.d->items) c.d->set(k, v); }
    const double band = band_arg ? *band_arg : to_float(c.d->at("rebalance_band_pct"));
    const double cash = to_float(orv(dget(orv(portfolio, Val::dict()), "cash", Val::integer(0)), Val::integer(0)));
    const ODict held = holdings_of(portfolio);
    std::vector<std::string> need; for (const auto& [t, v] : held.items) need.push_back(t);
    for (const auto& [t, v] : weights.items) if (!held.has(t)) need.push_back(t);
    std::sort(need.begin(), need.end(), [&](const std::string& a, const std::string& b) { const double wa = -weights.get(a), wb = -weights.get(b); return wa != wb ? wa < wb : a < b; });
    std::vector<std::string> missing; for (const auto& t : need) if (!prices.get(t)) missing.push_back(t);
    if (!missing.empty()) throw RebalanceError("no price for " + join(missing, ", ") + ": add to trader/prices.json (trader-alerts: alerts quote " + join(missing, " ") + " --write)");
    ODict value; for (const auto& t : need) value[t] = held.get(t, 0.0) * prices.get(t);
    const double total = cash + fsum(value.values());
    if (total <= 0) throw RebalanceError("the account is empty: nothing to rebalance (trader-budget sizes a first buy)");
    const double buf = total * to_float(c.d->at("cash_buffer_pct")) / 100.0;
    const double cap = total * to_float(c.d->at("max_position_pct")) / 100.0;
    const ODict target = capped_targets(weights, total - buf, cap);
    const bool whole = !truthy(c.d->at("fractional", Val::boolean(true)));
    const double min_o = to_float(orv(c.d->at("min_order_usd"), Val::integer(0))), max_o = to_float(orv(c.d->at("max_order_usd"), Val::integer(0)));

    Plan r{}; r.rows = Val::list();
    std::vector<Sell> sells; std::vector<Buy> buys;
    for (const auto& t : need) {
        const bool tracked = weights.has(t);
        const double tgt = target.get(t, 0.0), cur_pct = 100.0 * value.get(t) / total, tgt_pct = 100.0 * tgt / total, drift = cur_pct - tgt_pct;
        Val row = Val::dict();
        row.d->set("ticker", Val::str(t)); row.d->set("shares", Val::flt(round_n(held.get(t, 0.0), 6))); row.d->set("price", Val::flt(prices.get(t)));
        row.d->set("value", Val::flt(round_n(value.get(t), 2))); row.d->set("current_pct", Val::flt(round_n(cur_pct, 2))); row.d->set("target_pct", Val::flt(round_n(tgt_pct, 2)));
        row.d->set("drift", Val::flt(std::fabs(drift) >= 0.005 ? round_n(drift, 2) : 0.0)); row.d->set("target_usd", Val::flt(round_n(tgt, 2)));
        row.d->set("tracked", Val::boolean(tracked)); row.d->set("action", Val::str("hold"));
        r.rows.l->push_back(row);
        Val& rr = r.rows.l->back();
        if (!tracked) {
            if (sell_untracked && held.get(t, 0) > EPS) { sells.push_back({t, value.get(t), held.get(t), true, "not in your targets; selling the whole position (--sell-untracked)"}); rr.d->set("action", Val::str("sell all")); }
            else rr.d->set("action", Val::str("untracked, left alone"));
            continue;
        }
        if (std::fabs(drift) <= band + EPS) continue;
        const double delta = tgt - value.get(t);
        const std::string why = "drifted " + fmt_fs(drift, 1) + " points from its " + fmt_f(tgt_pct, 1) + "% target (band " + n_(band) + ")";
        if (delta < 0) { sells.push_back({t, -delta, 0, false, why}); rr.d->set("action", Val::str("sell")); }
        else { buys.push_back({t, delta, why}); rr.d->set("action", Val::str("buy")); }
    }

    double cash_after = cash;
    for (const auto& s : sells) {
        const double p = prices.get(s.ticker); double usd = s.usd, qty;
        if (max_o != 0 && usd > max_o + EPS && !s.all) { r.notes.push_back(s.ticker + " sell cut to your " + money(max_o) + " max order; run again after it fills"); usd = max_o; }
        if (s.all) qty = s.qty;
        else {
            qty = usd / p;
            qty = whole ? std::floor(qty + EPS) : std::floor(qty * 10000 + EPS) / 10000.0;
            const double h = held.get(s.ticker); if (h < qty) qty = h;   // min(qty, held) keeps qty on a tie
        }
        if (qty <= EPS || qty * p < min_o - EPS) { r.notes.push_back(s.ticker + " sell of " + money(qty * p) + " is under your " + money(min_o) + " minimum order: skipped"); continue; }
        cash_after += qty * p;
        Val o = Val::dict(); o.d->set("symbol", Val::str(s.ticker)); o.d->set("side", Val::str("sell")); o.d->set("qty", Val::str(fmt_qty(qty)));
        o.d->set("type", Val::str("market")); o.d->set("time_in_force", Val::str("day")); o.d->set("why", Val::str(s.why)); o.d->set("_usd", Val::flt(round_n(qty * p, 2)));
        r.orders.push_back({o, "sell", round_n(qty * p, 2)});
    }
    const double spendable = std::max(cash_after - buf, 0.0);
    std::vector<double> wants; for (const auto& b : buys) wants.push_back(b.usd);
    const double want = fsum(wants); double scale = 1.0;
    if (want > spendable + EPS) { scale = want != 0 ? spendable / want : 0.0; r.notes.push_back("buys scaled to " + fmt_f(100 * scale, 0) + "%: only " + money(spendable) + " is free above your " + money(buf) + " cash buffer"); }
    for (const auto& b : buys) {
        const double p = prices.get(b.ticker); double usd = b.usd * scale;
        if (max_o != 0 && usd > max_o + EPS) { r.notes.push_back(b.ticker + " buy cut to your " + money(max_o) + " max order; run again after it fills"); usd = max_o; }
        Val o = Val::dict(); double ousd;
        o.d->set("symbol", Val::str(b.ticker)); o.d->set("side", Val::str("buy"));
        if (whole) {
            const double qty = std::floor(usd / p + EPS);
            if (qty < 1 || qty * p < min_o - EPS) { r.notes.push_back(b.ticker + " buy of " + money(usd) + " does not reach one whole share or your minimum: skipped"); continue; }
            o.d->set("qty", Val::str(std::to_string(static_cast<long long>(qty)))); ousd = round_n(qty * p, 2);
        } else {
            usd = std::floor(usd + EPS);
            if (usd < min_o - EPS || usd < 1) { r.notes.push_back(b.ticker + " buy of " + money(usd) + " is under your " + money(min_o) + " minimum order: skipped"); continue; }
            o.d->set("notional", Val::str(std::to_string(static_cast<long long>(usd)))); ousd = usd;
        }
        o.d->set("_usd", Val::flt(ousd)); o.d->set("type", Val::str("market")); o.d->set("time_in_force", Val::str("day")); o.d->set("why", Val::str(b.why));
        r.orders.push_back({o, "buy", ousd});
    }
    std::vector<double> spent_v; for (const auto& o : r.orders) if (o.side == "buy") spent_v.push_back(o.usd);
    r.account_value = round_n(total, 2); r.cash = round_n(cash, 2); r.cash_buffer = round_n(buf, 2); r.cap = round_n(cap, 2); r.band = band;
    r.cash_after = round_n(cash_after - fsum(spent_v), 2);
    return r;
}
inline Val clean(const std::vector<Order>& orders) {
    Val l = Val::list();
    for (const auto& o : orders) { Val c = Val::dict(); for (const auto& [k, v] : o.o.d->items) if (!starts(k, "_")) c.d->set(k, v); l.l->push_back(c); }
    return l;
}
inline std::string report(const Plan& res) {
    std::vector<std::string> lines;
    lines.push_back("Account " + money(res.account_value) + " (cash " + money(res.cash) + ", buffer " + money(res.cash_buffer) + ", cap " + money(res.cap) + " per stock), band " + n_(res.band) + " points");
    lines.emplace_back();
    lines.push_back(ljust("ticker", 7) + " " + rjust("shares", 10) + " " + rjust("price", 10) + " " + rjust("value", 11) + " " + rjust("now %", 9) + " " + rjust("target %", 9) + " " + rjust("drift", 8) + "  action");
    for (const auto& row : *res.rows.l) {
        lines.push_back(ljust(get(row, "ticker").s, 7) + " " + rjust(fmt_qty(get(row, "shares").num()), 10) + " " + rjust(money(get(row, "price").num()), 10) + " " + rjust(money(get(row, "value").num()), 11) + " "
            + rjust(fmt_f(get(row, "current_pct").num(), 2), 8) + "% " + rjust(fmt_f(get(row, "target_pct").num(), 2), 8) + "% " + rjust(fmt_fs(get(row, "drift").num(), 2), 8) + "  " + get(row, "action").s);
    }
    lines.emplace_back();
    if (!res.orders.empty()) {
        lines.push_back("Proposed orders, sells first (place_stock_order arguments, nothing has been placed):");
        for (const auto& o : res.orders) {
            const std::string amt = has(o.o, "notional") ? "notional \"" + get(o.o, "notional").s + "\"" : "qty \"" + get(o.o, "qty").s + "\"";
            lines.push_back("  " + o.side + " " + get(o.o, "symbol").s + " " + amt + ", market, day  (about " + money(o.usd) + ")");
        }
        lines.push_back("Cash after, if all of them fill: about " + money(res.cash_after) + ".");
    } else lines.push_back("Nothing is outside the band. No trades.");
    for (const auto& n : res.notes) lines.push_back("Note: " + n);
    lines.emplace_back();
    lines.push_back("Selling can realize a taxable gain: check trader-tax (lots sell) first. Then trader-risk checks every order and trader-broker asks you yes or no for each one.");
    lines.push_back("Education and tooling, not financial advice.");
    return join(lines, "\n");
}
inline Val plan_json(const Plan& r) {
    Val o = Val::dict();
    o.d->set("account_value", Val::flt(r.account_value)); o.d->set("cash", Val::flt(r.cash)); o.d->set("cash_buffer", Val::flt(r.cash_buffer));
    o.d->set("cap", Val::flt(r.cap)); o.d->set("band", Val::flt(r.band)); o.d->set("rows", r.rows); o.d->set("orders", clean(r.orders));
    Val notes = Val::list(); for (const auto& n : r.notes) notes.l->push_back(Val::str(n));
    o.d->set("notes", notes); o.d->set("cash_after", Val::flt(r.cash_after));
    return o;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("rebalance", "Bring the portfolio back to its target weights (trader-rebalance).");
    p.opt({"--portfolio"}).def("trader/portfolio.json");
    p.opt({"--prices"}).def("trader/prices.json");
    p.opt({"--targets"}).def("trader/targets.json");
    p.opt({"--config"}).def("trader/config.json");
    p.opt({"--band"}).number().help("drift band in percentage points (default 5)");
    p.opt({"--sell-untracked"}).store_true().help("sell holdings that are not in the targets");
    p.opt({"--write"}).nargs("?").cnst(Val::str("trader/orders.json")).metavar("PATH").help("write the proposals (default trader/orders.json)");
    p.opt({"--json"}).store_true();
    const Val a = parse_args(p, argv);
    Plan res; Val pdoc, cfg;
    const std::string write = S(a, "write");
    try {
        cfg = orv(read_json(S(a, "config"), "config", false), Val::dict());
        const Val portfolio = read_json(S(a, "portfolio"), "portfolio (your holdings and cash)");
        pdoc = read_json(S(a, "prices"), "prices (trader-alerts: alerts quote --holdings --write)");
        ODict prices;
        const Val pv = orv(dget(pdoc, "prices", pdoc), Val::dict());
        if (pv.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(pv) + "' object has no attribute 'items'");
        for (const auto& [k, v] : pv.d->items) if (v.is_real()) prices[upper(k)] = v.num();
        const ODict weights = parse_targets(read_json(S(a, "targets"), "targets"), dget(cfg, "blocked"));
        const Val band = get(a, "band");
        res = plan(portfolio, prices, weights, cfg, band.is_none() ? std::nullopt : std::optional<double>(band.num()), B(a, "sell_untracked"));
        if (!write.empty() && !res.orders.empty()) {
            const std::string d = dirname(write); if (!d.empty()) makedirs(d);
            Val doc = Val::dict();
            doc.d->set("created_by", Val::str("trader-rebalance")); doc.d->set("created_at", Val::str(now_iso())); doc.d->set("status", Val::str("proposed"));
            doc.d->set("paper", Val::boolean(truthy(dget(cfg, "paper", Val::boolean(true)))));
            doc.d->set("note", Val::str("Proposals only, sells first. Nothing is placed until trader-risk passes each order and you say yes to it in the chat."));
            doc.d->set("prices_as_of", dget(pdoc, "as_of", Val::str(""))); doc.d->set("orders", clean(res.orders));
            DumpOpts o; o.indent = 2; write_text(write, dumps(doc, o) + "\n");
        }
    } catch (const RebalanceError& e) { eprint(std::string("rebalance: ") + e.what()); return 1; }
    if (B(a, "json")) { DumpOpts o; o.indent = 2; print(dumps(plan_json(res), o)); }
    else {
        print(report(res));
        if (!write.empty() && !res.orders.empty()) print("Wrote " + std::to_string(res.orders.size()) + " proposed order(s) to " + write + ".");
    }
    return res.orders.empty() ? 1 : 0;
}
PACKTOOL("trader-rebalance/rebalance", main);
}  // namespace packtools::trader_rebalance
