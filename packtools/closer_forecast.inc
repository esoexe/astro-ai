// forecaster/forecast: what you'll close this month (commit, likely, best case), your real win rate, and the deals
// that make the forecast wrong. A C++ port of closer-skill/forecaster/forecast.py (MIT, Jake Schincariol).
namespace packtools::closer_forecast {
using namespace pyrt;
using namespace packtools::closer;

inline const std::vector<std::pair<std::string, double>> STAGE_CHANCE = {{"lead", 0.05}, {"contacted", 0.10}, {"replied", 0.20}, {"call-booked", 0.30},
    {"proposal", 0.50}, {"contract-sent", 0.80}, {"won", 1.0}, {"lost", 0.0}};
inline constexpr double BASE_WIN = 0.25;   // the win rate the stage chances above assume for a deal that reached a call
inline constexpr int MIN_HISTORY = 10;
inline constexpr int QUIET_DAYS = 14;

struct Rates { std::size_t won = 0, lost = 0; std::optional<double> win_rate; std::optional<long long> avg_days; bool enough = false; };
// Won and lost deals that count toward a win rate (an unsubscribe from cold email is not a lost sale).
inline Rates rates(const Val& data) {
    Rates r; std::vector<long long> days;
    for (const Val& d : lstk(data, "deals")) {
        if (is(at(d, "stage"), "won")) {
            r.won++;
            if (truthy(g(d, "created")) && truthy(g(d, "close_date"))) days.push_back(date_fromiso(at(d, "close_date")) - date_fromiso(at(d, "created")));
        } else if (is(at(d, "stage"), "lost") && str_of(orv(g(d, "lost_reason"), sv(""))).find("unsubscribed") == std::string::npos) r.lost++;
    }
    const std::size_t n = r.won + r.lost;
    if (n) r.win_rate = static_cast<double>(r.won) / static_cast<double>(n);
    if (!days.empty()) { long long s = 0; for (long long x : days) s += x; r.avg_days = round_int(static_cast<double>(s) / static_cast<double>(days.size())); }
    r.enough = n >= static_cast<std::size_t>(MIN_HISTORY);
    return r;
}
inline std::pair<std::map<std::string, double>, std::string> chances(const Val& data) {
    const Rates r = rates(data);
    std::map<std::string, double> out;
    if (!r.enough) { for (const auto& [k, v] : STAGE_CHANCE) out[k] = v; return {out, "standard stage chances (fewer than " + std::to_string(MIN_HISTORY) + " closed deals of your own)"}; }
    const double scale = *r.win_rate / BASE_WIN;
    for (const auto& [k, v] : STAGE_CHANCE) out[k] = (0 < v && v < 1) ? std::min(0.95, v * scale) : v;
    return {out, "stage chances scaled to your win rate of " + std::to_string(round_int(100 * *r.win_rate)) + "% over " + std::to_string(r.won + r.lost) + " closed deals"};
}
inline long long int_of(const std::string& s) {   // int(s)
    const auto v = py_int(s); if (!v) throw PyErr("ValueError", "invalid literal for int() with base 10: " + str_repr(s)); return *v;
}
inline std::pair<long long, long long> month_bounds(const std::string& month) {
    const auto parts = split(month, "-");
    if (parts.size() > 2) throw PyErr("ValueError", "too many values to unpack (expected 2)");
    if (parts.size() < 2) throw PyErr("ValueError", "not enough values to unpack (expected 2, got " + std::to_string(parts.size()) + ")");
    const long long y = int_of(parts[0]), m = int_of(parts[1]);
    const long long start = py_date(y, m, 1);
    const long long end = py_date(y + (m == 12), m % 12 + 1, 1) - 1;
    return {start, end};
}

struct Row { Val deal_id, title, stage, close; double value, chance, weighted; };
struct Forecast { std::string month; double won = 0, commit = 0, best = 0; long long likely = 0; std::vector<Row> rows; std::string basis; };
inline Forecast forecast(const Val& data, const std::string& month, long long) {
    const auto [start, end] = month_bounds(month);
    const auto [ch, basis] = chances(data);
    Forecast f; f.month = month; f.basis = basis; double likely = 0;
    for (const Val& d : lstk(data, "deals"))
        if (is(at(d, "stage"), "won") && truthy(g(d, "close_date"))) { const long long c = date_fromiso(at(d, "close_date")); if (start <= c && c <= end) f.won += value_of(d); }
    for (const Val& d : lstk(data, "deals")) {
        if (!in(at(d, "stage"), OPEN) || !truthy(g(d, "close_date"))) continue;
        const long long close = date_fromiso(at(d, "close_date"));
        if (!(start <= close && close <= end)) continue;
        const double v = value_of(d), c = ch.at(at(d, "stage").s), w = v * c;
        f.rows.push_back({at(d, "id"), at(d, "title"), at(d, "stage"), at(d, "close_date"), v, c, w});
        likely += w; f.best += v;
        if (is(at(d, "stage"), "contract-sent")) f.commit += v;
    }
    std::stable_sort(f.rows.begin(), f.rows.end(), [](const Row& a, const Row& b) { return -a.weighted < -b.weighted; });
    f.likely = round_int(likely);
    return f;
}
inline std::size_t stage_index(const std::string& s) { for (std::size_t k = 0; k < STAGES.size(); ++k) if (STAGES[k] == s) return k; throw PyErr("ValueError", str_repr(s) + " is not in tuple"); }
inline std::vector<std::pair<std::string, std::string>> health(const Val& data, long long today) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const Val& d : lstk(data, "deals")) {
        if (!in(at(d, "stage"), OPEN)) continue;
        const std::string id = ps(at(d, "id"));
        if (!truthy(g(d, "close_date"))) {
            if (stage_index(at(d, "stage").s) >= stage_index("call-booked")) out.emplace_back(id, "no close date, so it is in no month's forecast");
        } else if (date_fromiso(at(d, "close_date")) < today) out.emplace_back(id, "close date " + ps(at(d, "close_date")) + " has passed: move it or close the deal");
        const long long quiet = today - date_fromiso(last_touch(data, d));
        if (quiet >= QUIET_DAYS && !is(at(d, "stage"), "lead")) out.emplace_back(id, "quiet for " + std::to_string(quiet) + " days");
    }
    return out;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("forecast.py", "What you'll close this month.");
    p.opt({"--file"}).def(DEFAULT_FILE);
    p.subcommands("cmd", true);
    { Parser& s = p.sub("month"); s.opt({"--month"}).help("YYYY-MM (default: this month)"); s.opt({"--today"}); }
    p.sub("rates");
    { Parser& s = p.sub("health"); s.opt({"--today"}); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    try {
        const Val data = load(S(a, "file"));
        if (cmd == "month") {
            const long long today = today_arg(get(a, "today"));
            const Forecast f = forecast(data, truthy(get(a, "month")) ? S(a, "month") : iso(today).substr(0, 7), today);
            print(f.month + " forecast");
            print("  Won so far:  " + money(f.won));
            print("  Commit:      " + money(f.commit) + "  (contracts out)");
            print("  Likely:      " + money(static_cast<double>(f.likely)) + "  (weighted)");
            print("  Best case:   " + money(f.best) + "  (everything closing this month)");
            for (const Row& r : f.rows)
                print("    " + ljust(head(r.title, 30), 30) + " " + ljust(ps(r.stage), 13) + " " + rjust(money(r.value), 9) + " x " + rjust(std::to_string(round_int(100 * r.chance)), 3) + "% = " +
                      rjust(money(r.weighted), 9) + "  close " + ps(r.close));
            print("  Based on: " + f.basis + ".");
        } else if (cmd == "rates") {
            const Rates r = rates(data);
            if (!r.win_rate) print("No closed deals yet.");
            else print("Won " + std::to_string(r.won) + ", lost " + std::to_string(r.lost) + ": " + std::to_string(round_int(100 * *r.win_rate)) + "% win rate." + (r.enough ? "" : " Too few to trust yet."));
            if (r.avg_days) print("A won deal takes " + std::to_string(*r.avg_days) + " days on average.");
        } else {
            const auto items = health(data, today_arg(get(a, "today")));
            for (const auto& [did, why] : items) print(ljust(did, 24) + " " + why);
            if (items.empty()) print("Every open deal has a close date in the future and recent activity.");
        }
    } catch (const PipelineError& e) { eprint(e.what()); return 1; }
    catch (const PyErr& e) { if (e.type != "ValueError") throw; eprint(e.what()); return 1; }
    return 0;
}
PACKTOOL("forecaster/forecast", main);
}  // namespace packtools::closer_forecast
