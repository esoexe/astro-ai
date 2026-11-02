// daytrade-timemachine/timemachine - the Time Machine: replays a planned day trade before a dollar is at risk. 10,000
// Monte Carlo replays of the rest of the session, each 5-minute bar drawn whole from the stock's own past bars at the same
// time of day (+- window bars), plus the plan's own rule re-run on every past full session. C++ port of
// day-trader-skill/trader-timemachine/timemachine.py (Jake Schincariol, MIT): same rules, files, output and exit codes, and
// the same random draws - PyRandom below is CPython's MT19937 (init_by_array seeding, getrandbits-based randbelow), so the
// same plan, data and --seed give the reel's numbers bit for bit. A simulation is not a prediction.
namespace packtools::daytrade_timemachine {
using namespace pyrt;
struct TMError : std::runtime_error { using std::runtime_error::runtime_error; };
namespace da = packtools::daytrade_alerts;
namespace dy = packtools::daytrade_yahoo;
static const double PAUSE = 0.5, BACKOFF = 5.0;
static const long long INTERVAL = 300, US = 1000000, DAY_US = 86400 * US;
static const int SESSION_BARS = 78, OPEN_MIN = 9 * 60 + 30, CLOSE_MIN = 16 * 60, EARLY_MIN = 13 * 60, MIN_SESSIONS = 20;
static const int SAMPLE_PATHS = 160, BAND_PATHS = 2000, HIST_BINS = 24;
static const double BAND_Q[5] = {0.05, 0.25, 0.5, 0.75, 0.95};
static const double COIN_FLIP = 0.05, STRONG = 0.15, MANY_NEITHER = 0.40;
static const char* const WEEKDAYS[7] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
static const std::map<std::string, std::string> NYSE_EARLY_CLOSE = {   // 1:00 p.m. ET (the tool's own table)
    {"2026-11-27", "day after Thanksgiving"}, {"2026-12-24", "Christmas Eve"}, {"2027-11-26", "day after Thanksgiving"}};

// ---------------------------------------------------------------- CPython's random.Random
struct PyRandom {
    static constexpr int N = 624, M = 397;   /* constexpr = inline: an unoptimised build odr-uses N */
    uint32_t mt[N]; int mti = N + 1;
    void init_genrand(uint32_t s) {
        mt[0] = s;
        for (mti = 1; mti < N; mti++) mt[mti] = 1812433253U * (mt[mti - 1] ^ (mt[mti - 1] >> 30)) + static_cast<uint32_t>(mti);
    }
    explicit PyRandom(long long seed) {   // random.Random(int): abs(seed) as 32-bit words, little end first
        unsigned long long n = seed < 0 ? 0ULL - static_cast<unsigned long long>(seed) : static_cast<unsigned long long>(seed);
        std::vector<uint32_t> key;
        while (n) { key.push_back(static_cast<uint32_t>(n & 0xffffffffULL)); n >>= 32; }
        if (key.empty()) key.push_back(0);
        init_genrand(19650218U);
        int i = 1; std::size_t j = 0;
        for (int k = std::max<int>(N, static_cast<int>(key.size())); k; k--) {
            mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1664525U)) + key[j] + static_cast<uint32_t>(j);
            i++; j++;
            if (i >= N) { mt[0] = mt[N - 1]; i = 1; }
            if (j >= key.size()) j = 0;
        }
        for (int k = N - 1; k; k--) {
            mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1566083941U)) - static_cast<uint32_t>(i);
            i++;
            if (i >= N) { mt[0] = mt[N - 1]; i = 1; }
        }
        mt[0] = 0x80000000U;
        mti = N;
    }
    uint32_t genrand() {
        static const uint32_t mag01[2] = {0x0U, 0x9908b0dfU};
        uint32_t y;
        if (mti >= N) {
            int kk = 0;
            for (; kk < N - M; kk++) { y = (mt[kk] & 0x80000000U) | (mt[kk + 1] & 0x7fffffffU); mt[kk] = mt[kk + M] ^ (y >> 1) ^ mag01[y & 0x1U]; }
            for (; kk < N - 1; kk++) { y = (mt[kk] & 0x80000000U) | (mt[kk + 1] & 0x7fffffffU); mt[kk] = mt[kk + (M - N)] ^ (y >> 1) ^ mag01[y & 0x1U]; }
            y = (mt[N - 1] & 0x80000000U) | (mt[0] & 0x7fffffffU);
            mt[N - 1] = mt[M - 1] ^ (y >> 1) ^ mag01[y & 0x1U];
            mti = 0;
        }
        y = mt[mti++];
        y ^= (y >> 11); y ^= (y << 7) & 0x9d2c5680U; y ^= (y << 15) & 0xefc60000U; y ^= (y >> 18);
        return y;
    }
    uint32_t getrandbits(int k) { return genrand() >> (32 - k); }   // 1 <= k <= 32
    long long randbelow(long long n) {                                // _randbelow_with_getrandbits, n < 2**32
        int k = 0; while ((n >> k) != 0) k++;
        long long r = getrandbits(k);
        while (r >= n) r = getrandbits(k);
        return r;
    }
    long long randint(long long a, long long b) { return a + randbelow(b - a + 1); }
};

// ---------------------------------------------------------------- time
inline long long parse_now(const Val& text) { try { return da::parse_now(text); } catch (const da::AlertError& e) { throw TMError(e.what()); } }
inline long long pymod(long long a, long long b) { const long long r = a % b; return (r != 0 && ((r < 0) != (b < 0))) ? r + b : r; }
inline long long pyfloordiv(long long a, long long b) { return (a - pymod(a, b)) / b; }
// '10:15 AM', '10:15am', '15:55' or '3:55' (a bare 1 to 8 o'clock means p.m.) -> minutes after midnight
inline int parse_clock(const Val& text, const std::string& what) {
    static const std::regex R(R"(^\s*([0-9]{1,2}):([0-9]{2})\s*(?:([AaPp])\.?\s*[Mm]\.?)?\s*$)");
    const std::string s = truthy(text) ? pystr(text) : std::string();
    std::smatch m;
    if (!std::regex_match(s, m, R)) throw TMError(what + " wants a New York time like 10:15 AM or 15:55, not " + repr(text));
    int h = std::stoi(m[1].str()); const int mi = std::stoi(m[2].str());
    const std::string ap = lower(m[3].str());
    if (mi > 59 || h > 23 || (!ap.empty() && !(1 <= h && h <= 12))) throw TMError(what + " " + repr(text) + " is not a clock time");
    if (ap == "p" && h != 12) h += 12;
    else if (ap == "a" && h == 12) h = 0;
    else if (ap.empty() && 1 <= h && h <= 8) h += 12;
    return h * 60 + mi;
}
inline std::string clock12(long long minutes) {
    const long long h = pyfloordiv(minutes, 60), m = pymod(minutes, 60);
    char b[32]; std::snprintf(b, sizeof b, "%lld:%02lld %s", pymod(h, 12) ? pymod(h, 12) : 12, m, h < 12 ? "AM" : "PM"); return b;
}
inline std::string clock24(long long minutes) { char b[32]; std::snprintf(b, sizeof b, "%02lld:%02lld", pyfloordiv(minutes, 60), pymod(minutes, 60)); return b; }
inline std::string utc_z(long long t) { long long y, us; unsigned mo, d; int h, mi, s; split_us(t, y, mo, d, h, mi, s, us); char b[40]; std::snprintf(b, sizeof b, "%04lld-%02u-%02uT%02d:%02d:%02dZ", y, mo, d, h, mi, s); return b; }
inline long long days_from_civil(long long y, unsigned m, unsigned d) {
    y -= m <= 2; const long long era = (y >= 0 ? y : y - 399) / 400; const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}
