// trader-budget/size - position sizing: turns target weights and your rules (budget, cash buffer, a cap on any one
// stock, a minimum order) into dollars per stock, then into proposed orders shaped like Alpaca's place_stock_order.
// Proposals only: nothing is placed. Money that cannot be invested is reported as "left idle". No network.
// C++ port of trader-skill/trader-budget/size.py (Jake Schincariol, MIT); same output, same exit codes.
namespace packtools::trader_budget {
using namespace pyrt;
struct SizeError : std::runtime_error { using std::runtime_error::runtime_error; };
constexpr double EPS = 1e-9;
inline Val defaults() {
    Val d = Val::dict();
    d.d->set("budget", Val()); d.d->set("cash_buffer_pct", Val::integer(10)); d.d->set("max_position_pct", Val::integer(25)); d.d->set("max_order_usd", Val::integer(2000));
    d.d->set("min_order_usd", Val::integer(5)); d.d->set("fractional", Val::boolean(true)); d.d->set("broker", Val::str("alpaca")); d.d->set("paper", Val::boolean(true));
    d.d->set("blocked", Val::list()); d.d->set("allow_options", Val::boolean(false));
    return d;
}
inline Val read_json(const std::string& path, const std::string& what) {
    if (!is_dir(path) && !exists(path)) throw SizeError("no " + what + " at " + path);
    try { return loads(read_text(path)); } catch (const JSONDecodeError& e) { throw SizeError(path + " is not valid JSON: " + e.what()); }
}
inline Val load_config(const std::string& path) {
    Val cfg = defaults();
    if (!path.empty() && exists(path)) {
        const Val c = read_json(path, "config");
        if (c.t != Val::DICT) throw PyErr("ValueError", "dictionary update sequence element #0 has the wrong length");
        for (const auto& [k, v] : c.d->items) cfg.d->set(k, v);
    }
    return cfg;
}
struct Row { std::string ticker; Val name; double weight = 0, source_weight = 0; };
inline std::vector<Row> parse_weights(Val data) {
    if (data.t == Val::DICT && data.d->has("weights")) data = data.d->at("weights");
    struct Raw { Val ticker, name, weight; };
    std::vector<Raw> rows;
    if (data.t == Val::DICT) for (const auto& [t, w] : data.d->items) rows.push_back({Val::str(t), Val::str(""), w});
    else if (data.t == Val::LIST) {
        for (std::size_t i = 0; i < data.l->size(); ++i) {
            const Val& r = (*data.l)[i];
            if (r.t != Val::DICT) throw SizeError("weights[" + std::to_string(i) + "] is not an object");
            const Val t = orv(r.d->at("ticker"), r.d->at("symbol"));
            const Val w = r.d->at("weight", r.d->at("weight_pct", r.d->at("pct")));
            if (!truthy(t) || w.is_none()) throw SizeError("weights[" + std::to_string(i) + "] needs a ticker and a weight");
            rows.push_back({t, r.d->at("name", Val::str("")), w});
        }
    } else throw SizeError("weights must be a list or an object");
    std::vector<Row> out;
    for (const auto& r : rows) {
        double w;
        try { w = to_float(r.weight); } catch (const PyErr&) { throw SizeError("weight for " + pystr(r.ticker) + " is not a number: " + repr(r.weight)); }
        out.push_back({upper(strip(pystr(r.ticker))), orv(r.name, Val::str("")), w, 0});
    }
    return out;
}
// AAPL=22.04,AXP=17.14 -> {ticker: weight}
inline Val weights_from_arg(const std::string& text) {
    Val out = Val::dict();
    for (std::string part : split(text, ",")) {
        part = strip(part);
        if (part.empty()) continue;
        if (part.find('=') == std::string::npos) throw SizeError("--weights wants TICKER=WEIGHT pairs, got " + str_repr(part));
        const auto kv = split(part, "=", 1);
        out.d->set(upper(strip(kv[0])), Val::str(strip(kv[1])));
    }
    return out;
}
inline ODict load_prices(const std::string& path) {
    ODict out; if (path.empty()) return out;
    const Val data = read_json(path, "prices");
    const Val prices = data.t == Val::DICT ? data.d->at("prices", data) : Val::dict();
    if (prices.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(prices) + "' object has no attribute 'items'");
    for (const auto& [t, p] : prices.d->items) if (const auto f = f_(p)) out[upper(t)] = *f;
    return out;
}
struct Dropped { std::string ticker, reason; };
// Drop blocked and non-positive weights, keep the top N, renormalise to 100.
inline std::vector<Row> normalise(const std::vector<Row>& rows, const Val& blocked_v, long long top, std::vector<Dropped>& dropped) {
    std::set<std::string> blocked; for (const auto& b : iter(orv(blocked_v, Val::list()))) blocked.insert(upper(pystr(b)));
    std::vector<Row> keep; std::set<std::string> seen;
    for (const auto& r : rows) {
        if (seen.count(r.ticker)) throw SizeError(r.ticker + " is listed twice in the weights");
        seen.insert(r.ticker);
        if (blocked.count(r.ticker)) dropped.push_back({r.ticker, "on your blocked list"});
        else if (r.weight <= 0) dropped.push_back({r.ticker, "weight is zero or negative"});
        else keep.push_back(r);
    }
    std::stable_sort(keep.begin(), keep.end(), [](const Row& a, const Row& b) { return -a.weight < -b.weight; });
    if (top) {
        const long long n = static_cast<long long>(keep.size());
        const long long cut = top >= 0 ? std::min(top, n) : std::max(0LL, n + top);   // keep[:top] with Python slicing
        for (long long k = cut; k < n; ++k) dropped.push_back({keep[static_cast<std::size_t>(k)].ticker, "outside the top " + std::to_string(top)});
        keep.resize(static_cast<std::size_t>(cut));
    }
    std::vector<double> ws; for (const auto& r : keep) ws.push_back(r.weight);
    const double total = fsum(ws);
    for (auto& r : keep) { r.source_weight = r.weight; r.weight = total != 0 ? 100.0 * r.weight / total : 0.0; }
    return keep;
}
// Exact (unrounded) dollars per ticker with the cap enforced.
inline ODict allocate(const std::vector<Row>& rows, double investable, double cap, std::set<std::string>& capped) {
    ODict alloc; std::vector<Row> active = rows; double money = investable;
    while (!active.empty()) {
        std::vector<double> ws; for (const auto& r : active) ws.push_back(r.weight);
        const double wsum = fsum(ws);
        ODict trial; for (const auto& r : active) trial[r.ticker] = money * r.weight / wsum;
        std::vector<std::string> over; for (const auto& r : active) if (trial.get(r.ticker) > cap + EPS) over.push_back(r.ticker);
        if (over.empty()) { for (const auto& [t, v] : trial.items) alloc[t] = v; break; }
        for (const auto& t : over) { alloc[t] = cap; capped.insert(t); money -= cap; }
        std::vector<Row> next; for (const auto& r : active) if (!capped.count(r.ticker)) next.push_back(r);
        active.swap(next);
        if (money <= EPS) { for (const auto& r : active) alloc[r.ticker] = 0.0; break; }
    }
    return alloc;
}
inline long long ifloor(double x) { return static_cast<long long>(std::floor(x)); }
// Round to whole dollars (unit 1) or cents (unit 0.01) and keep the total.
inline ODict round_to_unit(const ODict& exact, const std::set<std::string>& capped, const std::vector<std::string>& order, double cap, double unit) {
    const auto units = [&](double x) { return ifloor(x / unit + EPS); };
    const long long cap_u = units(cap), total_u = units(fsum(exact.values()));
    std::vector<std::pair<std::string, long long>> out;
    for (const auto& [t, v] : exact.items) out.emplace_back(t, capped.count(t) ? cap_u : ifloor(v / unit + 0.5 + EPS));
    const auto at = [&](const std::string& t) -> long long& { for (auto& [a, u] : out) if (a == t) return u; throw std::logic_error("ticker"); };
    long long sum_u = 0; for (const auto& [t, u] : out) sum_u += u;
    long long diff = total_u - sum_u;
    std::vector<std::string> takers; for (const auto& t : order) if (!capped.count(t)) takers.push_back(t);
    int guard = 0;
    while (diff != 0 && !takers.empty() && guard < 100000) {
        bool moved = false;
        for (const auto& t : takers) {
            if (diff == 0) break;
            long long& u = at(t);
            if (diff > 0 && u + 1 <= cap_u) { u += 1; diff -= 1; moved = true; }
            else if (diff < 0 && u > 0) { u -= 1; diff += 1; moved = true; }
        }
        if (!moved) break;
        guard++;
    }
    ODict r; for (const auto& [t, u] : out) r[t] = round_n(static_cast<double>(u) * unit, 2);
    return r;
}
// Whole shares: floor each slice, then buy one more share at a time for the stock furthest below its slice.
inline std::map<std::string, long long> whole_shares(const ODict& dollars, const ODict& prices, const std::vector<std::string>& order, double cap, double money) {
    std::map<std::string, long long> shares;
    for (const auto& t : order) shares[t] = dollars.get(t) > 0 ? ifloor(dollars.get(t) / prices.get(t) + EPS) : 0;
    std::vector<double> sp; for (const auto& t : order) sp.push_back(static_cast<double>(shares[t]) * prices.get(t));
    double left = money - fsum(sp);
    while (true) {
        std::optional<std::string> best; double gap = 0;
        for (const auto& t : order) {
            const double p = prices.get(t);
            if (p > left + EPS || static_cast<double>(shares[t] + 1) * p > cap + EPS) continue;
            const double g = dollars.get(t) - static_cast<double>(shares[t]) * p;
            if (!best || g > gap + EPS) { best = t; gap = g; }
        }
        if (!best) break;
        shares[*best] += 1; left -= prices.get(*best);
    }
    return shares;
}
// 1250 -> "1250", 1118.13 -> "1118.13" (Alpaca wants these as strings).
inline std::string num_(double x0) {
    const double x = round_n(x0, 2);
    if (std::fabs(x - static_cast<double>(round_int(x))) < 0.005) return std::to_string(round_int(x));
    return fmt_f(x, 2);
}
inline std::string money(double x) {
    if (std::fabs(x - static_cast<double>(round_int(x))) < 0.005) return "$" + commas(std::to_string(round_int(x)));
    return "$" + fmt_fc(x, 2);
}
inline Val size(const std::vector<Row>& rows, const Val& cfg, std::optional<double> budget_arg, const ODict& prices, std::optional<bool> whole_arg, bool cents, long long top) {
    const double budget = budget_arg ? *budget_arg : to_float(orv(dget(cfg, "budget"), Val::integer(0)));
    if (budget <= 0) throw SizeError("no budget: pass --budget or set \"budget\" in trader/config.json");
    const double buf_pct = to_float(dget(cfg, "cash_buffer_pct", Val::integer(10)));
    const double cap_pct = to_float(dget(cfg, "max_position_pct", Val::integer(25)));
    const double min_order = to_float(orv(dget(cfg, "min_order_usd", Val::integer(5)), Val::integer(0)));
    if (!(0 <= buf_pct && buf_pct < 100)) throw SizeError("cash_buffer_pct must be between 0 and 100");
    if (!(0 < cap_pct && cap_pct <= 100)) throw SizeError("max_position_pct must be above 0 and at most 100");
    const bool whole = whole_arg ? *whole_arg : !truthy(dget(cfg, "fractional", Val::boolean(true)));
    std::vector<Dropped> dropped;
    std::vector<Row> keep = normalise(rows, dget(cfg, "blocked"), top, dropped);
    if (keep.empty()) throw SizeError("nothing to size: no weights left after the blocked list");
    const double cash_buffer = round_n(budget * buf_pct / 100.0, 2), investable = round_n(budget - cash_buffer, 2), cap = round_n(budget * cap_pct / 100.0, 2);
    const double unit = (cents || whole) ? 0.01 : 1.0;
    if (whole) {
        std::vector<std::string> missing; for (const auto& r : keep) if (!prices.get(r.ticker)) missing.push_back(r.ticker);
        if (!missing.empty()) throw SizeError("whole shares need a price for: " + join(missing, ", ") + " (add them to trader/prices.json, e.g. with trader-alerts: alerts quote " + join(missing, " ") + " --write)");
    }
    std::vector<Row> live; ODict dollars; std::set<std::string> capped; std::map<std::string, long long> shares;
    while (true) {
        live = keep;
        std::vector<double> sw; for (const auto& r : live) sw.push_back(r.source_weight);
        const double tot = fsum(sw);
        for (auto& r : live) r.weight = 100.0 * r.source_weight / tot;
        capped.clear();
        const ODict exact = allocate(live, investable, cap, capped);
        std::vector<Row> sorted = live; std::stable_sort(sorted.begin(), sorted.end(), [&](const Row& a, const Row& b) { return -exact.get(a.ticker) < -exact.get(b.ticker); });
        std::vector<std::string> order; for (const auto& r : sorted) order.push_back(r.ticker);
        dollars = round_to_unit(exact, capped, order, cap, unit);
        shares.clear(); bool have_shares = false;
        if (whole) {
            shares = whole_shares(dollars, prices, order, cap, fsum(dollars.values())); have_shares = true;
            ODict d2; for (const auto& t : order) d2[t] = round_n(static_cast<double>(shares[t]) * prices.get(t), 2);
            dollars = d2;
        }
        std::vector<const Row*> small; for (const auto& r : live) if (dollars.get(r.ticker) < min_order - EPS) small.push_back(&r);
        if (small.empty()) break;
        const Row* worst = *std::min_element(small.begin(), small.end(), [](const Row* a, const Row* b) { return a->source_weight != b->source_weight ? a->source_weight < b->source_weight : a->ticker < b->ticker; });
        std::string reason;
        if (whole && have_shares && shares[worst->ticker] == 0 && prices.get(worst->ticker, 0) > 0)
            reason = "one share costs $" + num_(prices.get(worst->ticker)) + ", more than its slice; it cannot be bought in whole shares";
        else reason = "its slice ($" + num_(dollars.get(worst->ticker)) + ") is under your $" + num_(min_order) + " minimum order";
        const std::string wt = worst->ticker;
        dropped.push_back({wt, reason});
        std::vector<Row> k2; for (const auto& r : keep) if (r.ticker != wt) k2.push_back(r);
        keep.swap(k2);
        if (keep.empty()) throw SizeError("every slice is under your minimum order; raise the budget or hold fewer stocks");
    }
    std::vector<Row> by_w = live; std::stable_sort(by_w.begin(), by_w.end(), [](const Row& a, const Row& b) { return -a.weight < -b.weight; });
    Val positions = Val::list(); std::vector<double> inv;
    for (const auto& r : by_w) {
        const std::string& t = r.ticker;
        Val p = Val::dict();
        p.d->set("ticker", Val::str(t)); p.d->set("name", r.name); p.d->set("weight", Val::flt(round_n(r.weight, 2)));
        p.d->set("source_weight", Val::flt(r.source_weight)); p.d->set("dollars", Val::flt(dollars.get(t))); p.d->set("capped", Val::boolean(capped.count(t) > 0));
        if (prices.get(t)) { p.d->set("price", Val::flt(prices.get(t))); p.d->set("shares", whole ? Val::integer(shares[t]) : Val::flt(round_n(dollars.get(t) / prices.get(t), 4))); }
        positions.l->push_back(p); inv.push_back(dollars.get(t));
    }
    const double invested = round_n(fsum(inv), 2);
    Val res = Val::dict();
    res.d->set("budget", Val::flt(budget)); res.d->set("cash_buffer_pct", Val::flt(buf_pct)); res.d->set("cash_buffer", Val::flt(cash_buffer));
    res.d->set("investable", Val::flt(investable)); res.d->set("max_position_pct", Val::flt(cap_pct)); res.d->set("cap", Val::flt(cap));
    res.d->set("min_order_usd", Val::flt(min_order)); res.d->set("whole_shares", Val::boolean(whole));
    res.d->set("positions", positions); res.d->set("invested", Val::flt(invested)); res.d->set("idle", Val::flt(round_n(investable - invested, 2)));
    Val dl = Val::list(); for (const auto& d : dropped) { Val x = Val::dict(); x.d->set("ticker", Val::str(d.ticker)); x.d->set("reason", Val::str(d.reason)); dl.l->push_back(x); }
    res.d->set("dropped", dl);
    return res;
}
// Proposed buy orders, one per position, in Alpaca place_stock_order shape.
inline Val orders_from(const Val& result, const Val& source) {
    Val out = Val::list();
    for (const auto& p : *get(result, "positions").l) {
        if (get(p, "dollars").num() <= 0) continue;
        std::string why = fmt_f(get(p, "weight").num(), 2) + "% of the stock sleeve";
        if (truthy(source)) why = why + ", copying " + pystr(source);
        if (get(p, "capped").b) why += "; capped at " + num_(get(result, "max_position_pct").num()) + "% of the " + money(get(result, "budget").num()) + " budget";
        Val o = Val::dict(); o.d->set("symbol", get(p, "ticker")); o.d->set("side", Val::str("buy"));
        if (get(result, "whole_shares").b) o.d->set("qty", Val::str(std::to_string(get(p, "shares").i)));
        else o.d->set("notional", Val::str(num_(get(p, "dollars").num())));
        o.d->set("type", Val::str("market")); o.d->set("time_in_force", Val::str("day")); o.d->set("why", Val::str(why));
        out.l->push_back(o);
    }
    return out;
}
inline std::string name22(const Val& n) {
    if (!truthy(n)) return "";
    if (n.t == Val::STR) return cpslice(n.s, 0, 22);
    throw PyErr("TypeError", std::string("'") + type_name(n) + "' object is not subscriptable");
}
inline std::string report(const Val& r, const Val& orders, const Val& source) {
    std::vector<std::string> lines;
    if (truthy(source)) lines.push_back("Copying: " + pystr(source));
    lines.push_back("Budget " + money(get(r, "budget").num()) + ", cash buffer " + num_(get(r, "cash_buffer_pct").num()) + "% = " + money(get(r, "cash_buffer").num()) + ", investing " + money(get(r, "investable").num())
        + ", cap " + num_(get(r, "max_position_pct").num()) + "% = " + money(get(r, "cap").num()) + " per stock");
    lines.emplace_back();
    bool has_price = false; for (const auto& p : *get(r, "positions").l) if (has(p, "price")) has_price = true;
    std::string head = ljust("ticker", 7) + " " + ljust("name", 22) + " " + rjust("weight", 8) + " " + rjust("dollars", 11);
    if (has_price) head += " " + rjust("price", 10) + " " + rjust("shares", 10);
    lines.push_back(head + "  note");
    for (const auto& p : *get(r, "positions").l) {
        std::string row = ljust(get(p, "ticker").s, 7) + " " + ljust(name22(get(p, "name")), 22) + " " + rjust(fmt_f(get(p, "weight").num(), 2), 7) + "% " + rjust(money(get(p, "dollars").num()), 11);
        if (has_price) row += " " + rjust(has(p, "price") ? money(get(p, "price").num()) : "", 10) + " " + rjust(has(p, "shares") ? pystr(get(p, "shares")) : "", 10);
        if (get(p, "capped").b) row += "  capped at " + num_(get(r, "max_position_pct").num()) + "%";
        lines.push_back(row);
    }
    for (const auto& [label, key] : std::vector<std::pair<std::string, std::string>>{{"invested", "invested"}, {"cash buffer", "cash_buffer"}, {"left idle", "idle"}})
        lines.push_back(ljust("", 7) + " " + ljust(label, 22) + " " + rjust("", 8) + " " + rjust(money(get(r, key).num()), 11));
    if (get(r, "idle").num() >= 1) {
        lines.emplace_back();
        lines.push_back("Left idle " + money(get(r, "idle").num()) + ": " + (get(r, "whole_shares").b ? "whole shares do not divide evenly" : "every stock is at its cap; add stocks or raise max_position_pct"));
    }
    for (const auto& d : *get(r, "dropped").l) lines.push_back("Dropped " + get(d, "ticker").s + ": " + get(d, "reason").s);
    lines.emplace_back();
    lines.push_back("Proposed orders (place_stock_order arguments, nothing has been placed):");
    for (const auto& o : *orders.l) {
        const std::string amount = has(o, "notional") ? "notional \"" + get(o, "notional").s + "\"" : "qty \"" + get(o, "qty").s + "\"";
        lines.push_back("  buy " + get(o, "symbol").s + " " + amount + ", market, day");
    }
    lines.emplace_back();
    lines.push_back("Next: trader-risk checks every order, then trader-broker asks you yes or no for each one.");
    lines.push_back("Education and tooling, not financial advice.");
    return join(lines, "\n");
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("size", "Size positions to your budget (trader-budget).");
    p.opt({"--targets"}).def("trader/targets.json").help("target weights (default trader/targets.json)");
    p.opt({"--weights"}).help("inline weights instead of a file: AAPL=22.04,AXP=17.14");
    p.opt({"--config"}).def("trader/config.json").help("rules (default trader/config.json)");
    p.opt({"--budget"}).number().help("override the budget in the config");
    p.opt({"--top"}).integer().help("keep only the N biggest weights");
    p.opt({"--prices"}).help("prices JSON (needed for --whole-shares; shows share counts)");
    p.opt({"--whole-shares"}).store_true().help("buy whole shares only (needs --prices)");
    p.opt({"--cents"}).store_true().help("size to the cent instead of whole dollars");
    p.opt({"--write"}).nargs("?").cnst(Val::str("trader/orders.json")).metavar("PATH").help("write the proposed orders (default trader/orders.json)");
    p.opt({"--json"}).store_true().help("print JSON instead of the table");
    const Val a = parse_args(p, argv);
    Val result, orders, source = Val::str(""), cfg;
    const std::string write = S(a, "write");
    try {
        cfg = load_config(S(a, "config"));
        std::vector<Row> rows;
        if (truthy(get(a, "weights"))) rows = parse_weights(weights_from_arg(S(a, "weights")));
        else {
            const Val data = read_json(S(a, "targets"), "targets file (run trader-copycat, or pass --weights)");
            rows = parse_weights(data);
            if (data.t == Val::DICT) source = data.d->at("source", Val::str(""));
        }
        ODict prices = load_prices(S(a, "prices"));
        if (prices.empty() && exists("trader/prices.json") && !truthy(get(a, "prices"))) prices = load_prices("trader/prices.json");
        const Val budget = get(a, "budget"), top = get(a, "top");
        result = size(rows, cfg, budget.is_none() ? std::nullopt : std::optional<double>(budget.num()), prices,
                      B(a, "whole_shares") ? std::optional<bool>(true) : std::nullopt, B(a, "cents"), top.is_none() ? 0 : top.i);
        orders = orders_from(result, source);
        if (!write.empty()) {
            const std::string d = dirname(write); if (!d.empty()) makedirs(d);
            Val doc = Val::dict();
            doc.d->set("created_by", Val::str("trader-budget")); doc.d->set("created_at", Val::str(now_iso())); doc.d->set("status", Val::str("proposed"));
            doc.d->set("paper", Val::boolean(truthy(dget(cfg, "paper", Val::boolean(true)))));
            doc.d->set("note", Val::str("Proposals only. Nothing is placed until trader-risk passes each order and you say yes to it in the chat."));
            doc.d->set("source", source); doc.d->set("orders", orders);
            DumpOpts o; o.indent = 2; write_text(write, dumps(doc, o) + "\n");
        }
    } catch (const SizeError& e) { eprint(std::string("size: ") + e.what()); return 1; }
    if (B(a, "json")) { Val j = Val::dict(); j.d->set("result", result); j.d->set("orders", orders); DumpOpts o; o.indent = 2; print(dumps(j, o)); }
    else {
        print(report(result, orders, source));
        if (!write.empty()) print("Wrote " + std::to_string(orders.l->size()) + " proposed orders to " + write);
    }
    return 0;
}
PACKTOOL("trader-budget/size", main);
}  // namespace packtools::trader_budget
