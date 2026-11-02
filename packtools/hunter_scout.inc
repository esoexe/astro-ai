// hunter_scout.inc - hunter-scout/jobs: Job Scout, Jake Schincariol's jobs.py (hunter-skill, MIT) in C++.
// Pulls open roles from public job boards (Greenhouse, Lever, Ashby, Workday, Eightfold), filters them to the
// user's titles and places, scores each 0-100 against hunter/profile.json with the reasons, writes hunter/jobs.json,
// and adds picks to hunter/pipeline.json. --offline DIR reads <type>-<token>.json files instead of the network.
// The regexes run on pyre.h (Python's re semantics: Unicode \d \s \w \b, lookbehind, IGNORECASE case folding) and
// html.unescape() is pyre.h's html_unescape() (the HTML5 entity table).
namespace packtools::hunter_scout {
using namespace pyrt;
struct ScoutError : std::runtime_error { using std::runtime_error::runtime_error; };
using hunter_followup::peq; using hunter_followup::in_list;

inline const char* UA = "hunter-skill/1.0 (job scout; +https://github.com/Jakeschincariol/hunter-skill)";
inline const std::vector<std::string> AUTO = {"greenhouse", "lever", "ashby", "workday", "eightfold"};
inline const std::string EMDASH = "\xE2\x80\x94", ENDASH = "\xE2\x80\x93";

inline const std::vector<std::pair<std::string, std::vector<std::string>>> TITLE_ALIASES = {
    {"software engineer", {"software development engineer", "software engineering", "sde", "swe", "software developer"}},
    {"backend engineer", {"back-end engineer", "backend software engineer", "server engineer"}},
    {"frontend engineer", {"front-end engineer", "frontend software engineer"}},
    {"machine learning engineer", {"ml engineer", "deep learning engineer"}},
    {"infrastructure engineer", {"platform engineer", "site reliability engineer", "sre"}},
};
inline const std::vector<std::pair<std::string, std::vector<std::string>>> SKILL_ALIASES = {
    {"go", {"golang"}}, {"javascript", {"js"}}, {"typescript", {"ts"}}, {"kubernetes", {"k8s"}},
    {"postgresql", {"postgres"}}, {"machine learning", {"ml"}}, {"google cloud", {"gcp"}},
    {"aws", {"amazon web services"}}, {"apis", {"api", "rest"}}, {"distributed systems", {"distributed system"}},
    {"c++", {"cpp"}},
};
inline const std::vector<std::string> US_STATES = split_ws(
    "alabama alaska arizona arkansas california colorado connecticut delaware florida georgia "
    "hawaii idaho illinois indiana iowa kansas kentucky louisiana maine maryland massachusetts "
    "michigan minnesota mississippi missouri montana nebraska nevada ohio oklahoma oregon "
    "pennsylvania tennessee texas utah vermont virginia washington wisconsin wyoming");
inline const std::vector<std::string> US_CODES = split_ws(
    "AL AK AZ AR CA CO CT DE FL GA HI ID IL IN IA KS KY LA ME MD MA MI MN MS MO MT NE NV NH NJ NM "
    "NY NC ND OH OK OR PA RI SC SD TN TX UT VT VA WA WV WI WY DC");
inline const std::vector<std::string> NON_US = {
    "europe", "emea", "apac", "united kingdom", "london", "uk", "ireland", "dublin", "india",
    "bengaluru", "bangalore", "hyderabad", "israel", "poland", "warsaw", "germany", "berlin",
    "canada", "toronto", "singapore", "japan", "tokyo", "netherlands", "amsterdam", "france",
    "paris", "spain", "madrid", "brazil", "mexico", "australia", "sydney", "korea", "taiwan",
    "china", "vietnam", "switzerland", "zurich", "sweden", "lisbon", "portugal"};
inline const std::vector<std::pair<std::string, std::vector<std::string>>> AREAS = {
    {"san francisco bay area", {"san francisco", "bay area", "mountain view", "palo alto", "menlo park",
                                "sunnyvale", "san jose", "santa clara", "los gatos", "oakland",
                                "redwood city", "san mateo", "south san francisco", "cupertino", "foster city"}},
    {"new york", {"new york", "nyc", "brooklyn"}},
    {"seattle", {"seattle", "bellevue", "redmond", "kirkland"}},
    {"los angeles", {"los angeles", "santa monica", "culver city", "playa vista"}},
};
template <class T> inline const std::vector<std::string>* lookup(const T& table, const std::string& k) {
    for (const auto& [key, v] : table) if (key == k) return &v;
    return nullptr;
}

// ---------------------------------------------------------------- Python's operations on JSON values
inline Val pget(const Val& v, const std::string& k, const Val& dflt = Val()) {   // v.get(k, dflt)
    if (v.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(v) + "' object has no attribute 'get'");
    const Val* x = v.d->get(k); return x ? *x : dflt;
}
inline Val item(const Val& v, const std::string& k) {   // v[k]
    if (v.t != Val::DICT) throw PyErr("TypeError", std::string("'") + type_name(v) + "' object is not subscriptable");
    const Val* x = v.d->get(k); if (!x) throw PyErr("KeyError", str_repr(k));
    return *x;
}
inline List each(const Val& v) {   // for x in v
    if (v.t == Val::LIST) return *v.l;
    if (v.t == Val::DICT) { List o; for (const auto& kv : v.d->items) o.push_back(Val::str(kv.first)); return o; }
    if (v.t == Val::STR) { List o; for (const auto& c : cps(v.s)) o.push_back(Val::str(c)); return o; }
    throw PyErr("TypeError", std::string("'") + type_name(v) + "' object is not iterable");
}
inline List or_each(const Val& v) { return truthy(v) ? each(v) : List{}; }   // for x in (v or [])
inline std::string str_of(const Val& v, const char* method) {   // v.method() where v must be a str
    if (v.t != Val::STR) throw PyErr("AttributeError", std::string("'") + type_name(v) + "' object has no attribute '" + method + "'");
    return v.s;
}
inline std::string or_str(const Val& v, const char* method) { return truthy(v) ? str_of(v, method) : std::string(); }   // (v or "").method()
inline std::string re_arg(const Val& v) {   // a str for re.sub / re.search
    if (v.t != Val::STR) throw PyErr("TypeError", std::string("expected string or bytes-like object, got '") + type_name(v) + "'");
    return v.s;
}
inline std::string cat(const std::string& a, const Val& b) {   // a + b with b a str
    if (b.t != Val::STR) throw PyErr("TypeError", std::string("can only concatenate str (not \"") + type_name(b) + "\") to str");
    return a + b.s;
}
inline bool hashable(const Val& v) { return v.t != Val::LIST && v.t != Val::DICT; }
inline void need_hash(const Val& v) { if (!hashable(v)) throw PyErr("TypeError", std::string("unhashable type: '") + type_name(v) + "'"); }
inline bool num(const Val& v) { return v.t == Val::INT || v.t == Val::FLOAT || v.t == Val::BOOL; }
inline std::size_t py_slice_end(std::size_t size, long long n) {   // len(x[:n])
    if (n >= 0) return std::min<std::size_t>(size, static_cast<std::size_t>(n));
    const long long e = static_cast<long long>(size) + n; return e > 0 ? static_cast<std::size_t>(e) : 0;
}
// Unicode decimal digits as ASCII (what float() and int() accept)
inline std::string ascii_digits(const std::string& s) {
    std::string o;
    for (uint32_t c : to_u32(s)) o += (c >= 0x80 && uprop::decimal(c)) ? std::string(1, static_cast<char>('0' + hunter_followup::nd_value(c))) : utf8(c);
    return o;
}
inline long long py_int_digits(const std::string& s) {   // int() of a run of \d
    const auto r = parse_int(ascii_digits(s));
    if (!r) throw PyErr("OverflowError", "int too large to convert");
    return *r;
}
inline std::optional<double> py_float(const Val& v) {   // float(v), None on TypeError / ValueError
    if (num(v)) return v.num();
    if (v.t != Val::STR) return std::nullopt;
    return parse_float(ascii_digits(v.s));
}
inline Val int_of_double(double x) {   // int(x) for a finite float, exactly (big ints keep every digit)
    if (std::fabs(x) < 9.0e18) return Val::integer(static_cast<long long>(x));
    const auto [digits, k] = exact_decimal(x);
    Val v = Val::integer(0); v.s = (x < 0 ? "-" : "") + digits.substr(0, digits.size() - static_cast<std::size_t>(k)); return v;
}

// ---------------------------------------------------------------- small helpers
inline Val load_json(const std::string& path, const Val& dflt = Val()) {
    std::error_code ec;
    if (!path.empty() && std::filesystem::is_regular_file(path, ec)) return loads(read_text(path));
    return dflt;
}
inline void save_json(const std::string& path, const Val& data) {
    const std::string folder = dirname(path);
    if (!folder.empty()) makedirs(folder);
    DumpOpts o; o.indent = 2; o.ensure_ascii = false;
    write_text(path, dumps(data, o) + "\n");
}
inline std::string slug(const std::string& text, std::size_t limit = 48) {
    std::string s = re::sub("[^a-z0-9]+", "-", lower(text));
    std::size_t a = 0, b = s.size(); while (a < b && s[a] == '-') a++; while (b > a && s[b - 1] == '-') b--;
    s = s.substr(a, b - a);
    if (s.size() > limit) { s = s.substr(0, limit); const auto k = s.rfind('-'); if (k != std::string::npos) s = s.substr(0, k); }
    return s;
}
inline std::string clean_title(const Val& t) {
    std::string s = or_str(t, "replace");
    s = replace_all(replace_all(s, EMDASH, "-"), ENDASH, "-");
    return strip(re::sub(R"(\s+)", " ", s));
}
inline std::string html_text(const Val& v) {   // HTML (possibly escaped twice, as Greenhouse sends it) -> plain text
    if (truthy(v) && v.t != Val::STR) throw PyErr("TypeError", std::string("argument of type '") + type_name(v) + "' is not iterable");
    std::string s = html_unescape(truthy(v) ? v.s : std::string());
    if (s.find("&lt;") != std::string::npos || s.find("&amp;") != std::string::npos) s = html_unescape(s);
    s = re::sub(R"((?is)<(script|style).*?</\1>)", " ", s);
    s = re::sub(R"((?i)<br\s*/?>|</p>|</li>|</h\d>)", "\n", s);
    s = re::sub(R"(<[^>]+>)", " ", s);
    s = replace_all(s, "\xC2\xA0", " ");
    return strip(re::sub(R"([ \t]+)", " ", re::sub(R"(\n\s*\n+)", "\n", s)));
}
inline std::optional<long long> parse_day(const Val& value) {   // '2026-10-15' or an ISO stamp -> date
    const std::string s = truthy(value) ? pystr(value) : std::string();
    const auto m = re::match(R"((\d{4})-(\d{2})-(\d{2}))", s);
    if (!m) return std::nullopt;
    return py_date(py_int_digits(m.group(1)), py_int_digits(m.group(2)), py_int_digits(m.group(3)));
}
inline std::optional<long long> epoch_day(const Val& value, bool millis = false) {   // datetime.fromtimestamp(v, utc).date()
    const auto f = py_float(value);
    if (!f) return std::nullopt;
    double v = *f; if (millis) v /= 1000.0;
    if (std::isnan(v)) throw PyErr("ValueError", "Invalid value NaN (not a number)");
    if (std::isinf(v)) throw PyErr("OverflowError", "cannot convert float infinity to integer");
    double ip = 0; double fp = std::modf(v, &ip) * 1e6; fp = std::nearbyint(fp);   // microseconds, half to even
    if (fp >= 1e6) ip += 1.0; else if (fp < 0) ip -= 1.0;
    if (ip < -62135596800.0 || ip > 253402300799.0) throw PyErr("ValueError", "year is out of range");
    return static_cast<long long>(std::floor(ip / 86400.0));
}
inline Val money(const std::string& token) {   // int or None
    std::string t = lower(replace_all(replace_all(token, ",", ""), " ", ""));
    const int mult = ends(t, "k") ? 1000 : 1;
    while (ends(t, "k")) t.pop_back();
    const auto f = parse_float(ascii_digits(t));
    if (!f) return Val();
    const double x = *f * mult;
    if (!std::isfinite(x)) throw PyErr("OverflowError", "cannot convert float infinity to integer");
    return int_of_double(static_cast<double>(std::nearbyint(x)));
}
inline const std::string& pay_range_rx() {
    static const std::string MONEY = R"(\$\s?(\d{1,3}(?:,\d{3})+(?:\.\d+)?|\d+(?:\.\d+)?\s?[kK]?))";
    static const std::string rx = MONEY + R"(\s*(?:USD)?\s*(?:-|)" + ENDASH + "|" + EMDASH + R"(|to)\s*)" + MONEY;
    return rx;
}
inline bool lt_num(const Val& a, const Val& b) {   // a < b, exact for ints of any size
    if (a.t == Val::INT && b.t == Val::INT && !a.big() && !b.big()) return a.i < b.i;
    if (a.t == Val::INT && b.t == Val::INT) {
        const std::string x = a.big() ? a.s : std::to_string(a.i), y = b.big() ? b.s : std::to_string(b.i);
        const bool nx = x[0] == '-', ny = y[0] == '-';
        if (nx != ny) return nx;
        const std::string ax = nx ? x.substr(1) : x, ay = ny ? y.substr(1) : y;
        const bool less = ax.size() != ay.size() ? ax.size() < ay.size() : ax < ay;
        return nx ? (!less && ax != ay) : less;
    }
    return hunter_followup::plt(a, b);
}
inline Val pay_from_text(const Val& tv) {   // the first '$196,000 to $245,000' style range, or None
    const U32 s = to_u32(truthy(tv) ? re_arg(tv) : std::string());
    const auto m = re::compile(pay_range_rx(), re::I).search(s);
    if (!m) return Val();
    const Val lo = money(m.group(1)), hi = money(m.group(2));
    if (!truthy(lo) || !truthy(hi) || lt_num(hi, lo)) return Val();
    const auto e = static_cast<std::size_t>(m.end());
    const std::string tail = lower(from_u32(s, e, e + 30));
    std::string period = "year";
    if (lt_num(hi, Val::integer(1000)) || tail.find("hour") != std::string::npos || tail.find("/hr") != std::string::npos) period = "hour";
    Val p = Val::dict(); p.d->set("min", lo); p.d->set("max", hi); p.d->set("currency", Val::str("USD")); p.d->set("period", Val::str(period));
    return p;
}
inline Val mul(const Val& a, long long m) {   // a * m
    if (a.t == Val::FLOAT) return Val::flt(a.f * static_cast<double>(m));
    if (a.t == Val::BOOL) return Val::integer(a.b ? m : 0);
    if (a.t == Val::INT) {
        if (!a.big()) { long long r = 0; if (!__builtin_mul_overflow(a.i, m, &r)) return Val::integer(r); }
        return Val::flt(a.num() * static_cast<double>(m));   // past 2**63: only compared, never printed
    }
    if (a.t == Val::STR) { std::string o; for (long long k = 0; k < m; ++k) o += a.s; return Val::str(o); }
    if (a.t == Val::LIST) { List o; for (long long k = 0; k < m; ++k) o.insert(o.end(), a.l->begin(), a.l->end()); return Val::list(o); }
    throw PyErr("TypeError", std::string("unsupported operand type(s) for *: '") + type_name(a) + "' and 'int'");
}
inline Val annual(const Val& pay) {
    if (!truthy(pay)) return Val();
    const Val per = pget(pay, "period"); need_hash(per);
    const long long mult = peq(per, "month") ? 12 : peq(per, "hour") ? 2080 : 1;
    return mul(item(pay, "max"), mult);
}
inline std::string int_commas(const Val& n) {   // "{:,}".format(int(n))
    Val v;
    if (n.t == Val::INT) v = n;
    else if (n.t == Val::BOOL) v = Val::integer(n.b ? 1 : 0);
    else if (n.t == Val::FLOAT) {
        if (std::isnan(n.f)) throw PyErr("ValueError", "cannot convert float NaN to integer");
        if (std::isinf(n.f)) throw PyErr("OverflowError", "cannot convert float infinity to integer");
        v = int_of_double(std::trunc(n.f));
    } else if (n.t == Val::STR) {
        const auto r = parse_int(ascii_digits(n.s));
        if (!r) throw PyErr("ValueError", "invalid literal for int() with base 10: " + str_repr(n.s));
        v = Val::integer(*r);
    } else throw PyErr("TypeError", std::string("int() argument must be a string, a bytes-like object or a real number, not '") + type_name(n) + "'");
    const std::string d = v.big() ? v.s : std::to_string(v.i);
    return commas(d);
}
inline std::string fmt_money(const Val& n, const Val& currency = Val::str("USD")) {
    const Val cur = truthy(currency) ? currency : Val::str("USD");
    need_hash(cur);
    std::string sign;
    if (peq(cur, "USD")) sign = "$"; else if (peq(cur, "GBP")) sign = "\xC2\xA3"; else if (peq(cur, "EUR")) sign = "\xE2\x82\xAC";
    return (sign.empty() ? cat("", cur) + " " : sign) + int_commas(n);
}
inline std::string fmt_pay(const Val& pay) {
    const Val per = pget(pay, "period");
    const std::string tail = peq(per, "year") ? std::string() : cat(" a ", pay.d->has("period") ? per : Val::str("year"));
    return fmt_money(item(pay, "min"), pget(pay, "currency")) + " to " + fmt_money(item(pay, "max"), pget(pay, "currency")) + tail;
}

// ---------------------------------------------------------------- fetching
struct Fetcher {   // polite HTTP: identifies itself, pauses between calls; offline reads files
    std::string offline; double pause = 0.5; long timeout = 40; int calls = 0;
    std::chrono::steady_clock::time_point last{};
    bool off() const { return !offline.empty(); }
    Val get(const std::string& url, const std::string& name, const Val* body = nullptr) {
        if (off()) {
            const std::string path = path_join(offline, name);
            std::error_code ec;
            if (!std::filesystem::is_regular_file(path, ec)) throw ScoutError("offline: no file " + name);
            return loads(read_text(path));
        }
        const auto gap = std::chrono::duration<double>(std::chrono::steady_clock::now() - last).count();
        if (calls && gap < pause) std::this_thread::sleep_for(std::chrono::duration<double>(pause - gap));
        last = std::chrono::steady_clock::now(); calls++;
        std::vector<std::string> headers = {std::string("User-Agent: ") + UA, "Accept: application/json"};   // gzip: the engine's client asks for it and decodes
        HttpResp r;
        if (body) { headers.push_back("Content-Type: application/json"); r = http_post(url, dumps(*body), headers, timeout); }
        else r = http_get(url, headers, timeout);
        if (!r.error.empty()) throw ScoutError("could not reach " + url + ": " + r.error);
        if (r.status >= 400) throw ScoutError("HTTP " + std::to_string(r.status) + " for " + url);
        try { return loads(r.body); } catch (const PyErr&) { throw ScoutError("not JSON from " + url); }
    }
};

// ---------------------------------------------------------------- normalizers
inline Val make_job(const Val& board, const Val& source_id, const Val& title, const Val& location, const Val& url,
                    std::optional<long long> posted, const Val& text = Val::str(""), bool remote = false, const Val& pay = Val(), const Val& note = Val()) {
    const Val company = item(board, "company");
    const std::string sid = re::sub("[^a-z0-9]", "", lower(pystr(source_id)));
    const std::string ct = clean_title(title);
    const std::string loc = truthy(location) ? strip(re::sub(R"(\s+)", " ", re_arg(location))) : std::string();
    Val j = Val::dict();
    j.d->set("id", Val::str(slug(or_str(company, "lower"), 20) + "-" + slug(ct, 40) + "-" + (sid.size() <= 10 ? sid : sid.substr(sid.size() - 8))));
    j.d->set("company", company);
    j.d->set("title", Val::str(ct));
    j.d->set("location", Val::str(loc.empty() ? "not listed" : loc));
    j.d->set("url", url);
    j.d->set("source", item(board, "type"));
    j.d->set("posted", posted ? Val::str(date_iso(*posted)) : Val());
    j.d->set("remote", Val::boolean(remote));
    j.d->set("pay", pay);
    j.d->set("_text", truthy(text) ? text : Val::str(""));
    j.d->set("_sid", Val::str(pystr(source_id)));
    if (truthy(note)) j.d->set("posted_note", note);
    return j;
}
inline std::string join_strs(const std::string& sep, const List& parts) {   // sep.join(parts), every part a str
    std::string o;
    for (std::size_t k = 0; k < parts.size(); ++k) {
        if (parts[k].t != Val::STR) throw PyErr("TypeError", "sequence item " + std::to_string(k) + ": expected str instance, " + type_name(parts[k]) + " found");
        if (k) o += sep;
        o += parts[k].s;
    }
    return o;
}
inline List from_greenhouse(const Val& board, const Val& data) {
    List out;
    for (const Val& j : each(pget(data, "jobs", Val::list()))) {
        const Val locd = truthy(pget(j, "location")) ? pget(j, "location") : Val::dict();
        const Val loc = pget(locd, "name", Val::str(""));
        List names; for (const Val& o : each(pget(j, "offices", Val::list()))) if (truthy(pget(o, "name"))) names.push_back(pget(o, "name", Val::str("")));
        const std::string offices = join_strs(", ", names);
        const Val text = Val::str(html_text(pget(j, "content", Val::str(""))));
        const bool remote = lower(cat("", loc) + " " + offices).find("remote") != std::string::npos;
        const Val fp = pget(j, "first_published");
        out.push_back(make_job(board, pget(j, "id"), pget(j, "title"), loc, pget(j, "absolute_url"),
                               parse_day(truthy(fp) ? fp : pget(j, "updated_at")), text, remote, pay_from_text(text)));
    }
    return out;
}
inline List from_lever(const Val& board, const Val& data) {
    List out;
    if (data.t != Val::LIST) return out;
    for (const Val& p : *data.l) {
        const Val catg = truthy(pget(p, "categories")) ? pget(p, "categories") : Val::dict();
        List parts; for (const char* k : {"descriptionPlain", "descriptionBodyPlain", "additionalPlain"}) { const Val x = pget(p, k); parts.push_back(truthy(x) ? x : Val::str("")); }
        std::string text = join_strs("\n", parts);
        for (const Val& lst : or_each(pget(p, "lists"))) text = cat(cat(text + "\n", pget(lst, "text", Val::str(""))) + "\n", Val::str(html_text(pget(lst, "content", Val::str("")))));
        Val pay;
        const Val rng = pget(p, "salaryRange");
        if (truthy(rng) && truthy(pget(rng, "min")) && truthy(pget(rng, "max"))) {
            pay = Val::dict(); pay.d->set("min", item(rng, "min")); pay.d->set("max", item(rng, "max"));
            pay.d->set("currency", pget(rng, "currency", Val::str("USD")));
            pay.d->set("period", Val::str(pystr(pget(rng, "interval", Val::str("year"))).find("year") != std::string::npos ? "year" : "hour"));
        }
        if (!truthy(pay)) pay = pay_from_text(Val::str(text));
        const Val cl = pget(catg, "location");
        const bool remote = peq(pget(p, "workplaceType"), "remote") || lower(or_str(cl, "lower")).find("remote") != std::string::npos;
        out.push_back(make_job(board, pget(p, "id"), pget(p, "text"), cl, pget(p, "hostedUrl"), epoch_day(pget(p, "createdAt"), true), Val::str(text), remote, pay));
    }
    return out;
}
inline List from_ashby(const Val& board, const Val& data) {
    List out;
    for (const Val& j : each(pget(data, "jobs", Val::list()))) {
        const Val listed = pget(j, "isListed");
        if (listed.t == Val::BOOL && !listed.b) continue;
        Val pay;
        const Val compd = truthy(pget(j, "compensation")) ? pget(j, "compensation") : Val::dict();
        for (const Val& comp : or_each(pget(compd, "summaryComponents", Val::list()))) {
            if (peq(pget(comp, "compensationType"), "Salary") && truthy(pget(comp, "minValue")) && truthy(pget(comp, "maxValue"))) {
                const std::string interval = upper(pystr(pget(comp, "interval", Val::str("1 YEAR"))));
                pay = Val::dict(); pay.d->set("min", item(comp, "minValue")); pay.d->set("max", item(comp, "maxValue"));
                const Val cc = pget(comp, "currencyCode");
                pay.d->set("currency", truthy(cc) ? cc : Val::str("USD"));
                pay.d->set("period", Val::str(interval.find("MONTH") != std::string::npos ? "month" : interval.find("HOUR") != std::string::npos ? "hour" : "year"));
            }
        }
        Val loc = truthy(pget(j, "location")) ? pget(j, "location") : Val::str("");
        std::size_t extra = 0;
        for (const Val& s : or_each(pget(j, "secondaryLocations"))) if (truthy(pget(s, "location"))) extra++;
        if (extra) loc = Val::str(cat("", loc) + " (+" + std::to_string(extra) + " more)");
        const bool remote = lower(or_str(pget(j, "workplaceType"), "lower")) == "remote";
        const Val dp = pget(j, "descriptionPlain");
        const Val text = truthy(dp) ? dp : Val::str(html_text(pget(j, "descriptionHtml", Val::str(""))));
        out.push_back(make_job(board, pget(j, "id"), pget(j, "title"), loc, pget(j, "jobUrl"), parse_day(pget(j, "publishedAt")), text, remote,
                               truthy(pay) ? pay : pay_from_text(text)));
    }
    return out;
}
inline std::pair<std::optional<long long>, Val> workday_posted(const Val& text, long long today) {   // 'Posted Today' / 'Posted 30+ Days Ago'
    const std::string t = lower(or_str(text, "lower"));
    if (t.find("today") != std::string::npos) return {today, Val()};
    if (t.find("yesterday") != std::string::npos) return {today - 1, Val()};
    const auto m = re::search(R"((\d+)(\+?)\s*days?)", t);
    if (m) {
        const long long days = py_int_digits(m.group(1));
        if (days > 999999999 || today - days < -719162) throw PyErr("OverflowError", "date value out of range");
        return {today - days, m.group(2).empty() ? Val() : Val::str(std::to_string(days) + "+ days ago")};
    }
    return {std::nullopt, Val()};
}
inline std::string replace_n(const std::string& s, const std::string& a, const std::string& b, int n) {   // s.replace(a, b, n)
    std::string o; std::size_t k = 0; int done = 0;
    while (done < n) { const std::size_t f = s.find(a, k); if (f == std::string::npos) break; o += s.substr(k, f - k) + b; k = f + a.size(); done++; }
    return o + s.substr(k);
}
inline List from_workday(const Val& board, const Val& data, long long today) {
    List out;
    const std::string base = "https://" + pystr(item(board, "host")) + "/" + pystr(item(board, "site"));
    for (const Val& p : each(pget(data, "jobPostings", Val::list()))) {
        const std::string path = or_str(pget(p, "externalPath"), "rsplit");
        Val sid;
        const Val bf = pget(p, "bulletFields");
        if (truthy(bf)) {
            if (bf.t == Val::LIST) sid = (*bf.l)[0];
            else if (bf.t == Val::STR) sid = Val::str(cps(bf.s)[0]);
            else throw PyErr("KeyError", "0");
        } else { const auto k = path.rfind('_'); sid = Val::str(k == std::string::npos ? path : path.substr(k + 1)); }
        std::string loc = or_str(pget(p, "locationsText"), "lower");
        const auto m = re::match("/job/([^/]+)/", path);
        if (m && re::match(R"(\d+ Locations)", loc)) {
            const long long n = py_int_digits(split_ws(loc)[0]);
            loc = replace_all(replace_n(m.group(1), "-", ", ", 2), "-", " ") + " (+" + std::to_string(n - 1) + " more)";
        }
        const auto [posted, note] = workday_posted(pget(p, "postedOn"), today);
        const bool remote = lower(loc).find("remote") != std::string::npos;
        out.push_back(make_job(board, sid, pget(p, "title"), Val::str(loc), Val::str(base + path), posted, Val::str(""), remote, Val(), note));
    }
    return out;
}
inline List from_eightfold(const Val& board, const Val& data) {
    List out;
    for (const Val& p : each(pget(data, "positions", Val::list()))) {
        Val locs = pget(p, "locations");
        if (!truthy(locs)) { const Val l = pget(p, "location"); locs = Val::list({truthy(l) ? l : Val::str("")}); }
        List parts; for (const Val& l : each(locs)) if (truthy(l)) parts.push_back(Val::str(replace_all(str_of(l, "replace"), ",", ", ")));
        const std::string loc = join_strs("; ", parts);
        const Val wlo = pget(p, "work_location_option");
        // "remote_local" means remote near an office, so it is not counted as remote
        const bool remote = (truthy(wlo) ? pystr(wlo) : std::string()) == "remote" || lower(loc).find("remote") != std::string::npos;
        const Val cu = pget(p, "canonicalPositionUrl");
        const Val url = truthy(cu) ? cu : Val::str("https://" + pystr(item(board, "host")) + "/careers/job/" + pystr(pget(p, "id")));
        const Val pn = pget(p, "posting_name");
        // the search feed carries no description: skills are checked on the posting itself
        out.push_back(make_job(board, pget(p, "id"), truthy(pn) ? pn : pget(p, "name"), Val::str(loc), url, epoch_day(pget(p, "t_create")), Val::str(""), remote));
    }
    return out;
}

// ---------------------------------------------------------------- scoring
inline std::string norm(const Val& s) { return " " + strip(re::sub("[^a-z0-9+#]+", " ", lower(or_str(s, "lower")))) + " "; }
inline bool has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
inline std::pair<long long, Val> title_points(const std::string& title, const Val& roles) {   // (points 0-35, reason)
    const std::string t = norm(Val::str(title));
    long long best = 0; Val why;
    for (const Val& role : or_each(roles)) {
        const std::string r = strip(norm(role));
        if (r.empty()) continue;
        std::vector<std::string> names = {r};
        if (const auto* al = lookup(TITLE_ALIASES, r)) names.insert(names.end(), al->begin(), al->end());
        for (const auto& n : names) if (has(t, " " + n + " ")) return {35, Val::str("title matches " + pystr(role))};
        std::vector<std::string> words; for (const auto& w : split_ws(r)) if (w.size() > 2) words.push_back(w);
        long long pts = 0; Val reason;
        bool all = !words.empty(); for (const auto& w : words) if (!has(t, " " + w + " ")) all = false;
        if (all) { pts = 30; reason = Val::str("title has every word of " + pystr(role)); }
        else {
            std::size_t hit = 0; for (const auto& w : words) if (has(t, " " + w + " ")) hit++;
            pts = words.empty() ? 0 : static_cast<long long>(std::nearbyint(20.0 * static_cast<double>(hit) / static_cast<double>(words.size())));
            if (hit) reason = Val::str("title partly matches " + pystr(role));
        }
        if (pts > best) { best = pts; why = reason; }
    }
    return {best, why};
}
inline Val excluded(const std::string& title, const Val& words) {   // the first exclude word in the title, or None
    const std::string t = norm(Val::str(title));
    for (const Val& w : or_each(words))
        if (re::search("(?<![a-z0-9])" + re::escape(strip(norm(w))) + "(?:s|ship|ships)?(?![a-z0-9])", t)) return w;
    return Val();
}
inline List skill_hits(const std::string& text, const Val& skills) {
    List hits;
    for (const Val& s : or_each(skills)) {
        std::vector<std::string> names = {str_of(s, "lower")};
        if (const auto* al = lookup(SKILL_ALIASES, lower(s.s))) names.insert(names.end(), al->begin(), al->end());
        for (const auto& n : names) {
            bool hit;
            if (cps(n).size() <= 3 && isupper_str(cpslice(n, 0, 1)))
                hit = static_cast<bool>(re::search("(?<![A-Za-z0-9])" + re::escape(n) + "(?![A-Za-z0-9+#])", text));
            else
                hit = static_cast<bool>(re::search("(?<![a-z0-9])" + re::escape(lower(n)) + "(?![a-z0-9+#])", text, re::I));
            if (hit) { hits.push_back(s); break; }
        }
    }
    return hits;
}
inline bool is_us(const std::string& loc) {
    const std::string low = lower(loc);
    if (re::search(R"(united states|\bu\.s\.|\busa\b|\bus\b|remote \(u\.?s)", low)) return true;
    for (const auto& s : US_STATES) if (re::search("\\b" + s + "\\b", low)) return true;
    static const std::string codes = [] { std::string o; for (const auto& c : US_CODES) o += (o.empty() ? "" : "|") + c; return o; }();
    return static_cast<bool>(re::search("\\b(?:" + codes + ")\\b", loc));
}
inline bool non_us(const std::string& loc) {
    const std::string low = lower(loc);
    for (const auto& w : NON_US) if (re::search("\\b" + re::escape(w) + "\\b", low)) return true;
    return false;
}
inline Val place_match(const std::string& loc, const Val& places) {
    const std::string low = lower(loc);
    for (const Val& p : or_each(places)) {
        const std::string city = lower(strip(split(str_of(p, "split"), ",")[0]));
        std::vector<std::string> names;
        if (!city.empty()) { const auto* a = lookup(AREAS, city); names = a ? *a : std::vector<std::string>{city}; }
        for (const auto& [area, members] : AREAS)
            if (std::find(members.begin(), members.end(), city) != members.end() || city == area) names = members;
        for (const auto& n : names) if (!n.empty() && has(low, n)) return p;
    }
    return Val();
}
inline std::optional<long long> years_asked(const Val& text) {
    const auto m = re::search(R"((\d{1,2})\s*\+?\s*(?:-\s*\d{1,2}\s*)?(?:years|yrs))", truthy(text) ? re_arg(text) : std::string(), re::I);
    if (!m) return std::nullopt;
    return py_int_digits(m.group(1));
}
// Python's <, >= on JSON values (ints exact, str by code point, lists in order); other mixes raise TypeError
inline int py_cmp(const Val& a, const Val& b, const char* op) {   // -1, 0, 1; 2 = unordered (NaN)
    if (num(a) && num(b)) {
        const auto as_int = [](const Val& v) { return v.t == Val::BOOL ? Val::integer(v.b ? 1 : 0) : v; };
        const Val x = as_int(a), y = as_int(b);
        if (x.t == Val::INT && y.t == Val::INT) return lt_num(x, y) ? -1 : lt_num(y, x) ? 1 : 0;
        const double p = x.num(), q = y.num();
        if (std::isnan(p) || std::isnan(q)) return 2;
        return p < q ? -1 : p > q ? 1 : 0;
    }
    if (a.t == Val::STR && b.t == Val::STR) return a.s < b.s ? -1 : a.s > b.s ? 1 : 0;
    if (a.t == Val::LIST && b.t == Val::LIST) {
        const std::size_t n = std::min(a.l->size(), b.l->size());
        for (std::size_t k = 0; k < n; ++k) if (!hunter_followup::peq((*a.l)[k], (*b.l)[k])) return py_cmp((*a.l)[k], (*b.l)[k], op);
        return a.l->size() < b.l->size() ? -1 : a.l->size() > b.l->size() ? 1 : 0;
    }
    throw PyErr("TypeError", std::string("'") + op + "' not supported between instances of '" + type_name(a) + "' and '" + type_name(b) + "'");
}
inline bool py_gt(const Val& a, const Val& b) { const int c = py_cmp(a, b, ">"); return c == 1; }
inline bool py_lt(const Val& a, const Val& b) { const int c = py_cmp(a, b, "<"); return c == -1; }
inline bool py_ge(const Val& a, const Val& b) { const int c = py_cmp(a, b, ">="); return c == 1 || c == 0; }
inline Val plus_one(const Val& v) {   // v + 1
    if (v.t == Val::FLOAT) return Val::flt(v.f + 1.0);
    if (v.t == Val::BOOL) return Val::integer(v.b ? 2 : 1);
    if (v.t == Val::INT) {
        if (!v.big() && v.i < LLONG_MAX) return Val::integer(v.i + 1);
        Val r = Val::integer(0); r.s = std::to_string(static_cast<unsigned long long>(LLONG_MAX) + 1ULL);   // only reachable for huge ints
        if (v.big()) r.s = v.s;   // a big int plus one compares like itself against a two-digit asked
        return r;
    }
    if (v.t == Val::STR) throw PyErr("TypeError", "can only concatenate str (not \"int\") to str");
    if (v.t == Val::LIST) throw PyErr("TypeError", "can only concatenate list (not \"int\") to list");
    throw PyErr("TypeError", std::string("unsupported operand type(s) for +: '") + type_name(v) + "' and 'int'");
}
struct Score { long long score; List why, warn; };
inline Score score(const Val& job, const Val& profile, std::optional<long long> today) {
    const Val roles = truthy(pget(profile, "target_roles")) ? pget(profile, "target_roles") : Val::list();
    List why, warn;
    const std::string title = str_of(item(job, "title"), "lower");
    const auto [t_pts, t_why] = title_points(title, roles);
    if (truthy(t_why)) why.push_back(t_why);

    const Val jtext = pget(job, "_text", Val::str(""));
    const std::string text = cat(title + "\n", jtext);
    const Val skills = truthy(pget(profile, "skills")) ? pget(profile, "skills") : Val::list();
    const List hits = skill_hits(text, skills);
    long long s_pts;
    if (!strip(str_of(jtext, "strip")).empty()) {
        const double nsk = static_cast<double>(std::max<std::size_t>(1, std::min<std::size_t>(6, each(skills).size())));
        s_pts = static_cast<long long>(std::nearbyint(30.0 * std::min(1.0, static_cast<double>(hits.size()) / nsk)));
        if (!hits.empty()) {
            List six(hits.begin(), hits.begin() + static_cast<long>(std::min<std::size_t>(6, hits.size())));
            why.push_back(Val::str(std::to_string(hits.size()) + " of your skills in the posting: " + join_strs(", ", six)));
        } else warn.push_back(Val::str("none of your skills appear in the posting"));
    } else {
        s_pts = std::max<long long>(10, static_cast<long long>(std::nearbyint(30.0 * std::min(1.0, static_cast<double>(hits.size()) / 6.0))));
        warn.push_back(Val::str("the feed has no description, skills not checked: open the posting"));
    }

    const Val locv = item(job, "location");
    const std::string loc = or_str(locv, "lower");
    const Val place = place_match(loc, pget(profile, "locations"));
    const Val wa = truthy(pget(profile, "work_authorization")) ? pget(profile, "work_authorization") : Val::dict();
    const bool us_only = hunter_followup::peq(pget(wa, "country", Val::str("US")), "US");
    const auto foreign = [&] { return us_only && non_us(loc) && !is_us(loc); };
    long long l_pts;
    const bool wants_remote = truthy(pget(profile, "remote", Val::boolean(true)));
    if (truthy(pget(job, "remote")) && !foreign()) {
        l_pts = 15;
        why.push_back(Val::str(wants_remote ? "remote (" + pystr(locv) + ")" : "remote"));
        if (!wants_remote) l_pts = 8;
    } else if (truthy(place)) {
        l_pts = 15; why.push_back(Val::str("in " + pystr(place) + ", one of your locations"));
    } else if (truthy(pget(profile, "relocate")) && (is_us(loc) || !us_only) && !foreign()) {
        l_pts = 8; why.push_back(Val::str("in " + pystr(locv) + " (you are open to relocating)"));
    } else {
        l_pts = 0; warn.push_back(Val::str(pystr(locv) + " is outside your locations"));
    }

    const Val years = pget(profile, "years_experience");
    const auto asked = years_asked(pget(job, "_text", Val::str("")));
    const bool senior = static_cast<bool>(re::search(R"(\b(senior|sr\.?)\b)", title, re::I));
    long long y_pts;
    if (asked && !years.is_none() && py_gt(Val::integer(*asked), plus_one(years))) {
        y_pts = 3; warn.push_back(Val::str("asks for " + std::to_string(*asked) + "+ years, you have " + pystr(years)));
    } else if (senior && !years.is_none() && py_lt(years, Val::integer(5))) {
        y_pts = 4; warn.push_back(Val::str("senior title, you have " + pystr(years) + " years"));
    } else y_pts = 10;

    const Val floor = pget(profile, "salary_floor"), pay = pget(job, "pay");
    long long p_pts;
    if (truthy(pay) && truthy(floor)) {
        if (py_ge(annual(pay), floor)) {
            p_pts = 5; why.push_back(Val::str("pays " + fmt_pay(pay) + ", clears your " + fmt_money(floor, pget(pay, "currency")) + " floor"));
        } else {
            p_pts = 0; warn.push_back(Val::str("pays " + fmt_pay(pay) + ", under your " + fmt_money(floor, pget(pay, "currency")) + " floor"));
        }
    } else p_pts = 2;

    const auto posted = parse_day(pget(job, "posted"));
    long long f_pts;
    if (posted) {
        if (!today) throw PyErr("TypeError", "unsupported operand type(s) for -: 'NoneType' and 'datetime.date'");
        const long long age = *today - *posted;
        const Val note = pget(job, "posted_note");
        const std::string when = truthy(note) ? pystr(note) : age <= 0 ? "today" : age == 1 ? "yesterday" : std::to_string(age) + " days ago";
        f_pts = age <= 7 ? 5 : age <= 30 ? 3 : 1;
        (age <= 30 ? why : warn).push_back(Val::str("posted " + when));
    } else f_pts = 2;

    std::vector<std::string> dream; for (const Val& c : or_each(pget(profile, "dream_companies"))) dream.push_back(lower(str_of(c, "lower")));
    const std::string company = lower(str_of(item(job, "company"), "lower"));
    if (std::find(dream.begin(), dream.end(), company) != dream.end()) why.push_back(Val::str("one of your dream companies"));
    return {std::min<long long>(100, t_pts + s_pts + l_pts + y_pts + p_pts + f_pts), why, warn};
}

// ---------------------------------------------------------------- fetching one board
inline std::vector<std::string> title_keywords(const Val& roles) {
    std::vector<std::string> out;
    for (const Val& rv : or_each(roles)) {
        const std::string r = strip(str_of(rv, "strip"));
        if (r.empty()) continue;
        bool dup = false; for (const auto& o : out) if (lower(o) == lower(r)) dup = true;
        if (!dup) out.push_back(r);
    }
    if (out.size() > 3) out.resize(3);
    if (out.empty()) out.push_back("Software Engineer");
    return out;
}
inline std::string quote(const std::string& s) {   // urllib.parse.quote(s), safe='/'
    static const char* HEX = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '_' || c == '.' || c == '-' || c == '~' || c == '/') o += static_cast<char>(c);
        else { o += '%'; o += HEX[c >> 4]; o += HEX[c & 15]; }
    }
    return o;
}
inline Val workday_body(const std::string& kw, int limit) {
    Val b = Val::dict(); b.d->set("appliedFacets", Val::dict()); b.d->set("limit", Val::integer(limit));
    b.d->set("offset", Val::integer(0)); b.d->set("searchText", Val::str(kw)); return b;
}
inline std::string workday_url(const Val& b) { return "https://" + pystr(item(b, "host")) + "/wday/cxs/" + pystr(item(b, "tenant")) + "/" + pystr(item(b, "site")) + "/jobs"; }
inline std::string eightfold_url(const Val& b, const std::string& q, int num) {
    return "https://" + pystr(item(b, "host")) + "/api/apply/v2/jobs?domain=" + pystr(item(b, "domain")) + "&query=" + q + "&start=0&num=" + std::to_string(num);
}
inline const std::string GH = "https://boards-api.greenhouse.io/v1/boards/";
inline List fetch_board(Fetcher& f, const Val& board, std::optional<long long> today, const Val& roles, long long details, List& notes, const Val& exclude) {
    const Val kind = item(board, "type"), tokv = pget(board, "token");
    const std::string token = pystr(tokv);
    try {
        if (peq(kind, "greenhouse")) {
            List jobs = from_greenhouse(board, f.get(GH + token + "/jobs", "greenhouse-" + token + ".json"));
            // the list has no descriptions: fetch details for title matches only (newest first)
            List need;
            for (const Val& j : jobs)
                if (!truthy(item(j, "_text")) && title_points(item(j, "title").s, roles).first >= 15 && !truthy(excluded(item(j, "title").s, exclude))) need.push_back(j);
            const auto key = [](const Val& j) { const Val p = item(j, "posted"); return truthy(p) ? p : Val::str(""); };
            std::stable_sort(need.begin(), need.end(), [&](const Val& a, const Val& b) { return py_lt(key(b), key(a)); });
            need.resize(py_slice_end(need.size(), details));
            for (Val& j : need) {
                const std::string jid = pystr(item(j, "_sid"));
                Val one;
                try { one = f.get(GH + token + "/jobs/" + jid, "greenhouse-" + token + "-" + jid + ".json"); }
                catch (const ScoutError&) { continue; }
                Val wrap = Val::dict(); wrap.d->set("jobs", Val::list({one}));
                const List full = from_greenhouse(board, wrap);
                if (!full.empty()) { j.d->set("_text", item(full[0], "_text")); j.d->set("pay", item(full[0], "pay")); }
            }
            return jobs;
        }
        if (peq(kind, "lever")) return from_lever(board, f.get("https://api.lever.co/v0/postings/" + token + "?mode=json", "lever-" + token + ".json"));
        if (peq(kind, "ashby")) return from_ashby(board, f.get("https://api.ashbyhq.com/posting-api/job-board/" + token + "?includeCompensation=true", "ashby-" + token + ".json"));
        if (peq(kind, "workday") || peq(kind, "eightfold")) {
            const bool wd = peq(kind, "workday");
            List jobs; std::vector<Val> seen;
            const std::string url = wd ? workday_url(board) : std::string();
            for (const auto& kw : title_keywords(roles)) {
                List got;
                if (wd) { const Val body = workday_body(kw, 20); got = from_workday(board, f.get(url, "workday-" + token + ".json", &body), today ? *today : throw PyErr("TypeError", "unsupported operand type(s) for -: 'NoneType' and 'datetime.timedelta'")); }
                else got = from_eightfold(board, f.get(eightfold_url(board, quote(kw), 20), "eightfold-" + token + ".json"));
                for (const Val& j : got) {
                    const Val id = item(j, "id");
                    bool dup = false; for (const Val& x : seen) if (peq(x, id)) dup = true;
                    if (!dup) { seen.push_back(id); jobs.push_back(j); }
                }
                if (f.off()) break;
            }
            return jobs;
        }
        notes.push_back(Val::str(pystr(item(board, "company")) + ": no feed (" + pystr(kind) + ")"));
    } catch (const ScoutError& e) {
        notes.push_back(Val::str(pystr(item(board, "company")) + " (" + pystr(kind) + " " + token + "): " + e.what()));
    }
    return {};
}

