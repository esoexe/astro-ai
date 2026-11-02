// master-virality/collect - turn pasted posts, CSV/TSV exports and yt-dlp JSON into one clean list for swipe.
// C++ port of master-skills/skills/virality/tools/collect.py (Jake Schincariol, MIT); same output, same exit codes:
// 0 wrote at least one usable post, 1 nothing usable, 2 bad input. It reads files you already have and never
// fetches anything.
namespace packtools::master_collect {
using namespace pyrt;
using namespace packtools::master;

inline const std::vector<std::string> FIELDS = {"channel", "title", "hook", "views", "likes", "comments", "shares", "saves",
                                                "url", "duration", "posted", "platform", "followers", "median"};
inline const std::vector<std::pair<std::string, std::vector<std::string>>> ALIASES = {
    {"channel", {"channel", "creator", "account", "handle", "author", "username", "user", "page", "profile", "owner", "channel_name", "uploader_id", "uploader"}},
    {"title", {"title", "caption", "text", "description", "headline", "post", "content", "post_text", "video_title", "fulltitle"}},
    {"hook", {"hook", "first_line", "spoken_hook", "opening", "opener", "first_words"}},
    {"views", {"views", "view_count", "plays", "play_count", "playcount", "video_views", "impressions", "viewcount", "plays_count"}},
    {"likes", {"likes", "like_count", "reactions", "hearts", "digg_count", "diggcount", "favorites", "reaction_count"}},
    {"comments", {"comments", "comment_count", "replies"}},
    {"shares", {"shares", "share_count", "reposts", "repost_count", "sends"}},
    {"saves", {"saves", "save_count", "bookmarks", "collects", "collect_count"}},
    {"url", {"url", "link", "webpage_url", "permalink", "post_url", "video_url", "original_url", "short_url"}},
    {"duration", {"duration", "length", "duration_seconds", "video_length", "seconds", "runtime"}},
    {"posted", {"posted", "date", "upload_date", "published", "published_at", "created", "create_time", "release_date", "timestamp", "posted_at", "publish_time"}},
    {"platform", {"platform", "network", "site", "source", "extractor_key", "ie_key"}},
    {"followers", {"followers", "follower_count", "channel_follower_count", "subscribers", "subscriber_count", "fans"}},
    {"median", {"median", "baseline", "median_views", "typical_views"}},
};
inline bool in_lookup(const std::string& k) {   // k in _LOOKUP
    for (const auto& [f, names] : ALIASES)
        for (const auto& n : names)
            if (n == k) return true;
    return false;
}

inline const char* COUNT_RE = R"(^(\d+(?:[.,\s]\d+)*)\s*(k|m|b|bn|mil|million|thousand|billion)?$)";
// Only a real comment-for-DM call to action is stripped ("Comment X and I'll send you...", "Comment X for the prompt").
inline const char* CTA_PREFIX_RE =
    "^\\s*(?:comment|reply(?: with)?|type|drop|dm me)\\b[^.!?\\n]{0,60}?"
    "(?:\\b(?:and|&)\\s+I(?:'|\xE2\x80\x99)?(?:ll| will)\\s+(?:send|dm|message|share|give)\\b[^.!?\\n]{0,60}?"
    "|\\bfor (?:the|my|a|your)\\b[^.!?\\n]{0,40}?)"
    "(?:\xF0\x9F\x91\x87|\xE2\xAC\x87\xEF\xB8\x8F?|[.!:]|\\s-\\s)+\\s*";
inline const char* SENTENCE_END_RE = "(?<=[.!?])\\s+|\\n+|\\s+[\xF0\x9F\x91\x87\xE2\xAC\x87]";

inline std::string norm_key(const std::string& k) { return strip_chars(re::sub("[^a-z0-9]+", "_", lower(strip(k))), "_"); }
inline std::string collapse(const std::string& s) { return strip(re::sub("\\s+", " ", s)); }   // re.sub(r"\s+", " ", s).strip()
inline std::string rstrip_chars(const std::string& s, const std::string& chars) {
    const auto cs = cps(s); std::size_t b = cs.size();
    while (b > 0 && chars.find(cs[b - 1]) != std::string::npos) --b;
    std::string o; for (std::size_t k = 0; k < b; ++k) o += cs[k];
    return o;
}
inline std::string lstrip_at(const std::string& s) { std::size_t k = 0; while (k < s.size() && s[k] == '@') ++k; return s.substr(k); }   // .lstrip("@")
inline std::string ascii_num(const std::string& s) {   // float() reads any Unicode decimal digit
    std::string o;
    for (uint32_t c : ucase_detail::u32(s)) {
        if (c > 127 && uprop::decimal(c)) o += static_cast<char>('0' + digit_value(c));
        else o += utf8(c);
    }
    return o;
}
inline Val int_of_digits(const std::string& digits) {   // int("000123") -> 123, exact for any length
    std::size_t k = 0; while (k + 1 < digits.size() && digits[k] == '0') ++k;
    const std::string d = digits.substr(k);
    if (d.size() <= 18) return Val::integer(std::stoll(d));
    Val v = Val::integer(0); v.s = d; return v;
}
inline Val round_to_int(double x) {   // int(round(x)), half to even, exact for big floats
    if (std::isnan(x)) throw PyErr("ValueError", "cannot convert float NaN to integer");
    if (std::isinf(x)) throw PyErr("OverflowError", "cannot convert float infinity to integer");
    const double r = std::nearbyint(x);
    if (std::fabs(r) < 9e18) return Val::integer(static_cast<long long>(r));
    Val v = Val::integer(0); v.s = fmt_f(r, 0); return v;
}
inline std::optional<double> float_of(const std::string& s) { return parse_float(ascii_num(s)); }

// -------------------------------------------------------------------------- parsing
// 1.2M -> 1200000, 412K -> 412000, '12,400 views' -> 12400, 1.234 -> 1234. None if not a count.
inline Val parse_count(const Val& v) {
    if (v.is_none() || v.t == Val::BOOL) return Val();
    if (v.t == Val::INT) return (v.big() ? v.s[0] != '-' : v.i >= 0) ? v : Val();
    if (v.t == Val::FLOAT) return v.f >= 0 ? round_to_int(v.f) : Val();
    std::string s = lower(strip(pystr(v)));
    s = re::sub(R"(\b(views?|plays?|likes?|comments?|shares?|saves?|reactions?|followers?|subscribers?|subs)\b)", "", s);
    s = strip(replace_all(replace_all(replace_all(s, "+", ""), "~", ""), "\xC2\xA0", " "));
    if (s.empty() || s == "-" || s == "n/a" || s == "na" || s == "none" || s == "null" || s == "?") return Val();
    const re::Match m = re::match(COUNT_RE, s);
    if (!m) return Val();
    std::string num = m.group(1); const std::string suf = m.group(2);
    double mult = 1;
    if (suf == "k" || suf == "thousand") mult = 1e3;
    else if (suf == "m" || suf == "mil" || suf == "million") mult = 1e6;
    else if (suf == "b" || suf == "bn" || suf == "billion") mult = 1e9;
    num = replace_all(num, " ", "");
    if (mult != 1) {
        // With a suffix a single separator is a decimal point: 1.2M, or 1,2M in Europe.
        const auto commas = std::count(num.begin(), num.end(), ',');
        if (commas && num.find('.') != std::string::npos) num = replace_all(num, ",", "");
        else if (commas == 1 && cplen(split(num, ",")[1]) <= 2) num = replace_all(num, ",", ".");
        else num = replace_all(num, ",", "");
        const auto f = float_of(num);
        if (!f) return Val();
        return round_to_int(*f * mult);
    }
    if (re::match(R"(^\d{1,3}([.,])\d{3}(\1\d{3})*$)", num)) return int_of_digits(ascii_digits(re::sub("[.,]", "", num)));   // 12,400 or 1.234.567
    if (re::match(R"(^\d+,\d{1,2}$)", num)) return round_to_int(*float_of(replace_all(num, ",", ".")));                    // 3,5 (decimal comma)
    const auto f = float_of(replace_all(num, ",", ""));
    if (!f) return Val();
    return round_to_int(*f);
}

inline double hms(const re::Match& m) {   // int(h or 0) * 3600 + int(mi or 0) * 60 + float(se or 0)
    const double h = m.has(1) ? std::strtod(ascii_digits(m.group(1)).c_str(), nullptr) : 0;
    const double mi = m.has(2) ? std::strtod(ascii_digits(m.group(2)).c_str(), nullptr) : 0;
    const double se = m.has(3) ? *float_of(m.group(3)) : 0.0;
    return (h * 3600 + mi * 60) + se;
}
// 42, '0:42', '1:02:03', '42s', '1m 20s', 'PT1M20S' -> seconds (float).
inline Val parse_duration(const Val& v) {
    if (v.is_none() || v.t == Val::BOOL) return Val();
    if (v.is_num()) { const double x = v.num(); return x >= 0 ? Val::flt(x) : Val(); }
    const std::string s = lower(strip(pystr(v)));
    if (s.empty()) return Val();
    re::Match m = re::match(R"(^pt(?:(\d+)h)?(?:(\d+)m)?(?:(\d+(?:\.\d+)?)s)?$)", s);
    if (m && (m.has(1) || m.has(2) || m.has(3))) return Val::flt(hms(m));
    if (re::match(R"(^\d+(?::\d{1,2}){1,2}(?:\.\d+)?$)", s)) {
        std::vector<double> parts; for (const auto& p : split(s, ":")) parts.push_back(*float_of(p));
        while (parts.size() < 3) parts.insert(parts.begin(), 0.0);
        return Val::flt(parts[0] * 3600 + parts[1] * 60 + parts[2]);
    }
    m = re::match(R"(^(?:(\d+)\s*h(?:ours?|rs?)?)?\s*(?:(\d+)\s*m(?:in(?:utes?|s)?)?)?\s*(?:(\d+(?:\.\d+)?)\s*s(?:ec(?:onds?|s)?)?)?$)", s);
    if (m && (m.has(1) || m.has(2) || m.has(3))) return Val::flt(hms(m));
    const auto f = float_of(s);
    return f ? Val::flt(*f) : Val();
}

inline bool leap(long long y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
inline int month_days(long long y, int m) { static const int D[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}; return m == 2 && leap(y) ? 29 : D[m - 1]; }
inline std::string ymd(long long y, long long m, long long d) { char b[64]; std::snprintf(b, sizeof b, "%04lld-%02lld-%02lld", y, m, d); return b; }
// datetime.fromtimestamp(t, tz=utc).strftime("%Y-%m-%d") for whole seconds t
inline std::string utc_day(long long t) {
    long long z = t / 86400; if (t % 86400 < 0) --z;
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const long long doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    const long long d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9, y = yoe + era * 400 + (m <= 2);
    if (y - 1900 > 2147483647LL) throw PyErr("OSError", "[Errno 75] Value too large for defined data type");
    if (y < 1 || y > 9999) throw PyErr("ValueError", "year " + std::to_string(y) + " is out of range");
    return ymd(y, m, d);
}
inline long long timestamp_seconds(double d) {   // the whole seconds CPython keeps (microseconds rounded half to even)
    double ip = 0; double fp = std::modf(d, &ip);
    fp = std::nearbyint(fp * 1e6);
    if (fp >= 1e6) ip += 1.0;
    else if (fp < 0) ip -= 1.0;
    if (!(ip >= -9.2233720368547758e18 && ip < 9.2233720368547758e18)) throw PyErr("OverflowError", "timestamp out of range for platform time_t");
    return static_cast<long long>(ip);
}
// datetime.strptime(s, fmt) for the formats parse_date tries (C locale month names, IGNORECASE, \s+ for each space)
inline std::optional<std::string> strptime_ymd(const std::string& s, const std::string& fmt) {
    static const std::vector<std::string> ABBR = {"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};
    static const std::vector<std::string> FULL = {"january", "february", "march", "april", "may", "june", "july", "august", "september", "october", "november", "december"};
    const std::string b = "(jan|feb|mar|apr|may|jun|jul|aug|sep|oct|nov|dec)";
    const std::string B = "(september|february|november|december|january|october|august|march|april|june|july|may)";
    const std::string d = "(3[01]|[12]\\d|0[1-9]|[1-9]| [1-9])", m = "(1[0-2]|0[1-9]|[1-9])", Y = "(\\d\\d\\d\\d)";
    std::string pat; std::string order;
    if (fmt == "%b %d, %Y") { pat = b + "\\s+" + d + ",\\s+" + Y; order = "bdY"; }
    else if (fmt == "%B %d, %Y") { pat = B + "\\s+" + d + ",\\s+" + Y; order = "BdY"; }
    else if (fmt == "%d %b %Y") { pat = d + "\\s+" + b + "\\s+" + Y; order = "dbY"; }
    else if (fmt == "%d %B %Y") { pat = d + "\\s+" + B + "\\s+" + Y; order = "dBY"; }
    else if (fmt == "%m/%d/%Y") { pat = m + "/" + d + "/" + Y; order = "mdY"; }
    else if (fmt == "%d/%m/%Y") { pat = d + "/" + m + "/" + Y; order = "dmY"; }
    else { pat = Y + "/" + m + "/" + d; order = "Ymd"; }
    const re::Match g = re::match(pat, s, re::I);
    if (!g || g.end(0) != static_cast<long>(cplen(s))) return std::nullopt;   // no match, or unconverted data remains
    long long year = 0, month = 0, day = 0;
    for (int k = 0; k < 3; ++k) {
        const std::string v = g.group(k + 1);
        const char c = order[static_cast<std::size_t>(k)];
        if (c == 'b' || c == 'B') {
            const auto& names = c == 'b' ? ABBR : FULL; const std::string lv = lower(v);
            const auto it = std::find(names.begin(), names.end(), lv);
            if (it == names.end()) return std::nullopt;   // a_month.index() raises ValueError
            month = (it - names.begin()) + 1;
        } else {
            const long long n = std::stoll(ascii_digits(strip(v)));
            if (c == 'Y') year = n;
            else if (c == 'm') month = n;
            else day = n;
        }
    }
    if (year < 1 || year > 9999 || day > month_days(year, static_cast<int>(month))) return std::nullopt;
    return ymd(year, month, day);
}
// 20260901, 2026-09-01, unix seconds, 'Sep 1, 2026', ISO datetimes -> 'YYYY-MM-DD'.
inline Val parse_date(const Val& v) {
    if (v.is_none() || v.t == Val::BOOL || (v.t == Val::STR && v.s.empty())) return Val();
    std::string s;
    if (v.t == Val::INT && !v.big() && v.i <= 100000000000LL) {
        if (v.i > 100000000LL) return Val::str(utc_day(v.i));
        s = std::to_string(v.i);
    } else if (v.is_num()) {
        double x = v.num();
        if (x > 1e11) x = x / 1000.0;   // milliseconds
        if (x > 1e8) return Val::str(utc_day(timestamp_seconds(x)));
        if (std::isnan(x)) throw PyErr("ValueError", "cannot convert float NaN to integer");
        if (x < 0) return Val();          // str(int(x)) starts with "-" or is "0": nothing below matches it
        s = std::to_string(static_cast<long long>(x));
    } else s = strip(pystr(v));
    if (re::match(R"(^\d{8}$)", s)) s = cpslice(s, 0, 4) + "-" + cpslice(s, 4, 6) + "-" + cpslice(s, 6);
    if (const re::Match m = re::match(R"(^(\d{4})-(\d{2})-(\d{2}))", s)) return Val::str(m.group(1) + "-" + m.group(2) + "-" + m.group(3));
    for (const char* fmt : {"%b %d, %Y", "%B %d, %Y", "%d %b %Y", "%d %B %Y", "%m/%d/%Y", "%d/%m/%Y", "%Y/%m/%d"})
        if (const auto r = strptime_ymd(s, fmt)) return Val::str(*r);
    return Val();
}

inline Val platform_of(const std::string& url, const Val& hint) {
    const std::string u = lower(url);
    auto in = [&](const char* x) { return u.find(x) != std::string::npos; };
    if (in("youtube.com/shorts") || (in("youtu.be") && in("shorts"))) return Val::str("shorts");
    if (in("youtube.com") || in("youtu.be")) return Val::str("youtube");
    static const std::vector<std::pair<const char*, const char*>> HOSTS = {{"tiktok.com", "tiktok"}, {"instagram.com", "instagram"},
        {"linkedin.com", "linkedin"}, {"x.com", "x"}, {"twitter.com", "x"}, {"facebook.com", "facebook"}, {"threads.net", "threads"}};
    for (const auto& [host, name] : HOSTS)
        if (in(host)) return Val::str(name);
    std::string h;
    if (truthy(hint)) {
        if (hint.t != Val::STR) throw PyErr("AttributeError", std::string("'") + type_name(hint) + "' object has no attribute 'strip'");
        h = lower(strip(hint.s));
    }
    if (h == "youtubetab") return Val::str("youtube");
    return h.empty() ? Val() : Val::str(h);
}

// 'Comment "SETUP" and I'll send you the process 👇 I use AI to...' -> 'I use AI to...'
inline std::string strip_cta(const std::string& text) {
    const std::string out = re::sub(CTA_PREFIX_RE, "", text, 1, re::I);
    return strip(out).empty() ? text : out;
}
// First sentence of a caption or title, after any leading call to action.
inline Val hook_from(const std::string& text) {
    const std::string body = strip_cta(strip(text));
    std::string first = strip(body).empty() ? "" : strip(re::split(SENTENCE_END_RE, strip(body), 1)[0]);
    first = cpslice(re::sub("\\s+", " ", first), 0, 160);
    return first.empty() ? Val() : Val::str(first);
}
inline std::optional<std::string> canon_url(const std::string& u0) {
    if (u0.empty()) return std::nullopt;
    const std::string u = strip(u0);
    if (const re::Match m = re::search(R"((?:youtube\.com/(?:shorts/|watch\?v=)|youtu\.be/)([\w-]{6,}))", u)) return "yt:" + m.group(1);
    return lower(rstrip_chars(re::sub("[?#].*$", "", u), "/"));
}

// ------------------------------------------------------------------------ normalizing
inline bool empty_like(const Val& v) { return v.is_none() || (v.t == Val::STR && v.s.empty()); }   // v in (None, "")
// One raw record (any key names) -> the canonical post dict, or None if unusable.
inline Val normalize(const Dict& raw, const Dict& defaults) {
    Dict keyed;
    for (const auto& [k, v] : raw.items)
        if (!empty_like(v)) keyed.set(norm_key(k), v);
    Dict got;
    for (const auto& [f, names] : ALIASES)   // alias order is priority: title before description
        for (const auto& name : names)
            if (const Val* x = keyed.get(name)) { got.set(f, *x); break; }
    if (const Val* handle = keyed.get("uploader_id"))
        if (handle->t == Val::STR && starts(handle->s, "@")) got.set("channel", *handle);   // yt-dlp: the @handle is the stable creator key
    for (const auto& [f, v] : defaults.items)
        if (!empty_like(v) && empty_like(got.at(f))) got.set(f, v);
    Val post = Val::dict();
    for (const auto& f : FIELDS) post.d->set(f, Val());
    const Val channel = got.at("channel");
    if (!empty_like(channel)) post.d->set("channel", Val::str(collapse(pystr(channel))));
    const Val title = got.at("title");
    if (truthy(title)) post.d->set("title", Val::str(collapse(pystr(title))));
    const Val hook = got.at("hook");
    const Val ptitle = post.d->at("title");
    post.d->set("hook", truthy(hook) ? Val::str(collapse(pystr(hook))) : hook_from(ptitle.t == Val::STR ? ptitle.s : ""));
    for (const char* f : {"views", "likes", "comments", "shares", "saves", "followers", "median"}) post.d->set(f, parse_count(got.at(f)));
    post.d->set("duration", parse_duration(got.at("duration")));
    post.d->set("posted", parse_date(got.at("posted")));
    const Val url = got.at("url");
    if (truthy(url)) post.d->set("url", Val::str(strip(pystr(url))));
    const Val purl = post.d->at("url");
    post.d->set("platform", platform_of(purl.t == Val::STR ? purl.s : "", got.at("platform")));
    if (!truthy(post.d->at("channel")) || !(truthy(post.d->at("title")) || truthy(post.d->at("hook")))) return Val();
    bool any = false;
    for (const char* f : {"views", "likes", "comments", "shares", "saves"})
        if (!post.d->at(f).is_none()) any = true;
    return any ? post : Val();
}

// ------------------------------------------------------------------------ loading
using Pair = std::pair<Val, Val>;   // (record, defaults), both dicts
inline Val empty_dict() { return Val::dict(); }
// (record, defaults) pairs from any of the JSON shapes we accept
inline void records_from_json(const Val& data, std::vector<Pair>& out) {
    auto entries_of = [](const Val& d) -> const Val* { const Val* e = d.d->get("entries"); return e && e->t == Val::LIST ? e : nullptr; };
    if (data.t == Val::LIST) {
        const List rs = *data.l;
        for (const auto& r : rs) {
            if (r.t != Val::DICT) continue;
            if (entries_of(r)) records_from_json(r, out);   // several yt-dlp dumps in one list
            else out.emplace_back(r, empty_dict());
        }
        return;
    }
    if (data.t != Val::DICT) return;
    if (const Val* entries = entries_of(data)) {           // yt-dlp playlist / channel tab
        Val parent = Val::dict();
        parent.d->set("channel", orv(orv(data.d->at("uploader_id"), data.d->at("channel")), data.d->at("uploader")));
        parent.d->set("followers", data.d->at("channel_follower_count"));
        parent.d->set("platform", orv(data.d->at("extractor_key"), data.d->at("ie_key")));
        const List es = *entries->l;
        for (const auto& e : es) {
            if (e.t != Val::DICT) continue;
            if (entries_of(e)) { records_from_json(e, out); continue; }   // nested tabs
            out.emplace_back(e, parent);
        }
        return;
    }
    for (const char* key : {"posts", "videos", "items", "data", "rows", "results"}) {
        const Val* v = data.d->get(key);
        if (v && v->t == Val::LIST) {
            const List rs = *v->l;
            for (const auto& r : rs)
                if (r.t == Val::DICT) out.emplace_back(r, empty_dict());
            return;
        }
    }
    out.emplace_back(data, empty_dict());
}
inline Val zip_dict(const std::vector<std::string>& keys, const std::vector<std::string>& cells) {   // dict(zip(keys, cells))
    Val d = Val::dict();
    for (std::size_t k = 0; k < keys.size() && k < cells.size(); ++k) d.d->set(keys[k], Val::str(cells[k]));
    return d;
}
inline std::vector<Val> records_from_table(const std::string& text) {
    const std::string sample = cpslice(text, 0, 4096);
    const char delim = std::count(sample.begin(), sample.end(), '\t') >= std::count(sample.begin(), sample.end(), ',') ? '\t' : ',';
    std::vector<std::vector<std::string>> rows;
    for (auto& r : csv_rows(text, delim)) {
        bool any = false; for (const auto& c : r) if (!strip(c).empty()) any = true;
        if (any) rows.push_back(std::move(r));
    }
    std::vector<Val> out;
    if (rows.empty()) return out;
    std::vector<std::string> head; for (const auto& c : rows[0]) head.push_back(norm_key(c));
    for (std::size_t k = 1; k < rows.size(); ++k) out.push_back(zip_dict(head, rows[k]));
    return out;
}
inline std::vector<Val> records_from_paste(const std::string& text) {
    std::vector<Val> out;
    std::vector<std::string> order = {"channel", "views", "title", "url", "duration", "posted"};
    for (const auto& line : splitlines(text)) {
        if (strip(line).empty() || starts(lstrip_ws(line), "#")) continue;
        std::vector<std::string> cells;
        for (const auto& c : re::split("\\s*\\|\\s*|\\t", strip_chars(strip(line), "|"))) cells.push_back(strip(c));
        if (cells.size() < 3) continue;
        if (parse_count(Val::str(cells[1])).is_none() && in_lookup(norm_key(cells[1]))) {   // a header row
            order.clear(); for (const auto& c : cells) order.push_back(norm_key(c));
            continue;
        }
        out.push_back(zip_dict(order, cells));
    }
    return out;
}
// the raw (record, defaults) pairs of one file
inline std::vector<Pair> load_file(const std::string& path) {
    const std::string raw = decode_utf8_replace(path == "-" ? read_stdin_text() : read_text(path, true));
    const std::string text = strip(raw);
    std::vector<Pair> out;
    if (text.empty()) return out;
    if (text[0] == '[' || text[0] == '{') {
        Val data; bool ok = true;
        try { data = loads(text); } catch (const PyErr&) { ok = false; }
        if (ok) { records_from_json(data, out); return out; }
        std::vector<Val> docs;
        try {                                                      // JSON lines (yt-dlp -j)
            for (const auto& l : splitlines(text))
                if (!strip(l).empty()) docs.push_back(loads(l));
        } catch (const PyErr&) {
            throw PyErr("ValueError", path + " looks like JSON but does not parse");
        }
        for (const auto& d : docs) records_from_json(d, out);
        return out;
    }
    const std::string first = splitlines(text)[0];
    std::vector<Val> recs;
    if (first.find('|') != std::string::npos || !re::search("[,\\t]", first)) recs = records_from_paste(text);
    else {
        int known = 0; for (const auto& h : re::split("[,\\t]", first)) if (in_lookup(norm_key(h))) ++known;
        recs = known >= 2 ? records_from_table(text) : records_from_paste(text);
    }
    for (auto& r : recs) out.emplace_back(std::move(r), empty_dict());
    return out;
}
inline std::string str_or_empty(const Val& v) { return v.t == Val::STR ? v.s : ""; }
// load, normalize and de-duplicate posts from every path
inline std::vector<Val> collect(const std::vector<std::string>& paths, const Dict& defaults, long long& skipped) {
    std::vector<Val> posts; std::set<std::string> seen; skipped = 0;
    for (const auto& path : paths) {
        const std::vector<Pair> pairs = load_file(path);
        for (const auto& [record, parent] : pairs) {
            Dict merged = *parent.d;
            for (const auto& [k, v] : defaults.items)
                if (!empty_like(v)) merged.set(k, v);
            const Val post = normalize(*record.d, merged);
            if (post.is_none()) { ++skipped; continue; }
            const auto canon = canon_url(str_or_empty(post.d->at("url")));
            std::string key;
            if (canon && !canon->empty()) key = "u" + *canon;
            else {
                const Val t = orv(post.d->at("title"), post.d->at("hook"));
                key = "t" + lstrip_at(lower(post.d->at("channel").s)) + std::string(1, '\0') + lower(str_or_empty(t));
            }
            if (!seen.insert(key).second) continue;
            posts.push_back(post);
        }
    }
    return posts;
}
inline std::string creator_key(const std::string& name) { return lstrip_at(lower(strip(name))); }

inline void summary(const std::vector<Val>& posts, long long skipped, long long min_posts) {
    std::vector<std::pair<std::string, std::vector<const Val*>>> by;
    for (const auto& p : posts) {
        const std::string k = creator_key(p.d->at("channel").s);
        auto it = std::find_if(by.begin(), by.end(), [&](const auto& e) { return e.first == k; });
        if (it == by.end()) { by.emplace_back(k, std::vector<const Val*>{}); it = by.end() - 1; }
        it->second.push_back(&p);
    }
    eprint("\n  collected " + std::to_string(posts.size()) + " posts from " + std::to_string(by.size()) + " creators" +
           (skipped ? ", skipped " + std::to_string(skipped) + " unusable rows" : ""));
    std::stable_sort(by.begin(), by.end(), [](const auto& a, const auto& b) { return a.second.size() > b.second.size(); });
    for (const auto& [key, items] : by) {
        bool has_median = false; for (const Val* p : items) if (truthy(p->d->at("median"))) has_median = true;
        const std::string note = static_cast<long long>(items.size()) >= min_posts || has_median ? "" :
            "   under " + std::to_string(min_posts) + ": swipe will skip it unless you add a median";
        eprint("    " + rjust(std::to_string(items.size()), 3) + "  " + cpslice(items[0]->d->at("channel").s, 0, 40) + note);
    }
    long long missing = 0; for (const auto& p : posts) if (p.d->at("views").is_none()) ++missing;
    if (missing) eprint("  " + std::to_string(missing) + " posts have no view count; rank them with swipe --metric likes");
    eprint("");
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("collect", "Normalize pasted posts, CSV exports and yt-dlp JSON into one list for swipe.");
    p.epilog = "Paste format, one post per line: creator | views | hook or title | url | duration | date";
    p.pos("inputs").nargs("+").help("files to read, or - for stdin");
    p.opt({"-o", "--out"}).help("write the JSON here (default: stdout)");
    p.opt({"--creator"}).help("creator to use for rows that have none (one creator's list)");
    p.opt({"--platform"}).help("platform for rows that have none: shorts, tiktok, instagram, linkedin");
    p.opt({"--append"}).store_true().help("merge into an existing --out file");
    p.opt({"--min-posts"}).integer().def(4).help("warn about creators with fewer posts than this (default %(default)s)");
    p.opt({"--quiet"}).store_true().help("no summary on stderr");
    const Val a = parse_args(p, argv);
    std::vector<std::string> inputs; for (const auto& x : iterate(get(a, "inputs"))) inputs.push_back(x.s);
    for (const auto& path : inputs)
        if (path != "-" && !exists(path)) { eprint("no such file: " + path); return 2; }
    Dict defaults; defaults.set("channel", get(a, "creator")); defaults.set("platform", get(a, "platform"));
    long long skipped = 0;
    std::vector<Val> posts;
    try {
        posts = collect(inputs, defaults, skipped);
    } catch (const PyErr& e) {
        if (e.type != "ValueError") throw;
        eprint(e.what()); return 2;
    }
    const std::string out = S(a, "out"); const bool quiet = B(a, "quiet");
    if (B(a, "append") && !out.empty() && exists(out)) {
        long long ignored = 0;
        const std::vector<Val> old = collect({out}, Dict{}, ignored);
        std::set<std::string> known;
        for (const auto& q : old)
            if (truthy(q.d->at("url"))) known.insert(canon_url(q.d->at("url").s).value_or(""));
        std::vector<Val> merged = old;
        for (const auto& q : posts) {
            const Val u = q.d->at("url");
            if (!truthy(u) || !known.count(canon_url(u.s).value_or(""))) merged.push_back(q);
        }
        posts = merged;
    }
    if (posts.empty()) {
        eprint("no usable posts. Each needs a creator, a hook or title, and at least one count (views, likes, comments, shares or saves).");
        return 1;
    }
    DumpOpts o; o.indent = 1; o.ensure_ascii = false;
    const std::string payload = dumps(Val::list(posts), o);
    if (!out.empty()) {
        write_text(out, payload + "\n");
        if (!quiet) eprint("  wrote " + out);
    } else print(payload);
    if (!quiet) summary(posts, skipped, get(a, "min_posts").i);
    return 0;
}
PACKTOOL("master-virality/collect", main);
}  // namespace packtools::master_collect
