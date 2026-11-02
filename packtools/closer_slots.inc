// scheduler/slots: turns "Thursday at 2 works" into a date and time, lists your open slots from the busy times on
// your calendar, writes a calendar invite (.ics) and logs a booked call after your yes. A C++ port of
// closer-skill/scheduler/slots.py (MIT, Jake Schincariol). Datetimes are naive wall times in your own time zone,
// held as microseconds; an ISO time with an offset keeps its wall time.
#include <random>
namespace packtools::closer_slots {
using namespace pyrt;
using namespace packtools::closer;
struct ScheduleError : std::runtime_error { using std::runtime_error::runtime_error; };
inline constexpr long long US_MIN = 60000000LL, US_DAY = 86400000000LL;
inline const std::vector<std::string> DAYS = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};
inline const std::vector<std::string> MONTHS = {"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};
inline constexpr int WORK_FROM = 9, WORK_TO = 17;
inline const char* TIME_RE = R"(\b(?:at\s+)?(\d{1,2})(?::(\d{2}))?\s*(am|pm|a\.m\.|p\.m\.)?(?=\W|$))";
inline const char* NOON_RE = R"(\b(noon|midday)\b)";
inline const char* DAY_RE = R"(\b(next\s+)?(mon|tues?|wed(?:nes)?|thu(?:rs?)?|fri|sat(?:ur)?|sun)(?:day)?\b)";
inline const char* DATE_RE = R"(\b(jan|feb|mar|apr|may|jun|jul|aug|sep|oct|nov|dec)[a-z]*\.?\s+(\d{1,2})\b)";

// ---------------------------------------------------------------- naive datetimes
inline long long fdiv(long long a, long long b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }
inline long long dt(long long day, long long h, long long mi) { return day * US_DAY + h * 3600000000LL + mi * US_MIN; }
inline long long day_of(long long us) { return fdiv(us, US_DAY); }
inline long long dt_fromiso(const std::string& s) {   // datetime.fromisoformat, wall time
    std::string w = s;
    if (w.size() > 10) { const std::size_t k = w.find_first_of("Z+-", 10); if (k != std::string::npos) w = w.substr(0, k); }
    if (!fromiso_us(s) || !fromiso_us(w)) throw PyErr("ValueError", "Invalid isoformat string: " + str_repr(s));
    return *fromiso_us(w);
}
inline std::string strf(long long us, const std::string& f) {
    static const char* A[] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
    static const char* B[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
    const long long day = day_of(us), rem = (us - day * US_DAY) / 1000000;
    long long y; unsigned m, d; civil_from(day, y, m, d);
    const int H = static_cast<int>(rem / 3600), M = static_cast<int>(rem % 3600 / 60), S = static_cast<int>(rem % 60), wd = weekday(day);
    const auto two = [](long long v) { char b[24]; std::snprintf(b, sizeof b, "%02lld", v); return std::string(b); };
    std::string o;
    for (std::size_t k = 0; k < f.size(); ++k) {
        if (f[k] != '%' || k + 1 >= f.size()) { o += f[k]; continue; }
        switch (f[++k]) {
            case 'Y': { char b[24]; std::snprintf(b, sizeof b, "%04lld", y); o += b; break; }
            case 'm': o += two(m); break;
            case 'd': o += two(d); break;
            case 'H': o += two(H); break;
            case 'M': o += two(M); break;
            case 'S': o += two(S); break;
            case 'I': o += two(H % 12 == 0 ? 12 : H % 12); break;
            case 'p': o += H < 12 ? "AM" : "PM"; break;
            case 'A': o += A[wd]; break;
            case 'a': o += std::string(A[wd]).substr(0, 3); break;
            case 'B': o += B[m - 1]; break;
            case 'b': o += std::string(B[m - 1]).substr(0, 3); break;
            case '%': o += '%'; break;
            default: o += '%'; o += f[k];
        }
    }
    return o;
}
inline long long make_date(long long y, long long m, long long d) {
    if (m < 1 || m > 12) throw PyErr("ValueError", "month must be in 1..12");
    static const int ML[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    if (d < 1 || d > ML[m - 1] + (m == 2 && leap)) throw PyErr("ValueError", "day is out of range for month");   // CPython 3.13's wording
    return civil_days(y, static_cast<unsigned>(m), static_cast<unsigned>(d)) - 719468;
}

// The first date and time in a reply, or nullopt when there is no clear time.
// "at 2" with no am/pm means business hours: 1 to 7 is pm, 8 to 12 is am (12 is noon).
inline std::optional<long long> parse_when(const std::string& text, long long today) {
    std::string t = lower(text);
    std::optional<long long> day;
    long long ty; unsigned tm, td; civil_from(today, ty, tm, td);
    if (const auto m = re::compile(DATE_RE, re::I).search(t)) {
        const long long month = static_cast<long long>(std::find(MONTHS.begin(), MONTHS.end(), cpslice(m.group(1), 0, 3)) - MONTHS.begin()) + 1;
        const long long dd = std::stoll(m.group(2));
        const long long year = std::make_pair(month, dd) >= std::make_pair(static_cast<long long>(tm), static_cast<long long>(td)) ? ty : ty + 1;
        day = make_date(year, month, dd);
        t = cpslice(t, 0, m.start()) + " " + cpslice(t, m.end(), static_cast<long long>(cplen(t)));
    } else if (re::compile(R"(\btomorrow\b)").search(t)) day = today + 1;
    else if (re::compile(R"(\btoday\b|\bthis afternoon\b|\bthis morning\b)").search(t)) day = today;
    else if (const auto m2 = re::compile(DAY_RE, re::I).search(t)) {
        const long long want = static_cast<long long>(std::find(DAYS.begin(), DAYS.end(), cpslice(m2.group(2), 0, 3)) - DAYS.begin());
        // "next Monday" is read as the coming Monday; people mean both, so the confirm-time reply says the date
        long long ahead = ((want - weekday(today)) % 7 + 7) % 7; if (ahead == 0) ahead = 7;
        day = today + ahead;
    }
    if (!day) return std::nullopt;
    if (re::compile(NOON_RE, re::I).search(t)) return dt(*day, 12, 0);
    for (const auto& m : re::compile(TIME_RE, re::I).finditer(t)) {
        long long hour = std::stoll(m.group(1)); const long long minute = m.group(2).empty() ? 0 : std::stoll(m.group(2));
        const std::string ampm = replace_all(m.group(3), ".", "");
        if (hour > 12 || minute > 59 || (ampm.empty() && strip(m.group(0)).rfind("at", 0) != 0 && m.group(2).empty())) continue;
        if (ampm == "pm" && hour < 12) hour += 12;
        else if (ampm == "am" && hour == 12) hour = 0;
        else if (ampm.empty() && hour >= 1 && hour <= 7) hour += 12;
        return dt(*day, hour, minute);
    }
    return std::nullopt;
}
using Busy = std::vector<std::pair<long long, long long>>;
inline Busy load_busy(const std::string& path) {
    Busy b;
    const Val doc = loads(read_text(path));
    for (const Val& x : lst(doc)) {
        if (!has(x, "start")) throw PyErr("KeyError", "'start'");
        if (!has(x, "end")) throw PyErr("KeyError", "'end'");
        if (at(x, "start").t != Val::STR || at(x, "end").t != Val::STR) throw PyErr("TypeError", "fromisoformat: argument must be str");
        const long long s = dt_fromiso(at(x, "start").s), e = dt_fromiso(at(x, "end").s);
        b.emplace_back(s, e);
    }
    return b;
}
inline bool is_free(long long start, long long minutes, const Busy& busy) {
    const long long end = start + minutes * US_MIN;
    for (const auto& [b0, b1] : busy) if (!(end <= b0 || start >= b1)) return false;
    return true;
}
inline std::vector<long long> free_slots(const Busy& busy, long long start_day, long long days, long long minutes = 30, long long step = 30, std::size_t limit = 8) {
    std::vector<long long> out;
    for (long long day = start_day; out.size() < limit && day - start_day < days + 14; ++day) {
        if (weekday(day) < 5 && day - start_day < days) {
            const long long stop = dt(day, WORK_TO, 0);
            for (long long t = dt(day, WORK_FROM, 0); t + minutes * US_MIN <= stop && out.size() < limit; t += step * US_MIN)
                if (is_free(t, minutes, busy)) out.push_back(t);
        }
    }
    return out;
}
inline std::string uuid4() {
    std::random_device rd; std::mt19937_64 rng((static_cast<unsigned long long>(rd()) << 32) ^ rd() ^ static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
    unsigned char b[16]; for (auto& x : b) x = static_cast<unsigned char>(rng() & 0xFF);
    b[6] = static_cast<unsigned char>((b[6] & 0x0F) | 0x40); b[8] = static_cast<unsigned char>((b[8] & 0x3F) | 0x80);
    std::string o; const char* hx = "0123456789abcdef";
    for (int k = 0; k < 16; ++k) { if (k == 4 || k == 6 || k == 8 || k == 10) o += '-'; o += hx[b[k] >> 4]; o += hx[b[k] & 15]; }
    return o;
}
inline long long now_us(bool utc) {
    const std::time_t t = std::time(nullptr); std::tm tmv{};
#ifdef _WIN32
    if (utc) gmtime_s(&tmv, &t); else localtime_s(&tmv, &t);
#else
    if (utc) gmtime_r(&t, &tmv); else localtime_r(&t, &tmv);
#endif
    return dt(civil_days(tmv.tm_year + 1900, static_cast<unsigned>(tmv.tm_mon + 1), static_cast<unsigned>(tmv.tm_mday)) - 719468, tmv.tm_hour, tmv.tm_min) + tmv.tm_sec * 1000000LL;
}
inline std::string ics(const Val& me, const Val& contact, long long start, long long minutes, const std::string& title, const std::string& where = "Google Meet") {
    const long long end = start + minutes * US_MIN;
    const std::string tz = ps(orv(g(me, "timezone"), sv("America/Chicago")));
    std::vector<std::string> lines = {"BEGIN:VCALENDAR", "VERSION:2.0", "PRODID:-//Closer skill//EN", "METHOD:REQUEST", "BEGIN:VEVENT",
        "UID:" + uuid4() + "@closer-skill", "DTSTAMP:" + strf(now_us(true), "%Y%m%dT%H%M%SZ"),
        "DTSTART;TZID=" + tz + ":" + strf(start, "%Y%m%dT%H%M%S"), "DTEND;TZID=" + tz + ":" + strf(end, "%Y%m%dT%H%M%S"),
        "SUMMARY:" + title, "LOCATION:" + where, "ORGANIZER;CN=" + ps(orv(g(me, "name"), sv(""))) + ":mailto:" + ps(orv(g(me, "email"), sv("")))};
    if (truthy(g(contact, "email"))) lines.push_back("ATTENDEE;CN=" + ps(at(contact, "name")) + ";RSVP=TRUE:mailto:" + ps(at(contact, "email")));
    lines.push_back("END:VEVENT"); lines.push_back("END:VCALENDAR");
    return join(lines, "\r\n") + "\r\n";
}
inline std::string title_for(const Val& data, const Val& contact) {
    return ps(orv(g(contact, "company"), at(contact, "name"))) + " x " + ps(orv(g(g(data, "me", Val::dict()), "company"), sv("me")));
}
inline std::pair<bool, std::string> book(Val& data, const std::string& contact_id, long long start, long long minutes, const Val& approval, const std::optional<Busy>& busy, const Val& when_at) {
    const auto [ok, why] = parse_yes(approval);
    if (!ok) return {false, "Not booked: " + why + "."};
    const Val* cp = find(at(data, "contacts"), contact_id);
    if (!cp) return {false, "No contact " + contact_id};
    const Val c = *cp;
    if (truthy(g(c, "do_not_contact"))) return {false, "Not booked: " + ps(at(c, "name")) + " is do not contact."};
    if (busy && !is_free(start, minutes, *busy)) return {false, "Not booked: you are busy then. Run free for open slots."};
    Val deal; for (const Val& d : lstk(data, "deals")) if (is(at(d, "contact_id"), contact_id) && in(at(d, "stage"), OPEN)) { deal = d; break; }
    const long long end = start + minutes * US_MIN;
    add_activity(data, {{"date", truthy(when_at) ? when_at : sv(strf(now_us(false), "%Y-%m-%dT%H:%M"))}, {"contact_id", sv(contact_id)},
                        {"deal_id", deal.is_none() ? Val() : at(deal, "id")}, {"channel", sv("meeting")}, {"type", sv("meeting")}, {"direction", sv("out")},
                        {"subject", sv(title_for(data, c))}, {"approval", approval}, {"when", sv(strf(start, "%Y-%m-%dT%H:%M"))},
                        {"text", sv("Booked " + strf(start, "%a %b %d") + ", " + strf(start, "%H:%M") + "-" + strf(end, "%H:%M"))}});
    if (!deal.is_none()) {
        const auto idx = [](const std::string& s) { return std::find(STAGES.begin(), STAGES.end(), s) - STAGES.begin(); };
        if (idx(str_of(at(deal, "stage"))) < idx("call-booked")) deal.d->set("stage", sv("call-booked"));
        deal.d->set("next_step", sv("Call " + strf(start, "%a %b %d,") + " " + strf(start, "%H:%M")));
    }
    return {true, "Booked " + ps(at(c, "name")) + " for " + strf(start, "%a %b %d %H:%M") + ". /researcher can prep the brief now."};
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("slots.py", "Book calls on your calendar.");
    p.opt({"--file"}).def(DEFAULT_FILE);
    p.subcommands("cmd", true);
    { Parser& s = p.sub("parse"); s.opt({"--text"}).required(); s.opt({"--today"}); }
    { Parser& s = p.sub("free"); s.opt({"--busy"}).required(); s.opt({"--from"}).dest("start").required(); s.opt({"--days"}).integer().def(5); s.opt({"--length"}).integer().def(30); }
    { Parser& s = p.sub("invite"); s.opt({"--contact"}).required(); s.opt({"--start"}).required(); s.opt({"--minutes"}).integer().def(30); }
    { Parser& s = p.sub("book"); s.opt({"--contact"}).required(); s.opt({"--start"}).required(); s.opt({"--minutes"}).integer().def(30); s.opt({"--approval"}).required();
      s.opt({"--busy"}); s.opt({"--at"}).help("YYYY-MM-DDTHH:MM it was booked (default now)"); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd"), file = S(a, "file");
    try {
        if (cmd == "parse") {
            const auto when = parse_when(S(a, "text"), today_arg(get(a, "today")));
            print(when ? strf(*when, "%Y-%m-%dT%H:%M (%A %B %d, %I:%M %p)") : std::string("No clear time in that. Ask them."));
            return when ? 0 : 1;
        }
        if (cmd == "free") {
            for (const long long t : free_slots(load_busy(S(a, "busy")), date_fromiso(get(a, "start")), get(a, "days").i, get(a, "length").i)) print(strf(t, "%a %b %d  %I:%M %p"));
            return 0;
        }
        Val data = load(file);
        const Val* cp = find(at(data, "contacts"), S(a, "contact"));
        if (!cp) throw ScheduleError("No contact " + S(a, "contact"));
        const Val c = *cp;
        const long long start = dt_fromiso(S(a, "start"));
        if (cmd == "invite") {
            const std::string dir = path_join("closer", "invites");
            makedirs(dir);
            const std::string path = path_join(dir, strf(start, "%Y-%m-%d") + "-" + ps(at(c, "id")) + ".ics");
            write_text(path, ics(g(data, "me", Val::dict()), c, start, get(a, "minutes").i, title_for(data, c)));
            print("Wrote " + path);
        } else if (cmd == "book") {
            std::optional<Busy> busy; if (truthy(get(a, "busy"))) busy = load_busy(S(a, "busy"));
            const auto [ok, msg] = book(data, S(a, "contact"), start, get(a, "minutes").i, get(a, "approval"), busy, get(a, "at"));
            if (ok) save(data, file);
            print(msg);
            return ok ? 0 : 1;
        }
    } catch (const ScheduleError& e) { eprint(e.what()); return 1; }
    catch (const PipelineError& e) { eprint(e.what()); return 1; }
    catch (const OSError& e) { eprint(e.what()); return 1; }
    catch (const PyErr& e) { if (e.type != "ValueError" && e.type != "json.decoder.JSONDecodeError" && e.type != "UnicodeDecodeError") throw; eprint(e.what()); return 1; }
    return 0;
}
PACKTOOL("scheduler/slots", main);
}  // namespace packtools::closer_slots
