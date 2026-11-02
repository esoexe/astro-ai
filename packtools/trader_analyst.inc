// trader-analyst/facts - earnings reader: a company's reported numbers from SEC's XBRL "company facts" API, laid out
// quarter by quarter (revenue, margins, net income, diluted EPS, growth against a year earlier), the derived fourth
// quarter (10-K year minus the three 10-Qs), trailing twelve months, and the latest 10-Q/10-K and earnings 8-K with
// its press-release exhibit. Numbers only: it never says whether a stock is a buy.
// C++ port of trader-skill/trader-analyst/facts.py (Jake Schincariol, MIT); same sources, rules, output and exit codes.
// --offline DIR reads saved SEC files (the tests use it); network goes through the engine's client.
namespace packtools::trader_analyst {
using namespace pyrt;
struct FactsError : std::runtime_error { using std::runtime_error::runtime_error; };
static const char* TICKERS_URL = "https://www.sec.gov/files/company_tickers.json";
static const char* PLACEHOLDER = "you@example.com";
static const char* UA_HELP = "SEC requires a User-Agent with your name and email. Set \"sec_user_agent\": \"Your Name you@example.com\" in trader/config.json (your real name and email), or export SEC_USER_AGENT, or pass --user-agent.";
static const std::vector<std::pair<std::string, std::vector<std::string>>> CONCEPTS = {
    {"revenue", {"RevenueFromContractWithCustomerExcludingAssessedTax", "Revenues", "RevenuesNetOfInterestExpense", "RevenueFromContractWithCustomerIncludingAssessedTax", "SalesRevenueNet", "SalesRevenueGoodsNet", "SalesRevenueServicesNet"}},
    {"cost_of_revenue", {"CostOfGoodsAndServicesSold", "CostOfRevenue", "CostOfGoodsSold"}},
    {"gross_profit", {"GrossProfit"}},
    {"operating_income", {"OperatingIncomeLoss"}},
    {"net_income", {"NetIncomeLoss", "ProfitLoss", "NetIncomeLossAvailableToCommonStockholdersBasic"}},
    {"eps_diluted", {"EarningsPerShareDiluted", "EarningsPerShareBasicAndDiluted"}}};
// ---- Python number semantics for the values (ints stay ints)
inline bool small_int(const Val& v) { return v.t == Val::INT && !v.big(); }
inline Val vsub(const Val& a, const Val& b) { if (small_int(a) && small_int(b)) return Val::integer(a.i - b.i); return Val::flt(a.num() - b.num()); }
inline Val vsum(const std::vector<Val>& xs) {   // sum(): exact while ints, then compensated floats
    long long acc = 0; std::size_t k = 0;
    for (; k < xs.size() && small_int(xs[k]); ++k) acc += xs[k].i;
    if (k == xs.size()) return Val::integer(acc);
    std::vector<double> v = {static_cast<double>(acc)}; for (; k < xs.size(); ++k) v.push_back(xs[k].num());
    return Val::flt(fsum(v));
}
inline Val vround(const Val& v, int nd) { return v.t == Val::FLOAT ? Val::flt(round_n(v.f, nd)) : v; }   // round(int, n) is the int
inline long long dnum(const std::string& s) {   // date.fromisoformat(s) as a day number
    const auto bad = [&]() { return PyErr("ValueError", "Invalid isoformat string: " + str_repr(s)); };
    long long y = -1, m = -1, d = -1;
    const auto dig = [&](std::size_t a, std::size_t n) -> long long { if (a + n > s.size()) return -1; long long x = 0; for (std::size_t k = a; k < a + n; ++k) { if (s[k] < '0' || s[k] > '9') return -1; x = x * 10 + (s[k] - '0'); } return x; };
    if (s.size() == 10 && s[4] == '-' && s[7] == '-') { y = dig(0, 4); m = dig(5, 2); d = dig(8, 2); }
    else if (s.size() == 8) { y = dig(0, 4); m = dig(4, 2); d = dig(6, 2); }
    if (y < 0 || m < 0 || d < 0 || !valid_ymd(y, m, d)) throw bad();
    return civil_days(y, static_cast<unsigned>(m), static_cast<unsigned>(d));
}
inline std::string diso(long long n) { long long y; unsigned m, d; civil_from(n - 719468, y, m, d); char b[48]; std::snprintf(b, sizeof b, "%04lld-%02u-%02u", y, m, d); return b; }
inline std::optional<std::string> user_agent(const Val& cli, const std::string& config) {
    const char* env = std::getenv("SEC_USER_AGENT");
    std::string ua = strip(truthy(cli) ? pystr(cli) : (env && *env ? std::string(env) : std::string()));
    if (ua.empty() && !config.empty() && is_file(config)) {
        try { const Val c = loads(read_text(config)); ua = strip(pystr(orv(dget(c, "sec_user_agent"), Val::str("")))); } catch (const std::exception&) { ua = ""; }
    }
    if (ua.empty() || ua.find('@') == std::string::npos || ua.find(PLACEHOLDER) != std::string::npos) return std::nullopt;
    return ua;
}
struct Fetcher {
    std::optional<std::string> ua; std::string offline; double pause = 0.15; long long last = 0;
    Val json(const std::string& url, const std::string& name) {
        if (!offline.empty()) {
            const std::string path = path_join(offline, name);
            if (!is_file(path)) throw FactsError("offline: no file " + path);
            return loads(read_text(path));
        }
        if (!ua) throw FactsError(UA_HELP);
        const long long gap = now_us() - last, want = static_cast<long long>(pause * 1e6);
        if (gap < want) std::this_thread::sleep_for(std::chrono::microseconds(want - gap));
        last = now_us();
        const HttpResp r = http_get(url, {"User-Agent: " + *ua, "Accept-Encoding: gzip"}, 60);
        if (r.status == 404) throw FactsError("SEC has no data at " + url + " (no XBRL filings for this CIK?)");
        if (r.status >= 400) throw FactsError("HTTP " + std::to_string(r.status) + " for " + url);
        if (!r.error.empty() || r.status == 0) throw FactsError("could not reach " + url + ": " + (r.error.empty() ? std::string("no response") : r.error));
        return loads(r.body);
    }
};
// ---- facts
inline List facts_for(const Val& cf, const std::string& cname, const std::string& unit_hint) {
    const Val node = dget(dget(dget(cf, "facts", Val::dict()), "us-gaap", Val::dict()), cname);
    if (!truthy(node)) return {};
    const Val units = dget(node, "units", Val::dict());
    std::string unit; bool have = false;
    if (has(units, unit_hint)) { unit = unit_hint; have = true; }
    else if (units.t == Val::DICT && !units.d->items.empty()) { unit = units.d->items[0].first; have = true; }
    return have ? iter(dget(units, unit, Val::list())) : List{};
}
struct QV { Val value; std::string start, end; bool derived = false; Val fy, fp, form, filed; std::string cname; };
template <class V> struct OMap {   // insertion-ordered dict; assigning an existing key keeps its place
    std::vector<std::pair<std::string, V>> items;
    V* find(const std::string& k) { for (auto& kv : items) if (kv.first == k) return &kv.second; return nullptr; }
    const V* find(const std::string& k) const { for (const auto& kv : items) if (kv.first == k) return &kv.second; return nullptr; }
    void set(const std::string& k, V v) { if (V* p = find(k)) *p = std::move(v); else items.push_back({k, std::move(v)}); }
    bool empty() const { return items.empty(); }
};
struct Period { Val f, fy, fp, first_filed; std::string start, end; };
// {(start, end): fact}: the value from the latest filing, the fiscal label from the first
inline std::vector<Period> periods(const List& facts) {
    std::vector<std::pair<std::pair<std::string, std::string>, Val>> latest, first;
    const auto stamp = [](const Val& f) { return std::make_pair(pystr(dget(f, "filed", Val::str(""))), pystr(dget(f, "accn", Val::str("")))); };
    const auto slot = [](auto& m, const std::pair<std::string, std::string>& k) -> Val* { for (auto& kv : m) if (kv.first == k) return &kv.second; return nullptr; };
    for (const auto& f : facts) {
        if (!has(f, "start")) continue;
        const std::string form = split(pystr(dget(f, "form", Val::str(""))), "/")[0];
        if (form != "10-Q" && form != "10-K" && form != "20-F" && form != "40-F") continue;
        const auto key = std::make_pair(pystr(get(f, "start")), pystr(get(f, "end")));
        if (Val* l = slot(latest, key)) { if (stamp(f) > stamp(*l)) *l = f; } else latest.push_back({key, f});
        if (Val* l = slot(first, key)) { if (stamp(f) < stamp(*l)) *l = f; } else first.push_back({key, f});
    }
    std::vector<Period> out;
    for (const auto& [key, f] : latest) { const Val fs = *slot(first, key); out.push_back({f, dget(fs, "fy"), dget(fs, "fp"), dget(fs, "filed"), key.first, key.second}); }
    return out;
}
inline OMap<QV> quarterly(const List& facts, bool derive_q4 = true) {
    OMap<QV> q; std::vector<Period> years;
    for (const auto& p : periods(facts)) {
        const long long days = dnum(p.end) - dnum(p.start);
        if (80 <= days && days <= 100) q.set(p.end, QV{get(p.f, "val"), p.start, p.end, false, p.fy, p.fp, dget(p.f, "form"), dget(p.f, "filed"), ""});
        else if (350 <= days && days <= 380) years.push_back(p);
    }
    if (derive_q4) for (const auto& y : years) {
        if (q.find(y.end)) continue;
        std::vector<QV> inside; for (const auto& [_, v] : q.items) if (v.start >= y.start && v.end < y.end) inside.push_back(v);
        std::stable_sort(inside.begin(), inside.end(), [](const QV& a, const QV& b) { return a.end < b.end; });
        if (inside.size() != 3) continue;
        std::vector<Val> vals; for (const auto& v : inside) vals.push_back(v.value);
        q.set(y.end, QV{vsub(get(y.f, "val"), vsum(vals)), diso(dnum(inside.back().end) + 1), y.end, true, y.fy, Val::str("Q4"), dget(y.f, "form"), dget(y.f, "filed"), ""});
    }
    return q;
}
inline std::pair<OMap<QV>, std::vector<std::string>> series(const Val& cf, const std::string& key, const std::vector<std::string>& concepts) {
    const std::string unit = key == "eps_diluted" ? "USD/shares" : "USD";
    OMap<QV> merged; std::vector<std::string> used;
    for (const auto& c : concepts) {
        OMap<QV> q = quarterly(facts_for(cf, c, unit), true);
        if (q.empty()) continue;
        bool added = false;
        for (auto& [end, v] : q.items) if (!merged.find(end)) { v.cname = c; merged.set(end, v); added = true; }
        if (added) used.push_back(c);
    }
    return {merged, used};
}
inline const QV* yoy_match(const OMap<QV>& s, const std::string& end, long long days = 365, long long slack = 10) {
    const long long target = dnum(end) - days; std::optional<std::pair<long long, const QV*>> best;
    for (const auto& [e, v] : s.items) { const long long gap = std::llabs(dnum(e) - target); if (gap <= slack && (!best || gap < best->first)) best = std::make_pair(gap, &v); }
    return best ? best->second : nullptr;
}
inline Val pct(const Val& a, const Val& b) {
    if (a.is_none() || b.is_none() || b.num() == 0) return Val();
    return Val::flt(round_n(100.0 * vsub(a, b).num() / std::fabs(b.num()), 1));
}
inline Val margin(const Val& part, const Val& whole) { if (part.is_none() || !truthy(whole)) return Val(); return Val::flt(round_n(100.0 * part.num() / whole.num(), 1)); }
struct Series { std::map<std::string, OMap<QV>> s; Val concepts; };
inline Val qval(const OMap<QV>& m, const std::string& end) { const QV* v = m.find(end); return v ? v->value : Val(); }
// every number in the report
inline Val analyse(const Val& cf, long long quarters, const Val& name) {
    std::map<std::string, OMap<QV>> s; Val concepts = Val::dict();
    for (const auto& [key, cs] : CONCEPTS) { auto [m, used] = series(cf, key, cs); s[key] = m; Val ul = Val::list(); for (const auto& u : used) ul.l->push_back(Val::str(u)); concepts.d->set(key, ul); }
    const OMap<QV>& rev = s["revenue"]; const OMap<QV>& ni = s["net_income"];
    if (rev.empty() && ni.empty()) throw FactsError("no quarterly revenue or net income in the XBRL facts");
    std::set<std::string> all; for (const auto& kv : rev.items) all.insert(kv.first); for (const auto& kv : ni.items) all.insert(kv.first);
    std::vector<std::string> ends(all.rbegin(), all.rend());
    const long long n = static_cast<long long>(ends.size()), take = quarters >= 0 ? std::min(quarters, n) : std::max(0LL, n + quarters);
    ends.resize(static_cast<std::size_t>(take));
    Val rows = Val::list();
    for (const auto& end : ends) {
        const Val r = qval(rev, end); Val gp = qval(s["gross_profit"], end);
        if (gp.is_none() && !r.is_none() && s["cost_of_revenue"].find(end)) gp = vsub(r, s["cost_of_revenue"].find(end)->value);
        const Val oi = qval(s["operating_income"], end), nv = qval(ni, end), eps = qval(s["eps_diluted"], end);
        const QV* pr = yoy_match(rev, end); const QV* pn = yoy_match(ni, end); const QV* pe = yoy_match(s["eps_diluted"], end);
        const QV* base = rev.find(end) ? rev.find(end) : ni.find(end);
        bool derived = false; for (const char* k : {"revenue", "net_income"}) if (const QV* v = s[k].find(end)) if (v->derived) derived = true;
        Val row = Val::dict();
        row.d->set("end", Val::str(end)); row.d->set("start", base ? Val::str(base->start) : Val());
        row.d->set("fiscal", Val::str((base && truthy(base->fp) ? pystr(base->fp) : std::string()) + " " + (base && truthy(base->fy) ? pystr(base->fy) : std::string())));
        row.d->set("derived", Val::boolean(derived)); row.d->set("revenue", r); row.d->set("gross_profit", gp); row.d->set("operating_income", oi); row.d->set("net_income", nv);
        row.d->set("eps_diluted", eps.is_none() ? Val() : vround(eps, 2));
        row.d->set("gross_margin", margin(gp, r)); row.d->set("operating_margin", margin(oi, r)); row.d->set("net_margin", margin(nv, r));
        row.d->set("revenue_yoy", pct(r, pr ? pr->value : Val())); row.d->set("net_income_yoy", pct(nv, pn ? pn->value : Val())); row.d->set("eps_yoy", pct(eps, pe ? pe->value : Val()));
        row.d->set("filed", base ? base->filed : Val()); row.d->set("form", base ? base->form : Val());
        rows.l->push_back(row);
    }
    Val ttm = Val::dict();
    for (const char* key : {"revenue", "net_income", "eps_diluted"}) {
        std::vector<std::string> ks; for (const auto& kv : s[key].items) ks.push_back(kv.first); std::sort(ks.begin(), ks.end());
        if (ks.size() >= 4) { ks.erase(ks.begin(), ks.end() - 4); if (dnum(ks.back()) - dnum(ks.front()) < 300) { std::vector<Val> v; for (const auto& e : ks) v.push_back(s[key].find(e)->value); ttm.d->set(key, vsum(v)); } }
    }
    if (ttm.d->has("eps_diluted")) ttm.d->set("eps_diluted", vround(ttm.d->at("eps_diluted"), 2));
    std::string cik = pystr(dget(cf, "cik", Val::str(""))); { std::string sign; if (!cik.empty() && (cik[0] == '-' || cik[0] == '+')) { sign = cik.substr(0, 1); cik = cik.substr(1); } while (cplen(cik) + sign.size() < 10) cik = "0" + cik; cik = sign + cik; }
    Val out = Val::dict();
    out.d->set("entity", truthy(name) ? name : dget(cf, "entityName")); out.d->set("cik", Val::str(cik)); out.d->set("rows", rows); out.d->set("ttm", ttm); out.d->set("concepts", concepts);
    return out;
}
// a note when SEC's facts lag a report that is already filed
inline Val stale_note(const Val& result, const Val& filings) {
    const Val rep = dget(orv(filings, Val::dict()), "report");
    if (!truthy(rep) || !truthy(get(result, "rows")) || !truthy(dget(rep, "period"))) return Val();
    const std::string latest = get(get(result, "rows").l->at(0), "end").s, period = pystr(get(rep, "period"));
    if (period > latest && dnum(period) - dnum(latest) > 20)
        return Val::str("SEC's XBRL facts stop at " + latest + ", but a " + pystr(get(rep, "form")) + " for the period ending " + period + " was filed " + pystr(get(rep, "filed")) + ". Read it directly: " + pystr(get(rep, "url")));
    return Val();
}
inline std::string money(const Val& v) {
    if (v.is_none()) return "n/a";
    const double x = v.num();
    return std::fabs(x) >= 1e9 ? "$" + fmt_f(x / 1e9, 2) + "B" : "$" + fmt_f(x / 1e6, 1) + "M";
}
// plain sentences: what changed in the latest quarter
inline std::vector<std::string> changes(const Val& result) {
    const List& rows = *get(result, "rows").l;
    if (rows.empty()) return {};
    const Val cur = rows[0]; const Val prev = rows.size() > 1 ? rows[1] : Val();
    std::vector<std::string> out;
    std::string line = "Quarter ending " + get(cur, "end").s + (get(cur, "derived").b ? " (derived from the 10-K)" : "") + ": revenue " + money(get(cur, "revenue"));
    if (!get(cur, "revenue_yoy").is_none()) line += " (" + fmt_fs(get(cur, "revenue_yoy").num(), 1) + "% vs a year earlier)";
    line += ", net income " + money(get(cur, "net_income"));
    if (!get(cur, "net_income_yoy").is_none()) line += " (" + fmt_fs(get(cur, "net_income_yoy").num(), 1) + "%)";
    if (!get(cur, "eps_diluted").is_none()) {
        line += ", diluted EPS $" + fmt_f(get(cur, "eps_diluted").num(), 2);
        if (!get(cur, "eps_yoy").is_none()) line += " (" + fmt_fs(get(cur, "eps_yoy").num(), 1) + "%)";
    }
    out.push_back(line + ".");
    for (const auto& [key, nm] : std::vector<std::pair<std::string, std::string>>{{"gross_margin", "Gross margin"}, {"operating_margin", "Operating margin"}, {"net_margin", "Net margin"}}) {
        if (get(cur, key).is_none()) continue;
        std::string s = nm + " " + fmt_f(get(cur, key).num(), 1) + "%";
        if (truthy(prev) && !get(prev, key).is_none()) s += " (" + fmt_fs(get(cur, key).num() - get(prev, key).num(), 1) + " points vs the quarter before)";
        out.push_back(s + ".");
    }
    const Val cn = get(cur, "net_income"), co = get(cur, "operating_income");
    if (!cn.is_none() && !co.is_none() && cn.num() > co.num())
        out.push_back("Net income is above operating income: it includes gains outside the business (investments, tax items). Check the 10-Q before reading it as growth.");
    if (truthy(prev) && !get(cur, "revenue").is_none() && truthy(get(prev, "revenue")))
        out.push_back("Revenue " + fmt_fs(pct(get(cur, "revenue"), get(prev, "revenue")).num(), 1) + "% vs the quarter before (many businesses are seasonal: compare with a year earlier first).");
    return out;
}
// ---- filings
inline std::optional<std::string> exhibit_name(const std::vector<std::string>& names, const std::optional<std::string>& primary) {
    static const std::regex skip(R"(^(R\d+\.htm|\d{10}-\d{2}-\d{6}))");
    static const std::vector<std::regex> rank = {
        std::regex(R"(ex(?:hibit)?[-_]?99[-_.]?0?1(?!\d))", std::regex::ECMAScript | std::regex::icase), std::regex(R"(ex(?:hibit)?[-_]?99)", std::regex::ECMAScript | std::regex::icase),
        std::regex(R"(earnings|release|press)", std::regex::ECMAScript | std::regex::icase), std::regex(R"(ex(?:hibit)?[-_]?\d)", std::regex::ECMAScript | std::regex::icase)};
    std::vector<std::string> docs;
    for (const auto& n : names) {
        const std::string l = lower(n);
        if (!(ends(l, ".htm") || ends(l, ".html") || ends(l, ".txt") || ends(l, ".pdf"))) continue;
        if (primary && n == *primary) continue;
        if (std::regex_search(n, skip)) continue;
        docs.push_back(n);
    }
    for (const auto& rx : rank) { std::vector<std::string> hits; for (const auto& n : docs) if (std::regex_search(n, rx)) hits.push_back(n); if (!hits.empty()) { std::sort(hits.begin(), hits.end()); return hits[0]; } }
    return std::nullopt;
}
inline Val at_i(const Val& rec, const char* key, std::size_t i) {
    const Val l = get(rec, key);
    if (l.t != Val::LIST || i >= l.l->size()) throw PyErr(l.t == Val::LIST ? "IndexError" : "KeyError", l.t == Val::LIST ? "list index out of range" : str_repr(key));
    return (*l.l)[i];
}
// newest 10-Q/10-K and newest earnings 8-K (item 2.02) with its EX-99 exhibit
inline Val latest_filings(const Val& submissions, const std::string& cik, Fetcher* fetcher) {
    const Val rec = dget(dget(submissions, "filings", Val::dict()), "recent", Val::dict());
    Val out = Val::dict(); out.d->set("report", Val()); out.d->set("earnings_8k", Val());
    const long long base_cik = to_int(Val::str(cik));
    const List forms = iter(dget(rec, "form", Val::list()));
    for (std::size_t i = 0; i < forms.size(); ++i) {
        const std::string form = pystr(forms[i]);
        const std::string acc = pystr(at_i(rec, "accessionNumber", i)), nodash = replace_all(acc, "-", "");
        const std::string folder = "https://www.sec.gov/Archives/edgar/data/" + std::to_string(base_cik) + "/" + nodash + "/";
        if (get(out, "report").is_none() && (form == "10-Q" || form == "10-K" || form == "20-F" || form == "40-F")) {
            Val r = Val::dict(); r.d->set("form", Val::str(form)); r.d->set("filed", at_i(rec, "filingDate", i)); r.d->set("period", at_i(rec, "reportDate", i));
            r.d->set("accession", Val::str(acc)); r.d->set("url", Val::str(folder + pystr(at_i(rec, "primaryDocument", i)))); out.d->set("report", r);
        }
        if (get(out, "earnings_8k").is_none() && form == "8-K") {
            const std::string items = has(rec, "items") ? pystr(orv(at_i(rec, "items", i), Val::str(""))) : "";
            if (items.find("2.02") != std::string::npos) {
                const std::string primary = pystr(at_i(rec, "primaryDocument", i));
                Val e = Val::dict(); e.d->set("form", Val::str(form)); e.d->set("filed", at_i(rec, "filingDate", i)); e.d->set("accession", Val::str(acc));
                e.d->set("url", Val::str(folder + primary)); e.d->set("exhibit", Val()); e.d->set("index", Val::str(folder + acc + "-index.htm"));
                if (fetcher) {
                    try {
                        const Val idx = fetcher->json(folder + "index.json", nodash + "/index.json");
                        std::vector<std::string> names; for (const auto& it : iter(dget(dget(idx, "directory", Val::dict()), "item", Val::list()))) names.push_back(pystr(get(it, "name")));
                        if (const auto pick = exhibit_name(names, primary)) e.d->set("exhibit", Val::str(folder + *pick));
                    } catch (const FactsError&) {}
                }
                out.d->set("earnings_8k", e);
            }
        }
        if (truthy(get(out, "report")) && truthy(get(out, "earnings_8k"))) break;
    }
    return out;
}
// ---- output
inline std::string m_(const Val& v) { return v.is_none() ? "-" : fmt_fc(v.num() / 1e6, 0); }
inline std::string p_(const Val& v) { if (v.is_none()) return "-"; const double x = v.num(); return (x < 0 || x > 0) ? fmt_fs(x, 1) + "%" : "0.0%"; }
inline std::string pm_(const Val& v) { return v.is_none() ? "-" : fmt_f(v.num(), 1) + "%"; }
inline std::string render(const std::string& ticker, const Val& result, const Val& filings) {
    std::vector<std::string> out = {pystr(get(result, "entity")) + " (" + ticker + "), CIK " + get(result, "cik").s + ". US$ millions except EPS. Source: SEC XBRL company facts.", ""};
    out.push_back(ljust("quarter end", 11) + " " + ljust("fiscal", 9) + " " + rjust("revenue", 10) + " " + rjust("rev yoy", 8) + " " + rjust("gross", 7) + " " + rjust("oper", 7) + " " + rjust("net", 7) + " " + rjust("net inc", 10) + " " + rjust("EPS dil", 8) + " " + rjust("EPS yoy", 7));
    bool any_derived = false;
    for (const auto& r : *get(result, "rows").l) {
        if (get(r, "derived").b) any_derived = true;
        out.push_back(ljust(get(r, "end").s, 11) + " " + ljust(cpslice(strip(get(r, "fiscal").s), 0, 9), 9) + " " + rjust(m_(get(r, "revenue")), 10) + " " + rjust(p_(get(r, "revenue_yoy")), 8) + " " +
                      rjust(pm_(get(r, "gross_margin")), 7) + " " + rjust(pm_(get(r, "operating_margin")), 7) + " " + rjust(pm_(get(r, "net_margin")), 7) + " " + rjust(m_(get(r, "net_income")), 10) + " " +
                      rjust(get(r, "eps_diluted").is_none() ? "-" : fmt_f(get(r, "eps_diluted").num(), 2), 8) + " " + rjust(p_(get(r, "eps_yoy")), 7) + (get(r, "derived").b ? "  derived" : ""));
    }
    const Val t = get(result, "ttm");
    if (truthy(t)) {
        out.push_back("");
        out.push_back("Last 4 quarters: revenue " + (dget(t, "revenue").is_none() ? std::string("-") : "$" + m_(get(t, "revenue")) + "M") + ", net income " + (dget(t, "net_income").is_none() ? std::string("-") : "$" + m_(get(t, "net_income")) + "M") +
                      ", diluted EPS " + (dget(t, "eps_diluted").is_none() ? std::string("-") : "$" + fmt_f(get(t, "eps_diluted").num(), 2)) + ".");
    }
    out.push_back(""); out.push_back("What changed:");
    for (const auto& line : changes(result)) out.push_back("  - " + line);
    if (truthy(filings)) {
        out.push_back("");
        const Val rep = dget(filings, "report"), e8 = dget(filings, "earnings_8k");
        if (truthy(rep)) out.push_back("Latest " + pystr(get(rep, "form")) + ": period " + pystr(get(rep, "period")) + ", filed " + pystr(get(rep, "filed")) + "  " + pystr(get(rep, "url")));
        if (truthy(e8)) out.push_back("Latest earnings 8-K (item 2.02): filed " + pystr(get(e8, "filed")) + "  " + pystr(truthy(get(e8, "exhibit")) ? get(e8, "exhibit") : get(e8, "index")));
    }
    const Val note = stale_note(result, filings);
    if (truthy(note)) { out.push_back(""); out.push_back("NOTE: " + note.s); }
    if (any_derived) { out.push_back(""); out.push_back("Derived quarters are the 10-K year minus the three 10-Qs; derived EPS is approximate."); }
    return join(out, "\n") + "\n";
}
inline Val metrics_row(const std::string& ticker, const Val& result) {
    const List& rows = *get(result, "rows").l; const Val cur = rows.empty() ? Val::dict() : rows[0]; const Val t = get(result, "ttm");
    Val r = Val::dict();
    r.d->set("ticker", Val::str(ticker)); r.d->set("name", get(result, "entity")); r.d->set("cik", get(result, "cik")); r.d->set("period_end", dget(cur, "end"));
    r.d->set("revenue_ttm", dget(t, "revenue")); r.d->set("net_income_ttm", dget(t, "net_income")); r.d->set("eps_ttm", dget(t, "eps_diluted"));
    r.d->set("revenue_growth_yoy", dget(cur, "revenue_yoy")); r.d->set("eps_growth_yoy", dget(cur, "eps_yoy")); r.d->set("net_income_growth_yoy", dget(cur, "net_income_yoy"));
    r.d->set("gross_margin", dget(cur, "gross_margin")); r.d->set("operating_margin", dget(cur, "operating_margin")); r.d->set("net_margin", dget(cur, "net_margin"));
    r.d->set("source", Val::str("SEC XBRL company facts"));
    return r;
}
inline Val clean_title(const Val& title) {   // re.sub(r"\s*/[A-Z]{2,5}/?\s*$", "", title or "").strip() or None
    static const std::regex rx(R"(\s*/[A-Z]{2,5}/?\s*$)");
    const std::string s = strip(std::regex_replace(truthy(title) ? pystr(title) : std::string(), rx, ""));
    return s.empty() ? Val() : Val::str(s);
}
inline OMap<std::pair<std::string, Val>> ticker_ciks(Fetcher& f) {
    const Val data = f.json(TICKERS_URL, "company_tickers.json");
    OMap<std::pair<std::string, Val>> out;
    for (const auto& [_, v] : data.d->items) {
        const std::string t = replace_all(upper(pystr(get(v, "ticker"))), "-", ".");
        if (out.find(t)) continue;
        std::string c = pystr(get(v, "cik_str")); while (cplen(c) < 10) c = "0" + c;
        out.set(t, {c, clean_title(dget(v, "title"))});
    }
    return out;
}
inline std::string zfill10(const std::string& s0) { std::string s = s0, sign; if (!s.empty() && (s[0] == '-' || s[0] == '+')) { sign = s.substr(0, 1); s = s.substr(1); } while (cplen(s) + sign.size() < 10) s = "0" + s; return sign + s; }
inline int main(const std::vector<std::string>& argv) {
    Parser p("facts", "Earnings reader for trader-analyst. Built into A.S.T.R.O AI.");
    p.pos("tickers").nargs("*").help("tickers, e.g. AAPL KO");
    p.opt({"--cik"}).help("use this CIK instead of a ticker lookup (one company)"); p.opt({"--quarters"}).integer().def(6);
    p.opt({"--no-filings"}).store_true().help("skip the 8-K and 10-Q links"); p.opt({"--metrics"}).metavar("FILE").help("merge a row per ticker into this JSON");
    p.opt({"--user-agent"}); p.opt({"--config"}).def("trader/config.json"); p.opt({"--offline"}).metavar("DIR"); p.opt({"--json"}).store_true();
    const Val a = parse_args(p, argv);
    if (!truthy(get(a, "tickers")) && !truthy(get(a, "cik"))) { out(ap::help_text(ap::norm(p, p.prog))); return 2; }
    const auto ua = user_agent(get(a, "user_agent"), S(a, "config"));
    if (!ua && !truthy(get(a, "offline"))) { eprint(std::string("facts: ") + UA_HELP); return 2; }
    Fetcher f; f.ua = ua; f.offline = truthy(get(a, "offline")) ? S(a, "offline") : "";
    Val results = Val::list(); std::vector<Val> rows; int failed = 0;
    OMap<std::pair<std::string, Val>> ciks;
    try { if (!truthy(get(a, "cik"))) ciks = ticker_ciks(f); } catch (const FactsError& e) { eprint(std::string("facts: ") + e.what()); return 2; }
    std::vector<std::string> wanted; if (truthy(get(a, "tickers"))) for (const auto& t : *get(a, "tickers").l) wanted.push_back(t.s); else wanted.push_back(S(a, "cik"));
    for (const auto& t : wanted) {
        const std::string tick = upper(t);
        std::string cik; Val name;
        if (truthy(get(a, "cik"))) cik = zfill10(S(a, "cik"));
        else if (const auto* hit = ciks.find(replace_all(tick, "-", "."))) { cik = hit->first; name = hit->second; }
        if (cik.empty()) { eprint("facts: no SEC CIK for " + tick + " (is it a US filer? ETFs and foreign filers may not have XBRL facts)"); failed++; continue; }
        Val result, filings;
        try {
            const Val cf = f.json("https://data.sec.gov/api/xbrl/companyfacts/CIK" + cik + ".json", "companyfacts-CIK" + cik + ".json");
            result = analyse(cf, get(a, "quarters").i, name);
            if (!B(a, "no_filings")) { const Val sub = f.json("https://data.sec.gov/submissions/CIK" + cik + ".json", "submissions-CIK" + cik + ".json"); filings = latest_filings(sub, cik, &f); }
        } catch (const FactsError& e) { eprint("facts: " + tick + ": " + e.what()); failed++; continue; }
        Val r = Val::dict(), cl = Val::list(); for (const auto& c : changes(result)) cl.l->push_back(Val::str(c));
        r.d->set("ticker", Val::str(tick)); r.d->set("result", result); r.d->set("filings", filings); r.d->set("changes", cl); r.d->set("note", stale_note(result, filings));
        results.l->push_back(r); rows.push_back(metrics_row(tick, result));
        if (!B(a, "json")) { out(render(tick, result, filings)); out("\n"); }
    }
    DumpOpts d; d.indent = 2;
    if (B(a, "json")) print(dumps(results, d));
    if (truthy(get(a, "metrics")) && !rows.empty()) {
        const std::string mp = S(a, "metrics");
        OMap<Val> existing;
        if (is_file(mp)) for (const auto& r : iter(dget(loads(read_text(mp)), "companies", Val::list()))) existing.set(pystr(get(r, "ticker")), r);
        for (const auto& r : rows) existing.set(get(r, "ticker").s, r);
        const std::string folder = dirname(mp); if (!folder.empty()) makedirs(folder);
        std::vector<std::pair<std::string, Val>> sorted_rows = existing.items;
        std::stable_sort(sorted_rows.begin(), sorted_rows.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
        Val doc = Val::dict(), cl = Val::list(); for (const auto& kv : sorted_rows) cl.l->push_back(kv.second);
        doc.d->set("as_of", Val::str(today_iso())); doc.d->set("companies", cl);
        write_text(mp, dumps(doc, d) + "\n");
        if (!B(a, "json")) print("wrote " + mp + " (" + std::to_string(existing.items.size()) + " companies)");
    }
    return !results.l->empty() && !failed ? 0 : (!results.l->empty() ? 1 : 2);
}
PACKTOOL("trader-analyst/facts", main);
PACKTOOL("daytrade-analyst/facts", main);   // day-trader-skill ships the same facts.py (only its User-Agent string differs)
}  // namespace packtools::trader_analyst
