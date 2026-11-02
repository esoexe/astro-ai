// seed-closer, part 6: book (calls/<id>.ics, an RFC 5545 invite, with time zones from a built-in table and DST rules
// instead of tz files) and status (the funnel, or one investor's history).
namespace packtools::seed_closer {

struct Zone { int std_min; const char* rule; };   // rule: nullptr, "us", "eu" or "au"
inline const std::map<std::string, Zone> ZONES = {
    {"UTC", {0, nullptr}}, {"Europe/London", {0, "eu"}}, {"Europe/Dublin", {0, "eu"}}, {"Europe/Lisbon", {0, "eu"}},
    {"Europe/Paris", {60, "eu"}}, {"Europe/Berlin", {60, "eu"}}, {"Europe/Madrid", {60, "eu"}},
    {"Europe/Amsterdam", {60, "eu"}}, {"Europe/Stockholm", {60, "eu"}}, {"Europe/Zurich", {60, "eu"}},
    {"Europe/Rome", {60, "eu"}}, {"Europe/Athens", {120, "eu"}}, {"Europe/Helsinki", {120, "eu"}},
    {"America/New_York", {-300, "us"}}, {"America/Toronto", {-300, "us"}}, {"America/Detroit", {-300, "us"}},
    {"America/Chicago", {-360, "us"}}, {"America/Denver", {-420, "us"}}, {"America/Phoenix", {-420, nullptr}},
    {"America/Los_Angeles", {-480, "us"}}, {"America/Vancouver", {-480, "us"}}, {"America/Anchorage", {-540, "us"}},
    {"Pacific/Honolulu", {-600, nullptr}}, {"America/Mexico_City", {-360, nullptr}}, {"America/Sao_Paulo", {-180, nullptr}},
    {"Asia/Dubai", {240, nullptr}}, {"Asia/Kolkata", {330, nullptr}}, {"Asia/Singapore", {480, nullptr}},
    {"Asia/Shanghai", {480, nullptr}}, {"Asia/Hong_Kong", {480, nullptr}}, {"Asia/Tokyo", {540, nullptr}},
    {"Australia/Sydney", {600, "au"}}, {"Australia/Melbourne", {600, "au"}}, {"Australia/Brisbane", {600, nullptr}},
    {"Pacific/Kiritimati", {840, nullptr}}, {"Pacific/Pago_Pago", {-660, nullptr}},
};
inline const std::map<std::string, std::string> ZONE_ALIASES = {
    {"ET", "America/New_York"}, {"EST", "America/New_York"}, {"EDT", "America/New_York"},
    {"CT", "America/Chicago"}, {"CST", "America/Chicago"}, {"CDT", "America/Chicago"},
    {"MT", "America/Denver"}, {"PT", "America/Los_Angeles"}, {"PST", "America/Los_Angeles"},
    {"PDT", "America/Los_Angeles"}, {"GMT", "UTC"}, {"Z", "UTC"}};

inline std::string zone_name(const std::string& tz) {
    const std::string t = strip(tz);
    const auto al = ZONE_ALIASES.find(upper(t));
    const std::string z = al == ZONE_ALIASES.end() ? t : al->second;
    if (!ZONES.count(z)) {
        std::vector<std::string> all; for (const auto& [k, v] : ZONES) all.push_back(k);
        throw CloserError("unknown time zone " + str_repr(tz) + ". Use one of: " + join(all, ", ") + ", or give the start with an offset, like 2026-11-05T10:00-05:00");
    }
    return z;
}
// naive datetimes are seconds since 1970-01-01 00:00
inline long long at_time(long long day, int h) { return day * 86400 + h * 3600LL; }
inline long long day_of(long long secs) { return secs >= 0 ? secs / 86400 : -((-secs + 86399) / 86400); }
inline long long nth_sunday(long long y, int m, int n) { long long d = make_day(y, m, 1); d += (6 - weekday(d)) % 7; return d + 7LL * (n - 1); }
inline long long last_sunday(long long y, int m) { const long long d = make_day(y + (m == 12), m % 12 + 1, 1) - 1; return d - (weekday(d) + 1) % 7; }
inline int zone_offset(const std::string& zone, long long utc) {   // minutes ahead of UTC in this zone at this UTC moment
    const Zone& z = ZONES.at(zone);
    const int sd = z.std_min;
    if (!z.rule) return sd;
    const long long y = year_of(day_of(utc));
    const std::string rule = z.rule;
    if (rule == "us") {   // 2:00 local on the 2nd Sunday of March to 2:00 local on the 1st Sunday of November
        const long long start = at_time(nth_sunday(y, 3, 2), 2) - sd * 60LL;
        const long long end = at_time(nth_sunday(y, 11, 1), 2) - (sd + 60) * 60LL;
        return start <= utc && utc < end ? sd + 60 : sd;
    }
    if (rule == "eu") {   // 01:00 UTC on the last Sunday of March to 01:00 UTC on the last Sunday of October
        const long long start = at_time(last_sunday(y, 3), 1), end = at_time(last_sunday(y, 10), 1);
        return start <= utc && utc < end ? sd + 60 : sd;
    }
    const long long end = at_time(nth_sunday(y, 4, 1), 3) - (sd + 60) * 60LL;   // au
    const long long start = at_time(nth_sunday(y, 10, 1), 2) - sd * 60LL;
    return utc < end || utc >= start ? sd + 60 : sd;
}
inline std::string dt_str(long long secs, const char* fmt = "%04lld-%02u-%02u %02d:%02d:%02d") {   // str(datetime)
    long long y; unsigned m, d; civil_from(day_of(secs), y, m, d);
    const long long r = secs - day_of(secs) * 86400;
    char b[64]; std::snprintf(b, sizeof b, fmt, y, m, d, static_cast<int>(r / 3600), static_cast<int>(r / 60 % 60), static_cast<int>(r % 60));
    return b;
}
inline std::pair<long long, int> local_to_utc(long long local, const std::string& zone) {   // the earlier moment wins at fall-back
    const Zone& z = ZONES.at(zone);
    std::vector<std::pair<long long, int>> found;
    std::vector<int> offs; if (z.rule) offs = {z.std_min + 60, z.std_min}; else offs = {z.std_min};
    for (int off : offs) { const long long utc = local - off * 60LL; if (zone_offset(zone, utc) == off) found.emplace_back(utc, off); }
    if (found.empty()) throw CloserError(dt_str(local) + " does not exist in " + zone + ": the clocks jump forward that night");
    return *std::min_element(found.begin(), found.end());
}
inline std::string fmt_offset(int minutes) {
    char b[32]; std::snprintf(b, sizeof b, "%s%02d:%02d", minutes >= 0 ? "+" : "-", std::abs(minutes) / 60, std::abs(minutes) % 60);
    return b;
}
struct Start { long long utc; std::string zone; int off; long long local; };
inline Start parse_start(const Val& text, const std::string& tz) {
    const std::string s = strip(str_or(text));
    const re::Match m = re::match(R"(^(\d{4}-\d{2}-\d{2})[ T](\d{1,2}):(\d{2})(?::(\d{2}))?\s*(Z|[+-]\d{2}:?\d{2})?$)", s);
    if (!m) throw CloserError("--start wants a time like \"2026-11-05 10:00\" (in --tz) or 2026-11-05T15:00Z");
    const long long day = parse_day_s(m.group(1), "--start");
    const long long hh = to_ll(m.group(2)), mm = to_ll(m.group(3)), ss = m.has(4) ? to_ll(m.group(4)) : 0;
    if (hh > 23 || mm > 59 || ss > 59) throw CloserError("--start: " + s + " is not a real time");
    const long long local = day * 86400 + hh * 3600 + mm * 60 + ss;
    if (m.has(5)) {
        const std::vector<std::string> z = cps(m.group(5));
        int off = 0;
        if (!(z.size() == 1 && z[0] == "Z"))
            off = (z[0] == "+" ? 1 : -1) * static_cast<int>(to_ll(z[1] + z[2]) * 60 + to_ll(z[z.size() - 2] + z[z.size() - 1]));
        return Start{local - off * 60LL, off == 0 ? "UTC" : "UTC" + fmt_offset(off), off, local};
    }
    const std::string zone = zone_name(tz);
    const auto [utc, off] = local_to_utc(local, zone);
    return Start{utc, zone, off, local};
}

inline std::string ics_text(const std::string& s) {
    return replace_all(replace_all(replace_all(replace_all(replace_all(s, "\\", "\\\\"), ";", "\\;"), ",", "\\,"), "\r\n", "\n"), "\n", "\\n");
}
inline std::string ics_param(const std::string& v0) {
    const std::string v = ws_join(replace_all(v0, "\"", "'"));
    return re::search("[:;,]", v) ? "\"" + v + "\"" : v;
}
inline std::string ics_fold(const std::string& line) {   // RFC 5545 3.1: 75 octets, never inside a UTF-8 character
    std::vector<std::string> out; std::string cur; std::size_t size = 0;
    for (const std::string& ch : cps(line)) {
        if (size + ch.size() > 75) { out.push_back(cur); cur = " "; size = 1; }
        cur += ch; size += ch.size();
    }
    out.push_back(cur);
    return join(out, "\r\n");
}
inline std::string utc_stamp(long long secs) {
    long long y; unsigned m, d; civil_from(day_of(secs), y, m, d);
    const long long r = secs - day_of(secs) * 86400;
    char b[48]; std::snprintf(b, sizeof b, "%04lld%02u%02uT%02d%02d%02dZ", y, m, d, static_cast<int>(r / 3600), static_cast<int>(r / 60 % 60), static_cast<int>(r % 60));
    return b;
}
inline int hour_of(long long secs) { return static_cast<int>((secs - day_of(secs) * 86400) / 3600); }
inline int minute_of(long long secs) { return static_cast<int>((secs - day_of(secs) * 86400) / 60 % 60); }
inline std::string human_time(long long secs) {
    int h = hour_of(secs) % 12; if (!h) h = 12;
    char b[32]; std::snprintf(b, sizeof b, "%d:%02d %s", h, minute_of(secs), hour_of(secs) < 12 ? "AM" : "PM");
    return b;
}
inline std::string build_ics(const std::string& uid, long long seq, long long stamp, long long start, long long end, const std::string& title,
                             const std::string& desc, const std::string& location, const std::string& url,
                             const std::pair<std::string, std::string>& organizer, const std::pair<std::string, std::string>& attendee, const std::string& zone) {
    std::vector<std::string> lines = {"BEGIN:VCALENDAR", "VERSION:2.0", "PRODID:-//seed-skill//seed-closer 1.0//EN", "CALSCALE:GREGORIAN", "METHOD:REQUEST"};
    if (zone.find('/') != std::string::npos || zone == "UTC") lines.push_back("X-WR-TIMEZONE:" + zone);
    for (const std::string& l : {std::string("BEGIN:VEVENT"), "UID:" + uid, "SEQUENCE:" + itos(seq), "DTSTAMP:" + utc_stamp(stamp),
                                 "DTSTART:" + utc_stamp(start), "DTEND:" + utc_stamp(end), "SUMMARY:" + ics_text(title),
                                 "DESCRIPTION:" + ics_text(desc), "LOCATION:" + ics_text(location)}) lines.push_back(l);
    if (!url.empty()) lines.push_back("URL:" + url);
    for (const std::string& l : {"ORGANIZER;CN=" + ics_param(organizer.first) + ":mailto:" + organizer.second,
                                 "ATTENDEE;CN=" + ics_param(attendee.first) + ";CUTYPE=INDIVIDUAL;ROLE=REQ-PARTICIPANT;PARTSTAT=NEEDS-ACTION;RSVP=TRUE:mailto:" + attendee.second,
                                 "ATTENDEE;CN=" + ics_param(organizer.first) + ";CUTYPE=INDIVIDUAL;ROLE=CHAIR;PARTSTAT=ACCEPTED:mailto:" + organizer.second,
                                 std::string("STATUS:CONFIRMED"), std::string("TRANSP:OPAQUE"),
                                 std::string("BEGIN:VALARM"), std::string("ACTION:DISPLAY"), "DESCRIPTION:" + ics_text(title), std::string("TRIGGER:-PT10M"), std::string("END:VALARM"),
                                 std::string("END:VEVENT"), std::string("END:VCALENDAR")}) lines.push_back(l);
    std::string o; for (const std::string& l : lines) o += ics_fold(l) + "\r\n";
    return o;
}
inline long long parse_stamp(const Val& text) {
    if (!truthy(text)) { const long long us = now_us(); return us >= 0 ? us / 1000000 : -((-us + 999999) / 1000000); }
    const re::Match m = re::match(R"(^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})(?::(\d{2}))?Z$)", strip(seed_plan::str_of(text, "strip")));
    if (!m) throw CloserError("--stamp wants a UTC time like 2026-11-02T15:04:00Z");
    const long long day = py_date(to_ll(m.group(1)), to_ll(m.group(2)), to_ll(m.group(3)));
    const long long h = to_ll(m.group(4)), mi = to_ll(m.group(5)), s = m.has(6) ? to_ll(m.group(6)) : 0;
    if (h > 23) throw PyErr("ValueError", "hour must be in 0..23");
    if (mi > 59) throw PyErr("ValueError", "minute must be in 0..59");
    if (s > 59) throw PyErr("ValueError", "second must be in 0..59");
    return day * 86400 + h * 3600 + mi * 60 + s;
}
inline long long int_conv(const Val& v) {   // int(v) for a JSON value
    switch (v.t) {
        case Val::INT: return v.big() ? throw PyErr("OverflowError", "int too large") : v.i;
        case Val::BOOL: return v.b ? 1 : 0;
        case Val::FLOAT:
            if (std::isnan(v.f)) throw PyErr("ValueError", "cannot convert float NaN to integer");
            if (std::isinf(v.f)) throw PyErr("OverflowError", "cannot convert float infinity to integer");
            return static_cast<long long>(std::trunc(v.f));
        case Val::STR: {
            const re::Match m = re::fullmatch(R"(\s*([+-]?\d+(?:_\d+)*)\s*)", v.s);
            if (!m) throw PyErr("ValueError", "invalid literal for int() with base 10: " + str_repr(v.s));
            return std::stoll(ascii_digits(replace_all(m.group(1), "_", "")));
        }
        default: throw PyErr("TypeError", std::string("int() argument must be a string, a bytes-like object or a real number, not '") + type_name(v) + "'");
    }
}

inline int cmd_book(const Val& a) {
    const long long today = get_today(a);
    const std::string dir = a_dir(a);
    const Val cfg = load_config(dir);
    const Sender snd = sender_of(cfg);
    const Val base = base_values(cfg, snd);
    Val pipe = load_pipe(dir);
    const std::string id = pystr(pget(a, "id"));
    Val rec = *find(pipe, id);
    const Val st = pget(rec, "status");
    if (!in_v(CONVERSATION, st))
        throw CloserError(id + " is " + pystr(st) + ": book a call only after they replied and agreed to a time (record the reply with inbox --write or log first)");
    const Start sp = parse_start(pget(a, "start"), pystr(pget(a, "tz")));
    if (day_of(sp.local) < today) throw CloserError(pystr(pget(a, "start")) + " is in the past (today is " + iso(today) + ")");
    long long minutes;
    if (truthy(pget(a, "end"))) {
        const re::Match m = re::match(R"(^(\d{1,2}):(\d{2})$)", strip(pystr(pget(a, "end"))));
        if (!m) throw CloserError("--end wants a time like 10:30 (same day, same zone)");
        const long long h = to_ll(m.group(1)), mi = to_ll(m.group(2));
        if (h > 23) throw PyErr("ValueError", "hour must be in 0..23");
        if (mi > 59) throw PyErr("ValueError", "minute must be in 0..59");
        const long long local_end = day_of(sp.local) * 86400 + h * 3600 + mi * 60;
        const long long secs = local_end - sp.local;
        minutes = secs >= 0 ? secs / 60 : -((-secs + 59) / 60);
    } else {
        minutes = pget(a, "minutes").i;
    }
    if (minutes < 10 || minutes > 240) throw CloserError("the call is " + itos(minutes) + " minutes; book 10 to 240");
    const long long end = sp.utc + minutes * 60, local_end = sp.local + minutes * 60;
    const std::string link = str_or(pget(a, "link"));
    if (!link.empty() && !re::match(R"(^https://\S+$)", link)) throw CloserError("--link must be a full https:// video link (Google Meet, Zoom, ...)");
    if (link.empty() && !truthy(pget(a, "location"))) throw CloserError("say where: --link https://meet.google.com/... (or --location \"Phone: ...\")");
    const std::string where = !link.empty() ? link : ws_join(pystr(pget(a, "location")));
    const Val firm = pget(rec, "firm");
    const bool use_firm = truthy(firm) && !py_eq(need(rec, "firm"), need(rec, "name")) && lower(seed_plan::str_of(need(rec, "firm"), "lower")).find("angel") == std::string::npos;
    const std::string who = pystr(use_firm ? need(rec, "firm") : need(rec, "name"));
    const std::string startup = need(base, "startup").s;
    const std::string title = truthy(pget(a, "title")) ? pystr(pget(a, "title")) : startup + " \xC3\x97 " + who + " \xC2\xB7 Intro call";
    const long long ld = day_of(sp.local);
    const std::string when = std::string(DAY_NAMES[weekday(ld)]) + ", " + MONTHS[month_of(ld) - 1] + " " + itos(mday_of(ld)) + ", " + itos(year_of(ld)) + ", " +
                             human_time(sp.local) + " to " + human_time(local_end) + " " + sp.zone + " (UTC" + fmt_offset(sp.off) + ")";
    const std::string desc = "Intro call: " + pystr(need(rec, "name")) + " (" + pystr(need(rec, "firm")) + ") and " + snd.name + " (" + startup + ").\n" + when + ".\n" +
                             (!link.empty() ? "Join: " + link : where);
    const Val old = vor(pget(rec, "call"), Val::dict());
    const Val old_uid = pget(old, "uid");
    const std::string uid = truthy(old_uid) ? pystr(old_uid)
        : "seed-" + sha1_hex(pystr(need(rec, "id")) + "|" + pystr(need(rec, "email")) + "|" + startup).substr(0, 16) + "@" + domain_of(snd.email);
    const long long seq = int_conv(pget(old, "sequence", iv(0))) + (truthy(old) && !py_eq(pget(old, "start_utc"), sv(utc_stamp(sp.utc))) ? 1 : 0);
    const std::string text = build_ics(uid, seq, parse_stamp(pget(a, "stamp")), sp.utc, end, title, desc, where, link,
                                       {snd.name, snd.email}, {pystr(need(rec, "name")), pystr(need(rec, "email"))}, sp.zone);
    const std::string rel = "calls/" + pystr(need(rec, "id")) + ".ics";
    const std::string path = path_join(dir, rel);
    write_bytes(path, text);
    set_status(rec, "booked");
    add_label(rec, "Booked");
    const Val d = pget(rec, "draft");
    if (truthy(d) && py_eq(need(d, "kind"), sv("followup"))) { history(rec, today, "voided", pystr(need(d, "file"))); set_key(rec, "draft", Val()); }
    set_key(rec, "next_due", Val());
    const std::string local_s = dt_str(sp.local, "%04lld-%02u-%02u %02d:%02d");
    set_key(rec, "call", obj({{"title", sv(title)}, {"start_utc", sv(utc_stamp(sp.utc))}, {"end_utc", sv(utc_stamp(end))}, {"local", sv(local_s)},
                              {"tz", sv(sp.zone)}, {"offset", sv(fmt_offset(sp.off))}, {"minutes", iv(minutes)}, {"link", pget(a, "link")},
                              {"location", sv(where)}, {"uid", truthy(old_uid) ? old_uid : sv(uid)}, {"sequence", iv(seq)}, {"file", sv(rel)}}));
    history(rec, today, "booked", local_s + " " + sp.zone);
    save_pipe(dir, pipe, today);
    print("Booked: " + pystr(need(rec, "name")) + " (" + pystr(need(rec, "firm")) + "), " + when + ".");
    print("  " + utc_stamp(sp.utc) + " to " + utc_stamp(end) + " UTC. Wrote " + path + " (UID " + uid + ", sequence " + itos(seq) + ").");
    if (weekday(ld) >= 5) print(std::string("  note: that is a ") + DAY_NAMES[weekday(ld)] + ".");
    print("Nothing was sent. Show the invite and ask: \"Send this invite to " + pystr(need(rec, "name")) + "? Reply yes or no.\"");
    print("After the yes: create the event in the user's own calendar with " + pystr(need(rec, "email")) +
          " as a guest (a calendar MCP server the user added in A.S.T.R.O AI's MCP settings sends the invite), or attach " + rel + " to a reply in the thread.");
    print("Then prepare the call: seed-coach agenda for " + pystr(need(rec, "id")) + ".");
    return 0;
}

// ---------------------------------------------------------------- status
inline bool contains(const Val& container, const std::string& item) {   // item in container
    switch (container.t) {
        case Val::STR: return container.s.find(item) != std::string::npos;
        case Val::LIST: for (const Val& x : *container.l) if (py_eq(x, sv(item))) return true; return false;
        case Val::DICT: return container.d->has(item);
        default: throw PyErr("TypeError", std::string("argument of type '") + type_name(container) + "' is not iterable");
    }
}
inline Val funnel(const Val& pipe) {
    const std::vector<Val> recs = investors_of(pipe);
    long long sent = 0, replied = 0, interested = 0, booked = 0;
    for (const Val& r : recs) {
        if (!out_touches(r).empty()) sent++;
        if (!in_touches(r).empty()) replied++;
        if (in_v({"interested", "booked"}, pget(r, "status")) || contains(vor(pget(r, "labels"), Val::list()), "Interested")) interested++;
        if (py_eq(pget(r, "status"), sv("booked"))) booked++;
    }
    return obj({{"drafted", iv(static_cast<long long>(recs.size()))}, {"sent", iv(sent)}, {"replied", iv(replied)}, {"interested", iv(interested)}, {"booked", iv(booked)}});
}

inline int cmd_status(const Val& a) {
    const long long today = get_today(a);
    const std::string dir = a_dir(a);
    const Val cfg = vor(read_json(path_join(dir, "config.json"), "config", false), Val::dict());
    const Rules rules = outreach_rules(cfg);
    const Val pipe = load_pipe(dir);
    const bool json = truthy(pget(a, "json"));
    if (truthy(pget(a, "id"))) {
        const Val rec = *find(pipe, pystr(pget(a, "id")));
        if (json) { print(dumps(rec, json_opts())); return 0; }
        const Val labels = pget(rec, "labels");
        print(pystr(need(rec, "name")) + ", " + pystr(need(rec, "firm")) + " <" + pystr(need(rec, "email")) + ">: " + pystr(need(rec, "status")) +
              (truthy(labels) ? " (" + seed_plan::join_strs(iter_of(need(rec, "labels")), ", ") + ")" : std::string()));
        for (const Val& h : iter_of(pget(rec, "history", Val::list())))
            print("  " + pystr(need(h, "date")) + "  " + ljust(pystr(need(h, "event")), 14) + " " + pystr(pget(h, "note", sv(""))));
        if (truthy(pget(rec, "next_due"))) print("  next follow-up: " + pystr(need(rec, "next_due")));
        if (truthy(pget(rec, "call"))) {
            const Val c = need(rec, "call");
            print("  call: " + pystr(need(c, "local")) + " " + pystr(need(c, "tz")) + " (" + pystr(need(c, "start_utc")) + " UTC)");
        }
        return 0;
    }
    const Val f = funnel(pipe);
    const std::vector<Val> recs = investors_of(pipe);
    struct By { Val key; long long n; };
    std::vector<By> by;
    for (const Val& r : recs) {
        const Val s = pget(r, "status");
        bool hit = false;
        for (By& b : by) if (b.key.t == s.t && py_eq(b.key, s)) { b.n++; hit = true; break; }
        if (!hit) by.push_back({s, 1});
    }
    const auto by_get = [&](const std::string& s) { for (const By& b : by) if (py_eq(b.key, sv(s))) return b.n; return 0LL; };
    long long waiting = 0, approved = 0, due = 0;
    std::vector<Val> calls;
    for (const Val& r : recs) {
        const Val st = pget(draft_of(r), "state");
        if (py_eq(st, sv("drafted"))) waiting++;
        if (py_eq(st, sv("approved"))) approved++;
    }
    for (const Val& r : recs) {
        if (truthy(pget(r, "draft"))) continue;
        const auto nd = next_due(r, rules);
        if (nd && *nd <= today) due++;
    }
    for (const Val& r : recs) if (py_eq(pget(r, "status"), sv("booked")) && truthy(pget(r, "call"))) calls.push_back(r);
    const long long fs = need(f, "sent").i, fr = need(f, "replied").i;
    const double rate = fs ? 100.0 * static_cast<double>(fr) / static_cast<double>(fs) : 0.0;
    if (json) {
        std::vector<By> sorted_by = by;
        std::stable_sort(sorted_by.begin(), sorted_by.end(), [](const By& x, const By& y) { return pystr(x.key) < pystr(y.key); });
        Val bys = Val::dict(); for (const By& b : sorted_by) bys.d->set(pystr(b.key), iv(b.n));
        Val cl = Val::list();
        for (const Val& r : calls) {
            Val c = copy_dict(need(r, "call"));
            set_key(c, "id", need(r, "id")); set_key(c, "name", need(r, "name")); set_key(c, "firm", need(r, "firm"));
            cl.l->push_back(c);
        }
        print(dumps(obj({{"date", sv(iso(today))}, {"funnel", f}, {"reply_rate_pct", Val::flt(std::strtod(fmt_f(rate, 1).c_str(), nullptr))},
                         {"by_status", bys}, {"waiting_for_yes", iv(waiting)}, {"approved_not_sent", iv(approved)}, {"followups_due", iv(due)},
                         {"calls", cl}}), json_opts()));
        return 0;
    }
    const Val name = vor(pget(cfg_part(cfg, "startup"), "name"), sv("Outreach"));
    print(pystr(name) + " outreach, " + short_day(today));
    for (const char* k : {"drafted", "sent", "replied", "interested", "booked"}) {
        const std::string extra = std::string(k) == "replied" && fs ? "  (" + fmt_f(rate, 0) + "% of sent)" : "";
        const std::string n = itos(need(f, k).i);
        print("  " + ljust(k, 11) + " " + std::string(n.size() < 4 ? 4 - n.size() : 0, ' ') + n + extra);
    }
    std::vector<std::pair<std::string, long long>> stopped;
    for (const std::string& s : TERMINAL) if (by_get(s)) stopped.emplace_back(s, by_get(s));
    if (!stopped.empty()) {
        long long soft = 0;
        for (const Val& r : recs) if (py_eq(pget(r, "status"), sv("passed")) && contains(vor(pget(r, "labels"), Val::list()), "Not now")) soft++;
        std::vector<std::string> parts;
        for (const auto& [s, n] : stopped) parts.push_back(itos(n) + " " + s + (s == "passed" && soft ? " (" + itos(soft) + " not now)" : std::string()));
        print("  stopped: " + join(parts, ", "));
    }
    print("  waiting for a yes: " + itos(waiting) + "   approved, not sent: " + itos(approved) + "   follow-ups due today: " + itos(due));
    for (const Val& r : calls)
        print("  call: " + pystr(need(r, "name")) + " (" + pystr(need(r, "firm")) + ") " + pystr(need(need(r, "call"), "local")) + " " + pystr(need(need(r, "call"), "tz")));
    return 0;
}
}  // namespace packtools::seed_closer
