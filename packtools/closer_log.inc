// record-keeper/log: logs every email and call into closer/pipeline.json and call notes into closer/calls/.
// A C++ port of closer-skill/record-keeper/log.py (MIT, Jake Schincariol).
namespace packtools::closer_log {
using namespace pyrt;
using namespace packtools::closer;
struct LogError : std::runtime_error { using std::runtime_error::runtime_error; };
inline const char* SENT_BY_YOU = "sent by you from your inbox";

inline std::optional<std::string> addr(const Val& text) {
    const std::string s = text.t == Val::STR ? text.s : std::string();
    const auto m = re::compile(R"([\w.+'-]+@[\w-]+(\.[\w-]+)+)").search(s);
    if (!m) return std::nullopt;
    return lower(m.group(0));
}
inline std::map<std::string, Val> by_email(const Val& data) {   // the last contact with an address wins, like a dict comprehension
    std::map<std::string, Val> o;
    for (const Val& c : lstk(data, "contacts")) if (truthy(g(c, "email"))) o[lower(str_of(at(c, "email")))] = c;
    return o;
}
inline Val open_deal(const Val& data, const Val& contact_id) {
    List deals, open_;
    for (const Val& d : lstk(data, "deals")) if (repr(at(d, "contact_id")) == repr(contact_id)) deals.push_back(d);
    for (const Val& d : deals) if (in(at(d, "stage"), OPEN)) open_.push_back(d);
    if (!open_.empty()) return open_[0];
    return deals.empty() ? Val() : deals[0];
}
inline bool already(const Val& data, const Val& contact_id, const std::string& date, const Val& subject, const Val& direction) {
    for (const Val& a : lstk(data, "activities")) {
        if (repr(at(a, "contact_id")) == repr(contact_id) && head(at(a, "date"), 16) == cpslice(date, 0, 16) && repr(g(a, "direction")) == repr(direction)) {
            // same person, same minute, same way: the same email, unless both have subjects that differ
            if (!truthy(subject) || !truthy(g(a, "subject")) || repr(at(a, "subject")) == repr(subject)) return true;
        }
    }
    return false;
}
// opening / followup / reply for an email, from what is already logged
inline std::string classify(const Val& data, const Val& contact_id, const Val& direction) {
    if (is(direction, "in")) return "reply";
    const List acts = history_contact(data, contact_id);
    bool opened = false, heard = false;
    for (const Val& a : acts) { opened |= is(at(a, "type"), "opening"); heard |= is(g(a, "direction"), "in"); }
    if (!opened) return "opening";
    return heard ? "reply" : "followup";
}
struct SyncResult { List logged, skipped; std::vector<std::string> unknown; };
inline SyncResult sync(Val& data, const Val& emails) {
    const std::string me = lower(str_of(orv(g(g(data, "me", Val::dict()), "email"), sv(""))));
    const auto contacts = by_email(data);
    const auto known = [&](const std::optional<std::string>& a) { return a && contacts.count(*a) > 0; };
    SyncResult r; std::set<std::string> unknown;
    List sorted_ = lst(emails);
    std::stable_sort(sorted_.begin(), sorted_.end(), [](const Val& x, const Val& y) { return str_of(at(x, "date")) < str_of(at(y, "date")); });
    for (const Val& e : sorted_) {
        const auto sender = addr(g(e, "from"));
        const Val tos = orv(g(e, "to"), Val::list());
        std::vector<std::optional<std::string>> to; for (const Val& t : lst(tos)) to.push_back(addr(t));
        bool any_to = false; for (const auto& t : to) any_to |= known(t);
        std::string direction; std::optional<std::string> other;
        if ((sender && *sender == me) || (!known(sender) && any_to)) {
            direction = "out"; for (const auto& t : to) if (known(t)) { other = t; break; }
        } else { direction = "in"; other = sender; }
        if (!known(other)) { if (other && !other->empty()) unknown.insert(*other); continue; }
        const Val c = contacts.at(*other);
        if (already(data, at(c, "id"), str_of(at(e, "date")), g(e, "subject"), sv(direction))) { r.skipped.push_back(e); continue; }
        const Val deal = open_deal(data, at(c, "id"));
        std::vector<std::pair<std::string, Val>> fields = {{"date", sv(head(at(e, "date"), 16))}, {"contact_id", at(c, "id")}, {"deal_id", deal.is_none() ? Val() : at(deal, "id")},
            {"channel", sv("email")}, {"type", sv(classify(data, at(c, "id"), sv(direction)))}, {"direction", sv(direction)}, {"subject", g(e, "subject")},
            {"text", sv(strip(str_of(orv(g(e, "text"), sv("")))))}};
        if (direction == "out") fields.emplace_back("approval", orv(g(e, "approval"), sv(SENT_BY_YOU)));
        r.logged.push_back(add_activity(data, fields));
    }
    r.unknown.assign(unknown.begin(), unknown.end());
    return r;
}

inline std::string save_call_notes(const Val& contact, const std::string& when, long long minutes, const std::string& notes) {
    const std::string dir = path_join("closer", "calls");
    makedirs(dir);
    const std::string path = path_join(dir, cpslice(when, 0, 10) + "-" + slug(str_of(orv(g(contact, "company"), at(contact, "name")))) + ".md");
    write_text(path, "# Call: " + ps(at(contact, "name")) + ", " + ps(orv(g(contact, "company"), sv(""))) + "\n\n"
                   + "- When: " + replace_all(when, "T", " ") + "\n- Length: " + std::to_string(minutes) + " min\n\n" + strip(notes) + "\n");
    return path;
}
inline std::string strip_set(const std::string& s, const std::string& chars) {
    std::size_t a = 0, b = s.size();
    while (a < b && chars.find(s[a]) != std::string::npos) a++;
    while (b > a && chars.find(s[b - 1]) != std::string::npos) b--;
    return s.substr(a, b - a);
}
inline std::string summary(const std::string& notes, std::size_t limit = 200) {
    std::vector<std::string> lines;
    for (const std::string& l : splitlines(notes)) if (!strip(l).empty() && l.rfind("#", 0) != 0) lines.push_back(strip(strip_set(l, "-* ")));
    const std::string text = join(lines, " ");
    if (cplen(text) <= limit) return text;
    const std::string cut = cpslice(text, 0, static_cast<long long>(limit) - 3);
    const std::size_t sp = cut.rfind(' ');
    return (sp == std::string::npos ? cut : cut.substr(0, sp)) + "...";
}
inline std::string timeline(const Val& data, const std::string& contact_id) {
    const Val* c = find(at(data, "contacts"), contact_id);
    if (!c) throw LogError("No contact " + contact_id);
    std::vector<std::string> out = {ps(at(*c, "name")) + ", " + ps(orv(g(*c, "company"), sv("")))};
    for (const Val& a : history_contact(data, sv(contact_id))) {
        const Val dir = g(a, "direction");
        const std::string arrow = is(dir, "out") ? "->" : is(dir, "in") ? "<-" : "  ";
        const std::string what = ps(orv(g(a, "subject"), sv(""))), text = ps(orv(g(a, "text"), sv("")));
        out.push_back("  " + ljust(ps(at(a, "date")), 16) + " " + arrow + " " + ljust(ps(at(a, "channel")), 8) + " " + ljust(ps(at(a, "type")), 9) + " "
                      + (what.empty() ? std::string() : what + ": ") + cpslice(text, 0, 80));
    }
    return join(out, "\n");
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("log.py", "Log every email and call.");
    p.opt({"--file"}).def(DEFAULT_FILE);
    p.subcommands("cmd", true);
    { Parser& s = p.sub("sync"); s.opt({"--emails"}).required(); }
    { Parser& s = p.sub("log"); s.opt({"--contact"}).required(); s.opt({"--channel"}).required().choices(CHANNELS); s.opt({"--direction"}).choices({"in", "out"});
      s.opt({"--type"}).choices(TYPES); s.opt({"--subject"}); s.opt({"--text"}); s.opt({"--text-file"});
      s.opt({"--approval"}).help("the yes that sent it, word for word, or 'sent by you from your inbox'"); s.opt({"--deal"}); s.opt({"--at"}).required(); }
    { Parser& s = p.sub("call"); s.opt({"--contact"}).required(); s.opt({"--minutes"}).integer().required(); s.opt({"--notes-file"}).required(); s.opt({"--at"}).required(); }
    { Parser& s = p.sub("timeline"); s.opt({"--contact"}).required(); }
    { Parser& s = p.sub("unknown"); s.opt({"--emails"}).required(); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd"), file = S(a, "file");
    try {
        Val data = load(file);
        if (cmd == "sync" || cmd == "unknown") {
            const Val emails = loads(read_text(S(a, "emails")));
            const SyncResult r = sync(data, emails);
            if (cmd == "unknown") { print(r.unknown.empty() ? std::string("Every address is a contact already.") : join(r.unknown, "\n")); return 0; }
            save(data, file);
            print("Logged " + std::to_string(r.logged.size()) + " emails, " + std::to_string(r.skipped.size()) + " already logged.");
            for (const Val& act : r.logged) print("  " + ps(at(act, "date")) + " " + ps(at(act, "direction")) + " " + ps(g(act, "subject")));
            if (!r.unknown.empty()) print("Not contacts yet (ignored): " + join(r.unknown, ", "));
        } else if (cmd == "log") {
            const Val* cp = find(at(data, "contacts"), S(a, "contact"));
            if (!cp) throw LogError("No contact " + S(a, "contact"));
            const Val c = *cp;
            Val text = get(a, "text");
            if (truthy(get(a, "text_file"))) text = sv(strip(read_text(S(a, "text_file"))));
            const std::string channel = S(a, "channel");
            std::string kind;
            if (truthy(get(a, "type"))) kind = S(a, "type");
            else if (channel == "call" || channel == "meeting" || channel == "note") kind = channel;
            else kind = classify(data, at(c, "id"), get(a, "direction"));
            Val deal;
            if (truthy(get(a, "deal"))) { const Val* d = find(at(data, "deals"), S(a, "deal")); if (d) deal = *d; }
            else deal = open_deal(data, at(c, "id"));
            std::vector<std::pair<std::string, Val>> fields = {{"date", get(a, "at")}, {"contact_id", at(c, "id")}, {"deal_id", deal.is_none() ? Val() : at(deal, "id")},
                {"channel", sv(channel)}, {"type", sv(kind)}, {"direction", get(a, "direction")}, {"subject", get(a, "subject")}, {"text", orv(text, sv(""))}};
            if (truthy(get(a, "approval"))) fields.emplace_back("approval", get(a, "approval"));
            const Val act = add_activity(data, fields);
            save(data, file);
            print("Logged " + ps(at(act, "channel")) + " " + ps(at(act, "type")) + " for " + ps(at(c, "name")));
        } else if (cmd == "call") {
            const Val* cp = find(at(data, "contacts"), S(a, "contact"));
            if (!cp) throw LogError("No contact " + S(a, "contact"));
            const Val c = *cp;
            const std::string notes = read_text(S(a, "notes_file"));
            const long long minutes = get(a, "minutes").i; const std::string when = S(a, "at");
            const std::string path = save_call_notes(c, when, minutes, notes);
            const Val deal = open_deal(data, at(c, "id"));
            add_activity(data, {{"date", sv(when)}, {"contact_id", at(c, "id")}, {"deal_id", deal.is_none() ? Val() : at(deal, "id")}, {"channel", sv("call")}, {"type", sv("call")},
                                {"direction", Val()}, {"subject", Val()}, {"text", sv("Call, " + std::to_string(minutes) + " min. " + summary(notes))},
                                {"minutes", Val::integer(minutes)}, {"notes", sv(path)}});
            save(data, file);
            print("Logged the call and saved notes to " + path);
        } else if (cmd == "timeline") {
            print(timeline(data, S(a, "contact")));
        }
    } catch (const LogError& e) { eprint(e.what()); return 1; }
    catch (const PipelineError& e) { eprint(e.what()); return 1; }
    catch (const OSError& e) { eprint(e.what()); return 1; }
    catch (const PyErr& e) { if (e.type != "ValueError" && e.type != "json.decoder.JSONDecodeError" && e.type != "UnicodeDecodeError") throw; eprint(e.what()); return 1; }
    return 0;
}
PACKTOOL("record-keeper/log", main);
}  // namespace packtools::closer_log
