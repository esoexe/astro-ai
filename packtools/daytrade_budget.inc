// daytrade-budget/size - sizes ONE day trade from trader/plan.json (written by trader-exit) to your budget and your max
// loss per trade, then writes one bracket order (entry, stop-loss, take-profit) to trader/orders.json in the shape of
// Alpaca's place_stock_order. Whole shares: the smallest of max loss / risk a share, the position cap, the max order and
// the cash after the buffer. A market order is sized at the plan's entry, or at a fresh (under 15 minutes) higher price.
// Weights mode (v1) splits a budget across a list. Proposals only: nothing is placed. No network.
// C++ port of day-trader-skill/trader-budget/size.py (Jake Schincariol, MIT); same output, same exit codes. The weights
// maths is identical to trader-skill's, so it is shared with trader-budget/size.
namespace packtools::daytrade_budget {
using namespace pyrt;
using trader_budget::SizeError; using trader_budget::read_json; using trader_budget::parse_weights; using trader_budget::weights_from_arg;
using trader_budget::load_prices; using trader_budget::size; using trader_budget::num_; using trader_budget::money; using trader_budget::name22;
using trader_budget::Row;
using trader_alerts::to_eastern; using trader_alerts::eastern_to_utc; using trader_alerts::day_of; using trader_alerts::day_iso; using trader_alerts::strf;
constexpr double EPS = 1e-9;
constexpr int FRESH_MINUTES = 15;
inline Val defaults() {   // v2 defaults: the contract's config.example.json
    Val d = Val::dict();
    d.d->set("budget", Val()); d.d->set("cash_buffer_pct", Val::integer(10)); d.d->set("max_position_pct", Val::integer(75)); d.d->set("max_order_usd", Val::integer(4000));
    d.d->set("min_order_usd", Val::integer(5)); d.d->set("max_loss_per_trade_usd", Val::integer(50)); d.d->set("max_daily_loss_usd", Val::integer(150));
    d.d->set("max_trades_per_day", Val::integer(3)); d.d->set("max_orders_per_day", Val::integer(10)); d.d->set("min_reward_risk", Val::flt(1.0));
    d.d->set("require_stop", Val::boolean(true)); d.d->set("no_entries_before", Val::str("09:45")); d.d->set("no_entries_after", Val::str("15:30")); d.d->set("flat_by", Val::str("15:55"));
    d.d->set("fractional", Val::boolean(false)); d.d->set("broker", Val::str("alpaca")); d.d->set("paper", Val::boolean(true)); d.d->set("blocked", Val::list());
    d.d->set("allow_options", Val::boolean(false)); d.d->set("allow_margin", Val::boolean(false)); d.d->set("allow_short", Val::boolean(false)); d.d->set("pdt_guard", Val::boolean(false));
    return d;
}
inline void update(Val& into, const Val& from) {   // dict.update
    if (from.t != Val::DICT) throw PyErr("ValueError", "dictionary update sequence element #0 has the wrong length");
    for (const auto& [k, v] : from.d->items) into.d->set(k, v);
}
inline Val load_config(const std::string& path) {
    Val cfg = defaults();
    if (!path.empty() && exists(path)) update(cfg, read_json(path, "config"));
    return cfg;
}
// ---- time (New York)
inline long long parse_now(const Val& text) {   // ISO time -> naive UTC; no offset = New York time
    if (!truthy(text)) return now_us() / 1000000 * 1000000;
    const std::string t = replace_all(strip(pystr(text)), "Z", "+00:00");
    const auto us = fromiso_us(t);
    if (!us) throw SizeError("--now wants a time like \"2026-10-19 10:15\" (New York) or ...Z (UTC)");
    const std::size_t base = t.size() >= 10 && t[4] == '-' ? 10 : 8;
    const bool aware = t.size() > base + 1 && t.find_first_of("+-", base + 1) != std::string::npos;
    return aware ? *us : eastern_to_utc(*us);
}
inline std::optional<long long> parse_utc(const Val& text) {   // "2026-10-19T14:52:13Z" -> naive UTC
    if (!truthy(text)) return std::nullopt;
    return fromiso_us(replace_all(strip(pystr(text)), "Z", "+00:00"));
}
inline std::string ny_fmt(long long utc, bool with_date) { const std::string s = strf(to_eastern(utc), false); return with_date ? s : s.substr(11); }   // %Y-%m-%d %H:%M / %H:%M
// when this symbol's price is from: the quote's own time, else when it was fetched, else the file's as_of
inline std::optional<long long> price_time(const Val& doc, const std::string& sym) {
    const Val det = orv(dget(orv(dget(orv(doc, Val::dict()), "detail"), Val::dict()), sym), Val::dict());
    for (const char* key : {"time", "fetched"}) if (const auto t = parse_utc(dget(det, key))) return t;
    return parse_utc(dget(orv(doc, Val::dict()), "as_of"));
}
struct Fresh { std::optional<double> price; std::optional<long long> at; bool fresh = false; };
inline Fresh fresh_price(const Val& doc, const std::string& sym, long long now_utc, int minutes = FRESH_MINUTES) {
    if (doc.t != Val::DICT) return {};
    const auto p = f_(dget(orv(dget(doc, "prices"), Val::dict()), sym));
    if (!p || *p <= 0) return {};
    const auto t = price_time(doc, sym);
    if (!t) return {p, std::nullopt, false};
    const double age = static_cast<double>(now_utc - *t) / 1e6;
    return {p, t, -60 <= age && age <= minutes * 60};
}
// ---- money helpers
inline double round_cent(double x) { return std::floor(x * 100 + 0.5 + EPS) / 100.0; }   // to the cent, half up
inline long long shares_for(double usd, double per_share) {   // whole shares in exact integer cents
    const long long u = round_int(usd * 100), p = round_int(per_share * 100);
    if (p <= 0 || u <= 0) return 0;
    return u / p;
}
inline std::string price_(double x) { return fmt_f(round_cent(x), 2); }
inline Val optf(const std::optional<double>& x) { return x ? Val::flt(*x) : Val(); }
inline std::string setup_word(const Val& setup, const char* dflt) {
    const std::string s = setup.t == Val::STR ? setup.s : std::string();
    if (s == "orb") return "opening-range breakout";
    if (s == "flag") return "flag";
    if (s == "bounce") return "oversold bounce";
    return truthy(setup) ? pystr(setup) : std::string(dflt);
}
// ---- plan mode: one plan + your rules -> the sizing (qty may be 0, then "refusal" says why)
inline Val size_trade(const Val& plan, const Val& cfg, std::optional<double> budget_arg, std::optional<double> limit, std::optional<double> price, std::optional<double> cash, const std::string& today) {
    Val c = defaults(); if (truthy(cfg)) update(c, cfg);
    if (plan.t != Val::DICT) throw SizeError("the plan is not a JSON object");
    const std::string sym = upper(strip(pystr(orv(dget(plan, "symbol"), Val::str("")))));
    if (sym.empty()) throw SizeError("the plan has no symbol (run trader-exit)");
    const std::string side = lower(strip(pystr(orv(dget(plan, "side"), Val::str("buy")))));
    if (side != "buy") throw SizeError("this pack sizes long (buy) day trades only; the plan says " + repr(Val::str(side)));
    if (!today.empty() && truthy(dget(plan, "date")) && pystr(get(plan, "date")) != today)
        throw SizeError("the plan is for " + pystr(get(plan, "date")) + " and today is " + today + " in New York: make a new plan with trader-exit");
    const auto entry_plan = f_(dget(plan, "entry")), stop0 = f_(dget(plan, "stop")), target0 = f_(dget(plan, "target"));
    for (const auto& [name, v] : std::vector<std::pair<std::string, std::optional<double>>>{{"entry", entry_plan}, {"stop", stop0}, {"target", target0}})
        if (!v || *v <= 0) throw SizeError("the plan needs a positive " + name + " (run trader-exit); a bracket order needs entry, stop and target");
    std::set<std::string> blocked; for (const auto& b : iter(orv(dget(c, "blocked"), Val::list()))) blocked.insert(upper(pystr(b)));
    if (blocked.count(sym)) throw SizeError(sym + " is on your blocked list");
    const double budget = budget_arg ? *budget_arg : to_float(orv(dget(c, "budget"), Val::integer(0)));
    if (budget <= 0) throw SizeError("no budget: pass --budget or set \"budget\" in trader/config.json");
    const double buf_pct = to_float(orv(dget(c, "cash_buffer_pct"), Val::integer(0)));
    if (!(0 <= buf_pct && buf_pct < 100)) throw SizeError("cash_buffer_pct must be between 0 and 100");
    double ref; std::string ref_from;
    if (limit) { ref = round_cent(*limit); ref_from = "limit"; }
    else if (price && round_cent(*price) > round_cent(*entry_plan)) { ref = round_cent(*price); ref_from = "price"; }
    else { ref = round_cent(*entry_plan); ref_from = "plan"; }
    const double stop = round_cent(*stop0), target = round_cent(*target0);
    if (stop >= ref) throw SizeError("the stop " + price_(stop) + " is not below the entry " + price_(ref) + "; a long trade needs its stop underneath");
    if (target <= ref) throw SizeError("the target " + price_(target) + " is not above the entry " + price_(ref) + ": the move has already happened. Skip it or make a new plan with trader-exit");
    const long long risk_c = round_int((ref - stop) * 100), reward_c = round_int((target - ref) * 100);
    const double risk = static_cast<double>(risk_c) / 100.0;
    const double cash_buffer = round_n(budget * buf_pct / 100.0, 2);
    double available = budget - cash_buffer;
    std::string cash_note = "budget " + money(budget) + " minus the " + money(cash_buffer) + " cash buffer";
    if (cash && *cash - cash_buffer < available - 0.005) { available = *cash - cash_buffer; cash_note = "cash " + money(*cash) + " in trader/portfolio.json minus the " + money(cash_buffer) + " buffer"; }
    available = std::max(round_n(available, 2), 0.0);
    struct Lim { std::string name; double usd, per; long long shares; std::string how; };
    std::vector<Lim> limits;
    const auto max_loss = f_(dget(c, "max_loss_per_trade_usd"));
    if (max_loss && *max_loss != 0) limits.push_back({"max loss", *max_loss, risk, shares_for(*max_loss, risk), money(*max_loss) + " max loss / " + price_(risk) + " risk a share"});
    const auto cap_pct = f_(dget(c, "max_position_pct"));
    if (cap_pct && *cap_pct != 0) { const double cap = round_n(budget * *cap_pct / 100.0, 2); limits.push_back({"position cap", cap, ref, shares_for(cap, ref), num_(*cap_pct) + "% of " + money(budget) + " = " + money(cap) + " / " + price_(ref)}); }
    const auto max_order = f_(dget(c, "max_order_usd"));
    if (max_order && *max_order != 0) limits.push_back({"max order", *max_order, ref, shares_for(*max_order, ref), money(*max_order) + " / " + price_(ref)});
    limits.push_back({"cash", available, ref, shares_for(available, ref), cash_note + " = " + money(available) + " / " + price_(ref)});
    long long qty = limits[0].shares; for (const auto& l : limits) qty = std::min(qty, l.shares); qty = std::max(0LL, qty);
    std::string bound; for (const auto& l : limits) if (l.shares == qty) { bound = l.name; break; }
    const double cost = round_n(static_cast<double>(qty) * ref, 2), loss = round_n(static_cast<double>(qty * risk_c) / 100.0, 2);
    Val refusal;
    const auto mrr = f_(dget(c, "min_reward_risk"));
    if (qty == 0) {
        if (bound == "max loss") refusal = Val::str("one share risks " + money(risk) + " (entry " + price_(ref) + " minus stop " + price_(stop) + "), more than your " + money(*max_loss) + " max loss per trade. No order. Skip this trade, or plan a closer stop with trader-exit.");
        else { std::string how; for (const auto& l : limits) if (l.name == bound) { how = l.how; break; }
               refusal = Val::str("one share of " + sym + " costs " + money(ref) + ", more than your " + bound + " limit (" + how + "). No order."); }
    } else if (mrr && *mrr != 0 && static_cast<double>(reward_c) < *mrr * static_cast<double>(risk_c) - EPS) {
        refusal = Val::str("reward:risk " + fmt_f(static_cast<double>(reward_c) / static_cast<double>(risk_c), 2) + " (target " + price_(target) + ", entry " + price_(ref) + ", stop " + price_(stop) + ") is under your minimum "
                           + num_(to_float(get(c, "min_reward_risk"))) + ", so trader-risk would block it. No order. Skip it, or make a new plan with trader-exit.");
    } else if (cost < to_float(orv(dget(c, "min_order_usd"), Val::integer(0))) - 0.005) {
        refusal = Val::str(std::to_string(qty) + " share" + (qty == 1 ? "" : "s") + " of " + sym + " is " + money(cost) + ", under your " + money(to_float(get(c, "min_order_usd"))) + " minimum order. No order.");
    }
    Val lims = Val::list();
    for (const auto& l : limits) {
        Val o = Val::dict(); o.d->set("name", Val::str(l.name)); o.d->set("usd", Val::flt(l.usd)); o.d->set("per", Val::flt(l.per)); o.d->set("shares", Val::integer(l.shares));
        o.d->set("how", Val::str(l.how)); o.d->set("binds", Val::boolean(l.name == bound)); lims.l->push_back(o);
    }
    Val s = Val::dict();
    s.d->set("symbol", Val::str(sym)); s.d->set("side", Val::str("buy")); s.d->set("setup", dget(plan, "setup", Val::str(""))); s.d->set("date", dget(plan, "date"));
    s.d->set("entry_time", dget(plan, "entry_time", Val::str(""))); s.d->set("entry_plan", Val::flt(round_cent(*entry_plan)));
    s.d->set("entry", Val::flt(ref)); s.d->set("entry_from", Val::str(ref_from)); s.d->set("stop", Val::flt(stop)); s.d->set("target", Val::flt(target));
    s.d->set("risk_per_share", Val::flt(risk)); s.d->set("reward_risk", Val::flt(round_n(static_cast<double>(reward_c) / static_cast<double>(risk_c), 2)));
    s.d->set("stop_label", dget(plan, "stop_label", Val::str(""))); s.d->set("target_label", dget(plan, "target_label", Val::str("")));
    s.d->set("budget", Val::flt(budget)); s.d->set("cash_buffer_pct", Val::flt(buf_pct)); s.d->set("cash_buffer", Val::flt(cash_buffer)); s.d->set("cash_available", Val::flt(available));
    s.d->set("max_loss_per_trade_usd", optf(max_loss)); s.d->set("limits", lims); s.d->set("qty", Val::integer(qty)); s.d->set("bound_by", Val::str(bound));
    s.d->set("cost", Val::flt(cost)); s.d->set("max_loss", Val::flt(loss)); s.d->set("flat_by", orv(dget(plan, "flat_by"), dget(c, "flat_by")));
    s.d->set("order_type", Val::str(limit ? "limit" : "market")); s.d->set("refusal", refusal);
    return s;
}
// the one proposed order: Alpaca place_stock_order fields (strings) plus why and plan
inline Val bracket_order(const Val& s) {
    if (truthy(get(s, "refusal")) || get(s, "qty").i <= 0) return Val();
    const long long qty = get(s, "qty").i;
    Val o = Val::dict();
    o.d->set("symbol", get(s, "symbol")); o.d->set("side", Val::str("buy")); o.d->set("qty", Val::str(std::to_string(qty))); o.d->set("type", get(s, "order_type")); o.d->set("time_in_force", Val::str("day"));
    if (get(s, "order_type").s == "limit") o.d->set("limit_price", Val::str(price_(get(s, "entry").num())));
    const std::string b = get(s, "bound_by").s;
    const std::string bound = b == "max loss" ? money(to_float(orv(get(s, "max_loss_per_trade_usd"), Val::integer(0)))) + " max loss per trade" : b == "cash" ? std::string("cash after the buffer") : b;
    const std::string why = setup_word(get(s, "setup"), "day trade") + " on " + get(s, "symbol").s + ": " + std::to_string(qty) + " shares, bound by your " + bound + " (" + std::to_string(qty) + " x "
                            + price_(get(s, "risk_per_share").num()) + " = " + money(get(s, "max_loss").num()) + " at the stop)";
    o.d->set("order_class", Val::str("bracket")); o.d->set("take_profit_limit_price", Val::str(price_(get(s, "target").num()))); o.d->set("stop_loss_stop_price", Val::str(price_(get(s, "stop").num())));
    o.d->set("why", Val::str(why));
    Val p = Val::dict();
    for (const char* k : {"entry", "stop", "target", "risk_per_share", "max_loss", "setup", "entry_time", "flat_by"}) p.d->set(k, get(s, k));
    o.d->set("plan", p);
    return o;
}
inline std::string plan_report(const Val& s, const Val& order, const std::vector<std::string>& notes) {
    std::vector<std::string> lines;
    const auto P = [&](const char* k) { return price_(get(s, k).num()); };
    lines.push_back(get(s, "symbol").s + " buy, " + setup_word(get(s, "setup"), "setup") + ", entry " + P("entry_plan") + (truthy(get(s, "entry_time")) ? " at " + pystr(get(s, "entry_time")) : std::string()));
    lines.push_back("Stop " + P("stop") + (truthy(get(s, "stop_label")) ? ": " + pystr(get(s, "stop_label")) : std::string()) + ". Target " + P("target") + (truthy(get(s, "target_label")) ? ": " + pystr(get(s, "target_label")) : std::string()) + ".");
    for (const auto& n : notes) lines.push_back(n);
    const std::string from = get(s, "entry_from").s;
    const std::string src = from == "plan" ? "the plan's entry" : from == "price" ? "the latest price, above the plan's entry" : "your limit price";
    lines.push_back("Sized at " + P("entry") + " (" + src + "). Risk " + P("risk_per_share") + " a share (entry " + P("entry") + " minus stop " + P("stop") + "). Reward:risk " + fmt_f(get(s, "reward_risk").num(), 2) + ".");
    lines.emplace_back();
    lines.push_back("Limits (whole shares; the smallest one wins):");
    for (const auto& l : *get(s, "limits").l)
        lines.push_back("  " + ljust(get(l, "name").s, 13) + " " + ljust(get(l, "how").s, 58) + " " + rjust(std::to_string(get(l, "shares").i), 3) + " shares" + (get(l, "binds").b ? "  <- binds" : ""));
    lines.emplace_back();
    if (order.is_none()) {
        lines.push_back("No order: " + get(s, "refusal").s);
        lines.push_back("Education and tooling, not financial advice.");
        return join(lines, "\n");
    }
    const std::string q = std::to_string(get(s, "qty").i);
    lines.push_back("Size: " + q + " shares of " + get(s, "symbol").s + " = " + money(get(s, "cost").num()) + " at " + P("entry") + ". Max loss at the stop: " + q + " x " + P("risk_per_share") + " = " + money(get(s, "max_loss").num()) + ".");
    lines.push_back("A stop is an order, not a promise: in a fast market it can fill below " + P("stop") + ".");
    lines.emplace_back();
    lines.push_back("Proposed bracket order (place_stock_order arguments, nothing has been placed):");
    const std::string how = get(order, "type").s == "limit" ? "limit " + get(order, "limit_price").s : std::string("market");
    lines.push_back("  buy " + get(order, "qty").s + " " + get(order, "symbol").s + ", " + how + ", day, take-profit " + get(order, "take_profit_limit_price").s + ", stop-loss " + get(order, "stop_loss_stop_price").s);
    if (truthy(get(s, "flat_by"))) lines.push_back("Flat by " + pystr(get(s, "flat_by")) + " New York: anything still open then gets closed (trader-alerts rings, trader-broker flatten).");
    lines.emplace_back();
    lines.push_back("Next: trader-risk checks it, then trader-broker asks you yes or no.");
    lines.push_back("Education and tooling, not financial advice.");
    return join(lines, "\n");
}
// ---- weights mode (v1)
inline Val orders_from(const Val& result, const Val& source) {
    Val out = Val::list();
    for (const auto& p : *get(result, "positions").l) {
        if (get(p, "dollars").num() <= 0) continue;
        std::string why = fmt_f(get(p, "weight").num(), 2) + "% of the split";
        if (truthy(source)) why = why + ", weights from " + pystr(source);
        if (get(p, "capped").b) why += "; capped at " + num_(get(result, "max_position_pct").num()) + "% of the " + money(get(result, "budget").num()) + " budget";
        Val o = Val::dict(); o.d->set("symbol", get(p, "ticker")); o.d->set("side", Val::str("buy"));
        if (get(result, "whole_shares").b) o.d->set("qty", Val::str(std::to_string(get(p, "shares").i)));
        else o.d->set("notional", Val::str(num_(get(p, "dollars").num())));
        o.d->set("type", Val::str("market")); o.d->set("time_in_force", Val::str("day")); o.d->set("why", Val::str(why));
        out.l->push_back(o);
    }
    return out;
}
inline std::string report(const Val& r, const Val& orders, const Val& source) {
    std::vector<std::string> lines;
    if (truthy(source)) lines.push_back("Weights from: " + pystr(source));
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
        lines.push_back("  " + get(o, "side").s + " " + get(o, "symbol").s + " " + amount + ", " + get(o, "type").s + ", " + get(o, "time_in_force").s);
    }
    lines.emplace_back();
    lines.push_back("These buys have no stop. trader-risk blocks them while \"require_stop\" is true and applies your day-trade limits to every buy.");
    lines.push_back("Next: trader-risk checks every order, then trader-broker asks you yes or no for each one.");
    lines.push_back("Education and tooling, not financial advice.");
    return join(lines, "\n");
}
inline void write_orders(const std::string& path, const Val& orders, const Val& cfg, const Val& source, long long now_utc) {
    const std::string d = dirname(path); if (!d.empty()) makedirs(d);
    Val doc = Val::dict();
    doc.d->set("created_by", Val::str("trader-budget")); doc.d->set("created_at", Val::str(strf(now_utc, true))); doc.d->set("status", Val::str("proposed"));
    doc.d->set("paper", Val::boolean(truthy(dget(cfg, "paper", Val::boolean(true)))));
    doc.d->set("note", Val::str("Proposals only. Nothing is placed until trader-risk passes each order and you say yes to it in the chat."));
    doc.d->set("source", source); doc.d->set("orders", orders);
    const std::string tmp = path + ".tmp";
    DumpOpts o; o.indent = 2; write_text(tmp, dumps(doc, o) + "\n");
    std::error_code ec; std::filesystem::rename(fspath(tmp), fspath(path), ec);   // os.replace
    if (ec) { std::filesystem::remove(fspath(path), ec); std::filesystem::rename(fspath(tmp), fspath(path), ec); if (ec) throw OSError(ec.value(), path); }
}
inline std::optional<double> optarg_f(const Val& a, const char* k) { const Val v = get(a, k); return v.is_none() ? std::nullopt : std::optional<double>(v.num()); }
inline int run_plan(const Val& a, const Val& cfg) {
    const long long now = parse_now(get(a, "now"));
    const std::string today = day_iso(day_of(to_eastern(now)));
    const Val plan = read_json(S(a, "plan"), "plan (run trader-exit first, or use --weights for a split)");
    const Val pd = plan.t == Val::DICT ? plan : Val::dict();
    std::optional<double> limit;
    if (!get(a, "limit").is_none()) {
        if (S(a, "limit") == "plan") limit = f_(dget(pd, "entry"));
        else { limit = f_(get(a, "limit")); if (!limit || *limit <= 0) throw SizeError("--limit wants a price, like --limit 364.00"); }
    }
    const std::string sym = upper(pystr(orv(dget(pd, "symbol"), Val::str(""))));
    std::vector<std::string> notes; std::optional<double> price;
    const std::string ppath = truthy(get(a, "prices")) ? S(a, "prices") : exists("trader/prices.json") ? std::string("trader/prices.json") : std::string();
    if (!ppath.empty() && !limit && !sym.empty()) {
        const Val pdoc = read_json(ppath, "prices");
        const Fresh fp = fresh_price(pdoc, sym, now);
        if (fp.price && fp.fresh) {
            price = fp.price;
            if (round_cent(*fp.price) > round_cent(f_(dget(pd, "entry")).value_or(0)))
                notes.push_back(sym + " last " + price_(*fp.price) + " (as of " + ny_fmt(*fp.at, false) + " New York) is above the plan's entry " + price_(to_float(dget(pd, "entry"))) + ", so the risk is bigger: sized at " + price_(*fp.price) + ".");
        } else if (fp.price) {
            notes.push_back(sym + " in " + ppath + " is " + (fp.at ? "from " + ny_fmt(*fp.at, true) + " New York" : std::string("undated")) + ": not used (older than " + std::to_string(FRESH_MINUTES) + " minutes). Sized at the plan's entry.");
        }
    }
    std::optional<double> cash;
    if (truthy(get(a, "portfolio")) && exists(S(a, "portfolio"))) { const Val pf = read_json(S(a, "portfolio"), "portfolio"); if (pf.t == Val::DICT) cash = f_(dget(pf, "cash")); }
    const Val s = size_trade(plan, cfg, optarg_f(a, "budget"), limit, price, cash, today);
    const Val order = bracket_order(s);
    const std::string write = S(a, "write");
    if (!order.is_none() && !write.empty()) {
        const std::string src = "trader/plan.json (" + get(s, "symbol").s + " " + (truthy(get(s, "setup")) ? pystr(get(s, "setup")) : std::string("plan")) + " " + (truthy(get(s, "date")) ? pystr(get(s, "date")) : today) + ")";
        Val one = Val::list(); one.l->push_back(order);
        write_orders(write, one, cfg, Val::str(src), now);
    }
    if (B(a, "json")) {
        Val j = Val::dict(), os = Val::list(), ns = Val::list();
        if (!order.is_none()) os.l->push_back(order);
        for (const auto& n : notes) ns.l->push_back(Val::str(n));
        j.d->set("sizing", s); j.d->set("orders", os); j.d->set("notes", ns); DumpOpts o; o.indent = 2; print(dumps(j, o));
    } else {
        print(plan_report(s, order, notes));
        if (!order.is_none() && !write.empty()) print("Wrote 1 proposed bracket order to " + write);
    }
    return order.is_none() ? 1 : 0;
}
inline int run_weights(const Val& a, const Val& cfg) {
    Val source = Val::str("");
    std::vector<Row> rows;
    if (truthy(get(a, "weights"))) rows = parse_weights(weights_from_arg(S(a, "weights")));
    else {
        const Val data = read_json(S(a, "targets"), "weights file");
        rows = parse_weights(data);
        if (data.t == Val::DICT) source = data.d->at("source", Val::str(""));
    }
    ODict prices = load_prices(S(a, "prices"));
    if (prices.empty() && exists("trader/prices.json") && !truthy(get(a, "prices"))) prices = load_prices("trader/prices.json");
    const Val top = get(a, "top");
    const Val result = size(rows, cfg, optarg_f(a, "budget"), prices, B(a, "whole_shares") ? std::optional<bool>(true) : std::nullopt, B(a, "cents"), top.is_none() ? 0 : top.i);
    const Val orders = orders_from(result, source);
    const std::string write = S(a, "write");
    if (!write.empty()) write_orders(write, orders, cfg, source, parse_now(get(a, "now")));
    if (B(a, "json")) { Val j = Val::dict(); j.d->set("result", result); j.d->set("orders", orders); DumpOpts o; o.indent = 2; print(dumps(j, o)); }
    else {
        print(report(result, orders, source));
        if (!write.empty()) print("Wrote " + std::to_string(orders.l->size()) + " proposed orders to " + write);
    }
    return 0;
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("size", "Size a day trade to your budget and max loss (trader-budget).");
    p.opt({"--plan"}).def("trader/plan.json").help("the trade plan (default trader/plan.json)");
    p.opt({"--config"}).def("trader/config.json").help("rules (default trader/config.json)");
    p.opt({"--budget"}).number().help("override the budget in the config");
    p.opt({"--limit"}).nargs("?").cnst(Val::str("plan")).metavar("PRICE").help("a limit order instead of market, at the plan's entry or at PRICE");
    p.opt({"--prices"}).help("prices JSON (default trader/prices.json if it exists)");
    p.opt({"--portfolio"}).def("trader/portfolio.json").help("your account; its cash caps the size if it exists");
    p.opt({"--now"}).help("pretend it is this New York time, e.g. \"2026-10-19 10:15\"");
    p.opt({"--write"}).nargs("?").cnst(Val::str("trader/orders.json")).metavar("PATH").help("write the proposed order (default trader/orders.json)");
    p.opt({"--json"}).store_true().help("print JSON instead of the report");
    p.opt({"--weights"}).help("inline weights: AAPL=22.04,AXP=17.14");
    p.opt({"--targets"}).help("a weights file: {\"weights\": [{\"ticker\", \"weight\"}]}");
    p.opt({"--top"}).integer().help("keep only the N biggest weights");
    p.opt({"--whole-shares"}).store_true().help("whole shares only (needs --prices)");
    p.opt({"--cents"}).store_true().help("size to the cent instead of whole dollars");
    const Val a = parse_args(p, argv);
    try {
        const Val cfg = load_config(S(a, "config"));
        if (truthy(get(a, "weights")) || truthy(get(a, "targets"))) return run_weights(a, cfg);
        if (truthy(get(a, "top")) || B(a, "whole_shares") || B(a, "cents")) throw SizeError("--top, --whole-shares and --cents work with --weights or --targets");
        return run_plan(a, cfg);
    } catch (const SizeError& e) { eprint(std::string("size: ") + e.what()); return 1; }
}
PACKTOOL("daytrade-budget/size", main);
}  // namespace packtools::daytrade_budget