// ---------------------------------------------------------------- search
struct Picked { List automatic, manual, unknown; };
inline bool in_vals(const List& l, const Val& v) { for (const Val& x : l) if (peq(x, v)) return true; return false; }
inline Picked pick_boards(const Val& boards, const Val& companies, bool use_all, const Val& profile, const List& extra) {
    List allb = each(boards); allb.insert(allb.end(), extra.begin(), extra.end());
    std::vector<std::pair<std::string, Val>> by;
    for (const Val& b : allb) {
        const std::string k = lower(str_of(item(b, "company"), "lower"));
        bool found = false; for (auto& kv : by) if (kv.first == k) { kv.second = b; found = true; }
        if (!found) by.push_back({k, b});
    }
    List wanted;
    if (truthy(companies)) wanted = each(companies);
    else {
        List names; for (const Val& b : allb) names.push_back(item(b, "company"));
        if (use_all) wanted = names;
        else {
            const Val prof = truthy(profile) ? profile : Val::dict();
            const Val dream = pget(prof, "dream_companies");
            wanted = truthy(dream) ? each(dream) : names;
        }
    }
    Picked r;
    for (const Val& name : wanted) {
        const std::string k = lower(str_of(name, "lower"));
        const Val* b = nullptr; for (const auto& kv : by) if (kv.first == k) b = &kv.second;
        if (!b || !truthy(*b)) r.unknown.push_back(name);
        else if (in_list(item(*b, "type"), AUTO)) { if (!in_vals(r.automatic, *b)) r.automatic.push_back(*b); }
        else if (!in_vals(r.manual, *b)) r.manual.push_back(*b);
    }
    return r;
}
inline Val parse_board_flag(const Val& text) {   // 'Acme=greenhouse:acme' -> board dict
    const auto m = re::match(R"(\s*([^=]+)=(greenhouse|lever|ashby):([A-Za-z0-9_.\-]+)\s*$)", truthy(text) ? re_arg(text) : std::string());
    if (!m) throw ScoutError("--board wants Company=greenhouse|lever|ashby:token, got " + repr(text));
    Val b = Val::dict(); b.d->set("company", Val::str(strip(m.group(1)))); b.d->set("type", Val::str(m.group(2))); b.d->set("token", Val::str(m.group(3)));
    return b;
}
struct Opts { Val roles, places; bool remote_only = false; std::optional<long long> days; long long min_score = 0, limit = 25, details = 10; bool any_title = false; };
struct Result { long long scanned = 0, matched = 0; Val dropped; List jobs; };
inline Result search(Fetcher& f, const List& boards, const Val& profile, std::optional<long long> today, const Opts& o, List& notes) {
    Val roles = o.roles;
    if (!truthy(roles)) roles = pget(profile, "target_roles");
    if (!truthy(roles)) roles = Val::list();
    Val prof = Val::dict(); for (const auto& [k, v] : profile.d->items) prof.d->set(k, v);
    prof.d->set("target_roles", roles);
    if (truthy(o.places)) prof.d->set("locations", o.places);
    Result r;
    long long d_title = 0, d_excl = 0, d_place = 0, d_age = 0, d_score = 0;
    List kept;
    for (const Val& b : boards) {
        const List jobs = fetch_board(f, b, today, roles, o.details, notes, pget(profile, "exclude_titles"));
        r.scanned += static_cast<long long>(jobs.size());
        for (const Val& jc : jobs) {
            Val j = jc;
            const std::string title = item(j, "title").s;
            if (!o.any_title && title_points(title, roles).first < 15) { d_title++; continue; }
            if (truthy(excluded(title, pget(profile, "exclude_titles")))) { d_excl++; continue; }
            if (o.remote_only && !truthy(item(j, "remote"))) { d_place++; continue; }
            if (truthy(o.places) && !truthy(item(j, "remote")) && !truthy(place_match(item(j, "location").s, o.places))) { d_place++; continue; }
            const auto posted = parse_day(pget(j, "posted"));
            if (o.days && posted) {
                if (!today) throw PyErr("TypeError", "unsupported operand type(s) for -: 'NoneType' and 'datetime.date'");
                if (*today - *posted > *o.days) { d_age++; continue; }
            }
            const Score s = score(j, prof, today);
            if (s.score < o.min_score) { d_score++; continue; }
            j.d->set("score", Val::integer(s.score));
            List parts = s.why; for (const Val& w : s.warn) parts.push_back(Val::str("note: " + w.s));
            j.d->set("why", Val::str(join_strs("; ", parts)));
            kept.push_back(j);
        }
    }
    const auto low = [](const Val& j, const char* k) { return lower(str_of(item(j, k), "lower")); };
    std::stable_sort(kept.begin(), kept.end(), [&](const Val& a, const Val& b) {
        const long long sa = item(a, "score").i, sb = item(b, "score").i;
        if (sa != sb) return sa > sb;
        const std::string ca = low(a, "company"), cb = low(b, "company");
        if (ca != cb) return ca < cb;
        return low(a, "title") < low(b, "title");
    });
    std::vector<std::string> seen; List ranked;
    for (const Val& j : kept) {
        const std::string key = low(j, "company") + '\0' + low(j, "title") + '\0' + low(j, "location");
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
        seen.push_back(key); ranked.push_back(j);
    }
    r.matched = static_cast<long long>(ranked.size());
    Val dropped = Val::dict();
    dropped.d->set("title", Val::integer(d_title)); dropped.d->set("excluded", Val::integer(d_excl)); dropped.d->set("place", Val::integer(d_place));
    dropped.d->set("age", Val::integer(d_age)); dropped.d->set("score", Val::integer(d_score));
    r.dropped = dropped;
    r.jobs.assign(ranked.begin(), ranked.begin() + static_cast<long>(py_slice_end(ranked.size(), o.limit)));
    return r;
}
inline Val public_job(const Val& job) {
    Val o = Val::dict();
    for (const char* k : {"id", "company", "title", "location", "url", "source", "posted", "score", "why", "remote", "pay"}) o.d->set(k, pget(job, k));
    if (truthy(pget(job, "posted_note"))) o.d->set("posted_note", item(job, "posted_note"));
    return o;
}
inline std::string ljust(const std::string& s, std::size_t w) { const std::size_t n = cps(s).size(); return n >= w ? s : s + std::string(w - n, ' '); }
inline std::string render(const Result& r, long long nboards, const List& jobs, const List& manual, const List& notes, long long today, const std::string& out_path) {
    std::vector<std::string> lines;
    lines.push_back("Scanned " + commas(std::to_string(r.scanned)) + " listings on " + std::to_string(nboards) + " board" + (nboards == 1 ? "" : "s") +
                    " on " + date_iso(today) + ": " + std::to_string(r.matched) + " match your titles and filters.");
    long long i = 0;
    for (const Val& j : jobs) {
        ++i;
        char head[64]; std::snprintf(head, sizeof head, "%2lld. %3lld  ", i, item(j, "score").i);
        const Val pn = pget(j, "posted_note"), po = pget(j, "posted");
        const std::string when = truthy(pn) ? pystr(pn) : truthy(po) ? pystr(po) : "date unknown";
        lines.push_back(head + pystr(item(j, "company")) + ", " + pystr(item(j, "title")) + "  (" + pystr(item(j, "location")) + ")  " + when);
        lines.push_back("         " + pystr(item(j, "why")));
        lines.push_back("         " + pystr(item(j, "url")) + "   id: " + pystr(item(j, "id")));
    }
    if (jobs.empty()) lines.push_back("No listing passed the filters. Widen --title, --location or --days, or add boards.");
    for (const Val& b : manual)
        lines.push_back("Manual: " + pystr(item(b, "company")) + ". " + pystr(pget(b, "why_manual", Val::str(""))) + " " + pystr(pget(b, "route", Val::str(""))) + " " + pystr(pget(b, "careers", Val::str(""))));
    for (const Val& n : notes) lines.push_back("note: " + pystr(n));
    if (!out_path.empty()) lines.push_back("Saved to " + out_path + ". Add the ones you want to the pipeline: jobs.py pick <id> ...");
    lines.push_back("Fit scores rank listings against your profile; they are not a prediction of an offer.");
    std::string o; for (std::size_t k = 0; k < lines.size(); ++k) o += (k ? "\n" : "") + lines[k];
    return o + "\n";
}

