// trader-tax/lots - cost-basis lots: unrealized gains, what a sale would realize (fifo / hifo / specific lots),
// tax-loss harvesting proposals and wash-sale checks. Not tax advice: arithmetic on the lots the user gives it.
// C++ port of trader-skill/trader-tax/lots.py (Jake Schincariol, MIT); same rules, same files, same output.
// Adapted for A.S.T.R.O AI: messages name the helpers as A.S.T.R.O AI runs them (trader-alerts: alerts quote ...),
// and errors start with "lots:".
namespace packtools::trader_tax {
using namespace pyrt;
struct TaxError : std::runtime_error { using std::runtime_error::runtime_error; };
static const std::vector<std::set<std::string>> FAMILIES = {{"GOOGL", "GOOG"}, {"BRK.A", "BRK.B", "BRK-A", "BRK-B"}, {"FOXA", "FOX"}, {"NWSA", "NWS"}, {"LLYVA", "LLYVK"}, {"LEN", "LEN.B"}, {"UA", "UAA"}};
constexpr double EPS = 1e-9;
// ---- dates (proleptic Gregorian day numbers, like datetime.date)
struct Date { int y = 1970, m = 1, d = 1; };
inline bool leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
inline int mdays(int y, int m) { static const int n[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}; return m == 2 && leap(y) ? 29 : n[m - 1]; }
inline long long ord(const Date& t) { const int y = t.m <= 2 ? t.y - 1 : t.y; const long long era = (y >= 0 ? y : y - 399) / 400; const long long yoe = y - era * 400; const long long doy = (153 * (t.m + (t.m > 2 ? -3 : 9)) + 2) / 5 + t.d - 1; return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy; }
inline Date from_ord(long long z) { const long long era = (z >= 0 ? z : z - 146096) / 146097; const long long doe = z - era * 146097; const long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100); const long long mp = (5 * doy + 2) / 153; const int d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1); const int m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9); return {static_cast<int>(yoe + era * 400 + (m <= 2)), m, d}; }
inline Date add_days(const Date& t, long long n) { return from_ord(ord(t) + n); }
inline bool operator<(const Date& a, const Date& b) { return ord(a) < ord(b); }
inline bool operator<=(const Date& a, const Date& b) { return ord(a) <= ord(b); }
inline bool operator==(const Date& a, const Date& b) { return ord(a) == ord(b); }
inline std::string iso(const Date& t) { char b[32]; std::snprintf(b, sizeof b, "%04d-%02d-%02d", t.y, t.m, t.d); return b; }
// date.fromisoformat(str(s)[:10]): YYYY-MM-DD or YYYYMMDD
inline Date day(const Val& s) {
    const std::string v = cpslice(pystr(s), 0, 10);
    const auto bad = [&]() -> TaxError { return TaxError("not a date (YYYY-MM-DD): " + repr(s)); };
    std::string y, m, d;
    if (v.size() == 10 && v[4] == '-' && v[7] == '-') { y = v.substr(0, 4); m = v.substr(5, 2); d = v.substr(8, 2); }
    else if (v.size() == 8) { y = v.substr(0, 4); m = v.substr(4, 2); d = v.substr(6, 2); }
    else throw bad();
    for (const std::string* p : {&y, &m, &d}) for (char c : *p) if (c < '0' || c > '9') throw bad();
    const Date t{std::stoi(y), std::stoi(m), std::stoi(d)};
    if (t.y < 1 || t.m < 1 || t.m > 12 || t.d < 1 || t.d > mdays(t.y, t.m)) throw bad();
    return t;
}
inline Val read_json(const std::string& path, const std::string& what, bool required = true) {
    if (path.empty() || !exists(path)) { if (required) throw TaxError("no " + what + " at " + path); return Val(); }
    try { return loads(read_text(path)); } catch (const JSONDecodeError& e) { throw TaxError(path + " is not valid JSON: " + e.what()); }
}
inline void write_json(const std::string& path, const Val& data) {
    const std::string d = dirname(path); if (!d.empty()) makedirs(d);
    DumpOpts o; o.indent = 2; write_text(path, dumps(data, o) + "\n");
}
inline std::string money(double x) { return std::string(x < -0.004 ? "-" : "") + "$" + fmt_fc(std::fabs(x), 2); }
inline std::string money(const Val& v) { return money(to_float(v)); }
inline std::string fmt_qty(double x) { std::string s = fmt_f(x, 6); while (!s.empty() && s.back() == '0') s.pop_back(); if (!s.empty() && s.back() == '.') s.pop_back(); return s.empty() ? "0" : s; }
inline std::set<std::string> family(const std::string& ticker) { const std::string t = upper(ticker); for (const auto& f : FAMILIES) if (f.count(t)) return f; return {t}; }
inline std::string fam_str(const std::string& t) { const auto f = family(t); return join(std::vector<std::string>(f.begin(), f.end()), "/"); }
// ---- lots
struct Lot { Val id; Date date; double shares, cost, per_share; };
using LotMap = std::vector<std::pair<std::string, std::vector<Lot>>>;   // insertion-ordered {ticker: lots}
inline std::string idkey(const Val& id) { return repr(id); }
inline LotMap lots_of(const Val& portfolio) {
    LotMap out;
    const Val pf = truthy(portfolio) ? portfolio : Val::dict();
    for (const auto& p : iter(dget(pf, "positions", Val::list()))) {
        const std::string t = upper(pystr(orv(orv(dget(p, "ticker"), dget(p, "symbol")), Val::str(""))));
        if (t.empty()) continue;
        std::vector<Lot> lots; long long i = 0;
        for (const auto& l : iter(dget(p, "lots", Val::list()))) {
            ++i;
            const double sh = to_float(dget(l, "shares", Val::integer(0))), cost = to_float(dget(l, "cost", Val::integer(0)));
            if (sh <= EPS) continue;
            lots.push_back({orv(dget(l, "id"), Val::str(t + "-" + std::to_string(i))), day(dget(l, "date")), sh, cost, cost / sh});
        }
        if (lots.empty() && to_float(orv(dget(p, "shares", Val::integer(0)), Val::integer(0))) > 0)
            throw TaxError(t + " has shares but no lots; add its lots (date, shares, cost) to trader/portfolio.json");
        std::stable_sort(lots.begin(), lots.end(), [](const Lot& a, const Lot& b) { if (!(a.date == b.date)) return a.date < b.date; return pystr(a.id) < pystr(b.id); });
        bool found = false; for (auto& kv : out) if (kv.first == t) { kv.second = lots; found = true; }
        if (!found) out.push_back({t, lots});
    }
    return out;
}
inline const std::vector<Lot>* lots_for(const LotMap& m, const std::string& t) { for (const auto& kv : m) if (kv.first == t) return &kv.second; return nullptr; }
inline Date anniversary(const Date& d) { return (d.m == 2 && d.d == 29 && !leap(d.y + 1)) ? Date{d.y + 1, 2, 28} : Date{d.y + 1, d.m, d.d}; }
inline std::string term(const Date& acquired, const Date& sold) { return anniversary(acquired) < sold ? "long" : "short"; }   // held MORE than one year
using Prices = std::vector<std::pair<std::string, double>>;
inline double price_of(const Prices& px, const std::string& t) { double v = 0; for (const auto& kv : px) if (kv.first == t) v = kv.second; return v; }
struct Unreal { List rows, totals; std::vector<std::string> missing; };
inline Unreal unrealized(const Val& portfolio, const Prices& prices, const Date& today) {
    Unreal U; LotMap m = lots_of(portfolio);
    std::stable_sort(m.begin(), m.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [t, lots] : m) {
        const double p = price_of(prices, t);
        if (p == 0) { U.missing.push_back(t); continue; }
        double shares = 0, cost = 0, value = 0; double tm_g[2] = {0, 0};
        for (const auto& l : lots) {
            const double v = l.shares * p, g = v - l.cost; const std::string tm = term(l.date, today);
            Val r = Val::dict();
            r.d->set("ticker", Val::str(t)); r.d->set("id", l.id); r.d->set("date", Val::str(iso(l.date))); r.d->set("shares", Val::flt(l.shares));
            r.d->set("cost", Val::flt(round_n(l.cost, 2))); r.d->set("price", Val::flt(p)); r.d->set("value", Val::flt(round_n(v, 2)));
            r.d->set("gain", Val::flt(round_n(g, 2))); r.d->set("term", Val::str(tm)); r.d->set("long_term_on", Val::str(iso(add_days(anniversary(l.date), 1))));
            U.rows.push_back(r);
            shares += l.shares; cost += l.cost; value += v; tm_g[tm == "long"] += g;
        }
        Val tot = Val::dict();
        const double gain = round_n(value - cost, 2);
        double pct = cost != 0 ? round_n(100.0 * gain / cost, 2) : 0.0; if (std::fabs(pct) < 0.005) pct = 0.0;
        tot.d->set("ticker", Val::str(t)); tot.d->set("shares", Val::flt(shares)); tot.d->set("cost", Val::flt(round_n(cost, 2))); tot.d->set("value", Val::flt(round_n(value, 2)));
        tot.d->set("short", Val::flt(round_n(tm_g[0], 2))); tot.d->set("long", Val::flt(round_n(tm_g[1], 2))); tot.d->set("gain", Val::flt(gain)); tot.d->set("pct", Val::flt(pct));
        U.totals.push_back(tot);
    }
    return U;
}
inline double held_sum(const std::vector<const Lot*>& ls) { std::vector<double> v; for (auto* l : ls) v.push_back(l->shares); return fsum(v); }
inline std::vector<std::pair<const Lot*, double>> pick_lots(const std::vector<Lot>& lots, double shares, const std::string& method, const std::vector<std::string>& ids) {
    std::vector<const Lot*> all; for (const auto& l : lots) all.push_back(&l);
    const double held = held_sum(all);
    if (shares > held + EPS) throw TaxError("selling " + fmt_qty(shares) + " shares but only " + fmt_qty(held) + " are held");
    std::vector<const Lot*> order;
    if (method == "fifo") order = all;
    else if (method == "hifo") { order = all; std::stable_sort(order.begin(), order.end(), [](const Lot* a, const Lot* b) { return -a->per_share < -b->per_share; }); }
    else if (method == "specific") {
        if (ids.empty()) throw TaxError("--method specific needs --lots ID,ID");
        std::vector<std::pair<std::string, const Lot*>> by_id;   // {id: lot}, later duplicates win, first position kept
        for (auto* l : all) { bool f = false; for (auto& kv : by_id) if (kv.first == idkey(l->id)) { kv.second = l; f = true; } if (!f) by_id.push_back({idkey(l->id), l}); }
        std::vector<std::string> unknown; for (const auto& i : ids) { bool f = false; for (auto& kv : by_id) if (kv.first == idkey(Val::str(i))) f = true; if (!f) unknown.push_back(i); }
        if (!unknown.empty()) { std::vector<std::string> names; for (auto& kv : by_id) names.push_back(pystr(kv.second->id)); throw TaxError("no such lot: " + join(unknown, ", ") + " (lots: " + join(names, ", ") + ")"); }
        for (const auto& i : ids) for (auto& kv : by_id) if (kv.first == idkey(Val::str(i))) order.push_back(kv.second);
        if (shares > held_sum(order) + EPS) throw TaxError("the named lots hold fewer than " + fmt_qty(shares) + " shares");
    } else throw TaxError("unknown method " + repr(Val::str(method)) + " (fifo, hifo, specific)");
    std::vector<std::pair<const Lot*, double>> out; double left = shares;
    for (auto* l : order) { if (left <= EPS) break; const double take = l->shares < left ? l->shares : left; out.push_back({l, take}); left -= take; }
    return out;
}
inline Val simulate_sale(const std::vector<Lot>& lots, double shares, double price, const Date& sold, const std::string& method = "fifo", const std::vector<std::string>& ids = {}) {
    Val used = Val::list(); double proceeds = 0, basis = 0, tm_g[2] = {0, 0};
    for (const auto& [l, take] : pick_lots(lots, shares, method, ids)) {
        const double b = l->per_share * take, pr = price * take; const std::string tm = term(l->date, sold);
        Val u = Val::dict();
        u.d->set("id", l->id); u.d->set("acquired", Val::str(iso(l->date))); u.d->set("shares", Val::flt(round_n(take, 6)));
        u.d->set("basis", Val::flt(round_n(b, 2))); u.d->set("proceeds", Val::flt(round_n(pr, 2))); u.d->set("gain", Val::flt(round_n(pr - b, 2))); u.d->set("term", Val::str(tm));
        used.l->push_back(u);
        proceeds += pr; basis += b; tm_g[tm == "long"] += pr - b;
    }
    Val r = Val::dict();
    const double P = round_n(proceeds, 2), B = round_n(basis, 2);
    r.d->set("proceeds", Val::flt(P)); r.d->set("basis", Val::flt(B)); r.d->set("short", Val::flt(round_n(tm_g[0], 2))); r.d->set("long", Val::flt(round_n(tm_g[1], 2)));
    r.d->set("gain", Val::flt(round_n(P - B, 2))); r.d->set("lots", used);
    return r;
}
// ---- wash sales
struct Buy { std::string ticker; std::optional<double> shares; };
inline std::vector<Buy> planned_buys(const List& orders, const Prices& prices) {
    std::vector<Buy> out;
    for (const auto& o : orders) {
        if (lower(pystr(dget(o, "side", Val::str("")))) != "buy") continue;
        const std::string t = upper(pystr(dget(o, "symbol", Val::str(""))));
        std::optional<double> sh;
        if (!dget(o, "qty").is_none()) sh = to_float(get(o, "qty"));
        else if (!dget(o, "notional").is_none() && price_of(prices, t) != 0) sh = to_float(get(o, "notional")) / price_of(prices, t);
        out.push_back({t, sh});
    }
    return out;
}
inline List wash_check(const std::string& ticker, const Date& sold, const Val& used, const Val& portfolio, const List& orders, const Prices& prices) {
    const auto fam = family(ticker);
    std::map<std::string, double> sold_from; for (const auto& u : iter(used)) sold_from[idkey(get(u, "id"))] = to_float(get(u, "shares"));
    List hits; const Date lo = add_days(sold, -30), hi = add_days(sold, 30);
    for (const auto& [t, lots] : lots_of(portfolio)) {
        if (!fam.count(t)) continue;
        for (const auto& l : lots) {
            const auto it = sold_from.find(idkey(l.id));
            const double keep = l.shares - (it == sold_from.end() ? 0.0 : it->second);
            if (keep > EPS && lo <= l.date && l.date <= hi) {
                Val h = Val::dict(); h.d->set("ticker", Val::str(t)); h.d->set("date", Val::str(iso(l.date))); h.d->set("shares", Val::flt(round_n(keep, 6)));
                h.d->set("source", Val::str("lot " + pystr(l.id) + " you still hold")); hits.push_back(h);
            }
        }
    }
    for (const auto& b : planned_buys(orders, prices)) if (fam.count(b.ticker)) {
        Val h = Val::dict(); h.d->set("ticker", Val::str(b.ticker)); h.d->set("date", Val::str("planned")); h.d->set("shares", b.shares ? Val::flt(*b.shares) : Val());
        h.d->set("source", Val::str("a planned buy in trader/orders.json")); hits.push_back(h);
    }
    return hits;
}
// loss disallowed: proportional to the replacement shares (all of it if unknown)
inline double disallowed(double loss, double shares_sold, const List& hits) {
    if (loss >= 0 || hits.empty()) return 0.0;
    double repl = 0;
    for (const auto& h : hits) { if (get(h, "shares").is_none()) return round_n(-loss, 2); repl += to_float(get(h, "shares")); }
    if (shares_sold == 0) return 0.0;
    const double r = repl / shares_sold;
    return round_n(-loss * (1.0 < r ? 1.0 : r), 2);
}
inline List recent_loss_sales(const std::string& ticker, const Date& when, const Val& realized) {
    const auto fam = family(ticker); List out;
    for (const auto& r : iter(orv(realized, Val::list()))) {
        if (!fam.count(upper(pystr(dget(r, "symbol", Val::str(""))))) || to_float(dget(r, "gain", Val::integer(0))) >= 0) continue;
        const Date d = day(dget(r, "date"));
        if (d <= when && when <= add_days(d, 30)) out.push_back(r);
    }
    return out;
}
inline std::string rebuy_date(const Date& sold) { return iso(add_days(sold, 31)); }
// ---- harvest
inline List harvest(const Val& portfolio, const Prices& prices, const Date& today, double min_loss, double min_pct, const List& orders) {
    List cands; LotMap m = lots_of(portfolio);
    std::stable_sort(m.begin(), m.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [t, lots] : m) {
        const double p = price_of(prices, t);
        if (p == 0) continue;
        std::vector<const Lot*> losers; for (const auto& l : lots) if (l.shares * p < l.cost - 0.005) losers.push_back(&l);
        if (losers.empty()) continue;
        std::vector<double> sv, cv; for (auto* l : losers) { sv.push_back(l->shares); cv.push_back(l->cost); }
        const double shares = fsum(sv), cost = fsum(cv), loss = shares * p - cost, pct = cost != 0 ? 100.0 * loss / cost : 0.0;
        if (-loss < min_loss || -pct < min_pct) continue;
        std::vector<std::string> ids; Val idl = Val::list(); for (auto* l : losers) { ids.push_back(pystr(l->id)); idl.l->push_back(l->id); }
        const Val sale = simulate_sale(lots, shares, p, today, "specific", ids);
        const List hits = wash_check(t, today, get(sale, "lots"), portfolio, orders, prices);
        bool fifo_same = true; for (std::size_t k = 0; k < losers.size(); ++k) if (idkey(lots[k].id) != idkey(losers[k]->id)) fifo_same = false;
        Val c = Val::dict();
        c.d->set("ticker", Val::str(t)); c.d->set("price", Val::flt(p)); c.d->set("shares", Val::flt(round_n(shares, 6))); c.d->set("lots", idl);
        c.d->set("all_lots", Val::boolean(losers.size() == lots.size())); c.d->set("fifo_same", Val::boolean(fifo_same));
        c.d->set("loss", Val::flt(round_n(loss, 2))); c.d->set("pct", Val::flt(round_n(pct, 2))); c.d->set("short", get(sale, "short")); c.d->set("long", get(sale, "long"));
        Val hl = Val::list(); for (const auto& h : hits) hl.l->push_back(h);
        c.d->set("wash", hl); c.d->set("disallowed", Val::flt(disallowed(loss, shares, hits))); c.d->set("rebuy_on_or_after", Val::str(rebuy_date(today)));
        cands.push_back(c);
    }
    std::stable_sort(cands.begin(), cands.end(), [](const Val& a, const Val& b) { return get(a, "loss").f < get(b, "loss").f; });
    return cands;
}
inline std::string join_ids(const Val& ids) { std::vector<std::string> v; for (const auto& i : iter(ids)) v.push_back(pystr(i)); return join(v, ", "); }
inline List harvest_orders(const List& cands, bool include_wash) {
    List out;
    for (const auto& c : cands) {
        if (!get(c, "wash").l->empty() && !include_wash) continue;
        std::string why = "tax-loss harvest proposal: sell lots " + join_ids(get(c, "lots")) + " for an estimated " + money(get(c, "loss")) + " loss (" + money(get(c, "short")) + " short term, " + money(get(c, "long")) +
                          " long term). Do not buy " + fam_str(get(c, "ticker").s) + " again before " + get(c, "rebuy_on_or_after").s + " (wash sale). Not tax advice.";
        if (!get(c, "fifo_same").b) why += " These are not your oldest lots: tell your broker to sell these specific lots, or it may sell by FIFO and realize a different result.";
        Val o = Val::dict();
        o.d->set("symbol", get(c, "ticker")); o.d->set("side", Val::str("sell")); o.d->set("qty", Val::str(fmt_qty(get(c, "shares").f)));
        o.d->set("type", Val::str("market")); o.d->set("time_in_force", Val::str("day")); o.d->set("why", Val::str(why));
        out.push_back(o);
    }
    return out;
}
// ---- record a sale (the order filled): relieve the lots, add the proceeds, log the realized gain
inline void record_sale(Val& portfolio, const std::string& ticker, const Val& sale, const Date& sold, double price, Val& state) {
    const std::string t = upper(ticker);
    std::map<std::string, double> taken; for (const auto& u : iter(get(sale, "lots"))) taken[idkey(get(u, "id"))] = to_float(get(u, "shares"));
    if (portfolio.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(portfolio) + "' object has no attribute 'get'");
    for (const auto& p : iter(dget(portfolio, "positions", Val::list()))) {
        if (upper(pystr(dget(p, "ticker", Val::str("")))) != t) continue;
        Val nl = Val::list(); long long i = 0;
        for (const auto& l : iter(dget(p, "lots", Val::list()))) {
            ++i;
            const Val lid = orv(dget(l, "id"), Val::str(t + "-" + std::to_string(i)));
            l.d->set("id", lid);
            const auto it = taken.find(idkey(lid));
            if (it != taken.end()) {
                const double sh = to_float(l.d->at("shares")), take = it->second;
                l.d->set("cost", Val::flt(sh != 0 ? round_n(to_float(l.d->at("cost")) * (1 - take / sh), 2) : 0.0));
                l.d->set("shares", Val::flt(round_n(sh - take, 6)));
            }
            if (to_float(l.d->at("shares")) > EPS) nl.l->push_back(l);
        }
        p.d->set("lots", nl);
        std::vector<double> sv; for (const auto& l : *nl.l) sv.push_back(to_float(l.d->at("shares")));
        p.d->set("shares", sv.empty() ? Val::integer(0) : Val::flt(round_n(fsum(sv), 6)));
    }
    Val kept = Val::list(); for (const auto& p : iter(dget(portfolio, "positions", Val::list()))) if (to_float(dget(p, "shares", Val::integer(0))) > EPS) kept.l->push_back(p);
    portfolio.d->set("positions", kept);
    portfolio.d->set("cash", Val::flt(round_n(to_float(dget(portfolio, "cash", Val::integer(0))) + to_float(get(sale, "proceeds")), 2)));
    if (!state.d->has("realized")) state.d->set("realized", Val::list());
    Val realized = state.d->at("realized");
    std::vector<double> us; for (const auto& u : iter(get(sale, "lots"))) us.push_back(to_float(get(u, "shares")));
    Val r = Val::dict();
    r.d->set("date", Val::str(iso(sold))); r.d->set("symbol", Val::str(t)); r.d->set("shares", us.empty() ? Val::integer(0) : Val::flt(round_n(fsum(us), 6)));
    r.d->set("price", Val::flt(price)); r.d->set("proceeds", get(sale, "proceeds")); r.d->set("basis", get(sale, "basis")); r.d->set("gain", get(sale, "gain"));
    r.d->set("short", get(sale, "short")); r.d->set("long", get(sale, "long")); r.d->set("wash_disallowed", dget(sale, "wash_disallowed", Val::flt(0.0))); r.d->set("lots", get(sale, "lots"));
    if (realized.t != Val::LIST) throw PyErr("AttributeError", std::string("'") + type_name(realized) + "' object has no attribute 'append'");
    realized.l->push_back(r);
}
inline Val realized_ytd(const Val& state, int year) {
    double s = 0, l = 0, dis = 0; long long n = 0;
    for (const auto& r : iter(dget(truthy(state) ? state : Val::dict(), "realized", Val::list())))
        if (starts(pystr(dget(r, "date", Val::str(""))), std::to_string(year))) { s += to_float(dget(r, "short", Val::integer(0))); l += to_float(dget(r, "long", Val::integer(0))); dis += to_float(dget(r, "wash_disallowed", Val::integer(0))); n++; }
    Val o = Val::dict(); o.d->set("short", Val::flt(round_n(s, 2))); o.d->set("long", Val::flt(round_n(l, 2))); o.d->set("disallowed", Val::flt(round_n(dis, 2))); o.d->set("count", Val::integer(n));
    return o;
}
// ---- CLI
static const char* NOT_ADVICE = "Not tax advice: arithmetic on your lots. Wash-sale rules also count buys in your IRA, your spouse's accounts and other brokers, which this tool cannot see. Check with a tax professional before you act.";
struct Inputs { Val portfolio; Prices prices; Val state; List orders; Date today; };
inline Inputs inputs_(const Val& a) {
    Inputs I;
    I.portfolio = read_json(S(a, "portfolio"), "portfolio (lots)");
    const Val pdoc = orv(read_json(S(a, "prices"), "prices", false), Val::dict());
    if (pdoc.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(pdoc) + "' object has no attribute 'get'");
    const Val pm = orv(pdoc.d->at("prices", pdoc), Val::dict());
    if (pm.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(pm) + "' object has no attribute 'items'");
    for (const auto& [k, v] : pm.d->items) if (v.t == Val::INT || v.t == Val::FLOAT || v.t == Val::BOOL) {
        const std::string K = upper(k); const double f = to_float(v); bool hit = false;
        for (auto& kv : I.prices) if (kv.first == K) { kv.second = f; hit = true; }
        if (!hit) I.prices.push_back({K, f});
    }
    I.state = read_json(S(a, "state"), "tax state", false);
    if (!truthy(I.state)) { I.state = Val::dict(); I.state.d->set("realized", Val::list()); }
    const Val odoc = orv(read_json(S(a, "orders"), "orders", false), Val::dict());
    I.orders = iter(odoc.t == Val::DICT ? odoc.d->at("orders", Val::list()) : odoc);
    I.today = truthy(get(a, "today")) ? day(get(a, "today")) : day(Val::str(today_iso()));
    return I;
}
inline int cmd_report(const Val& a) {
    Inputs I = inputs_(a);
    const Unreal U = unrealized(I.portfolio, I.prices, I.today);
    if (B(a, "json")) {
        Val o = Val::dict(); Val rows = Val::list(), tots = Val::list(), miss = Val::list();
        for (const auto& r : U.rows) rows.l->push_back(r);
        for (const auto& t : U.totals) tots.l->push_back(t);
        for (const auto& m : U.missing) miss.l->push_back(Val::str(m));
        o.d->set("lots", rows); o.d->set("positions", tots); o.d->set("missing_prices", miss); o.d->set("realized_ytd", realized_ytd(I.state, I.today.y));
        DumpOpts d; d.indent = 2; print(dumps(o, d)); return 0;
    }
    print(ljust("ticker", 7) + " " + rjust("shares", 10) + " " + rjust("cost", 12) + " " + rjust("value", 12) + " " + rjust("gain", 12) + " " + rjust("gain %", 8));
    for (const auto& t : U.totals)
        print(ljust(get(t, "ticker").s, 7) + " " + rjust(fmt_qty(get(t, "shares").f), 10) + " " + rjust(money(get(t, "cost")), 12) + " " + rjust(money(get(t, "value")), 12) + " " + rjust(money(get(t, "gain")), 12) + " " + rjust(fmt_f(get(t, "pct").f, 2), 7) + "%");
    if (B(a, "lots")) {
        print("");
        print(ljust("lot", 10) + " " + ljust("bought", 11) + " " + rjust("shares", 10) + " " + rjust("cost", 11) + " " + rjust("gain", 11) + " " + rjust("term", 6) + " long term from");
        for (const auto& r : U.rows)
            print(ljust(pystr(get(r, "id")), 10) + " " + ljust(get(r, "date").s, 11) + " " + rjust(fmt_qty(get(r, "shares").f), 10) + " " + rjust(money(get(r, "cost")), 11) + " " + rjust(money(get(r, "gain")), 11) + " " + rjust(get(r, "term").s, 6) + " " + get(r, "long_term_on").s);
    }
    for (const auto& t : U.missing) print("No price for " + t + ": add it to trader/prices.json (trader-alerts: alerts quote " + t + " --write)");
    const Val ytd = realized_ytd(I.state, I.today.y);
    print("");
    print("Realized in " + std::to_string(I.today.y) + " (recorded sales): " + money(get(ytd, "short")) + " short term, " + money(get(ytd, "long")) + " long term, " + money(get(ytd, "disallowed")) + " disallowed by wash sales.");
    print(NOT_ADVICE);
    return 0;
}
inline int cmd_sell(const Val& a) {
    Inputs I = inputs_(a);
    const std::string t = upper(S(a, "ticker"));
    const LotMap m = lots_of(I.portfolio);
    const auto* lots = lots_for(m, t);
    if (!lots || lots->empty()) throw TaxError("no lots for " + t + " in " + S(a, "portfolio"));
    const double price = truthy(get(a, "price")) ? get(a, "price").f : price_of(I.prices, t);
    if (price == 0) throw TaxError("no price for " + t + ": pass --price");
    std::vector<std::string> ids; for (const auto& i : split(truthy(get(a, "lots")) ? S(a, "lots") : std::string(), ",")) if (!strip(i).empty()) ids.push_back(strip(i));
    const double shares = get(a, "shares").f; const std::string method = S(a, "method");
    Val sale = simulate_sale(*lots, shares, price, I.today, method, ids);
    const List hits = get(sale, "gain").f < 0 ? wash_check(t, I.today, get(sale, "lots"), I.portfolio, I.orders, I.prices) : List();
    Val hl = Val::list(); for (const auto& h : hits) hl.l->push_back(h);
    sale.d->set("wash", hl); sale.d->set("wash_disallowed", Val::flt(disallowed(get(sale, "gain").f, shares, hits)));
    if (B(a, "json")) { DumpOpts d; d.indent = 2; print(dumps(sale, d)); }
    else {
        print("Selling " + fmt_qty(shares) + " " + t + " at " + money(price) + " on " + iso(I.today) + " (" + method + "):");
        for (const auto& u : iter(get(sale, "lots")))
            print("  lot " + ljust(pystr(get(u, "id")), 8) + " bought " + get(u, "acquired").s + "  " + fmt_qty(get(u, "shares").f) + " shares  basis " + money(get(u, "basis")) + "  proceeds " + money(get(u, "proceeds")) + "  " + money(get(u, "gain")) + " (" + get(u, "term").s + " term)");
        const double g = get(sale, "gain").f;
        print("Total: proceeds " + money(get(sale, "proceeds")) + ", basis " + money(get(sale, "basis")) + ", " + (g >= 0 ? "gain" : "loss") + " " + money(g) + " (" + money(get(sale, "short")) + " short term, " + money(get(sale, "long")) + " long term).");
        for (const auto& h : hits) print("WASH SALE: " + get(h, "source").s + " (" + (truthy(get(h, "shares")) ? fmt_qty(get(h, "shares").f) : std::string("?")) + " shares, " + get(h, "date").s + ").");
        if (!hits.empty()) print("Up to " + money(get(sale, "wash_disallowed")) + " of this loss would be disallowed and added to the replacement shares' basis.");
        else if (g < 0) print("No wash-sale conflict found. Do not buy " + fam_str(t) + " again before " + rebuy_date(I.today) + ".");
        print(NOT_ADVICE);
    }
    if (B(a, "record")) {
        record_sale(I.portfolio, t, sale, I.today, price, I.state);
        write_json(S(a, "portfolio"), I.portfolio); write_json(S(a, "state"), I.state);
        if (!B(a, "json")) print("Recorded: lots updated in " + S(a, "portfolio") + ", sale logged in " + S(a, "state") + ".");
    }
    return 0;
}
inline int cmd_harvest(const Val& a) {
    Inputs I = inputs_(a);
    List buys; for (const auto& o : I.orders) if (lower(pystr(dget(o, "side", Val::str("")))) == "buy") buys.push_back(o);
    const double min_loss = get(a, "min_loss").num(), min_pct = get(a, "min_pct").num();
    const List cands = harvest(I.portfolio, I.prices, I.today, min_loss, min_pct, buys);
    const List props = harvest_orders(cands, B(a, "include_wash"));
    const Val w = get(a, "write");
    if (truthy(w) && !props.empty()) {
        Val doc = Val::dict(), ol = Val::list(); for (const auto& o : props) ol.l->push_back(o);
        doc.d->set("created_by", Val::str("trader-tax")); doc.d->set("created_at", Val::str(now_iso())); doc.d->set("status", Val::str("proposed"));
        doc.d->set("note", Val::str("Tax-loss harvesting proposals, not tax advice. Nothing is placed until trader-risk passes each order and you say yes to it in the chat."));
        doc.d->set("orders", ol); write_json(w.s, doc);
    }
    if (B(a, "json")) {
        Val o = Val::dict(), cl = Val::list(), ol = Val::list(); for (const auto& c : cands) cl.l->push_back(c); for (const auto& p : props) ol.l->push_back(p);
        o.d->set("candidates", cl); o.d->set("orders", ol); DumpOpts d; d.indent = 2; print(dumps(o, d));
        return cands.empty() ? 1 : 0;
    }
    if (cands.empty()) {
        print("No lot has a loss of at least " + money(min_loss) + (truthy(get(a, "min_pct")) ? " and " + pystr(get(a, "min_pct")) + "%" : std::string()) + ".");
        print(NOT_ADVICE); return 1;
    }
    for (const auto& c : cands) {
        print(ljust(get(c, "ticker").s, 6) + " sell " + fmt_qty(get(c, "shares").f) + " shares (lots " + join_ids(get(c, "lots")) + ") at " + money(get(c, "price")) + ": est. loss " + money(get(c, "loss")) + " (" + fmt_f(get(c, "pct").f, 2) + "%), " + money(get(c, "short")) + " short term, " + money(get(c, "long")) + " long term");
        for (const auto& h : iter(get(c, "wash"))) print("       WASH SALE RISK: " + get(h, "source").s + " (" + get(h, "date").s + ").");
        if (!get(c, "wash").l->empty()) print("       Up to " + money(get(c, "disallowed")) + " of the loss would be disallowed. Not proposed" + (B(a, "include_wash") ? " unless --include-wash" : "") + ".");
        else print("       Clear of wash sales in what this tool can see. Do not buy " + fam_str(get(c, "ticker").s) + " before " + get(c, "rebuy_on_or_after").s + ".");
    }
    if (truthy(w)) print(!props.empty() ? "Wrote " + std::to_string(props.size()) + " proposed sell order(s) to " + w.s + ". trader-risk checks them next." : std::string("Nothing written: every candidate has a wash-sale conflict."));
    print(NOT_ADVICE);
    return 0;
}
inline int cmd_wash(const Val& a) {
    Inputs I = inputs_(a);
    const std::string t = upper(S(a, "ticker"));
    const Date when = truthy(get(a, "date")) ? day(get(a, "date")) : I.today;
    const List hits = recent_loss_sales(t, when, dget(I.state, "realized", Val::list()));
    if (hits.empty()) {
        print("No recorded loss sale of " + fam_str(t) + " in the 30 days before " + iso(when) + ". Buying it then is not a wash sale against anything this tool knows about.");
        print(NOT_ADVICE); return 0;
    }
    for (const auto& r : hits)
        print("WASH SALE: you sold " + fmt_qty(to_float(dget(r, "shares"))) + " " + pystr(dget(r, "symbol")) + " at a " + money(dget(r, "gain")) + " loss on " + pystr(dget(r, "date")) + ". Buying " + t + " before " + rebuy_date(day(dget(r, "date"))) + " would disallow that loss.");
    print(NOT_ADVICE);
    return 1;
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("lots", "Cost-basis lots, gains, harvesting and wash sales (trader-tax). Not tax advice.");
    p.subcommands("cmd", false);
    const auto common = [](Parser& s) {
        s.opt({"--portfolio"}).def("trader/portfolio.json"); s.opt({"--prices"}).def("trader/prices.json"); s.opt({"--state"}).def("trader/state/tax.json");
        s.opt({"--orders"}).def("trader/orders.json").help("planned buys count for wash sales"); s.opt({"--today"}).help("YYYY-MM-DD (default: today)"); s.opt({"--json"}).store_true();
    };
    { Parser& s = p.sub("report"); common(s); s.opt({"--lots"}).store_true().help("show every lot"); }
    { Parser& s = p.sub("sell"); common(s); s.pos("ticker"); s.pos("shares").number(); s.opt({"--price"}).number();
      s.opt({"--method"}).def("fifo").choices({"fifo", "hifo", "specific"}); s.opt({"--lots"}).help("lot ids for --method specific: KO-1,KO-2");
      s.opt({"--record"}).store_true().help("the sale filled: update lots and log it"); }
    { Parser& s = p.sub("harvest"); common(s); s.opt({"--min-loss"}).number().def_float(100.0).help("dollars (default 100)");
      s.opt({"--min-pct"}).number().def_float(0.0).help("percent of cost (default 0)"); s.opt({"--include-wash"}).store_true().help("also propose candidates with a wash-sale conflict");
      s.opt({"--write"}).nargs("?").cnst(Val::str("trader/orders.json")).metavar("PATH").help("write proposals (default trader/orders.json)"); }
    { Parser& s = p.sub("wash"); common(s); s.pos("ticker"); s.opt({"--date"}).help("the planned buy date (default today)"); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { out(ap::help_text(ap::norm(p, p.prog))); return 1; }
    try {
        if (cmd == "report") return cmd_report(a);
        if (cmd == "sell") return cmd_sell(a);
        if (cmd == "harvest") return cmd_harvest(a);
        return cmd_wash(a);
    } catch (const TaxError& e) { eprint(std::string("lots: ") + e.what()); return 1; }
}
PACKTOOL("trader-tax/lots", main);
}  // namespace packtools::trader_tax
