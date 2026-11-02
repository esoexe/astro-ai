// daytrade-broker/ticket - the day-trader edition of trader-broker: order tickets and the decision log. Never talks to
// a broker and never places anything:
//   show     renders each proposed order as a plain-English ticket plus the exact place_stock_order JSON; a bracket order
//            shows its stop-loss, take-profit and the max loss at the stop; an order is READY only if trader-risk passed
//            THIS exact order today (by fingerprint) and nobody decided on it yet
//   approve  records the user's explicit yes for ONE order, word for word
//   decline / result / fill (also logged, so trader-risk can count day trades) / flatten (a sell ticket that closes a
//   position by flat_by; it still goes through trader-risk and the user's yes) / log
// Dates are New York dates; --now "2026-10-19 10:15" pins the clock.
// C++ port of day-trader-skill/trader-broker/ticket.py (Jake Schincariol, MIT); same checks, same files, same exit codes.
// Adapted for A.S.T.R.O AI: the second confirmation is A.S.T.R.O AI's Money action card (Allow / Deny), and with
// "broker": "ibkr" or "manual" the user places the order by hand (A.S.T.R.O AI has no IBKR connector).
namespace packtools::daytrade_broker {
using namespace pyrt;
struct TicketError : std::runtime_error { using std::runtime_error::runtime_error; };
static const char* STRING_FIELDS[] = {"qty", "notional", "limit_price", "stop_price", "trail_price", "trail_percent", "take_profit_limit_price", "stop_loss_stop_price", "stop_loss_limit_price"};
static const std::set<std::string> BATCH_WORDS = {"all", "every", "everything", "rest", "remaining", "them", "both", "batch", "each"};
static const std::set<std::string> DOUBT_WORDS = {"no", "not", "dont", "don't", "wait", "cancel", "stop", "but", "maybe", "later", "hold", "unless", "if"};
inline std::string tif_words(const std::string& t) {
    static const std::map<std::string, std::string> m = {{"day", "good for today"}, {"gtc", "good until canceled"}, {"opg", "at the open"}, {"cls", "at the close"}, {"ioc", "immediate or cancel"}, {"fok", "fill or kill"}};
    const auto it = m.find(t); return it == m.end() ? std::string() : it->second;
}
inline Val read_json(const std::string& path, const std::string& what, bool required = true) {
    if (path.empty() || !exists(path)) { if (required) throw TicketError("no " + what + " at " + path); return Val(); }
    try { return loads(read_text(path)); } catch (const JSONDecodeError& e) { throw TicketError(path + " is not valid JSON: " + e.what()); }
}
inline void write_json(const std::string& path, const Val& data) {
    const std::string d = dirname(path); if (!d.empty()) makedirs(d);
    const std::string tmp = path + ".tmp";
    DumpOpts o; o.indent = 2; write_text(tmp, dumps(data, o) + "\n");
    std::error_code ec; std::filesystem::rename(fspath(tmp), fspath(path), ec);   // os.replace
    if (ec) { std::filesystem::remove(fspath(path), ec); std::filesystem::rename(fspath(tmp), fspath(path), ec); if (ec) throw OSError(ec.value(), path); }
}
inline std::string rec_day(const Val& rec) {
    const Val d = dget(rec, "date");
    return truthy(d) ? pystr(d) : cpslice(pystr(dget(rec, "at", Val::str(""))), 0, 10);
}
inline int ud_digit(uint32_t c);
inline int ud_digit_(uint32_t c) { return ud_digit(c); }
// ---- time (New York, no tz files): naive UTC microseconds
using trader_alerts::to_eastern; using trader_alerts::eastern_offset; using trader_alerts::eastern_to_utc; using trader_alerts::strf; using trader_alerts::day_of; using trader_alerts::day_iso;
inline long long parse_now(const Val& text) {   // ISO time -> naive UTC; a time without an offset is New York time
    if (!truthy(text)) return now_us() / 1000000 * 1000000;
    const std::string t = replace_all(strip(pystr(text)), "Z", "+00:00");
    const auto us = fromiso_us(t);
    if (!us) throw TicketError("--now wants a time like \"2026-10-19 10:15\" (New York) or ...Z (UTC)");
    const std::size_t base = t.size() >= 10 && t[4] == '-' ? 10 : 8;
    const bool aware = t.size() > base + 1 && t.find_first_of("+-", base + 1) != std::string::npos;
    return aware ? *us : eastern_to_utc(*us);
}
inline std::string ny_iso(long long now_utc) {   // "2026-10-19T10:15:00-04:00"
    const long long l = to_eastern(now_utc) / 1000000 * 1000000; std::string i = iso_utc(l); i = i.substr(0, 19);
    char b[16]; std::snprintf(b, sizeof b, "%+03d:00", eastern_offset(now_utc)); return i + b;
}
inline std::string ny_date(long long now_utc) { return day_iso(day_of(to_eastern(now_utc))); }
inline bool is_ymd(const std::string& s) {   // re.match(r"^\d{4}-\d{2}-\d{2}$", s)
    auto cs = cps(s); if (!cs.empty() && cs.back() == "\n") cs.pop_back();
    if (cs.size() != 10) return false;
    for (std::size_t k = 0; k < 10; ++k) { if (k == 4 || k == 7) { if (cs[k] != "-") return false; } else if (ud_digit_(cp_of(cs[k])) < 0) return false; }
    return true;
}
inline std::string fingerprint(const Val& order) { return trader_risk::fingerprint(order); }   // the same function as trader-risk
inline Val str_num(const Val& v) {
    if (v.t == Val::BOOL) return v;
    if (v.is_num()) {
        const double f = v.num();
        if (!std::isfinite(f)) throw PyErr(std::isnan(f) ? "ValueError" : "OverflowError", std::isnan(f) ? "cannot convert float NaN to integer" : "cannot convert float infinity to integer");
        return Val::str(f == std::trunc(f) ? (v.t == Val::INT && !v.big() ? std::to_string(v.i) : pystr(Val::integer(static_cast<long long>(f)))) : float_repr(f));
    }
    return Val::str(pystr(v));
}
// The exact arguments for place_stock_order: Alpaca's parameter names, numbers as strings.
inline Val tool_input(const Val& order) {
    Val out = Val::dict();
    for (const char* k : trader_risk::ALPACA_FIELDS) {
        if (!has(order, k) || get(order, k).is_none()) continue;
        bool sf = false; for (const char* s : STRING_FIELDS) if (std::strcmp(s, k) == 0) sf = true;
        out.d->set(k, sf ? str_num(get(order, k)) : get(order, k));
    }
    return out;
}
inline std::string money(const Val& v) { std::string s = fmt_fc(to_float(v), 2); if (ends(s, ".00")) s = s.substr(0, s.size() - 3); return "$" + s; }
inline std::string p2(const Val& v) { return fmt_f(to_float(v), 2); }   // "%.2f" % float(v)
inline bool is_bracket(const Val& order) {
    const std::string oc = lower(pystr(orv(dget(order, "order_class"), Val::str(""))));
    const bool legs = !dget(order, "take_profit_limit_price").is_none() || !dget(order, "stop_loss_stop_price").is_none();
    return oc == "bracket" || (oc.empty() && legs);
}
// (dollars, stop) lost if the stop fills exactly at the stop, or (None, stop)
inline std::pair<std::optional<double>, Val> max_loss(const Val& order) {
    const Val planv = orv(dget(order, "plan"), Val::dict());
    std::optional<double> stop = f_(dget(order, "stop_loss_stop_price"));
    if (!stop || *stop == 0) stop = f_(dget(planv, "stop"));
    const Val sv = stop ? Val::flt(*stop) : Val();
    const auto q = f_(dget(order, "qty"));
    if (!stop || *stop == 0 || !q || *q == 0) return {std::nullopt, sv};
    std::optional<double> entry;
    const auto lp = f_(dget(order, "limit_price"));
    if (pystr(dget(order, "type", Val::str("market"))) == "limit" && lp && *lp != 0) entry = lp;
    else if (const auto ml = f_(dget(planv, "max_loss"))) return {round_n(*ml, 2), sv};
    else entry = f_(dget(planv, "entry"));
    if (!entry || *entry == 0) return {std::nullopt, sv};
    const double risk = std::fabs(*entry - *stop);
    return {round_n(*q * std::nearbyint(risk * 100) / 100.0, 2), sv};
}
inline Val loss_val(const std::optional<double>& l) { return l ? Val::flt(*l) : Val(); }
// the ticket in plain words, one part per line
inline std::vector<std::string> plain_parts(const Val& order) {
    const std::string side = upper(pystr(dget(order, "side", Val::str(""))));
    const std::string sym = pystr(dget(order, "symbol", Val::str("?")));
    const std::string typ = pystr(dget(order, "type", Val::str("market")));
    const std::string tw = tif_words(pystr(dget(order, "time_in_force", Val::str("day"))));
    const std::string tif = !tw.empty() ? tw : pystr(dget(order, "time_in_force"));
    if (is_bracket(order)) {
        const std::string bh = typ == "limit" && truthy(dget(order, "limit_price")) ? "limit " + p2(get(order, "limit_price")) : typ;
        std::vector<std::string> parts = {side + " " + pystr(str_num(dget(order, "qty"))) + " " + sym + ", " + bh + ", " + tif};
        if (!dget(order, "stop_loss_stop_price").is_none()) {
            std::string leg = "stop-loss " + p2(get(order, "stop_loss_stop_price"));
            if (!dget(order, "stop_loss_limit_price").is_none()) leg += " (limit " + p2(get(order, "stop_loss_limit_price")) + ")";
            parts.push_back(leg);
        }
        if (!dget(order, "take_profit_limit_price").is_none()) parts.push_back("take-profit " + p2(get(order, "take_profit_limit_price")));
        const auto [loss, stop] = max_loss(order);
        if (loss) parts.push_back("max loss about " + money(Val::flt(*loss)) + " if the stop fills at " + p2(stop) + " (it can fill worse in a fast market)");
        return parts;
    }
    std::string what;
    if (!dget(order, "notional").is_none()) what = money(get(order, "notional")) + " of " + sym;
    else { const Val q = str_num(dget(order, "qty")); what = pystr(q) + " share" + (q.t == Val::STR && q.s == "1" ? "" : "s") + " of " + sym; }
    std::string how;
    if (typ == "limit") how = "limit order at " + money(dget(order, "limit_price"));
    else if (typ == "stop") how = "stop order at " + money(dget(order, "stop_price"));
    else if (typ == "stop_limit") how = "stop " + money(dget(order, "stop_price")) + ", limit " + money(dget(order, "limit_price"));
    else how = replace_all(typ, "_", " ") + " order";
    return {side + " " + what + ", " + how + ", " + tif};
}
// BUY $1,250 of AAPL, market order, good for today  /  BUY 8 TSLA, market, good for today / stop-loss 358.32 / ...
inline std::string plain(const Val& order) { return join(plain_parts(order), " / "); }
inline std::vector<double> amount_numbers(const Val& order) {
    std::vector<double> nums;
    for (const char* k : {"notional", "qty", "limit_price", "stop_loss_stop_price", "take_profit_limit_price", "stop_loss_limit_price"}) { const Val v = dget(order, k); if (!v.is_none()) if (const auto f = f_(v)) nums.push_back(*f); }
    if (const auto loss = max_loss(order).first) nums.push_back(*loss);
    return nums;
}
// Unicode decimal digit value (Python's \d and float() accept these), or -1.
inline int ud_digit(uint32_t c) {
    static const uint32_t Z[] = {0x30, 0x660, 0x6F0, 0x7C0, 0x966, 0x9E6, 0xA66, 0xAE6, 0xB66, 0xBE6, 0xC66, 0xCE6, 0xD66, 0xDE6, 0xE50, 0xED0, 0xF20, 0x1040, 0x1090, 0x17E0, 0x1810, 0x1946, 0x19D0, 0x1A80, 0x1A90, 0x1B50, 0x1BB0, 0x1C40, 0x1C50, 0xA620, 0xA8D0, 0xA900, 0xA9D0, 0xA9F0, 0xAA50, 0xABF0, 0xFF10};
    for (uint32_t z : Z) if (c >= z && c <= z + 9) return static_cast<int>(c - z);
    return -1;
}
// re.findall(r"\d[\d,]*(?:\.\d+)?", raw) -> floats (commas removed)
inline std::vector<double> reply_numbers(const std::string& raw) {
    const auto cs = cps(raw); std::vector<double> out; std::size_t i = 0;
    const auto dg = [&](std::size_t k) { return k < cs.size() ? ud_digit(cp_of(cs[k])) : -1; };
    while (i < cs.size()) {
        if (dg(i) < 0) { i++; continue; }
        std::string n; n += static_cast<char>('0' + dg(i)); i++;
        while (i < cs.size() && (dg(i) >= 0 || cs[i] == ",")) { if (cs[i] != ",") n += static_cast<char>('0' + dg(i)); i++; }
        if (i + 1 < cs.size() && cs[i] == "." && dg(i + 1) >= 0) { n += '.'; i++; while (dg(i) >= 0) { n += static_cast<char>('0' + dg(i)); i++; } }
        out.push_back(std::strtod(n.c_str(), nullptr));
    }
    return out;
}
// Is this reply an explicit yes for THIS one order?
inline std::pair<bool, std::string> parse_yes(const std::string& reply, const Val& order, const List& batch_symbols) {
    const std::string raw = strip(reply);
    if (raw.empty()) return {false, "the reply is empty"};
    if (raw.find('?') != std::string::npos) return {false, "the reply is a question, not a yes"};
    std::vector<std::string> words; { const std::string lw = lower(raw); std::string cur; for (char ch : lw) { if ((ch >= 'a' && ch <= 'z') || ch == '\'') cur += ch; else if (!cur.empty()) { words.push_back(cur); cur.clear(); } } if (!cur.empty()) words.push_back(cur); }
    if (words.empty() || (words[0] != "yes" && words[0] != "y")) return {false, "only a reply that starts with \"yes\" counts (not ok, sure, go, do it)"};
    for (const auto& w : words) if (BATCH_WORDS.count(w)) return {false, "a yes covers one order at a time, not a batch"};
    for (std::size_t k = 1; k < words.size(); ++k) if (DOUBT_WORDS.count(words[k])) return {false, "the reply has a condition or a doubt in it; ask again"};
    const std::string sym = upper(pystr(dget(order, "symbol", Val::str(""))));
    std::set<std::string> tokens; { const std::string u = upper(raw); std::size_t i = 0; while (i < u.size()) { if (u[i] >= 'A' && u[i] <= 'Z') { std::size_t j = i + 1; while (j < u.size() && j - i < 7 && ((u[j] >= 'A' && u[j] <= 'Z') || u[j] == '.' || u[j] == '-')) j++; tokens.insert(u.substr(i, j - i)); i = j; } else i++; } }
    std::set<std::string> named;
    for (const auto& s : batch_symbols) {
        if (s.t != Val::STR) throw PyErr("AttributeError", std::string("'") + type_name(s) + "' object has no attribute 'upper'");
        const std::string su = upper(s.s); if (su != sym && tokens.count(su)) named.insert(su);
    }
    if (!named.empty()) return {false, "the reply names " + join(std::vector<std::string>(named.begin(), named.end()), ", ") + ", but this order is for " + sym};
    const auto nums = reply_numbers(raw);
    if (!nums.empty()) {
        const auto want = amount_numbers(order); bool hit = false;
        for (double n : nums) for (double w : want) if (std::fabs(n - w) < 0.005) hit = true;
        if (!hit) { std::vector<std::string> ns; for (double n : nums) ns.push_back(str_num(Val::flt(n)).s); return {false, "the reply says " + join(ns, ", ") + ", but this order is " + plain(order)}; }
    }
    return {true, "explicit yes for " + plain(order)};
}
inline Val load_state(const std::string& path) { const Val s = read_json(path, "broker state", false); if (truthy(s)) return s; Val d = Val::dict(); d.d->set("orders", Val::list()); return d; }
inline Val latest(const Val& state, const std::string& fp, const std::string& today) {
    Val found;
    for (const auto& rec : iter(dget(state, "orders", Val::list()))) { const Val f = dget(rec, "fingerprint"); if (f.t == Val::STR && f.s == fp && rec_day(rec) == today) found = rec; }
    return found;
}
struct Status { bool ready; std::string label; Val rv; };
inline Status status_of(const Val& order, const Val& risk, const Val& state, const std::string& today) {
    const std::string fp = fingerprint(order);
    Val rv; std::string why;
    if (!truthy(risk)) why = "trader-risk has not checked these orders (run trader-risk check)";
    else if (!(dget(risk, "date").t == Val::STR && dget(risk, "date").s == today)) why = "the risk check is from " + pystr(dget(risk, "date")) + ", not today; run trader-risk check again";
    else {
        for (const auto& r : iter(dget(risk, "results", Val::list()))) { const Val f = dget(r, "fingerprint"); if (f.t == Val::STR && f.s == fp) { rv = r; break; } }
        if (rv.is_none()) why = "this exact order was not in the last risk check (it changed or is new); run trader-risk check again";
    }
    if (rv.is_none()) return {false, "NOT READY: " + why, Val()};
    if (!(dget(rv, "verdict").t == Val::STR && dget(rv, "verdict").s == "PASS")) {
        std::vector<std::string> rs; for (const auto& x : iter(dget(rv, "reasons", Val::list()))) rs.push_back(pystr(x));
        return {false, "BLOCKED by trader-risk: " + join(rs, "; "), rv};
    }
    const Val rec = latest(state, fp, today);
    if (truthy(rec)) {
        const Val st = dget(rec, "status");
        if (st.t == Val::STR && st.s == "approved") return {false, "APPROVED by you at " + pystr(dget(rec, "at")) + ", waiting for the place_stock_order call", rv};
        return {false, "DONE: " + pystr(st) + " at " + pystr(dget(rec, "at")), rv};
    }
    return {true, "READY: trader-risk PASS, waiting for your yes", rv};
}
// entry or exit, as trader-risk saw it (older checks: a buy is an entry)
inline Val kind_of(const Val& order, const Val& rv) {
    if (truthy(rv) && truthy(dget(rv, "kind"))) return get(rv, "kind");
    return Val::str(lower(pystr(dget(order, "side", Val::str("")))) == "buy" ? "entry" : "exit");
}
inline std::string broker_of(const Val& cfg) { return lower(pystr(dget(cfg, "broker", Val::str("alpaca")))); }
inline bool by_hand(const std::string& b) { return b == "ibkr" || b == "manual"; }
inline std::string broker_name(const std::string& b) { return b == "ibkr" ? "Interactive Brokers" : b == "manual" ? "your broker" : "Alpaca"; }
inline std::string json_block(const Val& v, const std::string& indent) {
    DumpOpts o; o.indent = 2; std::string out; bool first = true;
    for (const auto& l : split(dumps(v, o), "\n")) { if (!first) out += "\n"; first = false; out += indent + l; }
    return out;
}
inline std::pair<std::string, bool> render(const Val& order, long long idx, long long total, const Val& cfg, const Val& risk, const Val& state, const std::string& today) {
    const Status st = status_of(order, risk, state, today);
    const std::string broker = broker_of(cfg);
    const std::string acct = truthy(dget(cfg, "paper", Val::boolean(true))) ? "PAPER account" : "LIVE ACCOUNT, REAL MONEY";
    std::vector<std::string> lines = {"ORDER " + std::to_string(idx) + " of " + std::to_string(total) + ", " + acct + ", " + broker_name(broker)};
    for (const auto& part : plain_parts(order)) lines.push_back("  " + part);
    if (truthy(dget(order, "why"))) { const Val w = get(order, "why"); if (w.t != Val::STR) throw PyErr("TypeError", std::string("can only concatenate str (not \"") + type_name(w) + "\") to str"); lines.push_back("  why: " + w.s); }
    lines.push_back("  " + st.label);
    for (const auto& w : iter(truthy(st.rv) ? dget(st.rv, "warnings", Val::list()) : Val::list())) lines.push_back("  note: " + pystr(w));
    if (by_hand(broker)) {
        lines.push_back("  Place this order yourself in " + (broker == "ibkr" ? std::string("your Interactive Brokers account") : std::string("your broker's app or website")) + ", exactly as shown.");
        lines.push_back("  A.S.T.R.O AI does not send it; it only goes to the market after you submit it there.");
    } else {
        lines.push_back("  A.S.T.R.O AI will call place_stock_order with exactly:");
        lines.push_back(json_block(tool_input(order), "    "));
        if (is_bracket(order)) {
            lines.push_back("  Alpaca places the entry, then the stop-loss and the take-profit as two orders");
            lines.push_back("  that cancel each other: when one fills, the other is canceled.");
        }
    }
    if (st.ready) {
        lines.push_back("  Reply \"yes\" to place this one order, or \"no\" to skip it.");
        if (!by_hand(broker)) lines.push_back("  A.S.T.R.O AI's Money action card then asks again: Allow places it, Deny stops it.");
    }
    return {join(lines, "\n"), st.ready};
}
inline Val ticket_json(const Val& order, long long idx, const Val& cfg, const Val& risk, const Val& state, const std::string& today) {
    const Status st = status_of(order, risk, state, today);
    Val t = Val::dict();
    t.d->set("index", Val::integer(idx)); t.d->set("ready", Val::boolean(st.ready)); t.d->set("status", Val::str(st.label)); t.d->set("plain", Val::str(plain(order)));
    t.d->set("fingerprint", Val::str(fingerprint(order))); t.d->set("paper", Val::boolean(truthy(dget(cfg, "paper", Val::boolean(true)))));
    t.d->set("broker", dget(cfg, "broker", Val::str("alpaca"))); t.d->set("tool", Val::str("place_stock_order")); t.d->set("tool_input", tool_input(order)); t.d->set("why", dget(order, "why", Val::str("")));
    const auto [loss, stop] = max_loss(order);
    t.d->set("bracket", Val::boolean(is_bracket(order))); t.d->set("max_loss", loss_val(loss)); t.d->set("stop", stop);
    return t;
}
// Record a filled order in trader/portfolio.json: cash and shares, with a cost-basis lot per buy (oldest lots are relieved first on a sell).
inline Val apply_fill(const std::string& portfolio_path, const Val& cfg, const std::string& symbol0, const std::string& side, const std::string& qty_s, const std::string& price_s, const std::string& date) {
    Val pf = read_json(portfolio_path, "portfolio", false);
    if (pf.is_none()) {
        const Val budget = dget(cfg, "budget");
        if (!truthy(budget)) throw TicketError("no " + portfolio_path + " yet and no budget in the config to start it from");
        pf = Val::dict(); pf.d->set("cash", Val::flt(to_float(budget))); pf.d->set("positions", Val::list());
    }
    if (pf.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(pf) + "' object has no attribute 'setdefault'");
    const std::string symbol = upper(symbol0);
    const double qty = to_float(Val::str(qty_s)), price = to_float(Val::str(price_s));
    if (qty <= 0 || price <= 0) throw TicketError("a fill needs a positive qty and price");
    const double cost = round_n(qty * price, 2);
    if (!pf.d->has("positions")) pf.d->set("positions", Val::list());
    Val positions = pf.d->at("positions");
    if (positions.t != Val::LIST) throw PyErr("TypeError", std::string("'") + type_name(positions) + "' object is not iterable");
    Val pos;
    for (const auto& p : *positions.l) if (upper(pystr(dget(p, "ticker", Val::str("")))) == symbol) pos = p;
    if (side == "buy") {
        if (pos.is_none()) { pos = Val::dict(); pos.d->set("ticker", Val::str(symbol)); pos.d->set("shares", Val::flt(0.0)); pos.d->set("lots", Val::list()); positions.l->push_back(pos); }
        if (!pos.d->has("lots")) pos.d->set("lots", Val::list());
        Val lots = pos.d->at("lots");
        Val lot = Val::dict(); lot.d->set("id", Val::str(symbol + "-" + std::to_string(lots.l->size() + 1))); lot.d->set("date", Val::str(date)); lot.d->set("shares", Val::flt(qty)); lot.d->set("cost", Val::flt(cost));
        lots.l->push_back(lot);
        pos.d->set("shares", Val::flt(round_n(to_float(pos.d->at("shares", Val::integer(0))) + qty, 6)));
        pf.d->set("cash", Val::flt(round_n(to_float(pf.d->at("cash", Val::integer(0))) - cost, 2)));
    } else {
        if (pos.is_none() || to_float(pos.d->at("shares", Val::integer(0))) + 1e-9 < qty)
            throw TicketError("cannot record selling " + pystr(str_num(Val::flt(qty))) + " " + symbol + ": the portfolio holds " + (pos.is_none() ? std::string("0") : pystr(pos.d->at("shares", Val::integer(0)))));
        double left = qty;
        List lots = iter(pos.d->at("lots", Val::list()));
        std::vector<Val> by_date(lots.begin(), lots.end());
        std::stable_sort(by_date.begin(), by_date.end(), [](const Val& a, const Val& b) { return pystr(dget(a, "date", Val::str(""))) < pystr(dget(b, "date", Val::str(""))); });
        for (auto& lot : by_date) {
            if (left <= 1e-9) break;
            const double sh = to_float(lot.d->at("shares")), c = to_float(lot.d->at("cost"));
            const double take = sh < left ? sh : left;   // min(left, shares)
            const double per = sh != 0 ? c / sh : 0;
            lot.d->set("cost", Val::flt(round_n(c - per * take, 2)));
            lot.d->set("shares", Val::flt(round_n(sh - take, 6)));
            left -= take;
        }
        Val kept = Val::list(); for (const auto& l : lots) if (to_float(l.d->at("shares")) > 1e-9) kept.l->push_back(l);
        pos.d->set("lots", kept);
        const double ns = round_n(to_float(pos.d->at("shares")) - qty, 6);
        pos.d->set("shares", Val::flt(ns));
        if (ns <= 1e-9) { for (auto it = positions.l->begin(); it != positions.l->end(); ++it) if (it->d == pos.d) { positions.l->erase(it); break; } }
        pf.d->set("cash", Val::flt(round_n(to_float(pf.d->at("cash", Val::integer(0))) + cost, 2)));
    }
    write_json(portfolio_path, pf);
    return pf;
}
// a fill in the log, so trader-risk can count day trades (a buy and a sell the same day)
inline Val load_state(const std::string& path);
inline void append_rec(Val& state, const Val& rec);
inline void log_fill(const std::string& state_path, long long now, const std::string& today, const std::string& symbol, const std::string& side, const std::string& qty, const std::string& price) {
    Val state = load_state(state_path);
    const double q = to_float(Val::str(qty)), pr = to_float(Val::str(price));
    Val r = Val::dict();
    r.d->set("at", Val::str(ny_iso(now))); r.d->set("date", Val::str(today)); r.d->set("status", Val::str("filled")); r.d->set("symbol", Val::str(upper(symbol))); r.d->set("side", Val::str(side));
    r.d->set("qty", str_num(Val::flt(q))); r.d->set("price", Val::flt(pr));
    r.d->set("plain", Val::str(upper(side) + " " + pystr(str_num(Val::flt(q))) + " " + upper(symbol) + " at " + fmt_f(pr, 2)));
    append_rec(state, r); write_json(state_path, state);
}
inline std::pair<long long, std::string> clock_(const Val& a) {
    const long long now = parse_now(has(a, "now") ? get(a, "now") : Val());
    const std::string today = has(a, "today") && truthy(get(a, "today")) ? S(a, "today") : ny_date(now);
    if (!is_ymd(today)) throw TicketError("--today wants a date like 2026-10-19");
    return {now, today};
}
struct Loaded { List orders; Val cfg, risk, state; std::string today; long long now = 0; };
inline Loaded load_(const Val& a) {
    const std::string path = S(a, "orders");
    const Val doc = read_json(path, "orders file (run trader-budget, or ticket flatten, first)");
    const Val orders = doc.t == Val::DICT ? doc.d->at("orders", doc) : doc;
    if (orders.t != Val::LIST || orders.l->empty()) throw TicketError("no orders in " + path);
    Loaded L; L.orders = *orders.l;
    L.cfg = orv(read_json(S(a, "config"), "config", false), Val::dict());
    L.risk = read_json(S(a, "risk"), "risk check", false);
    L.state = load_state(S(a, "state"));
    std::tie(L.now, L.today) = clock_(a);
    return L;
}
inline const Val& pick(const List& orders, const Val& index) {
    if (index.is_none()) throw TicketError("say which order: --index N");
    const long long i = index.i;
    if (!(1 <= i && i <= static_cast<long long>(orders.size()))) throw TicketError("there is no order " + std::to_string(i) + " (there are " + std::to_string(orders.size()) + ")");
    return orders[static_cast<std::size_t>(i - 1)];
}
inline void append_rec(Val& state, const Val& rec) {
    if (!state.d->has("orders")) state.d->set("orders", Val::list());
    Val o = state.d->at("orders"); if (o.t != Val::LIST) throw PyErr("AttributeError", std::string("'") + type_name(o) + "' object has no attribute 'append'");
    o.l->push_back(rec);
}
inline int cmd_show(const Val& a) {
    Loaded L = load_(a);
    std::vector<std::pair<long long, Val>> picks;
    if (truthy(get(a, "index"))) picks.push_back({get(a, "index").i, pick(L.orders, get(a, "index"))});
    else for (std::size_t k = 0; k < L.orders.size(); ++k) picks.push_back({static_cast<long long>(k + 1), L.orders[k]});
    if (B(a, "json")) {
        Val out = Val::list(); bool any = false;
        for (const auto& [i, o] : picks) { Val t = ticket_json(o, i, L.cfg, L.risk, L.state, L.today); any = any || get(t, "ready").b; out.l->push_back(t); }
        DumpOpts d; d.indent = 2; print(dumps(truthy(get(a, "index")) ? (*out.l)[0] : out, d));
        return any ? 0 : 1;
    }
    std::vector<std::string> blocks; bool any_ready = false;
    for (const auto& [i, o] : picks) { auto [text, ready] = render(o, i, static_cast<long long>(L.orders.size()), L.cfg, L.risk, L.state, L.today); blocks.push_back(text); any_ready = any_ready || ready; }
    print(join(blocks, "\n\n"));
    print("\nOne order at a time: a yes covers one ticket, never the batch. Education and tooling, not financial advice.");
    return any_ready ? 0 : 1;
}
inline int cmd_approve(const Val& a) {
    Loaded L = load_(a);
    const Val order = pick(L.orders, get(a, "index"));
    const long long idx = get(a, "index").i;
    const Status st = status_of(order, L.risk, L.state, L.today);
    if (!st.ready) throw TicketError("order " + std::to_string(idx) + " is not ready: " + st.label);
    List syms; for (const auto& o : L.orders) syms.push_back(dget(o, "symbol", Val::str("")));
    const auto [ok, why] = parse_yes(S(a, "reply"), order, syms);
    if (!ok) { print("Not recorded: " + why + ". Ask the user again with the ticket for order " + std::to_string(idx) + "."); return 1; }
    const auto [loss, stop] = max_loss(order);
    Val rec = Val::dict();
    rec.d->set("at", Val::str(ny_iso(L.now))); rec.d->set("date", Val::str(L.today)); rec.d->set("index", Val::integer(idx)); rec.d->set("fingerprint", Val::str(fingerprint(order)));
    rec.d->set("symbol", Val::str(upper(pystr(dget(order, "symbol", Val::str("")))))); rec.d->set("side", dget(order, "side"));
    rec.d->set("qty", dget(order, "qty")); rec.d->set("kind", kind_of(order, st.rv)); rec.d->set("max_loss", loss_val(loss)); rec.d->set("stop", stop);
    rec.d->set("plain", Val::str(plain(order)));
    rec.d->set("status", Val::str("approved")); rec.d->set("reply", get(a, "reply")); rec.d->set("paper", Val::boolean(truthy(dget(L.cfg, "paper", Val::boolean(true)))));
    rec.d->set("broker", dget(L.cfg, "broker", Val::str("alpaca")));
    append_rec(L.state, rec); write_json(S(a, "state"), L.state);
    print("Recorded your yes for order " + std::to_string(idx) + ": " + plain(order) + ".");
    const std::string broker = broker_of(L.cfg);
    if (by_hand(broker)) print("Now place it yourself in " + std::string(broker == "ibkr" ? "your Interactive Brokers account" : "your broker's app or website") + ", exactly as shown. Then: ticket result --index " + std::to_string(idx) + " --status placed|denied|failed");
    else {
        print("Now call place_stock_order with exactly:");
        DumpOpts d; d.indent = 2; print(dumps(tool_input(order), d));
        print("A.S.T.R.O AI's Money action card shows Allow / Deny. Then: ticket result --index " + std::to_string(idx) + " --status placed|denied|failed");
    }
    return 0;
}
inline int cmd_decline(const Val& a) {
    Loaded L = load_(a);
    const Val order = pick(L.orders, get(a, "index"));
    const long long idx = get(a, "index").i;
    Val rec = Val::dict();
    rec.d->set("at", Val::str(ny_iso(L.now))); rec.d->set("date", Val::str(L.today)); rec.d->set("index", Val::integer(idx)); rec.d->set("fingerprint", Val::str(fingerprint(order)));
    rec.d->set("symbol", Val::str(upper(pystr(dget(order, "symbol", Val::str("")))))); rec.d->set("side", dget(order, "side")); rec.d->set("plain", Val::str(plain(order)));
    rec.d->set("status", Val::str("declined")); rec.d->set("reply", orv(get(a, "reply"), Val::str("no")));
    append_rec(L.state, rec); write_json(S(a, "state"), L.state);
    print("Recorded: you said no to order " + std::to_string(idx) + " (" + plain(order) + "). Nothing was sent.");
    return 0;
}
inline int cmd_result(const Val& a) {
    Loaded L = load_(a);
    const Val order = pick(L.orders, get(a, "index"));
    const long long idx = get(a, "index").i;
    const std::string fp = fingerprint(order);
    const Val rec = latest(L.state, fp, L.today);
    if (!truthy(rec) || !(dget(rec, "status").t == Val::STR && dget(rec, "status").s == "approved"))
        throw TicketError("order " + std::to_string(idx) + " has no yes recorded today; a result can only follow an approval");
    const std::string status = S(a, "status");
    Val n = Val::dict();
    n.d->set("at", Val::str(ny_iso(L.now))); n.d->set("date", Val::str(L.today)); n.d->set("index", Val::integer(idx)); n.d->set("fingerprint", Val::str(fp));
    n.d->set("symbol", get(rec, "symbol")); n.d->set("side", get(rec, "side")); n.d->set("plain", get(rec, "plain")); n.d->set("status", Val::str(status));
    for (const char* k : {"qty", "kind", "max_loss", "stop"}) if (!dget(rec, k).is_none()) n.d->set(k, get(rec, k));
    if (truthy(get(a, "broker_id"))) n.d->set("broker_order_id", get(a, "broker_id"));
    if (truthy(get(a, "note"))) n.d->set("note", get(a, "note"));
    append_rec(L.state, n); write_json(S(a, "state"), L.state);
    print("Recorded order " + std::to_string(idx) + " as " + status + ".");
    if (status == "placed" && truthy(get(a, "filled_qty")) && truthy(get(a, "filled_price"))) {
        apply_fill(S(a, "portfolio"), L.cfg, pystr(get(rec, "symbol")), pystr(get(rec, "side")), S(a, "filled_qty"), S(a, "filled_price"), L.today);
        log_fill(S(a, "state"), L.now, L.today, pystr(get(rec, "symbol")), pystr(get(rec, "side")), S(a, "filled_qty"), S(a, "filled_price"));
        print("Added the fill to " + S(a, "portfolio") + ".");
    } else if (status == "placed") {
        print("When it fills, record it: ticket fill --symbol " + pystr(get(rec, "symbol")) + " --side " + pystr(get(rec, "side")) + " --qty Q --price P");
        if (dget(rec, "kind").t == Val::STR && get(rec, "kind").s == "entry" && is_bracket(order)) print("When the stop-loss or the take-profit fills later, record that sell the same way.");
    }
    return 0;
}
inline int cmd_fill(const Val& a) {
    const Val cfg = orv(read_json(S(a, "config"), "config", false), Val::dict());
    const long long now = parse_now(get(a, "now"));
    const std::string date = truthy(get(a, "date")) ? S(a, "date") : ny_date(now);
    if (!is_ymd(date)) throw TicketError("--date wants a date like 2026-10-19");
    const Val pf = apply_fill(S(a, "portfolio"), cfg, S(a, "symbol"), S(a, "side"), S(a, "qty"), S(a, "price"), date);
    log_fill(S(a, "state"), now, date, S(a, "symbol"), S(a, "side"), S(a, "qty"), S(a, "price"));
    print("Recorded " + S(a, "side") + " " + S(a, "qty") + " " + upper(S(a, "symbol")) + " at " + money(Val::str(S(a, "price"))) + " on " + date + " in " + S(a, "portfolio") + ". Cash now " + money(get(pf, "cash")) + ".");
    return 0;
}
inline int cmd_log(const Val& a) {
    const Val state = load_state(S(a, "state"));
    const std::string today = clock_(a).second;
    const bool all = B(a, "all");
    List rows; for (const auto& r : iter(dget(state, "orders", Val::list()))) if (all || rec_day(r) == today) rows.push_back(r);
    if (rows.empty()) { print(std::string("No decisions recorded") + (all ? "" : " today") + "."); return 1; }
    long long placed = 0;
    for (const auto& r : rows) {
        const std::string extra = truthy(dget(r, "broker_order_id")) ? " id " + pystr(get(r, "broker_order_id")) : "";
        const std::string st = pystr(dget(r, "status"));
        const std::string reply = (truthy(dget(r, "reply")) && (st == "approved" || st == "declined")) ? " reply \"" + pystr(get(r, "reply")) + "\"" : "";
        const std::string loss = (!dget(r, "max_loss").is_none() && st == "placed") ? " max loss " + money(get(r, "max_loss")) : "";
        const std::string idx = !dget(r, "index").is_none() ? "#" + pystr(get(r, "index")) : "  ";
        print(pystr(dget(r, "at")) + "  " + idx + " " + ljust(st, 9) + " " + pystr(dget(r, "plain")) + reply + loss + extra);
        if (dget(r, "status").t == Val::STR && st == "placed") placed++;
    }
    print(std::to_string(placed) + " placed" + (all ? "" : " today") + ".");
    return 0;
}
inline int cmd_flatten(const Val& a) {
    const Val cfg = orv(read_json(S(a, "config"), "config", false), Val::dict());
    const std::string sym = upper(strip(S(a, "symbol")));
    const auto [now, today] = clock_(a);
    std::string source = "the broker, get_open_position";
    double qty = 0;
    if (!get(a, "qty").is_none()) {
        const auto q = f_(get(a, "qty"));
        if (!q || *q <= 0) throw TicketError("--qty wants the number of shares you hold, like --qty 8");
        qty = *q;
    } else {
        const Val pf = orv(read_json(S(a, "portfolio"), "portfolio", false), Val::dict());
        std::vector<double> xs;   // sum() of floats (Neumaier, like Python 3.12+)
        for (const auto& p : iter(dget(pf, "positions", Val::list()))) {
            if (upper(pystr(orv(orv(dget(p, "ticker"), dget(p, "symbol")), Val::str("")))) != sym) continue;
            const Val sh = has(p, "shares") ? get(p, "shares") : dget(p, "qty");
            const auto f = f_(sh); xs.push_back(f && *f != 0 ? *f : 0);
        }
        qty = fsum(xs);
        source = S(a, "portfolio");
        if (qty <= 1e-9) throw TicketError("nothing to flatten: no " + sym + " shares in " + S(a, "portfolio") + ". Check the broker (get_open_position " + sym + ") and pass --qty if you do hold some");
    }
    const Val plan = orv(read_json(S(a, "plan"), "plan", false), Val::dict());
    Val flat_by = upper(pystr(dget(plan, "symbol", Val::str("")))) == sym ? dget(plan, "flat_by") : Val();
    flat_by = orv(orv(flat_by, dget(cfg, "flat_by")), Val::str("15:55"));
    const std::string fb = pystr(flat_by);
    Val order = Val::dict();
    order.d->set("symbol", Val::str(sym)); order.d->set("side", Val::str("sell")); order.d->set("qty", str_num(Val::flt(qty))); order.d->set("type", Val::str("market")); order.d->set("time_in_force", Val::str("day"));
    order.d->set("why", Val::str("flat by " + fb + " New York: closes the " + sym + " day trade before the close (qty from " + source + ")"));
    const Val old = read_json(S(a, "orders"), "orders", false);
    long long replaced = 0;
    if (old.t == Val::DICT) { const Val o = old.d->at("orders", Val::list()); replaced = o.t == Val::LIST ? static_cast<long long>(o.l->size()) : o.t == Val::DICT ? static_cast<long long>(o.d->items.size()) : o.t == Val::STR ? static_cast<long long>(cps(o.s).size()) : 0; }
    Val doc = Val::dict();
    doc.d->set("created_by", Val::str("trader-broker")); doc.d->set("created_at", Val::str(strf(now, true))); doc.d->set("status", Val::str("proposed"));
    doc.d->set("paper", Val::boolean(truthy(dget(cfg, "paper", Val::boolean(true)))));
    doc.d->set("note", Val::str("Flatten ticket. Nothing is placed until trader-risk passes it and you say yes in the chat."));
    doc.d->set("source", Val::str("ticket flatten " + sym + " on " + today));
    Val ol = Val::list(); ol.l->push_back(order); doc.d->set("orders", ol);
    write_json(S(a, "orders"), doc);
    print("Flatten " + sym + ": " + plain(order) + " (flat by " + fb + " New York).");
    print("Wrote it to " + S(a, "orders") + (replaced ? " (it replaced " + std::to_string(replaced) + " earlier proposal" + (replaced == 1 ? "" : "s") + ")" : "") + ".");
    if (get(a, "qty").is_none()) print("The qty comes from " + source + ". Check it against get_open_position " + sym + " before you say yes.");
    print("Next:");
    print("  1. trader-risk: the risk check. A sell that only closes what you hold is never blocked by the daily");
    print("     limits or the entry window.");
    print("  2. ticket show, then the user's yes for this one order.");
    print("  3. If the bracket's stop-loss and take-profit are still open, Alpaca holds these shares for them");
    print("     and rejects a new sell. Cancel them first: get_orders (status open, nested true), then");
    print("     cancel_order_by_id for each open leg (the Money action card asks first). Then place_stock_order with the ticket.");
    return 0;
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("ticket", "Order tickets and your yes (trader-broker). Never places orders.");
    p.subcommands("cmd", false);
    const auto clock = [](Parser& s) { s.opt({"--now"}).help("pretend it is this New York time, e.g. \"2026-10-19 10:15\""); s.opt({"--today"}).help("YYYY-MM-DD (default: today in New York)"); };
    const auto common = [&clock](Parser& s) {
        s.opt({"--orders"}).def("trader/orders.json"); s.opt({"--config"}).def("trader/config.json"); s.opt({"--risk"}).def("trader/state/risk.json");
        s.opt({"--state"}).def("trader/state/broker.json"); clock(s);
    };
    { Parser& s = p.sub("show"); common(s); s.opt({"--index"}).integer(); s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("approve"); common(s); s.opt({"--index"}).integer().required(); s.opt({"--reply"}).required().help("the user's reply, word for word"); }
    { Parser& s = p.sub("decline"); common(s); s.opt({"--index"}).integer().required(); s.opt({"--reply"}); }
    { Parser& s = p.sub("result"); common(s); s.opt({"--index"}).integer().required(); s.opt({"--status"}).required().choices({"placed", "denied", "failed"});
      s.opt({"--broker-id"}); s.opt({"--note"}); s.opt({"--filled-qty"}); s.opt({"--filled-price"}); s.opt({"--portfolio"}).def("trader/portfolio.json"); }
    { Parser& s = p.sub("fill"); s.opt({"--symbol"}).required(); s.opt({"--side"}).required().choices({"buy", "sell"}); s.opt({"--qty"}).required(); s.opt({"--price"}).required();
      s.opt({"--date"}).help("YYYY-MM-DD (default: today in New York)"); s.opt({"--now"}); s.opt({"--portfolio"}).def("trader/portfolio.json"); s.opt({"--config"}).def("trader/config.json"); s.opt({"--state"}).def("trader/state/broker.json"); }
    { Parser& s = p.sub("flatten"); s.opt({"--symbol"}).required(); s.opt({"--qty"}).help("shares you hold, from get_open_position (default: trader/portfolio.json)"); s.opt({"--orders"}).def("trader/orders.json").help("where the ticket is written");
      s.opt({"--portfolio"}).def("trader/portfolio.json"); s.opt({"--plan"}).def("trader/plan.json"); s.opt({"--config"}).def("trader/config.json"); clock(s); }
    { Parser& s = p.sub("log"); s.opt({"--state"}).def("trader/state/broker.json"); clock(s); s.opt({"--all"}).store_true(); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { out(ap::help_text(ap::norm(p, p.prog))); return 1; }
    try {
        if (cmd == "show") return cmd_show(a);
        if (cmd == "approve") return cmd_approve(a);
        if (cmd == "decline") return cmd_decline(a);
        if (cmd == "result") return cmd_result(a);
        if (cmd == "fill") return cmd_fill(a);
        if (cmd == "flatten") return cmd_flatten(a);
        return cmd_log(a);
    } catch (const TicketError& e) { eprint(std::string("ticket: ") + e.what()); return 1; }
}
PACKTOOL("daytrade-broker/ticket", main);
}  // namespace packtools::daytrade_broker
