// daytrade-exit/exit - the Exit Planner: sets the stop and the target BEFORE the entry, in exact cents (a long's stop
// rounds down, away from the entry; targets round half up), from trader/entry.json or flags. Stops: or-mid, or-low,
// price:X, pct:X, atr:K (14-bar ATR on the session's bars up to the entry bar). Refuses a stop at or above the entry, a
// target at or below it, a risk under 0.05% of the price, an untriggered entry and shorts. flat_by moves up with a
// 1:00 p.m. early close. It never places an order. C++ port of day-trader-skill/trader-exit/exit.py (Jake Schincariol,
// MIT); same math, files, output and exit codes. Python's Decimal is replaced by an exact fixed-point type (a 64-bit
// mantissa with overflow checks; any price a stock trades at fits).
namespace packtools::daytrade_exit {
using namespace pyrt;
struct PlanError : std::runtime_error { using std::runtime_error::runtime_error; };
namespace da = packtools::daytrade_alerts;
namespace dy = packtools::daytrade_yahoo;
using da::to_eastern; using da::eastern_to_utc; using da::day_of; using da::day_iso; using da::weekday; using da::session;
static const long long US = 1000000, MIN_US = 60 * US, DAY_US = 86400 * US;
static const int ATR_BARS = 14, OPEN_M = 9 * 60 + 30, CLOSE_M = 16 * 60;
static const double BACKOFF = 5.0;
static const char* NOTE = "A stop is a plan, not a guarantee: a gap or a fast market can fill past it.";

// ---------------------------------------------------------------- money math (exact, to the cent)
struct Dec { long long m = 0; int e = 0; };   // m x 10^-e, e >= 0
[[noreturn]] inline void too_big() { throw PlanError("a number has too many digits to plan with"); }
inline long long mul_(long long a, long long b) { long long r; if (__builtin_mul_overflow(a, b, &r)) too_big(); return r; }
inline long long add_(long long a, long long b) { long long r; if (__builtin_add_overflow(a, b, &r)) too_big(); return r; }
inline long long p10(int n) { long long r = 1; while (n-- > 0) r = mul_(r, 10); return r; }
inline Dec at_e(Dec a, int e) { if (e > a.e) { a.m = mul_(a.m, p10(e - a.e)); a.e = e; } return a; }
inline Dec operator+(Dec a, Dec b) { const int e = std::max(a.e, b.e); a = at_e(a, e); b = at_e(b, e); return {add_(a.m, b.m), e}; }
inline Dec operator-(Dec a, Dec b) { b.m = -b.m; return a + b; }
inline Dec operator*(Dec a, Dec b) { return {mul_(a.m, b.m), a.e + b.e}; }
inline int cmp(Dec a, Dec b) { const int e = std::max(a.e, b.e); a = at_e(a, e); b = at_e(b, e); return a.m < b.m ? -1 : a.m > b.m ? 1 : 0; }
inline bool operator<(Dec a, Dec b) { return cmp(a, b) < 0; }
inline bool operator<=(Dec a, Dec b) { return cmp(a, b) <= 0; }
inline bool operator>=(Dec a, Dec b) { return cmp(a, b) >= 0; }
inline bool operator!=(Dec a, Dec b) { return cmp(a, b) != 0; }
inline Dec half(Dec a) { return a.m % 2 == 0 ? Dec{a.m / 2, a.e} : Dec{mul_(a.m, 5), a.e + 1}; }
inline Dec hundredth(Dec a) { return {a.m, a.e + 2}; }   // a / 100
enum Round { HALF_UP, FLOOR, HALF_EVEN };
inline long long round_div(long long n, long long d, Round md) {   // n / d rounded, d != 0
    if (d < 0) { n = -n; d = -d; }
    const long long q = n / d, r = n % d;
    if (r == 0) return q;
    const bool neg = r < 0; const long long ar = neg ? -r : r, rest = d - ar;
    if (md == FLOOR) return neg ? q - 1 : q;
    const bool up = ar > rest || (ar == rest && (md == HALF_UP || q % 2 != 0));
    return up ? (neg ? q - 1 : q + 1) : q;
}
inline Dec quant(Dec a, int places, Round md) { if (a.e <= places) return at_e(a, places); return {round_div(a.m, p10(a.e - places), md), places}; }
inline Dec ratio_q(Dec n, Dec d, long long scale) {   // (n / d * scale).quantize(CENT, ROUND_HALF_UP)
    const int e = std::max(n.e, d.e); n = at_e(n, e); d = at_e(d, e);
    if (d.m == 0) throw PyErr("decimal.DivisionByZero", "[<class 'decimal.DivisionByZero'>]");
    return {round_div(mul_(n.m, mul_(scale, 100)), d.m, HALF_UP), 2};
}
inline Dec normal(Dec a) { while (a.e > 0 && a.m % 10 == 0) { a.m /= 10; a.e--; } if (a.m == 0) a.e = 0; return a; }
inline std::string to_str(Dec a) {
    const bool neg = a.m < 0;
    std::string digits = std::to_string(neg ? -(a.m + 1) + 1ULL : static_cast<unsigned long long>(a.m));
    if (a.e > 0) {
        if (static_cast<int>(digits.size()) <= a.e) digits.insert(0, static_cast<std::size_t>(a.e + 1) - digits.size(), '0');
        digits.insert(digits.size() - static_cast<std::size_t>(a.e), ".");
    }
    return (neg ? "-" : "") + digits;
}
inline double to_double(Dec a) { return std::strtod(to_str(a).c_str(), nullptr); }
inline std::optional<Dec> parse_dec(const std::string& s0) {   // Decimal(text), finite values
    const std::string s = strip(s0);
    std::size_t k = 0; bool neg = false;
    if (k < s.size() && (s[k] == '+' || s[k] == '-')) { neg = s[k] == '-'; k++; }
    Dec d; int nd = 0; bool dot = false; char prev = 0;
    for (; k < s.size(); ++k) {
        const char ch = s[k];
        if (ch >= '0' && ch <= '9') { d.m = add_(mul_(d.m, 10), ch - '0'); if (dot) d.e++; nd++; }
        else if (ch == '.' && !dot) dot = true;
        else if (ch == '_' && prev >= '0' && prev <= '9' && k + 1 < s.size() && s[k + 1] >= '0' && s[k + 1] <= '9') {}
        else break;
        prev = ch;
    }
    if (nd == 0) return std::nullopt;
    if (k < s.size() && (s[k] == 'e' || s[k] == 'E')) {
        k++; bool eneg = false; long long ex = 0; int ed = 0;
        if (k < s.size() && (s[k] == '+' || s[k] == '-')) { eneg = s[k] == '-'; k++; }
        for (; k < s.size() && s[k] >= '0' && s[k] <= '9'; ++k) { ex = std::min(ex * 10 + (s[k] - '0'), 100000LL); ed++; }
        if (ed == 0) return std::nullopt;
        long long e = d.e + (eneg ? ex : -ex);
        if (e < 0) { if (d.m != 0) d.m = mul_(d.m, p10(static_cast<int>(-e))); e = 0; }
        if (e > 1000) return std::nullopt;
        d.e = static_cast<int>(e);
    }
    if (k != s.size()) return std::nullopt;
    if (neg) d.m = -d.m;
    return d;
}
inline Dec D(const Val& x, const std::string& what = "a number") {
    std::string t;
    if (x.t == Val::FLOAT) t = repr(x);
    else {
        t = strip(pystr(x));
        std::size_t k = 0; while (k < t.size() && t[k] == '$') k++;
        t = replace_all(t.substr(k), ",", "");
    }
    const auto d = parse_dec(t);
    if (!d) throw PlanError(what + " is not a number: " + repr(x));
    return *d;
}
inline Dec cents(Dec x) { return quant(x, 2, HALF_UP); }
inline Dec cents(const Val& x) { return cents(D(x)); }
inline std::string money(Dec x) { return "$" + commas(to_str(quant(x, 2, HALF_EVEN))); }
inline std::string money(const Val& x) { return money(D(x)); }
inline std::string num(Dec x) { return to_str(normal(x)); }   // 1, 1.5, 2.25
inline std::string ratio(Dec x) { std::string s = fmt_f(to_double(x), 2); if (ends(s, "0")) s.pop_back(); return s; }
inline Val F(Dec x) { return Val::flt(to_double(x)); }

// ---------------------------------------------------------------- time
inline long long utc_now() { return now_us() / US * US; }
inline long long parse_now(const Val& text) {   // --now -> naive UTC; a time without an offset is New York time
    if (!truthy(text)) return utc_now();
    std::string t = strip(pystr(text));
    if (ends(upper(t), " ET")) t = strip(t.substr(0, t.size() - 3));
    static const std::regex HM(R"(^[0-9]{1,2}:[0-9]{2}$)");
    if (std::regex_match(t, HM)) t = day_iso(day_of(to_eastern(utc_now()))) + " " + (t.size() < 5 ? "0" + t : t);
    t = replace_all(t, "Z", "+00:00");
    const auto us = fromiso_us(t);
    if (!us) throw PlanError("--now wants a New York time like \"2026-10-19 10:15\" (or ...Z for UTC)");
    const std::size_t base = t.size() >= 10 && t[4] == '-' ? 10 : 8;
    const bool aware = t.size() > base + 1 && t.find_first_of("+-", base + 1) != std::string::npos;
    return aware ? *us : eastern_to_utc(*us);
}
inline int parse_hhmm(const Val& text, const std::string& what) {
    static const std::regex R(R"(^\s*([0-9]{1,2}):([0-9]{2})\s*$)");
    const std::string s = pystr(text); std::smatch m;
    if (!std::regex_match(s, m, R) || std::stoi(m[1].str()) > 23 || std::stoi(m[2].str()) > 59)
        throw PlanError(what + " wants a New York time like 15:55, not " + repr(text));
    return std::stoi(m[1].str()) * 60 + std::stoi(m[2].str());
}
inline int parse_clock(const Val& text, const std::string& what) {   // 10:15 AM, 10:15, 3:05 PM -> minutes
    static const std::regex R(R"(^\s*([0-9]{1,2}):([0-9]{2})\s*([AaPp][Mm])?\s*$)");
    const std::string s = pystr(text); std::smatch m;
    const std::string bad = what + " wants a time like 10:15 AM, not " + repr(text);
    if (!std::regex_match(s, m, R)) throw PlanError(bad);
    int h = std::stoi(m[1].str()); const int mi = std::stoi(m[2].str());
    if (m[3].matched) {
        if (h < 1 || h > 12) throw PlanError(bad);
        h = h % 12 + (upper(m[3].str()) == "PM" ? 12 : 0);
    }
    if (h > 23 || mi > 59) throw PlanError(bad);
    return h * 60 + mi;
}
inline std::string hm(int m) { char b[16]; std::snprintf(b, sizeof b, "%02d:%02d", m / 60, m % 60); return b; }
inline std::string ampm(int m) { const int h = m / 60; char b[24]; std::snprintf(b, sizeof b, "%d:%02d %s", h % 12 ? h % 12 : 12, m % 60, h < 12 ? "AM" : "PM"); return b; }
inline std::string strf_day(long long eday) {   // %a %Y-%m-%d
    static const char* W[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    return std::string(W[weekday(eday)]) + " " + day_iso(eday);
}
inline long long iso_day(const Val& v) {   // date.fromisoformat(str(v)); nullopt-style failure as PyErr
    const std::string s = pystr(v);
    const auto us = s.size() == 10 ? fromiso_us(s) : std::nullopt;
    if (!us) throw PyErr("ValueError", "Invalid isoformat string: " + str_repr(s));
    return day_of(*us);
}

// ---------------------------------------------------------------- stop / target rules
struct Rule { std::string kind; std::optional<Dec> val; };
inline Rule parse_stop(const std::string& text) {
    const std::string t = lower(strip(text));
    if (t == "or-mid" || t == "or_mid" || t == "mid" || t == "or-midpoint") return {"or_mid", std::nullopt};
    if (t == "or-low" || t == "or_low" || t == "low") return {"or_low", std::nullopt};
    static const std::regex R(R"(^(price|pct|atr):\s*\$?([0-9][0-9,]*\.?[0-9]*)$)");
    std::smatch m;
    if (!std::regex_match(t, m, R)) throw PlanError("--stop is or-mid, or-low, price:X, pct:X or atr:K (got " + str_repr(text) + ")");
    const std::string kind = m[1].str(); const Dec val = D(Val::str(m[2].str()), "--stop");
    if (val <= Dec{}) throw PlanError("--stop " + kind + " needs a number above 0");
    if (kind == "pct" && val >= Dec{100, 0}) throw PlanError("--stop pct:X needs X under 100");
    return {kind, val};
}
inline Rule parse_target(const std::string& text) {
    const std::string t = strip(text);
    static const std::regex R(R"(^([0-9]*\.?[0-9]+)\s*[Rr]$)"), P(R"(^price:\s*\$?([0-9][0-9,]*\.?[0-9]*)$)");
    std::smatch m;
    if (std::regex_match(t, m, R)) {
        const Dec r = D(Val::str(m[1].str()), "--target");
        if (r <= Dec{}) throw PlanError("--target needs an R multiple above 0, like 1R or 2R");
        return {"r", r};
    }
    const std::string low = lower(t);
    if (std::regex_match(low, m, P)) return {"price", D(Val::str(m[1].str()), "--target")};
    throw PlanError("--target is 2R, 1R, 1.5R ... or price:X (got " + str_repr(text) + ")");
}
inline std::string r_label(Dec r) {
    static const char* WORDS[] = {"", "one", "two", "three", "four", "five"};
    const Dec n = normal(r);
    const std::string word = n.e == 0 && n.m >= 1 && n.m <= 5 ? WORDS[n.m] : num(r);
    return num(r) + "R (" + word + " times the risk)";
}
struct Comp { Dec entry, stop, target, target_r, risk, rr; std::string stop_label, stop_detail, target_label; };
// the stop, the target and the risk, in exact cents; PlanError with the reason
inline Comp compute(const Val& entry, const Rule& stop_rule, const Rule& target_rule, const Val& or_high, const Val& or_low,
                    std::optional<double> atr = std::nullopt, int atr_bars = 0) {
    Comp c; const Dec e = cents(entry); c.entry = e;
    if (e <= Dec{}) throw PlanError("the entry price must be above $0");
    std::string detail, label; Dec raw;
    const std::string& kind = stop_rule.kind;
    if (kind == "or_mid" || kind == "or_low") {
        if (or_high.is_none() || or_low.is_none())
            throw PlanError("an " + replace_all(kind, "_", "-") + " stop needs the opening range (--or-high and --or-low)");
        const Dec oh = cents(or_high), ol = cents(or_low);
        if (oh <= ol) throw PlanError("the opening-range high " + money(oh) + " must be above its low " + money(ol));
        if (kind == "or_mid") {
            raw = half(oh + ol); label = "opening-range midpoint";
            detail = "(" + money(oh) + " + " + money(ol) + ") / 2 = $" + num(raw);
        } else {
            raw = ol; label = "opening-range low";
        }
    } else if (kind == "price") {
        raw = *stop_rule.val; label = "your price";
    } else if (kind == "pct") {
        raw = e * (Dec{1, 0} - hundredth(*stop_rule.val));
        label = num(*stop_rule.val) + "% under the entry";
        detail = money(e) + " x (1 - " + num(*stop_rule.val) + "%) = $" + num(raw);
    } else if (kind == "atr") {
        if (!atr) throw PlanError("an atr stop needs the session's bars");
        const Dec a = D(Val::flt(round_n(*atr, 6)));
        raw = e - *stop_rule.val * a;
        label = num(*stop_rule.val) + " x ATR(" + std::to_string(atr_bars) + ") under the entry";
        detail = "ATR $" + num(quant(a, 4, HALF_EVEN)) + " over the " + std::to_string(atr_bars) + " bars to the entry" + (atr_bars < ATR_BARS ? " (fewer than 14 so far)" : "");
    } else {
        throw PlanError("unknown stop rule " + str_repr(kind));
    }
    const Dec stop = quant(raw, 2, FLOOR);
    if (stop != raw) detail = detail.empty() ? "$" + num(raw) + " rounded down" : detail + ", rounded down";
    if (stop >= e) throw PlanError("the stop " + money(stop) + " (" + label + ") is at or above the entry " + money(e) + ": a long's stop goes under the entry");
    if (stop <= Dec{}) throw PlanError("the stop works out to " + money(stop) + ": pick a stop above $0");
    const Dec risk = e - stop, floor_risk = hundredth(e * Dec{5, 2});
    if (risk < floor_risk)
        throw PlanError("the stop is " + money(risk) + " under the entry, less than 0.05% of the price (" + money(quant(floor_risk, 2, HALF_UP)) + " on " + money(e) +
                        "): too tight to survive the spread, normal wiggle would stop you out. Widen it.");
    Dec target; std::string t_label;
    if (target_rule.kind == "r") {
        target = cents(e + *target_rule.val * risk); t_label = r_label(*target_rule.val);
    } else {
        target = cents(*target_rule.val);
    }
    if (target <= e) throw PlanError("the target " + money(target) + " is at or below the entry " + money(e) + ": a long's target goes above it");
    const Dec rr = ratio_q(target - e, risk, 1);
    if (t_label.empty()) t_label = "your price (" + ratio(rr) + "R)";
    c.stop = stop; c.stop_label = label; c.stop_detail = detail; c.target = target; c.target_label = t_label;
    c.target_r = target_rule.kind == "r" ? *target_rule.val : rr; c.risk = risk; c.rr = rr;
    return c;
}

// ---------------------------------------------------------------- bars for ATR (same reader as trader-entry)
struct Bar { long long t; double h, l, c; };
struct Chart { std::vector<Bar> bars; Val data_time; };
inline long long interval_s(const std::string& iv) { return iv == "1m" ? 60 : 300; }
inline Chart load_chart(const std::string& sym, const std::string& interval, std::optional<long long> day, const std::string& offline) {
    const std::string ysym = replace_all(upper(sym), ".", "-");
    std::string text;
    if (!offline.empty()) {
        std::string path = offline;
        if (is_dir(offline)) {
            path = path_join(offline, ysym + ".json");
            if (!exists(path)) path += ".gz";
        }
        if (!exists(path)) throw PlanError("no offline file " + path);
        try { text = dy::read_text_any(path); } catch (const std::exception& e) { throw PlanError("could not read " + path + ": " + e.what()); }
    } else {
        const long long back = day ? day_of(to_eastern(utc_now())) - *day : 0;
        const std::string rng = back <= 0 ? "1d" : back <= 6 ? "5d" : back <= 28 ? "1mo" : "60d";
        if (interval == "1m" && back > 6) throw PlanError("Yahoo keeps 1-minute bars for about a week");
        text = dy::get<PlanError>("/v8/finance/chart/" + ysym + "?range=" + rng + "&interval=" + interval, BACKOFF);
    }
    Val data, res;
    try { data = loads(text); } catch (const std::exception&) { throw PlanError("Yahoo returned something unexpected for " + sym); }
    const Val chart = data.t == Val::DICT ? get(data, "chart") : Val();
    if (data.t != Val::DICT || !has(data, "chart") || chart.t != Val::DICT || !has(chart, "result")) throw PlanError("Yahoo returned something unexpected for " + sym);
    res = get(chart, "result");
    if (!truthy(res)) {
        const Val err = orv(dget(orv(dget(data, "chart"), Val::dict()), "error"), Val::dict());
        throw PlanError("Yahoo has no data for " + sym + ": " + pystr(orv(dget(err, "description"), Val::str("unknown ticker"))));
    }
    const Val r = res.t == Val::LIST && !res.l->empty() ? orv((*res.l)[0], Val::dict()) : Val::dict();
    const Val meta = orv(dget(r, "meta"), Val::dict());
    if (upper(pystr(orv(dget(meta, "symbol"), Val::str(ysym)))) != ysym) throw PlanError("the chart file holds " + pystr(dget(meta, "symbol")) + ", not " + sym);
    const Val quotes = orv(dget(orv(dget(r, "indicators"), Val::dict()), "quote"), Val());
    const Val q = quotes.t == Val::LIST && !quotes.l->empty() ? orv((*quotes.l)[0], Val::dict()) : Val::dict();
    const auto at = [&](const char* key, std::size_t i) -> Val {
        const Val arr = orv(dget(q, key), Val::list());
        return arr.t == Val::LIST && i < arr.l->size() ? (*arr.l)[i] : Val();
    };
    Chart out; out.data_time = dget(meta, "regularMarketTime");
    const Val ts = orv(dget(r, "timestamp"), Val::list());
    if (ts.t == Val::LIST)
        for (std::size_t i = 0; i < ts.l->size(); ++i) {
            const Val t = (*ts.l)[i], c = at("close", i);
            if (t.is_none() || c.is_none()) continue;
            const Val h = at("high", i), lo = at("low", i);
            out.bars.push_back({to_int(t), to_float(h.is_none() ? c : h), to_float(lo.is_none() ? c : lo), to_float(c)});
        }
    return out;
}
// closed, on-grid bars of one New York session, oldest first (the live tick is dropped)
inline std::vector<Bar> session_bars(const Chart& chart, long long day, const std::string& interval, const Val& upto_slot = Val(), std::optional<long long> now_utc = std::nullopt) {
    const long long step = interval_s(interval);
    const auto s = session(day);
    if (!s) return {};
    const long long open_us = day * DAY_US + s->first * MIN_US, close_us = day * DAY_US + s->second * MIN_US;
    if (!now_utc && truthy(chart.data_time)) now_utc = to_int(chart.data_time) * US;
    std::optional<long long> local; if (now_utc) local = to_eastern(*now_utc);
    std::map<long long, Bar> out;
    for (const Bar& b : chart.bars) {
        if (b.t % step) continue;
        const long long et = to_eastern(b.t * US);
        if (day_of(et) != day || !(open_us <= et && et < close_us)) continue;
        if (local && et + step * US > *local) continue;
        const long long slot = (et - open_us) / US / step;
        if (!upto_slot.is_none() && static_cast<double>(slot) > to_float(upto_slot)) continue;
        out[slot] = b;
    }
    std::vector<Bar> v; for (const auto& kv : out) v.push_back(kv.second);
    return v;
}
// average true range of the last n bars (all of them if fewer); the session's first bar uses its own high - low
inline std::pair<double, int> atr_of(const std::vector<Bar>& bars, int n = ATR_BARS) {
    std::vector<double> trs; std::optional<double> prev;
    for (const Bar& b : bars) {
        double tr = b.h - b.l;
        if (prev) tr = std::max({tr, std::fabs(b.h - *prev), std::fabs(b.l - *prev)});
        trs.push_back(tr); prev = b.c;
    }
    const std::size_t k = trs.size() > static_cast<std::size_t>(n) ? trs.size() - static_cast<std::size_t>(n) : 0;
    const std::vector<double> use(trs.begin() + static_cast<std::ptrdiff_t>(k), trs.end());
    return {fsum(use) / static_cast<double>(use.size()), static_cast<int>(use.size())};
}

// ---------------------------------------------------------------- plan
inline Val read_json(const std::string& path, const std::string& what) {
    if (path.empty() || !exists(path)) throw PlanError("no " + what + " at " + path);
    std::string text;
    try { text = read_text(path); } catch (const PyErr& e) { if (e.type == "UnicodeDecodeError") throw PlanError(path + " is not valid JSON: " + e.what()); throw; }
    try { return loads(text); } catch (const PyErr& e) { throw PlanError(path + " is not valid JSON: " + e.what()); }
}
inline Val read_config(const std::string& path) {
    Val cfg = Val::dict(); cfg.d->set("flat_by", Val::str("15:55")); cfg.d->set("min_reward_risk", Val::flt(1.0));
    if (!path.empty() && exists(path)) {
        const Val doc = read_json(path, "config");
        for (const char* k : {"flat_by", "min_reward_risk"}) if (!dget(doc, k).is_none()) cfg.d->set(k, dget(doc, k));
    }
    return cfg;
}
inline const std::string* early_name(long long day) { return da::lookup(da::NYSE_EARLY_CLOSE, day_iso(day)); }
// (HH:MM, note); the config's flat-by moves up with a 1:00 p.m. early close
inline std::pair<std::string, std::string> resolve_flat_by(long long day, const Val& explicit_, const Val& cfg, std::optional<int> entry_time) {
    int t = parse_hhmm(orv(explicit_, orv(dget(cfg, "flat_by"), Val::str("15:55"))), truthy(explicit_) ? "--flat-by" : "flat_by");
    std::string note;
    if (const auto s = session(day)) {
        const int close_m = static_cast<int>(s->second);
        if (t >= close_m) {
            if (truthy(explicit_)) throw PlanError("--flat-by " + hm(t) + " is at or after the " + ampm(close_m) + " close on " + day_iso(day));
            const int moved = t - (CLOSE_M - close_m);
            if (moved <= OPEN_M) throw PlanError("flat_by " + hm(t) + " in the config is at or after the close");
            t = moved;
            const std::string* nm = early_name(day);
            note = "moved up for the 1:00 p.m. early close (" + (nm ? *nm : std::string()) + ")";
        }
    }
    if (entry_time && *entry_time >= t)
        throw PlanError("the entry at " + ampm(*entry_time) + " is at or after your flat-by " + hm(t) + ": there is no time left to hold it");
    return {hm(t), note};
}
struct Info { std::string symbol, source; Val entry, setup, date, entry_time, entry_bar, interval, or_high, or_low, or_minutes; };
inline Info gather(const Val& a) {   // the entry facts, from trader/entry.json or the flags
    if (truthy(get(a, "symbol")) && truthy(get(a, "from_path"))) throw PlanError("use --from or --symbol, not both");
    const std::string side = S(a, "side");
    if (side == "sell" || side == "short") throw PlanError("v2 plans longs only: no stops above a short entry. Plan a buy instead.");
    Info info;
    const Val entry = get(a, "entry");
    if (truthy(get(a, "symbol"))) {
        if (entry.is_none()) throw PlanError("--symbol needs --entry PRICE");
        if (get(a, "or_high").is_none() != get(a, "or_low").is_none()) throw PlanError("give both --or-high and --or-low, or neither");
        info.symbol = upper(S(a, "symbol")); info.entry = entry;
        info.setup = orv(get(a, "setup"), Val::str(get(a, "or_high").is_none() ? "level" : "orb"));
        info.date = get(a, "date"); info.entry_time = get(a, "time"); info.entry_bar = get(a, "entry_bar");
        info.interval = orv(get(a, "interval"), Val::str("5m")); info.or_high = get(a, "or_high"); info.or_low = get(a, "or_low");
        info.or_minutes = get(a, "or_minutes"); info.source = "flags";
    } else {
        const std::string path = truthy(get(a, "from_path")) ? S(a, "from_path") : "trader/entry.json";
        const Val doc = read_json(path, "entry (run /trader-entry first, or give --symbol and --entry)");
        const std::string dside = lower(pystr(orv(dget(doc, "side"), Val::str("buy"))));
        if (dside != "buy" && dside != "long") throw PlanError(path + " is a " + pystr(dget(doc, "side")) + ": v2 plans longs only");
        const Val status = dget(doc, "status");
        if (!(status.t == Val::STR && status.s == "triggered") && entry.is_none())
            throw PlanError(path + " says " + pystr(dget(doc, "symbol")) + " is " + pystr(orv(status, Val::str("unknown"))) +
                            ", not triggered: plan once it triggers, or give --entry PRICE to plan ahead");
        const Val orr = orv(dget(doc, "opening_range"), Val::dict());
        info.symbol = upper(pystr(orv(dget(doc, "symbol"), Val::str(""))));
        info.entry = entry.is_none() ? dget(doc, "price") : entry;
        info.setup = orv(get(a, "setup"), orv(dget(doc, "setup"), Val::str("orb")));
        info.date = orv(get(a, "date"), dget(doc, "date"));
        info.entry_time = orv(get(a, "time"), entry.is_none() ? dget(doc, "time") : Val());
        info.entry_bar = !get(a, "entry_bar").is_none() ? get(a, "entry_bar") : entry.is_none() ? dget(doc, "bar_index") : Val();
        info.interval = orv(get(a, "interval"), orv(dget(doc, "interval"), Val::str("5m")));
        info.or_high = !get(a, "or_high").is_none() ? get(a, "or_high") : dget(orr, "high");
        info.or_low = !get(a, "or_low").is_none() ? get(a, "or_low") : dget(orr, "low");
        info.or_minutes = orv(dget(orr, "minutes"), get(a, "or_minutes")); info.source = path;
        if (info.symbol.empty() || info.entry.is_none()) throw PlanError(path + " has no symbol or price");
    }
    if (!(info.interval.t == Val::STR && (info.interval.s == "5m" || info.interval.s == "1m"))) throw PlanError("interval is 5m or 1m");
    return info;
}
inline std::pair<Val, std::vector<std::string>> build_plan(const Val& a, long long now_utc) {
    const Info info = gather(a);
    const Val cfg = read_config(S(a, "config"));
    std::string stop_text = S(a, "stop");
    const bool orb = info.setup.t == Val::STR && info.setup.s == "orb";
    if (stop_text.empty()) {
        if (!orb) throw PlanError("a " + pystr(info.setup) + " setup has no opening-range stop to default to: give --stop price:X, pct:X or atr:K");
        stop_text = "or-mid";
    }
    const Rule stop_rule = parse_stop(stop_text);
    const Rule target_rule = parse_target(S(a, "target").empty() ? "2R" : S(a, "target"));
    long long day;
    if (truthy(info.date)) {
        try { day = iso_day(info.date); } catch (const PyErr&) { throw PlanError("date wants YYYY-MM-DD, not " + repr(info.date)); }
    } else {
        day = day_of(to_eastern(now_utc));
    }
    std::optional<int> entry_time; if (truthy(info.entry_time)) entry_time = parse_clock(info.entry_time, "the entry time");
    const std::string interval = info.interval.s;
    std::optional<double> atr; int atr_n = 0;
    if (stop_rule.kind == "atr") {
        const Chart chart = load_chart(info.symbol, interval, day, S(a, "offline"));
        std::optional<long long> now; if (truthy(get(a, "now"))) now = parse_now(get(a, "now"));
        const auto bars = session_bars(chart, day, interval, info.entry_bar, now);
        if (bars.size() < 3)
            throw PlanError("an atr stop needs at least 3 closed " + interval + " bars of " + info.symbol + " on " + day_iso(day) + " by the entry; found " + std::to_string(bars.size()));
        std::tie(atr, atr_n) = atr_of(bars);
    }
    const Comp c = compute(info.entry, stop_rule, target_rule, info.or_high, info.or_low, atr, atr_n);
    const auto [flat_by, flat_note] = resolve_flat_by(day, get(a, "flat_by"), cfg, entry_time);
    Val orr;
    if (!info.or_high.is_none() && !info.or_low.is_none()) {
        orr = Val::dict(); orr.d->set("high", F(cents(info.or_high))); orr.d->set("low", F(cents(info.or_low)));
        orr.d->set("minutes", Val::integer(truthy(info.or_minutes) ? to_int(info.or_minutes) : 15));
    }
    Val plan = Val::dict(); auto& p = *plan.d;
    p.set("created_by", Val::str("trader-exit")); p.set("symbol", Val::str(info.symbol)); p.set("side", Val::str("buy"));
    p.set("date", Val::str(day_iso(day))); p.set("setup", info.setup); p.set("entry", F(c.entry));
    p.set("entry_time", entry_time ? Val::str(ampm(*entry_time)) : Val()); p.set("entry_bar", info.entry_bar);
    p.set("interval", info.interval); p.set("opening_range", orr); p.set("stop", F(c.stop)); p.set("stop_rule", Val::str(stop_rule.kind));
    p.set("stop_label", Val::str(c.stop_label)); p.set("target", F(c.target)); p.set("target_r", F(c.target_r));
    p.set("target_label", Val::str(c.target_label)); p.set("risk_per_share", F(c.risk)); p.set("reward_risk", F(c.rr));
    p.set("flat_by", Val::str(flat_by)); p.set("stop_detail", Val::str(c.stop_detail));
    p.set("created_at", Val::str(da::strf(now_utc, true))); p.set("note", Val::str(NOTE));
    if (atr) { p.set("atr", Val::flt(round_n(*atr, 4))); p.set("atr_bars", Val::integer(atr_n)); }
    std::vector<std::string> warnings;
    if (!flat_note.empty()) warnings.push_back("Flat-by " + flat_by + ": " + flat_note + ".");
    const Val mrr = dget(cfg, "min_reward_risk");
    try {
        if (!mrr.is_none() && c.rr < D(mrr))
            warnings.push_back("Reward:risk " + ratio(c.rr) + " is under your min_reward_risk " + num(D(mrr)) + " in the config: trader-risk checks that.");
    } catch (const PlanError&) {}
    const long long today = day_of(to_eastern(now_utc));
    if (day != today) warnings.push_back("This plan is for " + day_iso(day) + ", not today (" + day_iso(today) + ").");
    return {plan, warnings};
}
inline std::vector<std::string> describe(const Val& plan) {
    const long long day = iso_day(get(plan, "date"));
    const Val su = get(plan, "setup");
    const std::string setup = su.t == Val::STR && su.s == "orb" ? "opening-range breakout" : su.t == Val::STR && su.s == "level" ? "close above a level" : pystr(su);
    const Dec pct = ratio_q(D(get(plan, "risk_per_share")), D(get(plan, "entry")), 100);
    std::string when;
    if (truthy(dget(plan, "entry_time"))) {
        when = " at " + pystr(get(plan, "entry_time"));
        if (!dget(plan, "entry_bar").is_none()) when += " (bar " + std::to_string(to_int(get(plan, "entry_bar"))) + ")";
    }
    const Val iv = get(plan, "interval");
    std::vector<std::string> out;
    out.push_back("Exit plan: " + pystr(get(plan, "symbol")) + " long, " + strf_day(day) + ", " + setup + " on " + (iv.t == Val::STR && iv.s == "5m" ? "5-minute" : "1-minute") + " bars");
    out.push_back("  Entry         " + money(get(plan, "entry")) + when);
    out.push_back("  Stop          " + money(get(plan, "stop")) + "  " + pystr(get(plan, "stop_label")) + (truthy(dget(plan, "stop_detail")) ? ": " + pystr(get(plan, "stop_detail")) : ""));
    out.push_back("  Target        " + money(get(plan, "target")) + "  " + pystr(get(plan, "target_label")));
    out.push_back("  Risk a share  " + money(get(plan, "risk_per_share")) + " (" + to_str(pct) + "% of the entry)");
    out.push_back("  Reward:risk   " + ratio(D(get(plan, "reward_risk"))));
    out.push_back("  Flat by       " + pystr(get(plan, "flat_by")) + " ET, win or lose: no overnight hold");
    out.push_back("Set before the entry. If price hits the stop, you are out; never move it further away.");
    out.push_back(NOTE);
    return out;
}
inline int cmd_plan(const Val& a) {
    const long long now = parse_now(get(a, "now"));
    const auto [plan, warnings] = build_plan(a, now);
    const std::string write = S(a, "write");
    DumpOpts o; o.indent = 2;
    if (!write.empty()) {
        const std::string d = dirname(write); if (!d.empty()) makedirs(d);
        write_text(write, dumps(plan, o) + "\n");
    }
    if (B(a, "json")) {
        Val doc = Val::dict(); for (const auto& kv : plan.d->items) doc.d->set(kv.first, kv.second);
        Val w = Val::list(); for (const auto& s : warnings) w.l->push_back(Val::str(s));
        doc.d->set("warnings", w); print(dumps(doc, o));
        return 0;
    }
    for (const auto& line : describe(plan)) print(line);
    for (const auto& w : warnings) print("Note: " + w);
    if (!write.empty()) print("Wrote " + write + ".");
    print("Next: /trader-timemachine replays it, then /trader-budget sizes it to your max loss. Nothing was placed.");
    return 0;
}
inline int cmd_show(const Val& a) {
    const std::string path = S(a, "plan");
    if (!exists(path)) { print("No plan at " + path + " yet. Make one: exit plan --write"); return 1; }
    const Val plan = read_json(path, "plan");
    for (const char* k : {"symbol", "date", "entry", "stop", "target", "risk_per_share", "reward_risk", "flat_by"})
        if (dget(plan, k).is_none()) throw PlanError(path + " is missing " + str_repr(k) + ": make a new plan with exit plan --write");
    if (B(a, "json")) { DumpOpts o; o.indent = 2; print(dumps(plan, o)); return 0; }
    Val merged = Val::dict();
    merged.d->set("setup", Val::str("orb")); merged.d->set("interval", Val::str("5m")); merged.d->set("stop_label", Val::str("")); merged.d->set("target_label", Val::str(""));
    for (const auto& kv : plan.d->items) merged.d->set(kv.first, kv.second);
    for (const auto& line : describe(merged)) print(line);
    const std::string today = day_iso(day_of(to_eastern(parse_now(get(a, "now")))));
    const Val pd = get(plan, "date");
    if (!(pd.t == Val::STR && pd.s == today)) print("Note: this plan is for " + pystr(pd) + ", not today (" + today + ").");
    return 0;
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("exit", "Exit Planner (trader-exit): the stop and target before the entry.");
    p.subcommands("cmd", false);
    {
        Parser& s = p.sub("plan"); s.help_line = "set the stop and target";
        s.opt({"--from"}).dest("from_path").metavar("PATH").help("entry.json to plan from (default trader/entry.json)");
        s.opt({"--symbol"}); s.opt({"--entry"}).number().help("entry price (or a plan-ahead price with --from)");
        s.opt({"--or-high"}).number(); s.opt({"--or-low"}).number(); s.opt({"--or-minutes"}).integer().def(15);
        s.opt({"--setup"}).choices({"orb", "level"}); s.opt({"--date"}).help("session date YYYY-MM-DD (default: the entry's, or today)");
        s.opt({"--time"}).help("entry time, e.g. 10:15 AM"); s.opt({"--entry-bar"}).integer(); s.opt({"--interval"}).choices({"5m", "1m"});
        s.opt({"--side"}).def("buy").choices({"buy", "long", "sell", "short"}).help("v2: buy only");
        s.opt({"--stop"}).help("or-mid (orb default), or-low, price:X, pct:X, atr:K"); s.opt({"--target"}).help("2R (default), 1R, 1.5R, price:X");
        s.opt({"--flat-by"}).help("HH:MM New York (default: config flat_by, 15:55)"); s.opt({"--config"}).def("trader/config.json");
        s.opt({"--offline"}).metavar("PATH").help("saved Yahoo chart JSON for an atr stop");
        s.opt({"--now"}).help("pretend it is this New York time, e.g. \"2026-10-19 10:15\"");
        s.opt({"--write"}).nargs("?").cnst(Val::str("trader/plan.json")).metavar("PATH").help("save the plan (default trader/plan.json)");
        s.opt({"--json"}).store_true();
    }
    {
        Parser& s = p.sub("show"); s.help_line = "print the current plan";
        s.opt({"--plan"}).def("trader/plan.json"); s.opt({"--now"}); s.opt({"--json"}).store_true();
    }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { out(ap::help_text(ap::norm(p, p.prog))); return 1; }
    try {
        return cmd == "plan" ? cmd_plan(a) : cmd_show(a);
    } catch (const PlanError& e) { eprint(std::string("exit: ") + e.what()); return 1; }
}
PACKTOOL("daytrade-exit/exit", main);
}  // namespace packtools::daytrade_exit