// ---------------------------------------------------------------- pipeline
inline Val empty_pipeline() { Val p = Val::dict(); p.d->set("roles", Val::list()); p.d->set("contacts", Val::list()); p.d->set("threads", Val::list()); return p; }
inline std::pair<List, List> pick(const Val& jobs_doc, const List& ids, Val& pipeline, std::optional<long long> today) {   // (added, skipped)
    if (pipeline.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(pipeline) + "' object has no attribute 'setdefault'");
    for (const char* k : {"roles", "contacts", "threads"}) if (!pipeline.d->has(k)) pipeline.d->set(k, Val::list());
    std::vector<std::pair<Val, Val>> by_id;
    for (const Val& j : each(pget(jobs_doc, "jobs", Val::list()))) {
        const Val id = item(j, "id"); need_hash(id);
        bool found = false; for (auto& kv : by_id) if (peq(kv.first, id)) { kv.second = j; found = true; }
        if (!found) by_id.push_back({id, j});
    }
    Val roles = item(pipeline, "roles");
    List have_ids, have_urls;
    for (const Val& r : each(roles)) { const Val id = pget(r, "id"); need_hash(id); if (!in_vals(have_ids, id)) have_ids.push_back(id); }
    for (const Val& r : each(roles)) if (truthy(pget(r, "url"))) { const Val u = pget(r, "url"); need_hash(u); have_urls.push_back(u); }
    List added, skipped;
    for (const Val& i : ids) {
        Val j; for (const auto& kv : by_id) if (peq(kv.first, i)) j = kv.second;
        if (!truthy(j)) { skipped.push_back(Val::str(pystr(i) + ": not in jobs.json")); continue; }
        const Val jid = item(j, "id");
        bool dup = in_vals(have_ids, jid);
        if (!dup) { const Val u = item(j, "url"); need_hash(u); dup = in_vals(have_urls, u); }
        if (dup) { skipped.push_back(Val::str(pystr(i) + ": already in the pipeline")); continue; }
        Val role = Val::dict();
        role.d->set("id", jid); role.d->set("company", item(j, "company")); role.d->set("title", item(j, "title")); role.d->set("url", item(j, "url"));
        role.d->set("stage", Val::str("saved")); role.d->set("applied", Val());
        role.d->set("next_step", Val::str("Research the company (/hunter-research) and tailor the resume (/hunter-resume)"));
        if (!today) throw PyErr("AttributeError", "'NoneType' object has no attribute 'isoformat'");
        role.d->set("next_date", Val::str(date_iso(*today)));
        role.d->set("notes", Val::str(date_iso(*today) + ": Scout fit " + pystr(pget(j, "score")) + ": " + pystr(pget(j, "why", Val::str("")))));
        hunter_followup::append(roles, role);
        have_ids.push_back(jid);
        added.push_back(jid);
    }
    return {added, skipped};
}