inline bool leap(long long y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
inline int weekday_of(long long days) { return static_cast<int>(pymod(days + 3, 7)); }   // 1970-01-01 was a Thursday; Monday = 0
// datetime.date.fromisoformat (3.11+): YYYY-MM-DD, YYYYMMDD and the ISO week forms YYYY-Www[-D], YYYYWww[D]
inline std::optional<long long> date_fromiso(const std::string& s) {
    static const std::regex CAL(R"(^([0-9]{4})-?([0-9]{2})-?([0-9]{2})$)"), WEEK(R"(^([0-9]{4})-?W([0-9]{2})(?:-?([0-9]))?$)");
    std::smatch m;
    if (std::regex_match(s, m, CAL)) {
        if ((s.size() == 10) != (s.find('-') != std::string::npos)) return std::nullopt;   // both dashes or none
        if (s.size() == 10 && (s[4] != '-' || s[7] != '-')) return std::nullopt;
        const long long y = std::stoll(m[1].str()); const unsigned mo = static_cast<unsigned>(std::stoi(m[2].str())), d = static_cast<unsigned>(std::stoi(m[3].str()));
        static const unsigned DIM[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (y < 1 || mo < 1 || mo > 12 || d < 1 || d > DIM[mo - 1] + (mo == 2 && leap(y))) return std::nullopt;
        return days_from_civil(y, mo, d);
    }
    if (std::regex_match(s, m, WEEK)) {
        const bool dashed = s.size() > 4 && s[4] == '-';
        if (m[3].matched && dashed != (s.size() >= 10 && s[8] == '-')) return std::nullopt;
        const long long y = std::stoll(m[1].str()); const int w = std::stoi(m[2].str()), d = m[3].matched ? std::stoi(m[3].str()) : 1;
        if (y < 1) return std::nullopt;
        const long long jan4 = days_from_civil(y, 1, 4), wk1 = jan4 - weekday_of(jan4);
        const int jan1 = weekday_of(days_from_civil(y, 1, 1));
        const int weeks = (jan1 == 3 || (jan1 == 2 && leap(y))) ? 53 : 52;
        if (w < 1 || w > weeks || d < 1 || d > 7) return std::nullopt;
        return wk1 + (w - 1) * 7 + (d - 1);
    }
    return std::nullopt;
}

// ---------------------------------------------------------------- data
inline std::string chart_url(const std::string& sym, const std::string& host = "query2") {
    return "https://" + host + ".finance.yahoo.com/v8/finance/chart/" + replace_all(upper(sym), ".", "-") + "?range=60d&interval=5m";
}
// one request for 60 days of 5-min bars: query2 first, one retry after a 429, then query1 on 429 or 5xx
inline std::string fetch_chart(const std::string& sym) {
    long long last = -1;
    const char* hosts[2] = {"query2", "query1"};
    for (int n = 0; n < 2; ++n) {
        if (n) dy::pause(PAUSE);
        for (int attempt = 0; attempt < 2; ++attempt) {
            const HttpResp r = http_get(chart_url(sym, hosts[n]), {std::string("User-Agent: ") + dy::UA, "Accept: application/json"}, 20);
            if (r.status == 0 || (r.status < 400 && !r.error.empty())) throw TMError("could not reach Yahoo: " + (r.error.empty() ? std::string("no response") : r.error));
            if (r.status < 400) return decode_utf8_replace(r.body);
            last = r.status;
            if (r.status == 429 && attempt == 0) { dy::pause(BACKOFF); continue; }
            if (r.status == 429 || r.status >= 500) break;
            if (r.status == 404) throw TMError("Yahoo has no chart for " + upper(sym) + " (HTTP 404): check the ticker");
            throw TMError("Yahoo answered HTTP " + std::to_string(r.status) + " for " + upper(sym));
        }
    }
    if (last == 429) throw TMError("Yahoo is rate limiting (HTTP 429). Wait a minute and run it again.");
    throw TMError("Yahoo answered HTTP " + (last < 0 ? std::string("None") : std::to_string(last)) + " for " + upper(sym) + " on both hosts. Try again later.");
}
// a saved Yahoo chart file (.json or .json.gz), or a folder holding <SYM>-60d5m.json(.gz)
inline std::string read_offline(std::string path, const std::string& sym) {
    if (is_dir(path)) {
        bool found = false;
        for (const std::string& name : {upper(sym) + "-60d5m.json", upper(sym) + "-60d5m.json.gz"})
            if (exists(path_join(path, name))) { path = path_join(path, name); found = true; break; }
        if (!found) throw TMError("no " + upper(sym) + "-60d5m.json(.gz) in " + path);
    }
    if (!exists(path)) throw TMError("no offline chart file " + path);
    try { return dy::read_text_any(path); } catch (const std::exception& e) { throw TMError("could not read " + path + ": " + e.what()); }
}
inline Val chart_result(const std::string& text, const std::string& sym) {
    Val data;
    try { data = loads(text); } catch (const PyErr&) { throw TMError("Yahoo returned something that is not JSON for " + upper(sym)); }
    const Val chart = data.t == Val::DICT ? dget(data, "chart") : Val();
    if (chart.t != Val::DICT) throw TMError("Yahoo returned something unexpected for " + upper(sym));
    const Val res = dget(chart, "result");
    if (!truthy(res)) {
        const Val err = orv(dget(chart, "error"), Val::dict());
        throw TMError("Yahoo has no data for " + upper(sym) + ": " + pystr(orv(dget(err, "description"), Val::str("unknown ticker"))));
    }
    return res.t == Val::LIST ? (*res.l)[0] : Val::dict();
}
struct TBar { std::string date; long long slot = 0; std::string start; double o = 0, h = 0, l = 0, c = 0, v = 0; };
struct Dropped { long long no_price = 0, off_grid = 0, not_complete = 0, outside_hours = 0; };
// Yahoo chart result -> (meta, bars, dropped): regular-session 5-min bars only, oldest first; dropped = a missing price, off
// the 5-minute grid (the live tick), not complete yet (now = the earlier of --now and the data's regularMarketTime), and
// outside 9:30 to 16:00 New York time
inline std::tuple<Val, std::vector<TBar>, Dropped> parse_bars(const Val& result, long long now_utc) {
    const Val meta = orv(dget(result, "meta"), Val::dict());
    const Val stamps = orv(dget(result, "timestamp"), Val::list());
    const Val quotes = dget(orv(dget(result, "indicators"), Val::dict()), "quote");
    const Val q = truthy(quotes) && quotes.t == Val::LIST ? orv((*quotes.l)[0], Val::dict()) : Val::dict();
    std::vector<Val> cols;
    for (const char* k : {"open", "high", "low", "close", "volume"}) cols.push_back(orv(dget(q, k), Val::list()));
    long long as_of = now_utc;
    const Val rmt = dget(meta, "regularMarketTime");
    if ((rmt.t == Val::INT || rmt.t == Val::FLOAT || rmt.t == Val::BOOL) && to_int(rmt) * US < as_of) as_of = to_int(rmt) * US;
    Dropped dropped; std::map<long long, TBar> bars;
    const auto at = [](const Val& col, std::size_t i) { return col.t == Val::LIST && i < col.l->size() ? (*col.l)[i] : Val(); };
    if (stamps.t == Val::LIST)
        for (std::size_t i = 0; i < stamps.l->size(); ++i) {
            const Val ts0 = (*stamps.l)[i];
            const Val o = at(cols[0], i), h = at(cols[1], i), l = at(cols[2], i), c = at(cols[3], i), v = at(cols[4], i);
            if (ts0.is_none() || o.is_none() || h.is_none() || l.is_none() || c.is_none()) { dropped.no_price++; continue; }
            const long long ts = to_int(ts0);
            if (pymod(ts, INTERVAL)) { dropped.off_grid++; continue; }
            const long long start = ts * US, local = da::to_eastern(start);
            const long long day = pyfloordiv(local, DAY_US), tod_min = (local - day * DAY_US) / (60 * US);
            const long long mins = tod_min - OPEN_MIN;
            if (mins < 0 || mins >= SESSION_BARS * 5) { dropped.outside_hours++; continue; }
            if (start + INTERVAL * US > as_of) { dropped.not_complete++; continue; }
            TBar b; b.date = da::day_iso(day); b.slot = mins / 5; b.start = clock24(tod_min);
            b.o = to_float(o); b.h = to_float(h); b.l = to_float(l); b.c = to_float(c); b.v = truthy(v) ? to_float(v) : 0.0;
            bars[ts] = b;
        }
    std::vector<TBar> out; for (auto& kv : bars) out.push_back(kv.second);
    return {meta, out, dropped};
}
using Day = std::map<long long, TBar>;               // slot -> bar
using Days = std::map<std::string, Day>;             // date -> slots (sorted like sorted(days))
using Session = std::pair<std::string, std::vector<TBar>>;
inline Days split_sessions(const std::vector<TBar>& bars) { Days d; for (const auto& b : bars) d[b.date][b.slot] = b; return d; }
struct Short { std::string date; long long bars; };
// full sessions (all 78 bars) strictly before the plan's date, oldest first, and the short ones left out
inline std::pair<std::vector<Session>, std::vector<Short>> full_sessions(const Days& days, const std::string& before) {
    std::vector<Session> full; std::vector<Short> shrt;
    for (const auto& [d, s] : days) {
        if (d >= before) continue;
        if (static_cast<int>(s.size()) == SESSION_BARS) {
            std::vector<TBar> v; for (int i = 0; i < SESSION_BARS; ++i) v.push_back(s.at(i));
            full.emplace_back(d, v);
        } else {
            shrt.push_back({d, static_cast<long long>(s.size())});
        }
    }
    return {full, shrt};
}

// ---------------------------------------------------------------- the simulation (the reel's sim.py)
using Ratio = std::array<double, 4>;   // (o, h, l, c) / the previous bar's close
inline std::vector<std::vector<Ratio>> build_pool(const std::vector<Session>& sessions) {
    std::vector<std::vector<Ratio>> pool(SESSION_BARS);
    for (const auto& [d, D] : sessions)
        for (int i = 1; i < SESSION_BARS; ++i) {
            const double pc = D[static_cast<std::size_t>(i - 1)].c; const TBar& b = D[static_cast<std::size_t>(i)];
            pool[static_cast<std::size_t>(i)].push_back({b.o / pc, b.h / pc, b.l / pc, b.c / pc});
        }
    return pool;
}
inline double pct(const std::vector<double>& a, double q) {
    const long long n = static_cast<long long>(a.size());
    return a[static_cast<std::size_t>(std::min(n - 1, static_cast<long long>(q * static_cast<double>(n))))];
}
// statistics.mean: the exact sum (fsum) over n
inline double mean(const std::vector<double>& v) { return fsum(v) / static_cast<double>(v.size()); }
inline Val flist(const std::vector<double>& v) { Val l = Val::list(); for (double x : v) l.l->push_back(Val::flt(x)); return l; }
// Monte Carlo replays of bars k+1..last; same draws and order as the reel's sim.py
inline Val simulate(const std::vector<std::vector<Ratio>>& pool, double entry, double stop, double target, int k, int last,
                    long long replays, long long seed, int window, const std::string& side) {
    const bool lng = side == "buy";
    const double risk = lng ? entry - stop : stop - entry;
    const double r_target = round_n((lng ? target - entry : entry - target) / risk, 4);
    const auto slot = [&](PyRandom& rng, int i) -> std::size_t {
        if (window == 0) return static_cast<std::size_t>(i);
        return static_cast<std::size_t>(std::min<long long>(SESSION_BARS - 1, std::max<long long>(1, i + rng.randint(-window, window))));
    };
    const auto pick = [&](PyRandom& rng, int i) -> const Ratio& { const auto& col = pool[slot(rng, i)]; return col[static_cast<std::size_t>(rng.randbelow(static_cast<long long>(col.size())))]; };
    PyRandom rng(seed);
    std::vector<int> outcomes; std::vector<double> closes; Val paths = Val::list();
    outcomes.reserve(static_cast<std::size_t>(replays)); closes.reserve(static_cast<std::size_t>(replays));
    for (long long n = 0; n < replays; ++n) {
        double p = entry; int outcome = 0;   // 0 none (close), 1 target, 2 stop
        const bool keep = n < SAMPLE_PATHS;
        std::vector<double> path; if (keep) path.push_back(entry);
        for (int i = k + 1; i <= last; ++i) {
            const Ratio& r = pick(rng, i);
            const double hi = p * r[1], lo = p * r[2], cl = p * r[3];
            if (outcome == 0) {
                if (lng) { if (lo <= stop) outcome = 2; else if (hi >= target) outcome = 1; }   // both in one bar: the loss
                else { if (hi >= stop) outcome = 2; else if (lo <= target) outcome = 1; }
            }
            p = cl;
            if (keep) path.push_back(round_n(p, 2));
        }
        outcomes.push_back(outcome); closes.push_back(p);
        if (keep) paths.l->push_back(flist(path));
    }
    long long cnt[3] = {0, 0, 0}, green = 0;
    std::vector<double> Rs; Rs.reserve(closes.size());
    for (std::size_t i = 0; i < closes.size(); ++i) {
        const int o = outcomes[i]; const double p = closes[i];
        cnt[o]++;
        if (o == 1 || (o == 0 && (lng ? p > entry : p < entry))) green++;
        Rs.push_back(o == 1 ? r_target : o == 2 ? -1.0 : (lng ? p - entry : entry - p) / risk);
    }
    std::vector<double> srt = closes; std::sort(srt.begin(), srt.end());
    // percentile bands per bar for the fan chart: a second run of 2,000 paths, same seed
    PyRandom rng2(seed);
    const int width = last - k + 1;
    std::vector<std::vector<double>> allp(BAND_PATHS, std::vector<double>(static_cast<std::size_t>(width)));
    for (int n = 0; n < BAND_PATHS; ++n) {
        double p = entry; auto& path = allp[static_cast<std::size_t>(n)]; path[0] = entry;
        for (int i = k + 1; i <= last; ++i) { p = p * pick(rng2, i)[3]; path[static_cast<std::size_t>(i - k)] = p; }
    }
    Val bands = Val::list();
    for (int j = 0; j < width; ++j) {
        std::vector<double> col; col.reserve(BAND_PATHS);
        for (const auto& pp : allp) col.push_back(pp[static_cast<std::size_t>(j)]);
        std::sort(col.begin(), col.end());
        std::vector<double> b; for (double q : BAND_Q) b.push_back(round_n(pct(col, q), 2));
        bands.l->push_back(flist(b));
    }
    const double lo_ = pct(srt, 0.01), hi_ = pct(srt, 0.99), W = (hi_ - lo_) / HIST_BINS;
    std::vector<long long> bins(HIST_BINS, 0);
    for (double c : srt) {
        const long long b = W > 0 ? static_cast<long long>((c - lo_) / W) : 0;   // int() truncates toward zero
        if (0 <= b && b < HIST_BINS) bins[static_cast<std::size_t>(b)]++;
    }
    const double R = static_cast<double>(replays);
    Val m = Val::dict();
    m.d->set("replays", Val::integer(replays)); m.d->set("seed", Val::integer(seed)); m.d->set("window", Val::integer(window));
    m.d->set("bars_left", Val::integer(last - k));
    m.d->set("p_target", Val::flt(round_n(static_cast<double>(cnt[1]) / R, 4))); m.d->set("p_stop", Val::flt(round_n(static_cast<double>(cnt[2]) / R, 4)));
    m.d->set("p_neither", Val::flt(round_n(static_cast<double>(cnt[0]) / R, 4))); m.d->set("p_green", Val::flt(round_n(static_cast<double>(green) / R, 4)));
    m.d->set("median_close", Val::flt(round_n(pct(srt, 0.5), 2))); m.d->set("p5_close", Val::flt(round_n(pct(srt, 0.05), 2)));
    m.d->set("p95_close", Val::flt(round_n(pct(srt, 0.95), 2))); m.d->set("avg_R", Val::flt(round_n(mean(Rs), 3)));
    m.d->set("r_at_target", Val::flt(r_target)); m.d->set("band_paths", Val::integer(BAND_PATHS)); m.d->set("bands", bands);
    m.d->set("sample_paths", paths);
    Val hist = Val::dict(), bl = Val::list();
    for (long long b : bins) bl.l->push_back(Val::integer(b));
    hist.d->set("lo", Val::flt(round_n(lo_, 2))); hist.d->set("hi", Val::flt(round_n(hi_, 2))); hist.d->set("bins", bl);
    m.d->set("hist", hist);
    return m;
}

// ---------------------------------------------------------------- stop rules and the history replay
inline double cents_away(double x, bool lng) {   // a stop to the cent, away from the entry: down for a buy, up for a sell
    const double c = round_n(x * 100, 4);
    return (lng ? std::floor(c) : std::ceil(c)) / 100.0;
}
template <class D> inline std::pair<double, double> opening_range(const D& d, int n) {
    double oh = d.at(0).h, ol = d.at(0).l;
    for (int i = 1; i < n; ++i) { oh = std::max(oh, d.at(i).h); ol = std::min(ol, d.at(i).l); }
    return {oh, ol};
}
inline double rule_stop(const Val& rule, double oh, double ol, double entry, bool lng) {
    const std::string r = pystr(get(rule, "stop_rule"));
    if (r == "or_mid") return cents_away((oh + ol) / 2, lng);
    if (r == "or_low" || r == "or_high") return cents_away(lng ? ol : oh, lng);
    const double frac = to_float(get(rule, "stop_pct")) / 100.0;
    return cents_away(lng ? entry * (1 - frac) : entry * (1 + frac), lng);
}
// the plan's rule on every past full session: entry = the first bar after the opening range to close beyond it (up to the
// --until bar), then stop first, then target, bar by bar, else the close at --until
inline Val history_replay(const std::vector<Session>& sessions, const Val& rule, int last, const std::string& side) {
    const bool lng = side == "buy";
    const int n = static_cast<int>(to_int(get(rule, "or_minutes")) / 5);
    const double target_r = to_float(get(rule, "target_r"));
    Val trades = Val::list(); long long tgt = 0, stp = 0, cls = 0, green = 0; std::vector<double> Rs;
    for (const auto& [d, D] : sessions) {
        const auto [oh, ol] = opening_range(D, n);
        int kk = -1;
        for (int i = n; i <= last; ++i) if (lng ? D[static_cast<std::size_t>(i)].c > oh : D[static_cast<std::size_t>(i)].c < ol) { kk = i; break; }
        if (kk < 0) continue;
        const double e = round_n(D[static_cast<std::size_t>(kk)].c, 2);
        const double s = rule_stop(rule, oh, ol, e, lng);
        const double risk = lng ? e - s : s - e;
        if (risk <= 0) continue;
        const double t = lng ? round_n(e + target_r * risk, 2) : round_n(e - target_r * risk, 2);
        int oc = 0;
        for (int i = kk + 1; i <= last && i < SESSION_BARS; ++i) {
            const TBar& b = D[static_cast<std::size_t>(i)];
            if (lng ? b.l <= s : b.h >= s) { oc = 2; break; }
            if (lng ? b.h >= t : b.l <= t) { oc = 1; break; }
        }
        const double cl = D[static_cast<std::size_t>(last)].c;
        const double R = oc == 1 ? (lng ? t - e : e - t) / risk : oc == 2 ? -1.0 : (lng ? cl - e : e - cl) / risk;
        const double Rr = round_n(R, 2);
        Val tr = Val::dict();
        tr.d->set("date", Val::str(d)); tr.d->set("time", Val::str(clock12(OPEN_MIN + 5 * (kk + 1)))); tr.d->set("entry", Val::flt(e));
        tr.d->set("stop", Val::flt(s)); tr.d->set("target", Val::flt(t)); tr.d->set("outcome", Val::str(oc == 1 ? "target" : oc == 2 ? "stop" : "close"));
        tr.d->set("R", Val::flt(Rr));
        trades.l->push_back(tr);
        (oc == 1 ? tgt : oc == 2 ? stp : cls)++;
        if (Rr > 0) green++;
        Rs.push_back(Rr);
    }
    Val h = Val::dict();
    h.d->set("sessions", Val::integer(static_cast<long long>(sessions.size()))); h.d->set("triggered", Val::integer(static_cast<long long>(trades.l->size())));
    h.d->set("target", Val::integer(tgt)); h.d->set("stop", Val::integer(stp)); h.d->set("close", Val::integer(cls)); h.d->set("green", Val::integer(green));
    h.d->set("avg_R", Rs.empty() ? Val() : Val::flt(round_n(mean(Rs), 3)));
    h.d->set("rule", rule); h.d->set("trades", trades);
    return h;
}

// ---------------------------------------------------------------- the plan
inline double num_(const Val& v, const std::string& what) {
    double x;
    try { x = to_float(v); } catch (const PyErr&) { throw TMError("the plan needs a number for " + what + ", got " + repr(v)); }
    if (!(x > 0) || std::isinf(x)) throw TMError("the plan's " + what + " must be a positive price, got " + repr(v));
    return x;
}
// a plan dict (trader/plan.json shape) -> the checked plan the time machine runs
inline Val normalize_plan(const Val& raw, const std::string& src = "flags") {
    static const std::regex SYM(R"(^[A-Z][A-Z0-9.\-]{0,9}$)");
    const std::string sym = upper(strip(pystr(orv(dget(raw, "symbol"), Val::str("")))));
    if (!std::regex_search(sym, SYM)) throw TMError("the plan needs a ticker symbol, got " + repr(dget(raw, "symbol")));
    const std::string side = lower(pystr(orv(dget(raw, "side"), Val::str("buy"))));
    if (side != "buy" && side != "sell") throw TMError("side must be buy or sell, got " + repr(dget(raw, "side")));
    const std::string interval = pystr(orv(dget(raw, "interval"), Val::str("5m")));
    if (interval != "5m") throw TMError("5-minute bars only (the plan says " + interval + "): Yahoo keeps 60 days of 5-min bars but only about 7 days of 1-min bars, too few sessions for a fair replay pool");
    const auto dd = date_fromiso(pystr(orv(dget(raw, "date"), Val::str(""))));
    if (!dd) throw TMError("the plan needs a date like 2026-10-19, got " + repr(dget(raw, "date")));
    const std::string date = da::day_iso(*dd);
    const double entry = num_(dget(raw, "entry"), "entry"), stop = num_(dget(raw, "stop"), "stop"), target = num_(dget(raw, "target"), "target");
    const bool lng = side == "buy";
    const auto fs = [](double x) { return pystr(Val::flt(x)); };
    if (lng && !(stop < entry && entry < target)) throw TMError("a buy needs stop < entry < target (got stop " + fs(stop) + ", entry " + fs(entry) + ", target " + fs(target) + ")");
    if (!lng && !(target < entry && entry < stop)) throw TMError("a sell needs target < entry < stop (got target " + fs(target) + ", entry " + fs(entry) + ", stop " + fs(stop) + ")");
    std::optional<long long> k;
    const Val et = dget(raw, "entry_time");
    if (truthy(et)) {
        const int close_min = parse_clock(et, "entry_time");
        if (pymod(close_min - OPEN_MIN, 5)) throw TMError("entry_time " + pystr(et) + " is not on the 5-minute grid (use the bar's close, e.g. 10:15 AM)");
        k = pyfloordiv(close_min - OPEN_MIN, 5) - 1;
    }
    const Val eb = dget(raw, "entry_bar");
    if (!eb.is_none()) {
        long long kb;
        try { kb = to_int(eb); } catch (const PyErr&) { throw TMError("entry_bar must be a bar number, got " + repr(eb)); }
        if (k && kb != *k) throw TMError("the plan disagrees with itself: entry_bar " + std::to_string(kb) + " but entry_time " + pystr(et) + " is bar " + std::to_string(*k));
        k = kb;
    }
    if (!k) throw TMError("the plan needs entry_time (the entry bar's close, e.g. 10:15 AM) or entry_bar");
    if (!(0 <= *k && *k <= SESSION_BARS - 2)) throw TMError("the entry must close between 9:35 AM and 3:55 PM to leave bars to replay (got " + clock12(OPEN_MIN + 5 * (*k + 1)) + ")");
    const Val orng = orv(dget(raw, "opening_range"), Val::dict());
    const Val or0 = orng.t == Val::DICT ? (has(orng, "minutes") ? get(orng, "minutes") : Val::integer(15)) : Val::integer(15);
    long long or_min;
    try { or_min = to_int(or0); } catch (const PyErr&) { throw TMError("opening_range.minutes must be a number"); }
    if (or_min < 5 || or_min % 5 || or_min >= 390) throw TMError("opening_range.minutes must be a multiple of 5 (got " + std::to_string(or_min) + ")");
    const double risk = lng ? entry - stop : stop - entry;
    Val p = Val::dict();
    p.d->set("symbol", Val::str(sym)); p.d->set("side", Val::str(side)); p.d->set("date", Val::str(date));
    p.d->set("setup", Val::str(pystr(orv(dget(raw, "setup"), Val::str("orb"))))); p.d->set("entry", Val::flt(entry));
    p.d->set("entry_time", Val::str(clock12(OPEN_MIN + 5 * (*k + 1)))); p.d->set("entry_bar", Val::integer(*k));
    p.d->set("stop", Val::flt(stop)); p.d->set("target", Val::flt(target)); p.d->set("risk_per_share", Val::flt(round_n(risk, 2)));
    p.d->set("target_r", Val::flt(round_n((lng ? target - entry : entry - target) / risk, 2)));
    Val o2 = Val::dict();
    if (orng.t == Val::DICT) for (const auto& kv : orng.d->items) o2.d->set(kv.first, kv.second);
    o2.d->set("minutes", Val::integer(or_min));
    p.d->set("opening_range", o2);
    p.d->set("stop_rule", Val::str(pystr(orv(dget(raw, "stop_rule"), Val::str("auto"))))); p.d->set("interval", Val::str("5m"));
    p.d->set("flat_by", dget(raw, "flat_by")); p.d->set("from", Val::str(src));
    return p;
}
inline Val load_plan(const Val& a, long long now_utc) {   // the plan from --plan or from the flags
    if (truthy(get(a, "symbol"))) {
        std::vector<std::string> missing;
        for (const char* f : {"entry", "stop", "target", "entry_time"}) if (get(a, f).is_none()) missing.push_back("--" + replace_all(f, "_", "-"));
        if (!missing.empty()) throw TMError("with --symbol also give " + join(missing, ", "));
        Val raw = Val::dict(), orng = Val::dict();
        orng.d->set("minutes", get(a, "or_minutes"));
        raw.d->set("symbol", get(a, "symbol")); raw.d->set("side", get(a, "side"));
        raw.d->set("date", truthy(get(a, "date")) ? get(a, "date") : Val::str(da::day_iso(da::day_of(da::to_eastern(now_utc)))));
        for (const char* f : {"entry", "stop", "target", "entry_time", "setup"}) raw.d->set(f, get(a, f));
        raw.d->set("opening_range", orng); raw.d->set("stop_rule", get(a, "stop_rule")); raw.d->set("interval", Val::str("5m"));
        return normalize_plan(raw, "flags");
    }
    const std::string path = S(a, "plan");
    if (!exists(path)) throw TMError("no plan at " + path + ": run /trader-exit first, or pass --symbol --date --entry --stop --target --entry-time");
    Val raw;
    try { raw = loads(read_text(path)); } catch (const PyErr& e) { throw TMError(path + " is not valid JSON: " + e.what()); }
    if (raw.t != Val::DICT) throw TMError(path + " is not a plan");
    return normalize_plan(raw, path);
}
// the rule the history replay uses (None unless orb): 'auto' works the stop rule out from the plan day's own opening range
inline Val resolve_rule(const Val& plan, const Day& entry_day) {
    if (lower(pystr(get(plan, "setup"))) != "orb") return Val();
    const bool lng = pystr(get(plan, "side")) == "buy";
    const long long or_minutes = to_int(get(get(plan, "opening_range"), "minutes"));
    const int n = static_cast<int>(or_minutes / 5);
    Val rule = Val::dict();
    rule.d->set("setup", Val::str("orb")); rule.d->set("or_minutes", Val::integer(or_minutes)); rule.d->set("stop_rule", get(plan, "stop_rule"));
    rule.d->set("target_r", get(plan, "target_r"));
    rule.d->set("stop_pct", Val::flt(round_n(100.0 * to_float(get(plan, "risk_per_share")) / to_float(get(plan, "entry")), 4)));
    const std::string far_side = lng ? "or_low" : "or_high";
    std::string sr = pystr(get(rule, "stop_rule"));
    const double pstop = to_float(get(plan, "stop"));
    if (sr == "auto") {
        sr = "pct";
        bool all = true; for (int i = 0; i < n; ++i) if (!entry_day.count(i)) all = false;
        if (all) {
            const auto [oh, ol] = opening_range(entry_day, n);
            if (std::fabs(cents_away((oh + ol) / 2, lng) - pstop) < 0.005) sr = "or_mid";
            else if (std::fabs(cents_away(lng ? ol : oh, lng) - pstop) < 0.005) sr = far_side;
        }
    } else if ((sr == "or_low" || sr == "or_high") && sr != far_side) {
        sr = "pct";
    } else if (sr != "or_mid" && sr != far_side) {
        sr = "pct";
    }
    rule.d->set("stop_rule", Val::str(sr));
    if (sr != "pct") { Val r2 = Val::dict(); for (const auto& kv : rule.d->items) if (kv.first != "stop_pct") r2.d->set(kv.first, kv.second); rule = r2; }
    return rule;
}

// ---------------------------------------------------------------- the verdict
inline double fget(const Val& d, const char* k, double dflt = 0) { const Val v = dget(d, k); return v.is_none() ? dflt : to_float(v); }
inline std::string until12(const Val& mc) {
    const std::string t = pystr(orv(dget(mc, "until"), Val::str("16:00")));
    return clock12(std::stoll(t.substr(0, 2)) * 60 + std::stoll(t.substr(3)));
}
inline double r_at(const Val& mc) { const Val r = dget(mc, "r_at_target"); return truthy(r) ? to_float(r) : 1.0; }
struct Lean { double gap, share, even; };
inline std::optional<Lean> lean(const Val& mc) {
    const double pt = fget(mc, "p_target"), ps = fget(mc, "p_stop");
    if (pt + ps <= 0) return std::nullopt;
    const double share = pt / (pt + ps), even = 1.0 / (1.0 + r_at(mc));
    return Lean{share - even, share, even};
}
inline std::string verdict(const Val& mc) {
    const double pt = fget(mc, "p_target"), ps = fget(mc, "p_stop");
    const auto lv = lean(mc);
    if (!lv) return "No replay reached the target or the stop by " + until12(mc) + ": the price at " + until12(mc) + " decides every one.";
    const double d = lv->gap, r = r_at(mc);
    const bool one_r = std::fabs(r - 1.0) < 0.005;
    std::string head;
    if (std::fabs(d) < COIN_FLIP) head = one_r ? "Close to a coin flip" : "Close to break-even";
    else if (std::fabs(d) < STRONG) head = d > 0 ? "Leans toward the target" : "Leans toward the stop";
    else head = d > 0 ? "Strongly toward the target" : "Strongly toward the stop";
    const std::string odds = "target first " + fmt_f(100 * pt, 1) + " % vs stop first " + fmt_f(100 * ps, 1) + " %";
    if (one_r) return head + ": " + odds + ".";
    return head + ": at " + fmt_g(round_n(r, 2)) + "R the target has to come first in " + fmt_f(100 * lv->even, 0) + " % of the replays that hit one side; it did in " +
           fmt_f(100 * lv->share, 0) + " % (" + odds + ").";
}
inline std::vector<std::string> verdict_notes(const Val& mc) {   // printed right below the verdict, so nothing reads one-sided
    std::vector<std::string> notes;
    const double pn = fget(mc, "p_neither");
    const auto lv = lean(mc);
    if (pn > MANY_NEITHER && lv) notes.push_back(fmt_f(100 * pn, 1) + " % of the replays hit neither by " + until12(mc) + "; the price at " + until12(mc) + " decides those.");
    const Val avg = dget(mc, "avg_R");
    if (lv && std::fabs(lv->gap) >= COIN_FLIP && !avg.is_none() && to_float(avg) != 0 && (to_float(avg) > 0) != (lv->gap > 0))
        notes.push_back("Average R is " + fmt_fs(to_float(avg), 3) + " all the same: the replays that hit neither pull it the other way.");
    return notes;
}
inline Val slist(const std::vector<std::string>& v) { Val l = Val::list(); for (const auto& s : v) l.l->push_back(Val::str(s)); return l; }

// ---------------------------------------------------------------- the run
inline Val time_machine(const std::string& text, const Val& plan, long long now_utc, long long replays, long long seed, long long window,
                        const Val& until, const std::string& source) {
    if (!(1 <= replays && replays <= 1000000)) throw TMError("--replays must be between 1 and 1,000,000");
    if (!(0 <= window && window <= 10)) throw TMError("--window must be between 0 and 10 bars");
    const std::string sym = pystr(get(plan, "symbol"));
    const Val result = chart_result(text, sym);
    auto [meta, bars, dropped] = parse_bars(result, now_utc);
    const std::string msym = upper(pystr(orv(dget(meta, "symbol"), Val::str(""))));
    if (!msym.empty() && msym != replace_all(sym, ".", "-")) throw TMError("the data is for " + msym + " but the plan is for " + sym);
    const Val gran = dget(meta, "dataGranularity");
    if (truthy(gran) && !(gran.t == Val::STR && gran.s == "5m")) throw TMError("the data has " + pystr(gran) + " bars; the time machine needs 5-minute bars");
    if (bars.empty()) throw TMError("no complete 5-minute bars for " + sym + " in the data");
    const int k = static_cast<int>(to_int(get(plan, "entry_bar")));
    const std::string date = pystr(get(plan, "date")), entry_time = pystr(get(plan, "entry_time"));
    const bool early = NYSE_EARLY_CLOSE.count(date) > 0;
    const int end_min = early ? EARLY_MIN : CLOSE_MIN;
    const int until_min = until.is_none() ? end_min : parse_clock(until, "--until");
    if (pymod(until_min - OPEN_MIN, 5) || until_min > end_min)
        throw TMError("--until must be on the 5-minute grid and no later than " + clock12(end_min) + (early ? " (an early close)" : ""));
    const int last = static_cast<int>(pyfloordiv(until_min - OPEN_MIN, 5)) - 1;
    if (last <= k) throw TMError("--until " + clock12(until_min) + " leaves nothing to replay after the " + entry_time + " entry");
    const Days days = split_sessions(bars);
    const auto it = days.find(date);
    const Day entry_day = it == days.end() ? Day() : it->second;
    if (!entry_day.count(k)) {
        const std::string reason = entry_day.empty() ? "no bars for that day" : "the data stops at the " + entry_day.rbegin()->second.start + " bar";
        throw TMError("the entry is not in the data: no complete " + clock24(OPEN_MIN + 5 * k) + " bar (closing " + entry_time + ") on " + date + " for " + sym + " (" + reason + ")");
    }
    const TBar& eb = entry_day.at(k);
    const double entry = to_float(get(plan, "entry")), stop = to_float(get(plan, "stop")), target = to_float(get(plan, "target"));
    std::vector<std::string> warn;
    if (!(eb.l - 0.01 <= entry && entry <= eb.h + 0.01))
        warn.push_back("the plan's entry " + fmt_f(entry, 2) + " is outside that bar's range " + fmt_f(eb.l, 2) + " to " + fmt_f(eb.h, 2));
    const auto [sessions, shrt] = full_sessions(days, date);
    if (static_cast<int>(sessions.size()) < MIN_SESSIONS)
        throw TMError("only " + std::to_string(sessions.size()) + " full session(s) of " + sym + " before " + date + " in the data; the time machine needs at least " + std::to_string(MIN_SESSIONS));
    const auto pool = build_pool(sessions);
    const std::string side = pystr(get(plan, "side"));
    Val mc = simulate(pool, entry, stop, target, k, last, replays, seed, static_cast<int>(window), side);
    mc.d->set("until", Val::str(clock24(until_min))); mc.d->set("pool_sessions", Val::integer(static_cast<long long>(sessions.size())));
    mc.d->set("pool_from", Val::str(sessions.front().first)); mc.d->set("pool_to", Val::str(sessions.back().first));
    Val bt = Val::list(); for (int j = k; j <= last; ++j) bt.l->push_back(Val::str(clock24(OPEN_MIN + 5 * (j + 1))));
    mc.d->set("band_times", bt);
    const Val rule = resolve_rule(plan, entry_day);
    Val plan_out = Val::dict();
    for (const auto& kv : plan.d->items) if (!kv.second.is_none()) plan_out.d->set(kv.first, kv.second);
    Val history;
    if (!rule.is_none()) {
        plan_out.d->set("stop_rule", get(rule, "stop_rule"));
        history = history_replay(sessions, rule, last, side);
    } else {
        history = Val::dict();
        history.d->set("sessions", Val::integer(static_cast<long long>(sessions.size()))); history.d->set("triggered", Val::integer(0));
        history.d->set("rule", Val()); history.d->set("trades", Val::list());
        history.d->set("note", Val::str("skipped: the history replay knows the opening-range breakout (orb) only, this plan's setup is " + repr(get(plan, "setup"))));
    }
    const TBar& lb = bars.back();
    Val data = Val::dict(), dr = Val::dict(), sl = Val::list();
    dr.d->set("no_price", Val::integer(dropped.no_price)); dr.d->set("off_grid", Val::integer(dropped.off_grid));
    dr.d->set("not_complete", Val::integer(dropped.not_complete)); dr.d->set("outside_hours", Val::integer(dropped.outside_hours));
    for (const auto& s : shrt) { Val x = Val::dict(); x.d->set("date", Val::str(s.date)); x.d->set("bars", Val::integer(s.bars)); sl.l->push_back(x); }
    data.d->set("symbol", Val::str(msym.empty() ? sym : msym)); data.d->set("interval", Val::str("5m")); data.d->set("range", Val::str("60d"));
    data.d->set("last_bar", Val::str(lb.date + " " + lb.start + " ET")); data.d->set("dropped", dr); data.d->set("short_sessions_left_out", sl);
    Val doc = Val::dict();
    doc.d->set("created_by", Val::str("trader-timemachine")); doc.d->set("created_at", Val::str(utc_z(now_utc))); doc.d->set("source", Val::str(source));
    doc.d->set("data", data); doc.d->set("plan", plan_out); doc.d->set("mc", mc); doc.d->set("history", history);
    doc.d->set("verdict", Val::str(verdict(mc))); doc.d->set("verdict_notes", slist(verdict_notes(mc))); doc.d->set("warnings", slist(warn));
    doc.d->set("note", Val::str("A simulation built from " + sym + "'s own last " + std::to_string(sessions.size()) +
                                " full sessions. It is not a prediction: earnings, news and halts can do things those sessions never did. Same seed, same numbers."));
    return doc;
}

// ---------------------------------------------------------------- output
inline std::string pc(double x) { return fmt_f(100.0 * x, 1) + " %"; }
inline std::string ljust(const std::string& s, std::size_t w) { const std::size_t n = cplen(s); return n >= w ? s : s + std::string(w - n, ' '); }
inline std::string G(const Val& d, const char* k) { const Val v = dget(d, k); return v.t == Val::STR ? v.s : pystr(v); }
inline long long I(const Val& d, const char* k) { return to_int(dget(d, k)); }
inline std::vector<std::string> report(const Val& doc) {
    const Val p = get(doc, "plan"), m = get(doc, "mc"), h = get(doc, "history");
    const bool lng = G(p, "side") == "buy";
    const std::string until = until12(m);
    const auto dd = date_fromiso(G(p, "date"));
    const std::string wd = dd ? WEEKDAYS[weekday_of(*dd)] : "";
    std::vector<std::string> lines = {
        G(p, "symbol") + " " + G(p, "side") + ", " + wd + " " + G(p, "date") + ": entry " + fmt_f(fget(p, "entry"), 2) + " at " + G(p, "entry_time") + " (bar " +
            std::to_string(I(p, "entry_bar")) + "), stop " + fmt_f(fget(p, "stop"), 2) + ", target " + fmt_f(fget(p, "target"), 2) + " (" + fmt_g(fget(p, "target_r")) +
            "R, risk " + fmt_f(fget(p, "risk_per_share"), 2) + " a share).",
        commas(std::to_string(I(m, "replays"))) + " replays of the rest of the session, " + G(p, "entry_time") + " to " + until + " (" + std::to_string(I(m, "bars_left")) +
            " five-minute bars), seed " + std::to_string(I(m, "seed")) + ".",
        "Each bar is drawn whole from " + G(p, "symbol") + "'s own 5-min bars at the same time of day (+-" + std::to_string(I(m, "window")) + " bars) over its last " +
            std::to_string(I(m, "pool_sessions")) + " full sessions (" + G(m, "pool_from") + " to " + G(m, "pool_to") + ").",
        "",
        "  target first   " + pc(fget(m, "p_target")),
        "  stop first     " + pc(fget(m, "p_stop")),
        "  neither        " + ljust(pc(fget(m, "p_neither")), 8) + " still open at " + until,
        "  green          " + ljust(pc(fget(m, "p_green")), 8) + " target first, or " + (lng ? "above" : "below") + " the entry at " + until,
        "",
        "  Price at " + until + ": median " + fmt_f(fget(m, "median_close"), 2) + ", 90 % of replays between " + fmt_f(fget(m, "p5_close"), 2) + " and " + fmt_f(fget(m, "p95_close"), 2) + ".",
        "  Average R: " + fmt_fs(fget(m, "avg_R"), 3) + " (R = the " + fmt_f(fget(p, "risk_per_share"), 2) + " risk: +" + fmt_g(fget(m, "r_at_target")) + "R at the target, -1R at the stop).",
        "",
    };
    const Val ru = dget(h, "rule");
    if (truthy(ru)) {
        const std::string sr = G(ru, "stop_rule");
        const std::string stop_words = sr == "or_mid" ? "opening-range midpoint" : sr == "or_low" ? "opening-range low" : sr == "or_high" ? "opening-range high" :
                                       fmt_f(fget(ru, "stop_pct"), 2) + " % from the entry";
        if (truthy(dget(h, "triggered")))
            lines.push_back("History: the same rule (" + std::to_string(I(ru, "or_minutes")) + "-min opening range, stop at the " + stop_words + ", " + fmt_g(fget(ru, "target_r")) +
                            "R target) on each of those " + std::to_string(I(h, "sessions")) + " real sessions fired " + std::to_string(I(h, "triggered")) + " times: " +
                            std::to_string(I(h, "target")) + " target, " + std::to_string(I(h, "stop")) + " stop, " + std::to_string(I(h, "close")) + " neither. Average R " +
                            fmt_fs(fget(h, "avg_R"), 3) + ".");
        else
            lines.push_back("History: the same rule never fired in those " + std::to_string(I(h, "sessions")) + " sessions.");
    } else {
        lines.push_back("History: " + (has(h, "note") ? G(h, "note") : std::string("skipped.")));
    }
    lines.push_back(""); lines.push_back(G(doc, "verdict"));
    const Val notes = dget(doc, "verdict_notes");
    if (notes.t == Val::LIST) for (const Val& n : *notes.l) lines.push_back(pystr(n));
    lines.push_back("");
    const Val warns = dget(doc, "warnings");
    if (warns.t == Val::LIST) for (const Val& w : *warns.l) lines.push_back("Note: " + pystr(w) + ".");
    const Val shrt = dget(get(doc, "data"), "short_sessions_left_out");
    if (truthy(shrt)) {
        std::vector<std::string> ds; for (const Val& x : *shrt.l) ds.push_back(G(x, "date"));
        lines.push_back("Left out " + std::to_string(shrt.l->size()) + " short session(s) (half days or gaps): " + join(ds, ", ") + ".");
    }
    lines.push_back(G(doc, "note") + " Repeat this run with --seed " + std::to_string(I(m, "seed")) + ".");
    return lines;
}
inline std::vector<std::string> render_chart(const Val& doc, int height = 17) {   // ASCII fan chart: one column per 5-min bar
    const Val p = get(doc, "plan"), m = get(doc, "mc");
    const Val bands = get(m, "bands"), times = get(m, "band_times");
    const std::size_t nb = bands.l->size();
    const double stop = fget(p, "stop"), target = fget(p, "target"), entry = fget(p, "entry");
    double lo = std::min(stop, target), hi = std::max(stop, target);
    for (const Val& b : *bands.l) { lo = std::min(lo, to_float((*b.l)[0])); hi = std::max(hi, to_float((*b.l)[4])); }
    double pad = (hi - lo) * 0.04; if (pad == 0) pad = 0.01;
    lo -= pad; hi += pad;
    const double step = (hi - lo) / (height - 1);
    const auto row = [&](double price) { return static_cast<int>(std::max(0.0, std::min(static_cast<double>(height - 1), std::nearbyint((hi - price) / step)))); };
    std::vector<std::string> grid(static_cast<std::size_t>(height), std::string(nb, ' '));
    for (std::size_t j = 0; j < nb; ++j) {
        const Val& b = (*bands.l)[j];
        int r[5]; for (int q = 0; q < 5; ++q) r[q] = row(to_float((*b.l)[static_cast<std::size_t>(q)]));
        for (int x = r[4]; x <= r[0]; ++x) grid[static_cast<std::size_t>(x)][j] = '.';
        for (int x = r[3]; x <= r[1]; ++x) grid[static_cast<std::size_t>(x)][j] = ':';
        grid[static_cast<std::size_t>(r[2])][j] = '*';
    }
    std::vector<std::pair<int, std::string>> marks = {{row(target), "target"}};   // a dict: the stop overwrites a shared row's label
    if (row(stop) == row(target)) marks[0].second = "stop"; else marks.emplace_back(row(stop), "stop");
    const auto mark = [&](int r) -> std::string { for (const auto& kv : marks) if (kv.first == r) return kv.second; return ""; };
    if (mark(row(entry)).empty()) marks.emplace_back(row(entry), "entry");
    for (int r : {row(target), row(stop)}) for (char& ch : grid[static_cast<std::size_t>(r)]) if (ch == ' ') ch = '-';
    std::vector<std::string> out = {G(p, "symbol") + " " + G(p, "date") + ", " + commas(std::to_string(has(m, "band_paths") ? I(m, "band_paths") : BAND_PATHS)) +
                                    " replay paths from the " + G(p, "entry_time") + " entry (seed " + std::to_string(I(m, "seed")) + ")"};
    for (int r = 0; r < height; ++r) {
        const std::string label = mark(r);
        const double price = label == "target" ? target : label == "stop" ? stop : label == "entry" ? entry : hi - r * step;
        char b[64]; std::snprintf(b, sizeof b, "%9s |", fmt_f(price, 2).c_str());
        const std::string left = (!label.empty() || r == 0 || r == height - 1) ? std::string(b) : "          |";
        out.push_back(left + grid[static_cast<std::size_t>(r)] + (label.empty() ? "" : "  " + label));
    }
    std::string axis(nb, ' ');
    std::set<std::size_t> cols = {0, nb / 2, nb ? nb - 1 : 0};
    for (std::size_t j : cols) {
        if (j >= times.l->size()) continue;
        const std::string t = pystr((*times.l)[j]);
        const long long hh = std::stoll(t.substr(0, 2));
        const std::string lab = std::to_string(hh % 12 ? hh % 12 : 12) + ":" + t.substr(3);
        const long long s = std::max<long long>(0, std::min<long long>(static_cast<long long>(j), static_cast<long long>(nb) - static_cast<long long>(lab.size())));
        const std::size_t su = static_cast<std::size_t>(s);
        axis.replace(su, std::min(lab.size(), axis.size() - su), lab);   // axis[s:s+len] = list(lab), growing past the end like a slice
    }
    out.push_back("          +" + std::string(nb, '-'));
    out.push_back("           " + axis);
    out.push_back("* median   : middle half of the paths   . 90 % of the paths   - stop and target");
    return out;
}

// ---------------------------------------------------------------- commands
inline void write_doc(const std::string& path, const Val& doc) {
    const std::string d = dirname(path);
    if (!d.empty()) makedirs(d);
    DumpOpts o; o.indent = 1;
    write_text(path, dumps(doc, o) + "\n");
}
inline Val compute(const Val& a) {
    const long long now = parse_now(get(a, "now"));
    const Val plan = load_plan(a, now);
    const std::string sym = pystr(get(plan, "symbol"));
    std::string text, source;
    if (truthy(get(a, "offline"))) { text = read_offline(S(a, "offline"), sym); source = "offline"; }
    else { text = fetch_chart(sym); source = "yahoo"; }
    return time_machine(text, plan, now, get(a, "replays").i, get(a, "seed").i, get(a, "window").i, get(a, "until"), source);
}
inline int cmd_run(const Val& a) {
    const Val doc = compute(a);
    const bool write = truthy(get(a, "write"));
    if (write) write_doc(S(a, "write"), doc);
    if (B(a, "json")) {
        Val slim = Val::dict(), mc = Val::dict();
        for (const auto& kv : get(doc, "mc").d->items) if (kv.first != "sample_paths") mc.d->set(kv.first, kv.second);
        for (const auto& kv : doc.d->items) slim.d->set(kv.first, kv.first == "mc" ? mc : kv.second);
        DumpOpts o; o.indent = 2; print(dumps(slim, o));
    } else {
        print(join(report(doc), "\n"));
        if (write) print("Wrote " + S(a, "write") + ".");
        print("Next: /trader-budget sizes it to your budget and your max loss per trade.");
    }
    return 0;
}
inline int cmd_chart(const Val& a) {
    Val doc;
    if (truthy(get(a, "from_file"))) {
        const std::string f = S(a, "from_file");
        if (!exists(f)) throw TMError("no " + f + ": run `timemachine run --write` first");
        try { doc = loads(read_text(f)); } catch (const PyErr&) { throw TMError(f + " is not a time machine result"); }
        const Val mc = doc.t == Val::DICT ? dget(doc, "mc") : Val();
        const Val bands = mc.t == Val::DICT ? dget(mc, "bands") : Val();
        if (mc.t != Val::DICT || !has(mc, "bands") || bands.t != Val::LIST || !has(doc, "plan")) throw TMError(f + " is not a time machine result");
    } else {
        doc = compute(a);
    }
    print(join(render_chart(doc), "\n"));
    std::vector<std::string> tail = {has(doc, "verdict") ? G(doc, "verdict") : ""};
    const Val notes = dget(doc, "verdict_notes");
    if (notes.t == Val::LIST) for (const Val& n : *notes.l) tail.push_back(pystr(n));
    print(join(tail, "\n"));
    return 0;
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("timemachine", "Time Machine: replay a planned day trade (trader-timemachine).");
    p.subcommands("cmd", false);
    const auto common = [](Parser& s) {
        s.opt({"--plan"}).def("trader/plan.json").help("the plan from trader-exit");
        s.opt({"--symbol"}).help("or give the plan as flags: ticker");
        s.opt({"--side"}).def("buy").choices({"buy", "sell"});
        s.opt({"--date"}).help("the plan's day, YYYY-MM-DD (default today, New York)");
        s.opt({"--entry"}).number(); s.opt({"--stop"}).number(); s.opt({"--target"}).number();
        s.opt({"--entry-time"}).help("the entry bar's close, e.g. \"10:15 AM\"");
        s.opt({"--setup"}).def("orb"); s.opt({"--or-minutes"}).integer().def(15);
        s.opt({"--stop-rule"}).def("auto").choices({"auto", "or_mid", "or_low", "or_high", "pct"});
        s.opt({"--replays"}).integer().def(10000); s.opt({"--seed"}).integer().def(19);
        s.opt({"--window"}).integer().def(2).help("draw from the same time of day +- this many bars");
        s.opt({"--until"}).help("end each replay at this time (default 16:00, the close)");
        s.opt({"--offline"}).metavar("FILE").help("a saved Yahoo chart JSON (.json or .json.gz), no network");
        s.opt({"--now"}).help("pretend it is this time (\"2026-10-19 10:15\" New York, or ISO with Z)");
    };
    { Parser& s = p.sub("run"); s.help_line = "replay the plan: 10,000 simulated sessions plus the history"; common(s);
      s.opt({"--json"}).store_true();
      s.opt({"--write"}).nargs("?").cnst(Val::str("trader/timemachine.json")).metavar("PATH").help("save the result (default trader/timemachine.json)"); }
    { Parser& s = p.sub("chart"); s.help_line = "ASCII fan chart of the replays"; common(s);
      s.opt({"--from"}).dest("from_file").nargs("?").cnst(Val::str("trader/timemachine.json")).metavar("PATH").help("draw a saved result instead of running again"); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { out(ap::help_text(ap::norm(p, p.prog))); return 1; }
    try {
        if (cmd == "run") return cmd_run(a);
        return cmd_chart(a);
    } catch (const TMError& e) { eprint(std::string("timemachine: ") + e.what()); return 1; }
}
PACKTOOL("daytrade-timemachine/timemachine", main);
}  // namespace packtools::daytrade_timemachine
