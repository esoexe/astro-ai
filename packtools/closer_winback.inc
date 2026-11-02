// win-back/winback: brings back the deals that went quiet: lists who is worth one more try (and who it will never
// touch, with why), drafts the win-back email from why the deal was lost, and logs it after your yes.
// A C++ port of closer-skill/win-back/winback.py (MIT, Jake Schincariol).
namespace packtools::closer_winback {
using namespace pyrt;
using namespace packtools::closer;
struct WinbackError : std::runtime_error { using std::runtime_error::runtime_error; };
inline constexpr long long LOST_WAIT = 60, COMPETITOR_WAIT = 180, QUIET_DAYS = 30, COOLDOWN = 90;
inline const std::vector<std::pair<std::string, std::string>>& reasons() {
    static const std::vector<std::pair<std::string, std::string>> R = {
        {"timing", R"(timing|busy|season|later|next (year|quarter|month)|not (the )?right time|fall|spring|summer|winter)"},
        {"budget", R"(budget|price|expensive|afford|cost)"},
        {"competitor", R"(went with|another (agency|vendor|company)|someone else|competitor|in-house|nephew)"},
        {"undecided", R"(think|undecided|no decision|went quiet|ghost|no reply|stalled)"}};
    return R;
}
inline const char* NEVER = R"(unsubscribe|said no|not interested|do not contact|remove)";
inline std::pair<std::string, std::string> template_for(const std::string& kind) {
    if (kind == "timing") return {"Checking back in, {first}", "Hi {first}, when we spoke you said the timing wasn't right{when}. Is now any better for {topic}? If not, no problem at all, just say so and I'll leave it.\n\n{me}"};
    if (kind == "budget") return {"A smaller way to start, {first}", "Hi {first}, last time the price was the sticking point. We now have {plan_b}, if that makes it easier. Happy to walk you through it, or to leave it here.\n\n{me}"};
    if (kind == "competitor") return {"How's it going, {first}?", "Hi {first}, hope the project with the other team went well. If anything is still on your list, I'm around. If not, no reply needed.\n\n{me}"};
    return {"Still on your list, {first}?", "Hi {first}, we talked about {topic} a while back and I never heard where it landed. Still something you want to fix? A yes or no is all I need.\n\n{me}"};
}
inline std::string reason_kind(const Val& text) {
    const std::string t = text.t == Val::STR ? text.s : std::string();
    for (const auto& [key, rxs] : reasons()) if (re::compile(rxs, re::I).search(t)) return key;
    return "undecided";
}
inline long long lost_on(const Val& data, const Val& deal) {
    const List acts = history_deal(data, at(deal, "id"));
    for (auto it = acts.rbegin(); it != acts.rend(); ++it)
        if (is(at(*it, "type"), "stage") && ps(orv(g(*it, "text"), sv(""))).find("-> lost") != std::string::npos) return date_fromiso(head(at(*it, "date"), 10));
    return date_fromiso(last_touch(data, deal));
}
struct Assessment { std::string state, why; Val kind; };
// state: ready, later, never, skip
inline Assessment assess(const Val& data, const Val& deal, long long today) {
    const Val* cp = find(at(data, "contacts"), at(deal, "contact_id"));
    const Val c = cp ? *cp : Val::dict();
    const List acts = history_contact(data, at(deal, "contact_id"));
    if (truthy(g(c, "do_not_contact")) || suppressed(data, g(c, "email"))) return {"never", "do not contact", Val()};
    for (const Val& a : acts) if (is(g(a, "direction"), "in") && says_no(ps(orv(g(a, "text"), sv(""))))) return {"never", "they said no", Val()};
    if (is(at(deal, "stage"), "won")) return {"skip", "customer", Val()};
    List winbacks; for (const Val& a : history_deal(data, at(deal, "id"))) if (is(at(a, "type"), "winback")) winbacks.push_back(a);
    if (!winbacks.empty()) {
        const long long last = date_fromiso(head(at(winbacks.back(), "date"), 10));
        bool replied = false; for (const Val& a : acts) replied |= is(g(a, "direction"), "in") && str_of(at(a, "date")) > str_of(at(winbacks.back(), "date"));
        if (!replied) return {"never", "a win-back already went on " + date_iso(last) + " with no reply: one is the limit", Val()};
        if (today - last < COOLDOWN) return {"later", "win-back on " + date_iso(last) + "; wait until " + date_iso(last + COOLDOWN), Val()};
    }
    if (is(at(deal, "stage"), "lost")) {
        const std::string reason = ps(orv(g(deal, "lost_reason"), sv("")));
        if (re::compile(NEVER, re::I).search(reason)) return {"never", "lost: " + reason, Val()};
        const std::string kind = reason_kind(sv(reason));
        const long long since = lost_on(data, deal);
        if (truthy(g(deal, "revisit"))) {
            const long long rv = date_fromiso(at(deal, "revisit"));
            if (today < rv) return {"later", "they asked to revisit on " + date_iso(rv), sv(kind)};
            return {"ready", "revisit date " + date_iso(rv) + " has come (" + reason + ")", sv(kind)};
        }
        const long long wait = kind == "competitor" ? COMPETITOR_WAIT : LOST_WAIT;
        if (today - since < wait) return {"later", "lost " + date_iso(since) + " (" + reason + "); try from " + date_iso(since + wait), sv(kind)};
        return {"ready", "lost " + std::to_string(today - since) + " days ago: " + reason, sv(kind)};
    }
    const long long quiet = today - date_fromiso(last_touch(data, deal));
    long long followups = 0; for (const Val& a : acts) followups += is(at(a, "type"), "followup");
    if (quiet >= QUIET_DAYS && followups >= 3) return {"ready", "quiet " + std::to_string(quiet) + " days after the last follow-up", sv("undecided")};
    return {"skip", "still active", Val()};
}
struct Row { std::string deal_id, title, state, why; double value; };
inline std::vector<Row> candidates(const Val& data, long long today) {
    static const std::map<std::string, int> order = {{"ready", 0}, {"later", 1}, {"never", 2}, {"skip", 3}};
    std::vector<Row> out;
    for (const Val& d : lstk(data, "deals")) { const Assessment s = assess(data, d, today); out.push_back({ps(at(d, "id")), ps(at(d, "title")), s.state, s.why, g(d, "value", Val::integer(0)).num()}); }
    std::stable_sort(out.begin(), out.end(), [](const Row& x, const Row& y) { const int a = order.at(x.state), b = order.at(y.state); if (a != b) return a < b; return -x.value < -y.value; });
    return out;
}
struct Draft { Val to; std::string subject, body, kind; };
inline Draft draft(const Val& data, const std::string& deal_id, long long today) {
    const Val* dp = find(at(data, "deals"), deal_id);
    if (!dp) throw WinbackError("No deal " + deal_id);
    const Val deal = *dp;
    const Assessment s = assess(data, deal, today);
    if (s.state != "ready") throw WinbackError("Not today: " + s.why + ".");
    const Val* cp = find(at(data, "contacts"), at(deal, "contact_id"));
    if (!cp) throw PyErr("TypeError", "'NoneType' object is not subscriptable");
    const Val c = *cp; const Val me = g(data, "me", Val::dict());
    const std::string reason = ps(orv(g(deal, "lost_reason"), sv("")));
    const auto m = re::compile(R"(\b(spring|summer|fall|autumn|winter|january|february|march|april|may|june|july|august|september|october|november|december)\b)", re::I).search(reason);
    const auto words = split_ws(str_of(at(c, "name"))); if (words.empty()) throw PyErr("IndexError", "list index out of range");
    const std::map<std::string, std::string> fields = {{"first", words[0]}, {"topic", lower(str_of(at(deal, "title")))}, {"when", m ? " until the " + lower(m.group(1)) : std::string()},
        {"plan_b", ps(orv(g(me, "plan_b"), sv("[fill: plan_b]")))}, {"me", split(str_of(orv(g(me, "name"), sv(""))), " ")[0]}};
    const auto [subject, body] = template_for(str_of(s.kind));
    return {g(c, "email"), closer_prospect::format(subject, fields), closer_prospect::format(body, fields), str_of(s.kind)};
}
inline std::pair<bool, std::string> record(Val& data, const std::string& deal_id, const std::string& text, const Val& approval, const std::string& when) {
    const auto [ok, why] = parse_yes(approval);
    if (!ok) return {false, "Not recorded: " + why + "."};
    const Val* dp = find(at(data, "deals"), deal_id);
    if (!dp) return {false, "No deal " + deal_id};
    const Val deal = *dp;
    const Assessment s = assess(data, deal, date_fromiso(cpslice(when, 0, 10)));
    if (s.state != "ready") return {false, "Not recorded: " + s.why + "."};
    if (text.find("[fill:") != std::string::npos) return {false, "Not recorded: the message still has a [fill: ...] in it."};
    add_activity(data, {{"date", sv(when)}, {"contact_id", at(deal, "contact_id")}, {"deal_id", sv(deal_id)}, {"channel", sv("email")}, {"type", sv("winback")},
                        {"direction", sv("out")}, {"subject", Val()}, {"text", sv(text)}, {"approval", approval}});
    if (is(at(deal, "stage"), "lost")) { deal.d->set("stage", sv("contacted")); deal.d->set("lost_reason", Val()); }
    deal.d->set("next_step", sv("Wait for a reply to the win-back (no follow-up)"));
    return {true, "Logged the win-back. The deal is open again; if they don't reply, that's the end of it."};
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("winback.py", "Win back deals that went quiet.");
    p.opt({"--file"}).def(DEFAULT_FILE);
    p.subcommands("cmd", true);
    { Parser& s = p.sub("list"); s.opt({"--today"}); }
    { Parser& s = p.sub("draft"); s.opt({"--deal"}).required(); s.opt({"--today"}); }
    { Parser& s = p.sub("record"); s.opt({"--deal"}).required(); s.opt({"--text"}); s.opt({"--text-file"}); s.opt({"--approval"}).required(); s.opt({"--at"}).required(); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd"), file = S(a, "file");
    try {
        Val data = load(file);
        if (cmd == "list") {
            for (const Row& r : candidates(data, today_arg(get(a, "today"))))
                if (r.state != "skip") print(ljust(upper(r.state), 6) + " " + ljust(cpslice(r.title, 0, 30), 30) + " " + r.why);
        } else if (cmd == "draft") {
            const Draft d = draft(data, S(a, "deal"), today_arg(get(a, "today")));
            print("To: " + ps(d.to) + "\nSubject: " + d.subject + "\n\n" + d.body);
        } else {
            Val text = get(a, "text");
            if (truthy(get(a, "text_file"))) text = sv(strip(read_text(S(a, "text_file"))));
            if (!truthy(text)) throw WinbackError("Give --text or --text-file: exactly what went out.");
            const auto [ok, msg] = record(data, S(a, "deal"), text.s, get(a, "approval"), S(a, "at"));
            if (ok) save(data, file);
            print(msg);
            return ok ? 0 : 1;
        }
    } catch (const WinbackError& e) { eprint(e.what()); return 1; }
    catch (const PipelineError& e) { eprint(e.what()); return 1; }
    catch (const OSError& e) { eprint(e.what()); return 1; }
    return 0;
}
PACKTOOL("win-back/winback", main);
}  // namespace packtools::closer_winback