// ---------------------------------------------------------------- check
inline std::string check_board(Fetcher& f, const Val& b) {   // one small live call that proves the token resolves
    const Val kind = item(b, "type");
    const std::string token = pystr(pget(b, "token"));
    if (peq(kind, "greenhouse")) {
        const Val d = f.get(GH + token, "greenhouse-board-" + token + ".json");
        return "ok  greenhouse " + ljust(token, 12) + " board name: " + pystr(pget(d, "name"));
    }
    if (peq(kind, "lever")) {
        const Val d = f.get("https://api.lever.co/v0/postings/" + token + "?mode=json&limit=1", "lever-" + token + ".json");
        return "ok  lever " + ljust(token, 12) + " " + (truthy(d) ? "has postings" : "EMPTY (no open postings or wrong token)");
    }
    if (peq(kind, "ashby")) {
        const Val d = f.get("https://api.ashbyhq.com/posting-api/job-board/" + token + "?includeCompensation=true", "ashby-" + token + ".json");
        const Val jobs = pget(d, "jobs", Val::list());
        if (jobs.t != Val::LIST && jobs.t != Val::DICT && jobs.t != Val::STR) throw PyErr("TypeError", std::string("object of type '") + type_name(jobs) + "' has no len()");
        return "ok  ashby " + ljust(token, 12) + " " + std::to_string(each(jobs).size()) + " postings";
    }
    if (peq(kind, "workday")) {
        const Val body = workday_body("", 1);
        const Val d = f.get(workday_url(b), "workday-" + token + ".json", &body);
        return "ok  workday " + ljust(token, 12) + " " + pystr(pget(d, "total")) + " postings";
    }
    if (peq(kind, "eightfold")) {
        const Val d = f.get(eightfold_url(b, "", 1), "eightfold-" + token + ".json");
        return "ok  eightfold " + ljust(token, 10) + " " + pystr(pget(d, "count")) + " postings";
    }
    return "manual " + pystr(item(b, "company")) + ": " + pystr(pget(b, "careers"));
}

