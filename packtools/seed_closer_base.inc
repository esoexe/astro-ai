// seed-closer, part 2: closer.py's own building blocks. Files, business days, the outreach rules in config.json, the
// sender, the template and its placeholders, the personal-line check, and the pipeline records (seed/pipeline.json).
#include <algorithm>
#include <map>
#include <optional>
#include <set>
namespace packtools::seed_closer {
using namespace pyrt;
using namespace packtools::seed;
using seed_model::py_eq;
using seed_plan::pget;
using seed_plan::vor;
using seed_scout::int_str;

struct CloserError : std::runtime_error { explicit CloserError(const std::string& m) : std::runtime_error(m) {} };

inline const std::string OPTOUT = "Not a fit? Reply 'no' and I won't follow up.";
inline constexpr long long MAX_CAP = 50, MAX_TOUCHES = 3;
inline const std::vector<std::string> COLD = {"intro", "followup"};
inline const std::vector<std::string> TERMINAL = {"passed", "bounced", "optout"};
inline const std::vector<std::string> CONVERSATION = {"replied", "interested", "booked"};
inline const std::vector<std::pair<std::string, std::vector<std::string>>> ALLOWED = {
    {"drafted", {"drafted", "approved"}},
    {"approved", {"drafted", "sent"}},
    {"sent", {"sent", "replied", "interested", "passed", "bounced", "optout"}},
    {"replied", {"replied", "interested", "booked", "passed", "bounced", "optout"}},
    {"interested", {"interested", "booked", "passed", "bounced", "optout"}},
    {"booked", {"booked", "passed", "bounced", "optout"}},
    {"passed", {}}, {"bounced", {}}, {"optout", {}},
};
inline const std::map<std::string, int> RANK = {{"replied", 1}, {"interested", 2}, {"booked", 3}};
inline const std::map<std::string, std::string> LABELS = {
    {"interested", "Interested"}, {"question", "Asked a question"}, {"not_now", "Not now"}, {"no", "No"},
    {"optout", "Opt-out"}, {"bounce", "Bounced"}, {"ooo", "Out of office"}, {"unclear", "Replied"}};
inline const std::map<std::string, std::string> NEW_STATUS = {
    {"interested", "interested"}, {"question", "replied"}, {"not_now", "passed"}, {"no", "passed"},
    {"optout", "optout"}, {"bounce", "bounced"}, {"unclear", "replied"}};
inline const std::vector<std::pair<std::string, std::string>> EVENT_CLASS = {
    {"interested", "interested"}, {"question", "question"}, {"replied", "unclear"}, {"notnow", "not_now"},
    {"passed", "no"}, {"no", "no"}, {"optout", "optout"}, {"bounced", "bounce"}, {"ooo", "ooo"}};
inline std::vector<std::string> events() {   // ("sent", "denied", "note") + tuple(EVENT_CLASS)
    std::vector<std::string> o = {"sent", "denied", "note"};
    for (const auto& [k, v] : EVENT_CLASS) o.push_back(k);
    return o;
}
inline const std::string DEFAULT_ASK = "would a 20-minute call next week work?";
inline const std::vector<std::string> PERSONAL_DOMAINS = {"gmail.com", "googlemail.com", "yahoo.com", "hotmail.com", "outlook.com", "live.com",
                                                          "icloud.com", "me.com", "aol.com", "proton.me", "protonmail.com"};
inline const std::set<std::string> PLACEHOLDER_NAMES = {"your name", "name", "founder", "you", "me", "first last", "firstname lastname",
                                                        "jane doe", "john doe", "todo", "tbd", "your full name"};
inline const char* const DAY_NAMES[] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
inline const char* const MONTHS[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September",
                                     "October", "November", "December"};
inline const char* const EMAIL_RE = R"(^[A-Za-z0-9._%+'-]+@[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)+$)";
inline const char* const ID_RE = R"(^[a-z0-9][a-z0-9-]{0,63}$)";
inline const char* const PLACEHOLDER_RE = R"([{}\[\]<>]|\b(todo|tbd|xxx|lorem|ipsum|placeholder|insert here)\b)";

// ---------------------------------------------------------------- small Python-isms
template <class C> inline bool in(const C& c, const std::string& s) { return std::find(c.begin(), c.end(), s) != c.end(); }
inline bool in_v(const std::vector<std::string>& c, const Val& v) { return v.t == Val::STR && in(c, v.s); }   // v in ("a", "b")
inline std::string str_or(const Val& v) { return truthy(v) ? pystr(v) : std::string(); }   // str(v or "")
inline std::string ws_join(const std::string& s) { return join(split_ws(s), " "); }        // " ".join(s.split())
inline std::string rstrip_c(std::string s, const std::string& cs) { while (!s.empty() && cs.find(s.back()) != std::string::npos) s.pop_back(); return s; }
inline std::string lstrip_c(const std::string& s, const std::string& cs) { std::size_t i = 0; while (i < s.size() && cs.find(s[i]) != std::string::npos) i++; return s.substr(i); }
inline std::string strip_c(const std::string& s, const std::string& cs) { return lstrip_c(rstrip_c(s, cs), cs); }
inline bool starts(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }
inline bool ends(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }
inline std::string plural(long long n, const char* one = "", const char* many = "s") { return n == 1 ? one : many; }
inline std::string itos(long long n) { return std::to_string(n); }
inline Val need(const Val& o, const std::string& k) {   // o[k]
    if (o.t == Val::DICT) { const Val* v = o.d->get(k); if (!v) throw PyErr("KeyError", str_repr(k)); return *v; }
    if (o.t == Val::LIST || o.t == Val::STR) throw PyErr("TypeError", std::string(o.t == Val::LIST ? "list" : "string") + " indices must be integers" + (o.t == Val::LIST ? " or slices, not str" : ", not 'str'"));
    throw PyErr("TypeError", std::string("'") + type_name(o) + "' object is not subscriptable");
}
inline std::vector<Val> iter_of(const Val& v) {   // for x in v
    switch (v.t) {
        case Val::LIST: return *v.l;
        case Val::DICT: { std::vector<Val> o; for (const auto& [k, x] : v.d->items) o.push_back(Val::str(k)); return o; }
        case Val::STR: { std::vector<Val> o; for (const std::string& c : cps(v.s)) o.push_back(Val::str(c)); return o; }
        default: throw PyErr("TypeError", std::string("'") + type_name(v) + "' object is not iterable");
    }
}
inline Val copy_dict(const Val& d) { Val c = Val::dict(); c.d->items = d.d->items; return c; }   // dict(d)
inline void set_key(Val& d, const std::string& k, const Val& v) { d.d->set(k, v); }
inline Val list_of(const std::vector<std::string>& v) { Val l = Val::list(); for (const std::string& s : v) l.l->push_back(Val::str(s)); return l; }
inline Val list_append_default(Val& rec, const std::string& key) {   // rec.setdefault(key, []) for .append
    const Val* cur = rec.d->get(key);
    if (!cur) { Val l = Val::list(); rec.d->set(key, l); return l; }
    if (cur->t != Val::LIST) throw PyErr("AttributeError", std::string("'") + type_name(*cur) + "' object has no attribute 'append'");
    return *cur;
}
inline std::string sha(const std::string& data) { return sha256_hex(data); }

// ---------------------------------------------------------------- files
inline Val read_json(const std::string& path, const std::string& what, bool required = true) {
    if (!exists(path)) {
        if (required) throw CloserError("no " + what + " at " + path);
        return Val();
    }
    std::string text;
    try { text = read_text_utf8(path); }
    catch (const PyErr& e) { if (e.type == "UnicodeDecodeError") throw CloserError(path + " is not valid JSON: " + e.what()); throw; }
    try { return loads(text); }
    catch (const PyErr& e) { if (e.type == "json.decoder.JSONDecodeError" || e.type == "ValueError") throw CloserError(path + " is not valid JSON: " + e.what()); throw; }
}
inline DumpOpts json_opts(bool ascii = false) { DumpOpts o; o.indent = 2; o.ensure_ascii = ascii; return o; }
inline void write_json(const std::string& path, const Val& data) {
    const std::string d = pyrt::dirname(path);
    if (!d.empty()) makedirs(d);
    const std::string tmp = path + ".tmp";
    write_text(tmp, dumps(data, json_opts()) + "\n");
    os_replace(tmp, path);
}
inline void write_bytes(const std::string& path, const std::string& data) {
    const std::string d = pyrt::dirname(path);
    if (!d.empty()) makedirs(d);
    write_text(path, data);
}
inline std::string slug(const Val& text) { return strip_c(re::sub("[^a-z0-9]+", "-", lower(str_or(text))), "-"); }

// ---------------------------------------------------------------- dates and business days (days since 1970-01-01)
inline long long parse_day(const Val& text, const std::string& what = "--today") {
    const re::Match m = re::match(R"(^(\d{4})-(\d{2})-(\d{2})$)", strip(str_or(text)));
    if (!m) throw CloserError(what + " wants a date like 2026-10-26, got " + repr(text));
    const long long y = to_ll(m.group(1)), mo = to_ll(m.group(2)), d = to_ll(m.group(3));
    if (!valid_date(y, static_cast<int>(mo), static_cast<int>(d))) throw CloserError(what + ": " + pystr(text) + " is not a real date");
    return make_day(y, static_cast<int>(mo), static_cast<int>(d));
}
inline long long parse_day_s(const std::string& s, const std::string& what = "--today") { return parse_day(Val::str(s), what); }
inline std::string iso(long long d) { return iso_day(d); }
inline int weekday(long long d) { return static_cast<int>(((d % 7) + 7 + 3) % 7); }
inline int month_of(long long d) { long long y; unsigned m, dd; civil_from(d, y, m, dd); return static_cast<int>(m); }
inline int mday_of(long long d) { long long y; unsigned m, dd; civil_from(d, y, m, dd); return static_cast<int>(dd); }
inline long long year_of(long long d) { long long y; unsigned m, dd; civil_from(d, y, m, dd); return y; }
inline std::string short_day(long long d) { return std::string(DAY_NAMES[weekday(d)]).substr(0, 3) + " " + iso(d); }   // Mon 2026-10-26
inline std::string spoken_day(long long d) {   // Monday, Oct 26
    return std::string(DAY_NAMES[weekday(d)]) + ", " + std::string(MONTHS[month_of(d) - 1]).substr(0, 3) + " " + itos(mday_of(d));
}

struct Rules { long long cap = 20; std::vector<long long> follow; long long max_touches = 3; bool business = true; std::set<std::string> holidays; };
inline bool is_workday(long long d, const Rules& r) { return !r.business || (weekday(d) < 5 && !r.holidays.count(iso(d))); }
inline long long add_days(long long d, long long n, const Rules& r) {   // n business days after d, or n calendar days
    if (!r.business) return d + n;
    while (n > 0) { d += 1; if (is_workday(d, r)) n -= 1; }
    return d;
}
inline long long next_workday(long long d, const Rules& r) { while (!is_workday(d, r)) d += 1; return d; }

// ---------------------------------------------------------------- config
inline Val load_config(const std::string& dir) {
    return read_json(path_join(dir, "config.json"), "config (seed-plan writes it: plan.py init, then fill in your details)");
}
inline Val cfg_part(const Val& cfg, const std::string& key) {   // (cfg or {}).get(key) or {}
    if (!truthy(cfg)) return Val::dict();
    return vor(pget(cfg, key), Val::dict());
}
inline void dict_update(Val& o, const Val& other) {   // o.update(other)
    if (other.t == Val::DICT) { for (const auto& [k, v] : other.d->items) o.d->set(k, v); return; }
    if (other.t != Val::LIST && other.t != Val::STR) throw PyErr("TypeError", std::string("'") + type_name(other) + "' object is not iterable");
    const std::vector<Val> els = iter_of(other);
    for (std::size_t i = 0; i < els.size(); ++i) {
        const Val& e = els[i];
        if (e.t != Val::LIST && e.t != Val::STR && e.t != Val::DICT)
            throw PyErr("TypeError", "cannot convert dictionary update sequence element #" + itos(static_cast<long long>(i)) + " to a sequence");
        const std::vector<Val> kv = iter_of(e);
        if (kv.size() != 2)
            throw PyErr("ValueError", "dictionary update sequence element #" + itos(static_cast<long long>(i)) + " has length " + itos(static_cast<long long>(kv.size())) + "; 2 is required");
        if (kv[0].t == Val::LIST || kv[0].t == Val::DICT) throw PyErr("TypeError", std::string("unhashable type: '") + type_name(kv[0]) + "'");
        if (kv[0].t == Val::STR) o.d->set(kv[0].s, kv[1]);
    }
}
inline bool plain_int(const Val& v) { return v.t == Val::INT; }   // isinstance(v, int) and not isinstance(v, bool)
inline int big_sign(const Val& v) { return v.big() ? (v.s[0] == '-' ? -1 : 1) : (v.i < 0 ? -1 : v.i > 0 ? 1 : 0); }
inline Rules outreach_rules(const Val& cfg) {
    Val o = obj({{"daily_cap", iv(20)}, {"followup_days", Val::list({iv(3), iv(7)})}, {"max_touches", iv(3)}, {"business_days", bv(true)}});
    const Val c = truthy(cfg) ? cfg : Val::dict();
    const Val upd = pget(c, "outreach");
    if (truthy(upd)) dict_update(o, upd);
    Rules r;
    const Val cap = pget(o, "daily_cap");
    if (!plain_int(cap) || big_sign(cap) < 1) throw CloserError("outreach.daily_cap must be a whole number of emails a day, like 20");
    if (cap.big() || cap.i > MAX_CAP)
        throw CloserError("outreach.daily_cap is " + int_str(big_of(cap)) + "; the Closer allows at most " + itos(MAX_CAP) +
                          " a day. More than that from one inbox looks like spam and hurts every email you send.");
    r.cap = cap.i;
    const Val days = pget(o, "followup_days");
    bool ok = days.t == Val::LIST;
    if (ok) {
        for (const Val& x : *days.l) if (!plain_int(x) || big_sign(x) < 1 || x.big()) { ok = false; break; }
        for (std::size_t i = 1; ok && i < days.l->size(); ++i) if ((*days.l)[i].i <= (*days.l)[i - 1].i) ok = false;
    }
    if (!ok) throw CloserError("outreach.followup_days must be rising whole days after the intro, like [3, 7]");
    const Val touches = pget(o, "max_touches");
    if (!plain_int(touches) || touches.big() || touches.i < 1 || touches.i > MAX_TOUCHES)
        throw CloserError("outreach.max_touches is " + repr(touches) + "; it is 1 to " + itos(MAX_TOUCHES) + " (the intro and up to two follow-ups)");
    const Val hol = vor(pget(o, "holidays"), Val::list());
    if (hol.t != Val::LIST) throw CloserError("outreach.holidays must be a list of dates like [\"2026-11-26\"]");
    for (const Val& h : *hol.l) r.holidays.insert(iso(parse_day(h, "outreach.holidays")));
    for (std::size_t i = 0; i < days.l->size() && static_cast<long long>(i) < touches.i - 1; ++i) r.follow.push_back((*days.l)[i].i);
    r.max_touches = 1 + static_cast<long long>(r.follow.size());
    r.business = truthy(pget(o, "business_days", bv(true)));
    return r;
}

inline bool valid_email(const std::string& addr) { return re::match(EMAIL_RE, addr) && addr.find("..") == std::string::npos; }
inline std::string after_at(const std::string& addr) { const std::size_t at = addr.rfind('@'); return at == std::string::npos ? addr : addr.substr(at + 1); }
inline bool personal_mailbox(const std::string& addr) { return in(PERSONAL_DOMAINS, after_at(lower(addr))); }

struct Sender { std::string name, email, first, title, address; };
inline Sender sender_of(const Val& cfg) {
    const Val f = cfg_part(cfg, "founder");
    Sender s;
    s.name = ws_join(str_or(pget(f, "name")));
    s.email = strip(str_or(pget(f, "email")));
    if (s.name.empty() || PLACEHOLDER_NAMES.count(lower(s.name)) || re::search(PLACEHOLDER_RE, s.name, re::I))
        throw CloserError("seed/config.json founder.name is " + str_repr(s.name) + ": put your real name there. Every email is signed by a real person.");
    if (!valid_email(s.email))
        throw CloserError("seed/config.json founder.email must be the address you send from (your own inbox), got " + str_repr(s.email));
    s.first = split_ws(s.name)[0];
    s.title = ws_join(str_or(vor(pget(f, "title"), sv("Founder"))));
    s.address = ws_join(str_or(pget(f, "company_address")));
    return s;
}

inline std::string pct_f(double x, int prec) { return fmt_f(x, prec); }   // "%.<prec>f" % x
inline Val money_short(const Val& v) {
    if (v.t == Val::NONE || (v.t == Val::STR && v.s.empty())) return Val();
    if (v.t == Val::STR) return sv(strip(v.s));
    const double x = to_float(v);
    const std::pair<const char*, double> units[] = {{"B", 1e9}, {"M", 1e6}, {"K", 1e3}};
    for (const auto& [unit, div] : units)
        if (std::fabs(x) >= div) return sv("$" + rstrip_c(rstrip_c(pct_f(x / div, 2), "0"), ".") + unit);
    return sv("$" + pct_f(x, 0));
}
inline std::string sentence_case(const Val& text) {   // 'A dog-walking app.' -> 'a dog-walking app' (acronyms are left alone)
    const std::string s = rstrip_c(strip(str_or(text)), ".");
    const std::size_t sp = s.find(' ');
    const std::string first = sp == std::string::npos ? s : s.substr(0, sp);
    if (first == "A" || first == "An" || first == "The" ||
        (cplen(first) > 1 && isupper_str(cpslice(first, 0, 1)) && islower_str(cpslice(first, 1))))
        return lower(cpslice(s, 0, 1)) + cpslice(s, 1);
    return s;
}
inline Val base_values(const Val& cfg, const Sender& snd) {
    const Val st = cfg_part(cfg, "startup");
    const std::string name = strip(str_or(pget(st, "name")));
    const std::string one = strip(str_or(pget(st, "one_liner")));
    if (name.empty() || one.empty()) throw CloserError("seed/config.json needs startup.name and startup.one_liner for the email");
    std::vector<std::string> sig;
    for (const std::string& x : {snd.name, snd.title + ", " + name, snd.address}) if (!x.empty()) sig.push_back(x);
    const auto or_none = [](const Val& v) { return truthy(v) ? v : Val(); };
    return obj({{"startup", sv(name)}, {"one_liner", sv(rstrip_c(one, "."))}, {"one_liner_sentence", sv(sentence_case(sv(one)))},
                {"tagline", sv(sentence_case(vor(pget(st, "tagline"), sv(one))))}, {"raise", money_short(pget(st, "raise"))},
                {"stage", sv(strip(str_or(vor(pget(st, "stage"), sv("seed")))))}, {"instrument", or_none(pget(st, "instrument"))},
                {"sector", or_none(pget(st, "sector"))}, {"geography", or_none(pget(st, "geography"))},
                {"sender", sv(snd.name)}, {"sender_first", sv(snd.first)}, {"title", sv(snd.title)},
                {"company_address", snd.address.empty() ? Val() : sv(snd.address)}, {"optout", sv(OPTOUT)},
                {"signature", sv(join(sig, "\n"))}, {"extra", sv("")}, {"ask", sv(DEFAULT_ASK)}});
}

// ---------------------------------------------------------------- template
inline const char* const SECTION_RE = R"(^=== *(.+?) *=== *$)";
inline const std::vector<std::string> NEEDED = {"intro", "followup 1", "followup 2"};
inline const std::map<std::string, std::string> MISSING_WHY = {
    {"raise", "add startup.raise to seed/config.json"}, {"company_address", "add founder.company_address"},
    {"instrument", "add startup.instrument"}, {"sector", "add startup.sector"}, {"geography", "add startup.geography"}};
struct Section { std::string subject, body; };
using Template = std::map<std::string, Section>;

inline std::string default_template() { return abspath(packtools::skill_file("template.txt")); }
inline std::string template_path(const Val& a) {
    if (truthy(pget(a, "template"))) return pystr(pget(a, "template"));
    const std::string mine = path_join(pystr(pget(a, "dir")), "template.txt");
    return exists(mine) ? mine : default_template();
}
inline Template load_template(const std::string& path) {
    if (!exists(path)) throw CloserError("no template at " + path);
    const std::string text = replace_all(read_text_utf8(path), "\r\n", "\n");
    std::vector<std::pair<std::string, std::vector<std::string>>> sections;   // a dict: first position kept, last value wins
    const auto put = [&](const std::string& n, const std::vector<std::string>& b) {
        for (auto& [k, v] : sections) if (k == n) { v = b; return; }
        sections.emplace_back(n, b);
    };
    std::optional<std::string> name; std::vector<std::string> buf;
    for (const std::string& line : split(text, "\n")) {
        const re::Match m = re::match(SECTION_RE, line);
        if (m) {
            if (name && !name->empty()) put(*name, buf);
            name = lower(strip(m.group(1))); buf.clear();
        } else if (name) buf.push_back(line);
    }
    if (name && !name->empty()) put(*name, buf);
    Template out;
    for (auto& [n, lines] : sections) {
        std::size_t i = 0; while (i < lines.size() && strip(lines[i]).empty()) i++;
        if (i == lines.size() || !starts(lower(lines[i]), "subject:"))
            throw CloserError("template " + path + ": the '" + n + "' section must start with a Subject: line");
        const std::vector<std::string> rest(lines.begin() + static_cast<long>(i) + 1, lines.end());
        out[n] = Section{strip(lines[i].substr(lines[i].find(':') + 1)), strip_c(join(rest, "\n"), "\n")};
    }
    for (const std::string& n : NEEDED) if (!out.count(n)) throw CloserError("template " + path + " has no '=== " + n + " ===' section");
    return out;
}

inline std::string tidy(const std::string& text) {
    std::vector<std::string> ls; for (const std::string& l : split(text, "\n")) ls.push_back(rstrip(l));
    std::string s = join(ls, "\n");
    s = re::sub("[ \\t]{2,}", " ", s);
    s = re::sub("\\n{3,}", "\n\n", s);
    return strip(s);
}
inline std::string fill(const std::string& text, const Val& values, const std::string& where) {
    std::vector<FmtPiece> parsed;
    try { parsed = fmt_parse(text); }
    catch (const PyErr& e) { if (e.type != "ValueError") throw; throw CloserError(where + ": " + e.what() + " (write a literal brace as {{ or }})"); }
    std::set<std::string> used;
    for (const FmtPiece& p : parsed) {
        if (!p.field) continue;
        if (!re::match(R"(^\w+$)", p.name)) throw CloserError(where + ": {" + p.name + "} is not a placeholder the Closer knows");
        used.insert(p.name);
    }
    for (const std::string& f : used)
        if (!values.d->get(f)) {
            std::vector<std::string> known; for (const auto& [k, v] : values.d->items) known.push_back(k);
            std::sort(known.begin(), known.end());
            throw CloserError(where + " uses {" + f + "}, which the Closer does not fill. It knows: " + join(known, ", "));
        }
    for (const std::string& f : used)
        if (values.d->get(f)->is_none()) {
            const auto w = MISSING_WHY.find(f);
            throw CloserError(where + " uses {" + f + "} but it is empty: " + (w == MISSING_WHY.end() ? std::string("fill it in") : w->second));
        }
    return tidy(format_map(text, values));
}
inline long long words(const std::string& text) { return static_cast<long long>(re::findall(R"(\S+)", text).size()); }

// ---------------------------------------------------------------- the personal line
inline const std::vector<std::string> GENERIC = {
    R"x(hope (this|my|the) (email|e-mail|note|message) finds you)x",
    R"x(hope you('re| are) (doing )?(well|good|great))x",
    R"x(hope all is well)x",
    R"x(\b(big|huge|long[- ]time|longtime) fan\b)x",
    R"x(\bi('m| am) (a|such a) fan\b)x",
    R"x(\b(love|loved|admire|admired|respect|appreciate)( all)? (what|the work|your work|everything)\b)x",
    R"x(\b(impressive|amazing|great|incredible|stellar|fantastic|strong|awesome) (portfolio|track record|firm|fund|work|team|career|background|experience)\b)x",
    R"x(\bcame across your (profile|firm|fund|website|name|page|linkedin|work)\b)x",
    R"x(\b(found|saw) your (profile|firm|fund|website|name|page) (on|online|and)\b)x",
    R"x(\bwanted to (reach out|connect|introduce myself|get in touch)\b)x",
    R"x(\bi('m| am) reaching out\b)x",
    R"x(\byou('re| are) (a|an|such a) (great|amazing|top|leading|respected|successful|smart|experienced|legendary|well[- ]known) (investor|vc|partner|angel)\b)x",
    R"x(\byou invest in (startups|companies|founders|great|early[- ]stage|the best)\b)x",
    R"x(\bthought leader)x",
    R"x(\bi know you('re| are) (busy|swamped)\b)x",
    R"x(\b(perfect|great|good) fit for your (fund|firm|portfolio)\b)x",
    R"x(\bi think you('d| would) (love|like|be interested|be excited)\b)x",
    R"x(\byour (fund|firm|portfolio) (is|looks) (great|amazing|interesting|impressive)\b)x",
};
inline std::set<std::string> word_set(const std::string& s) { const auto v = split_ws(s); return std::set<std::string>(v.begin(), v.end()); }
inline const std::set<std::string> STOP = word_set(
    "a an the and or but so of to in on at for with from by as is are was were be been being it its "
    "this that these those you your yours yourself i me my we our us they them their he she his her "
    "about into over than then just very more most also how what who when why which all any some can "
    "will would should could has have had do does did not no yes one get got make made");
inline const std::set<std::string> SOFT = word_set(
    "startup startups invest invests invested investing investor investors investment investments fund "
    "funds firm firms venture ventures capital partner partners seed round rounds company companies "
    "founder founders portfolio vc vcs team teams work great space sector industry market markets "
    "thesis focus early stage angel love like");
inline const std::set<std::string> NUMBER_WORDS = {"two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven", "twelve",
                                                   "dozen", "twenty", "thirty", "fifty", "hundred", "thousand", "million", "billion"};
inline std::set<std::string> tokens(const std::string& text) {
    std::set<std::string> out;
    for (const std::string& w : re::findall(R"([a-z0-9]+(?:['-][a-z0-9]+)*)", replace_all(lower(text), "\xE2\x80\x99", "'"))) {
        out.insert(w);
        if (w.find('-') != std::string::npos) for (const std::string& p : split(w, "-")) out.insert(p);
    }
    return out;
}
inline void flat_strings(const Val& v, std::vector<std::string>& out) {
    if (v.t == Val::STR) out.push_back(v.s);
    else if (v.t == Val::DICT) { for (const auto& [k, x] : v.d->items) flat_strings(x, out); }
    else if (v.t == Val::LIST) { for (const Val& x : *v.l) flat_strings(x, out); }
}
inline std::string norm_line(const std::string& text) { return strip(re::sub("[^a-z0-9]+", " ", lower(text))); }
inline std::pair<bool, std::string> check_personal_line(const std::string& line, const Val& inv, const std::string& startup = "") {
    const std::string s = ws_join(line);
    if (s.empty()) return {false, "no personal_line: write one real sentence about this investor from their public record (a deal they led, a post, a talk), with the source in the row"};
    if (re::search(PLACEHOLDER_RE, s, re::I)) return {false, "the personal line still has a placeholder in it: " + str_repr(s)};
    if (s.find("\xE2\x80\xA6") != std::string::npos || ends(s, "...")) return {false, "the personal line is cut off (it ends in an ellipsis): finish the sentence"};
    const long long n = words(s);
    if (n < 7) return {false, "the personal line is " + itos(n) + " words: a real one says what they did and why it matters to you"};
    if (n > 45) return {false, "the personal line is " + itos(n) + " words: keep it to one sentence (45 words at most)"};
    const std::string low = replace_all(lower(s), "\xE2\x80\x99", "'");
    for (const std::string& pat : GENERIC) {
        const re::Match m = re::search(pat, low);
        if (m) return {false, "the personal line is generic (\"" + m.group(0) + "\"): it could go to any investor. Say what this one actually did, from a source"};
    }
    std::set<std::string> who = tokens(pystr(pget(inv, "name", sv("")))), t2 = tokens(pystr(pget(inv, "firm", sv("")))), t3 = tokens(startup);
    who.insert(t2.begin(), t2.end()); who.insert(t3.begin(), t3.end());
    std::set<std::string> record;
    for (const char* field : {"last_relevant_deal", "reasons", "sectors", "geo", "role", "focus", "why", "notes"}) {
        std::vector<std::string> texts; flat_strings(pget(inv, field), texts);
        for (const std::string& t : texts) { const auto k = tokens(t); record.insert(k.begin(), k.end()); }
    }
    const std::set<std::string> mine = tokens(s);
    std::vector<std::string> anchors;
    for (const std::string& w : record) if (!STOP.count(w) && !SOFT.count(w) && !who.count(w) && mine.count(w)) anchors.push_back(w);
    if (!anchors.empty()) {
        anchors.resize(std::min<std::size_t>(3, anchors.size()));
        return {true, "specific: mentions " + join(anchors, ", ") + " from their record"};
    }
    bool num_word = false; for (const std::string& w : mine) if (NUMBER_WORDS.count(w)) num_word = true;
    if (re::search(R"(\d)", s) || num_word) return {true, "specific: has a number"};
    for (const std::string& sentence : re::split(R"([.!?;:]\s+)", s)) {
        const std::vector<std::string> ws = re::findall(R"([A-Za-z][\w&'.-]*)", sentence);
        for (std::size_t i = 1; i < ws.size(); ++i) {
            const std::string& w = ws[i];
            if (w[0] >= 'A' && w[0] <= 'Z' && !who.count(strip_c(lower(w), ".'")) && w != "I" && w != "I'm" && w != "I've" && w != "I'd")
                return {true, "specific: names " + strip_c(w, ".")};
        }
    }
    return {false, "the personal line mentions nothing from this investor's record (a deal, a post, a sector, a number): it reads like it could go to anyone"};
}

// ---------------------------------------------------------------- .eml
inline std::string domain_of(const std::string& addr) { return lower(after_at(addr)); }
inline std::string message_id(const std::string& rec_id, long long n, const std::string& subject, long long day, const Sender& snd) {
    const std::string h = sha1_hex(rec_id + "|" + itos(n) + "|" + subject + "|" + iso(day)).substr(0, 12);
    return "<" + rec_id + "-" + itos(n) + "." + h + "@" + domain_of(snd.email) + ">";
}
inline std::string make_eml(const Sender& snd, const std::string& to_name, const std::string& to_email, const std::string& subject,
                            const std::string& body, long long day, const std::string& mid, const std::string& in_reply_to = "") {
    return build_eml(snd.name, snd.email, to_name, to_email, subject, body, day, mid, in_reply_to);
}
struct ReadEml { Eml msg; std::string data; };
inline ReadEml read_eml(const std::string& dir, const Val& rel) {
    const std::string path = path_join(dir, pystr(rel));
    if (!exists(path)) throw CloserError("the draft " + path + " is missing: run draft (or followup) again");
    ReadEml r; r.data = read_bytes(path); r.msg = parse_eml(r.data);
    return r;
}
inline std::string eml_body(const Eml& msg) { return rstrip_c(replace_all(msg.content(), "\r\n", "\n"), "\n"); }

// ---------------------------------------------------------------- pipeline
inline std::string pipe_path(const std::string& dir) { return path_join(dir, "pipeline.json"); }
inline Val load_pipe(const std::string& dir) {
    Val doc = read_json(pipe_path(dir), "pipeline", false);
    if (!truthy(doc)) doc = obj({{"investors", Val::list()}});
    if (pget(doc, "investors").t != Val::LIST) throw CloserError(pipe_path(dir) + " has no investors list");
    return doc;
}
inline void save_pipe(const std::string& dir, Val& pipe, long long today) { set_key(pipe, "updated", sv(iso(today))); write_json(pipe_path(dir), pipe); }
inline std::vector<Val> investors_of(const Val& pipe) { return *need(pipe, "investors").l; }
inline std::optional<Val> find(const Val& pipe, const std::string& rec_id, bool required = true) {
    for (const Val& rec : investors_of(pipe)) if (py_eq(pget(rec, "id"), sv(rec_id))) return rec;
    if (required) throw CloserError(rec_id + " is not in the pipeline (draft them first)");
    return std::nullopt;
}
inline void set_status(Val& rec, const std::string& nw) {
    const Val old = pget(rec, "status");
    bool ok = false;
    if (old.is_none()) ok = nw == "drafted";
    else if (old.t == Val::STR) for (const auto& [k, v] : ALLOWED) if (k == old.s) ok = in(v, nw);
    if (!ok) throw CloserError(pystr(pget(rec, "id")) + " cannot go from " + (truthy(old) ? pystr(old) : std::string("nothing")) + " to " + nw);
    set_key(rec, "status", sv(nw));
}
inline void history(Val& rec, long long day, const std::string& event, const std::string& note = "") {
    list_append_default(rec, "history").l->push_back(obj({{"date", sv(iso(day))}, {"event", sv(event)}, {"note", sv(note)}}));
}
inline std::vector<Val> touches_of(const Val& rec) { return iter_of(pget(rec, "touches", Val::list())); }
inline std::vector<Val> out_touches(const Val& rec, const std::vector<std::string>* kinds = nullptr) {
    std::vector<Val> o;
    for (const Val& t : touches_of(rec))
        if (py_eq(pget(t, "from"), sv("us")) && (!kinds || in_v(*kinds, pget(t, "kind")))) o.push_back(t);
    return o;
}
inline std::vector<Val> in_touches(const Val& rec) {
    std::vector<Val> o; for (const Val& t : touches_of(rec)) if (py_eq(pget(t, "from"), sv("them"))) o.push_back(t);
    return o;
}
inline std::optional<long long> next_due(const Val& rec, const Rules& r) {   // the day the next follow-up is due, or None
    if (!py_eq(pget(rec, "status"), sv("sent")) || !in_touches(rec).empty()) return std::nullopt;
    const std::vector<Val> cold = out_touches(rec, &COLD);
    const std::size_t k = cold.size();
    if (!k || k > r.follow.size()) return std::nullopt;
    long long due = add_days(parse_day(need(cold[0], "date"), "touch date"), r.follow[k - 1], r);
    if (k >= 2) due = std::max(due, add_days(parse_day(need(cold.back(), "date"), "touch date"), r.follow[k - 1] - r.follow[k - 2], r));
    if (truthy(pget(rec, "hold_until"))) due = std::max(due, add_days(parse_day(need(rec, "hold_until"), "hold_until"), 1, r));
    return due;
}
inline void add_label(Val& rec, const std::string& label) {
    Val labels = Val::list();
    for (const Val& x : iter_of(vor(pget(rec, "labels"), Val::list())))
        if (!py_eq(x, sv(label)) && (label == "Out of office" || !py_eq(x, sv("Out of office")))) labels.l->push_back(x);
    labels.l->push_back(sv(label));
    set_key(rec, "labels", labels);
}
inline long long cold_used(const Val& pipe, long long today) {   // cold emails sent today plus every approved one not sent yet
    long long n = 0;
    for (const Val& rec : investors_of(pipe)) {
        for (const Val& t : out_touches(rec, &COLD)) if (py_eq(pget(t, "date"), sv(iso(today)))) n++;
        const Val d = pget(rec, "draft");
        if (truthy(d) && py_eq(pget(d, "state"), sv("approved")) && in_v(COLD, pget(d, "kind"))) n++;
    }
    return n;
}

// ---------------------------------------------------------------- investors.json
inline std::vector<Val> load_investors(const std::string& dir) {
    const std::string path = path_join(dir, "investors.json");
    const Val doc = read_json(path, "investors file (seed-investors writes it: investors.py rank)");
    const Val rows = doc.t == Val::DICT ? pget(doc, "investors") : doc;
    if (rows.t != Val::LIST) throw CloserError(path + " has no investors list");
    std::vector<Val> out;
    for (const Val& r0 : *rows.l) {
        if (r0.t != Val::DICT) continue;
        Val r = copy_dict(r0);
        const Val id = pget(r, "id");
        set_key(r, "id", sv(truthy(id) ? pystr(id) : slug(pget(r, "name"))));
        out.push_back(r);
    }
    return out;
}
inline double fit_of(const Val& r) {
    try { return to_float(vor(pget(r, "fit"), iv(0))); }
    catch (const PyErr& e) { if (e.type == "TypeError" || e.type == "ValueError") return 0.0; throw; }
}
inline std::string id_of(const Val& r) { return need(r, "id").s; }   // load_investors made every id a str
inline std::optional<std::string> row_problem(const Val& inv) {
    const std::string id = id_of(inv);
    if (!re::match(ID_RE, id)) return "its id " + str_repr(id) + " is not a short lowercase slug like maya-chen";
    if (strip(str_or(pget(inv, "name"))).empty()) return std::string("it has no name");
    if (!valid_email(strip(str_or(pget(inv, "email"))))) return std::string("it has no valid email (add the address they publish, never a guessed one)");
    bool src = false;
    for (const Val& u : iter_of(vor(pget(inv, "sources"), Val::list()))) if (re::match(R"(^https?://\S+$)", pystr(u))) { src = true; break; }
    if (!src) return std::string("it has no public source URL: the Closer only writes to people found in public sources (seed-investors keeps the link for every row)");
    return std::nullopt;
}
inline Val intro_values(const Val& base, const Val& inv, const std::string& line) {
    Val v = copy_dict(base);
    const std::string name = ws_join(pystr(pget(inv, "name")));
    const Val fn = pget(inv, "first_name");
    set_key(v, "first_name", sv(truthy(fn) ? pystr(fn) : split_ws(name).at(0)));
    set_key(v, "name", sv(name));
    const Val firm = pget(inv, "firm");
    set_key(v, "firm", sv(truthy(firm) ? pystr(firm) : name));
    set_key(v, "personal_line", sv(line));
    set_key(v, "intro_subject", sv(""));
    set_key(v, "intro_day", sv(""));
    return v;
}
}  // namespace packtools::seed_closer