// ---------------------------------------------------------------- CLI
inline std::optional<long long> today_of(const Val& v) { return truthy(v) ? parse_day(v) : parse_day(Val::str(today_iso())); }
inline int main(const std::vector<std::string>& argv) {
    const std::string BOARDS = skill_file("boards.json");
    Parser p("jobs", "Job Scout: public job boards ranked by fit to hunter/profile.json.");
    p.subcommands("cmd", false);
    { Parser& s = p.sub("search"); s.help_line = "fetch, filter, score, write hunter/jobs.json";
      s.opt({"--profile"}).def(Val::str("hunter/profile.json"));
      s.opt({"--boards"}).def(Val::str(BOARDS));
      s.opt({"--board"}).append().def(Val::list()).help("extra board: 'Company=greenhouse:token'");
      s.opt({"--company"}).append().def(Val::list()).help("only these companies (repeat)");
      s.opt({"--all"}).store_true().help("every automatic board in boards.json");
      s.opt({"--title"}).append().def(Val::list()).help("target titles (default: profile target_roles)");
      s.opt({"--any-title"}).store_true().help("keep every title, just score it");
      s.opt({"--location"}).append().def(Val::list()).help("keep these places (plus remote)");
      s.opt({"--remote"}).store_true().help("remote roles only");
      s.opt({"--days"}).integer().help("posted within N days");
      s.opt({"--min-score"}).integer().def(Val::integer(0));
      s.opt({"--limit"}).integer().def(Val::integer(25));
      s.opt({"--details"}).integer().def(Val::integer(10)).help("Greenhouse descriptions fetched per board");
      s.opt({"--today"}).help("YYYY-MM-DD (default: today)");
      s.opt({"--out"}).def(Val::str("hunter/jobs.json"));
      s.opt({"--offline"}).metavar("DIR");
      s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("pick"); s.help_line = "add listings from jobs.json to hunter/pipeline.json";
      s.pos("ids").nargs("+");
      s.opt({"--jobs"}).def(Val::str("hunter/jobs.json"));
      s.opt({"--pipeline"}).def(Val::str("hunter/pipeline.json"));
      s.opt({"--today"}); }
    { Parser& s = p.sub("boards"); s.help_line = "list bundled boards: automatic or manual, and why";
      s.opt({"--boards"}).def(Val::str(BOARDS)); }
    { Parser& s = p.sub("check"); s.help_line = "verify board tokens live (one small call each)";
      s.opt({"--boards"}).def(Val::str(BOARDS));
      s.opt({"--company"}).append().def(Val::list());
      s.opt({"--board"}).append().def(Val::list());
      s.opt({"--offline"}).metavar("DIR"); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { out(ap::help_text(ap::norm(p, p.prog))); return 2; }

    if (cmd == "pick") {
        const std::string jobs_path = S(a, "jobs");
        const Val jobs_doc = load_json(jobs_path);
        if (!truthy(jobs_doc)) { eprint("pick: no " + jobs_path + " yet. Run `jobs.py search` first."); return 2; }
        const std::string pipe_path = S(a, "pipeline");
        Val pipeline = load_json(pipe_path);
        if (!truthy(pipeline)) pipeline = empty_pipeline();
        const auto [added, skipped] = pick(jobs_doc, *get(a, "ids").l, pipeline, today_of(get(a, "today")));
        if (!added.empty()) save_json(pipe_path, pipeline);
        for (const Val& x : added) print("added " + pystr(x) + " (stage: saved)");
        for (const Val& x : skipped) print("skipped " + pystr(x));
        return added.empty() ? 1 : 0;
    }

    Val dflt = Val::dict(); dflt.d->set("boards", Val::list());
    const Val doc = load_json(S(a, "boards"), dflt);
    if (cmd == "boards") {
        for (const Val& bd : each(item(doc, "boards"))) {
            if (in_list(item(bd, "type"), AUTO))
                print("auto    " + ljust(pystr(item(bd, "company")), 11) + " " + ljust(pystr(item(bd, "type")), 10) + " " + pystr(pget(bd, "token")));
            else {
                print("manual  " + ljust(pystr(item(bd, "company")), 11) + " " + pystr(pget(bd, "careers")));
                print("        why: " + pystr(pget(bd, "why_manual")));
                print("        route: " + pystr(pget(bd, "route")));
            }
        }
        print("Verified " + pystr(pget(doc, "verified")) + ". Re-check any time with `jobs.py check`.");
        return 0;
    }

    List extra;
    try { for (const Val& x : *get(a, "board").l) extra.push_back(parse_board_flag(x)); }
    catch (const ScoutError& e) { eprint(std::string("error: ") + e.what()); return 2; }
    const Val companies = get(a, "company");
    const Val offline = get(a, "offline");
    Fetcher f; f.offline = truthy(offline) ? offline.s : std::string();

    if (cmd == "check") {
        Picked pk = pick_boards(item(doc, "boards"), companies, !truthy(companies), Val(), extra);
        if (!extra.empty() && !truthy(companies)) {
            List keep; for (const Val& x : pk.automatic) if (in_vals(extra, x)) keep.push_back(x);
            if (!keep.empty()) pk.automatic = keep;
        }
        f.pause = 1.0;
        int bad = 0;
        for (const Val& bd : pk.automatic) {
            try { print(check_board(f, bd)); }
            catch (const ScoutError& e) {
                bad++;
                print("FAIL " + pystr(item(bd, "company")) + " " + pystr(item(bd, "type")) + " " + pystr(pget(bd, "token")) + ": " + e.what());
            }
        }
        for (const Val& bd : pk.manual) print("manual " + pystr(item(bd, "company")) + ": no feed to check, search " + pystr(pget(bd, "careers")));
        for (const Val& n : pk.unknown) print("unknown company " + pystr(n) + ": add it with --board 'Name=greenhouse:token'");
        return bad || !pk.unknown.empty() ? 1 : 0;
    }

    const std::string prof_path = S(a, "profile");
    const Val profile = load_json(prof_path);
    if (profile.is_none()) {
        eprint("search: no " + prof_path + ". Copy skills/hunter-apply/profile.example.json to hunter/profile.json and fill in your own details first.");
        return 2;
    }
    const auto today = today_of(get(a, "today"));
    Picked pk = pick_boards(item(doc, "boards"), companies, B(a, "all"), profile, extra);
    if (!extra.empty() && !truthy(companies)) for (const Val& x : extra) if (!in_vals(pk.automatic, x)) pk.automatic.push_back(x);
    List notes;
    for (const Val& u : pk.unknown) notes.push_back(Val::str(pystr(u) + " is not in boards.json: add it with --board 'Name=greenhouse:token' (or lever/ashby)"));
    if (profile.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(profile) + "' object has no attribute 'get'");
    Opts o;
    o.roles = truthy(get(a, "title")) ? get(a, "title") : Val();
    o.places = truthy(get(a, "location")) ? get(a, "location") : Val();
    o.remote_only = B(a, "remote");
    if (!get(a, "days").is_none()) o.days = get(a, "days").i;
    o.min_score = get(a, "min_score").i; o.limit = get(a, "limit").i; o.details = get(a, "details").i;
    o.any_title = B(a, "any_title");
    const Result r = search(f, pk.automatic, profile, today, o, notes);
    if (!today) throw PyErr("AttributeError", "'NoneType' object has no attribute 'isoformat'");
    Val outd = Val::dict();
    outd.d->set("as_of", Val::str(date_iso(*today)));
    outd.d->set("scanned", Val::integer(r.scanned));
    outd.d->set("matched", Val::integer(r.matched));
    List names; for (const Val& b : pk.automatic) names.push_back(item(b, "company"));
    outd.d->set("boards", Val::list(names));
    List man; for (const Val& m : pk.manual) { Val x = Val::dict(); x.d->set("company", item(m, "company")); x.d->set("careers", pget(m, "careers")); x.d->set("route", pget(m, "route")); man.push_back(x); }
    outd.d->set("manual", Val::list(man));
    List pub; for (const Val& j : r.jobs) pub.push_back(public_job(j));
    outd.d->set("jobs", Val::list(pub));
    const std::string out_path = S(a, "out");
    if (!out_path.empty()) save_json(out_path, outd);
    if (B(a, "json")) {
        Val j = Val::dict(); for (const auto& [k, v] : outd.d->items) j.d->set(k, v);
        j.d->set("notes", Val::list(notes));
        DumpOpts jo; jo.indent = 2;
        print(dumps(j, jo));
    } else out(render(r, static_cast<long long>(pk.automatic.size()), pub, pk.manual, notes, *today, out_path));
    return pub.empty() ? 1 : 0;
}
PACKTOOL("hunter-scout/jobs", main);
}  // namespace packtools::hunter_scout
