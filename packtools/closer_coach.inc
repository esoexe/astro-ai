// sales-coach/coach: reads a call's captions (one "Name: words" line per turn) and, for every line from the other
// side, shows you the objection and the answer to say next (filled from "me" in the pipeline, never invented), a
// buying signal, or a question to answer first; review scores the call afterwards. A C++ port of
// closer-skill/sales-coach/coach.py (MIT, Jake Schincariol); objections.json ships with the skill.
#include <csignal>
namespace packtools::closer_coach {
using namespace pyrt;
using namespace packtools::closer;
inline const char* LINE_RE = R"(^\s*(?:\[?\d{1,2}:\d{2}(?::\d{2})?\]?\s*)?([^:]{1,40}):\s*(.+?)\s*$)";
inline const char* BUYING = R"(how soon|when (can|could) (we|you) start|what('s| is) (the )?next step|send (me )?(the|a) (contract|agreement|invoice)|how do (we|i) (get started|sign up|start)|sounds good|let'?s do it|i'?m in\b|where do i sign|can we start)";
inline const char* QUESTION = R"(\?\s*$|^(how|what|when|where|why|who|can|could|do|does|is|are|will|would)\b)";
inline bool rx(const char* p, const std::string& s) { return static_cast<bool>(re::compile(p, re::I).search(s)); }

struct Objection { std::string key, label, say, why, pattern; };
inline std::vector<Objection> load_objections() {
    const Val lib = loads(read_text(packtools::skill_file("objections.json")));
    std::vector<Objection> o;
    for (const auto& [key, v] : lib.d->items) {
        std::vector<std::string> parts; for (const Val& h : lst(at(v, "hear"))) parts.push_back("\\b" + str_of(h) + "\\b");
        o.push_back({key, ps(at(v, "label")), str_of(at(v, "say")), ps(at(v, "why")), join(parts, "|")});
    }
    return o;
}
inline Val offer_from(const std::string& pipeline_file) {
    if (!exists(pipeline_file)) return Val::dict();
    return g(load(pipeline_file), "me", Val::dict());
}
inline std::string fill(const std::string& text, const Val& offer) {
    std::string o; long long last = 0;
    for (const auto& m : re::compile(R"(\{(\w+)\})").finditer(text)) {
        o += cpslice(text, last, m.start());
        const Val v = g(offer, m.group(1));
        o += truthy(v) ? ps(v) : "[your " + replace_all(m.group(1), "_", " ") + "]";
        last = m.end();
    }
    return o + cpslice(text, last, static_cast<long long>(cplen(text)));
}
// The objection key heard in one line, or "". The first match in the library order wins.
inline std::string detect(const std::string& text, const std::vector<Objection>& lib) {
    for (const auto& o : lib) if (re::compile(o.pattern, re::I).search(text)) return o.key;
    return "";
}
inline std::pair<Val, std::string> parse_line(const std::string& line) {
    const auto m = re::compile(LINE_RE).match(line);
    if (!m) return {Val(), strip(line)};
    return {sv(strip(m.group(1))), strip(m.group(2))};
}
inline bool is_me(const Val& who, const std::string& me) { return truthy(who) && (lower(who.s) == lower(me) || lower(who.s) == "you"); }
struct Card { std::string kind, heard, key, label, say, why; Val who; };
// What to show you for one caption line; nullopt when it is your own line or there is nothing to say.
inline std::optional<Card> coach_line(const Val& who, const std::string& text, const std::string& me, const std::vector<Objection>& lib, const Val& offer) {
    if (text.empty() || is_me(who, me)) return std::nullopt;
    const std::string key = detect(text, lib);
    if (!key.empty()) {
        for (const auto& o : lib) if (o.key == key) return Card{"objection", text, key, o.label, fill(o.say, offer), o.why, who};
    }
    if (rx(BUYING, text))
        return Card{"buying", text, "", "Buying signal", "Great. The next step is " + ps(orv(g(offer, "next_step"), sv("a short agreement"))) + ". Want me to send that over today?",
                    "They are asking how to buy. Stop selling and agree the next step.", who};
    if (rx(QUESTION, text)) return Card{"question", text, "", "They asked a question", "Answer it directly before you move on.", text, who};
    return std::nullopt;
}
inline std::string show(const Card& c) {
    const std::string bar(60, '-');
    return bar + "\n" + c.label + "  (" + (truthy(c.who) ? ps(c.who) : std::string("them")) + ": \"" + cpslice(c.heard, 0, 90) + "\")\nSay this next: " + c.say + "\n  " + c.why + "\n" + bar;
}
inline std::vector<Card> listen(const std::vector<std::string>& lines, const std::string& me, const std::vector<Objection>& lib, const Val& offer) {
    std::vector<Card> cards;
    for (const std::string& line : lines) { const auto [who, text] = parse_line(line); if (auto c = coach_line(who, text, me, lib, offer)) cards.push_back(*c); }
    return cards;
}
inline volatile std::sig_atomic_t g_stop = 0;
inline void on_sigint(int) { g_stop = 1; }
inline void follow(const std::string& path, const std::string& me, const std::vector<Objection>& lib, const Val& offer, double poll = 1.0) {
    print("Listening to " + path + ". Ctrl-C to stop."); flush();
    g_stop = 0; const auto prev = std::signal(SIGINT, on_sigint);
    std::size_t pos = 0;
    while (!g_stop) {
        if (exists(path)) {
            const std::string all = read_bytes(path);
            const std::string chunk = pos < all.size() ? all.substr(pos) : std::string();
            pos = std::max(pos, all.size());
            for (const Card& c : listen(splitlines(decode_utf8_replace(chunk)), me, lib, offer)) { print(show(c)); flush(); }
        }
        for (int k = 0; k < 20 && !g_stop; ++k) std::this_thread::sleep_for(std::chrono::duration<double>(poll / 20));
    }
    std::signal(SIGINT, prev);
    print("\nStopped listening.");
}
struct Review { long long talk_share = 0, questions_asked = 0; std::vector<std::pair<std::string, std::string>> objections; std::vector<std::string> signals; bool next_step = false; };
inline Review review(const std::vector<std::string>& lines, const std::string& me, const std::vector<Objection>& lib) {
    long long mine = 0, theirs = 0; Review r;
    for (const std::string& line : lines) {
        const auto [who, text] = parse_line(line);
        if (text.empty()) continue;
        const long long words = static_cast<long long>(split_ws(text).size());
        if (is_me(who, me)) {
            mine += words;
            const std::string t = rstrip(text);
            if (!t.empty() && t.back() == '?') r.questions_asked++;
        } else {
            theirs += words;
            const std::string k = detect(text, lib);
            if (!k.empty()) r.objections.emplace_back(k, text);
            else if (rx(BUYING, text)) r.signals.push_back(text);
        }
    }
    const long long total = mine + theirs ? mine + theirs : 1;
    std::vector<std::string> last_mine;
    for (const std::string& l : lines) { const auto pl = parse_line(l); if (pl.first.t == Val::STR ? (lower(pl.first.s) == lower(me) || lower(pl.first.s) == "you") : (lower(me).empty())) last_mine.push_back(pl.second); }
    std::vector<std::string> tail(last_mine.size() > 3 ? last_mine.end() - 3 : last_mine.begin(), last_mine.end());
    r.next_step = !last_mine.empty() && rx(R"(\b(monday|tuesday|wednesday|thursday|friday|tomorrow|next week|send|invite|calendar|agreement|contract|proposal)\b)", join(tail, " "));
    r.talk_share = round_int(100.0 * static_cast<double>(mine) / static_cast<double>(total));
    return r;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("coach.py", "Tells you what to say next on a sales call.");
    p.opt({"--file"}).def(Val::str(path_join("closer", "pipeline.json"))).help("for your offer (me)");
    p.subcommands("cmd", true);
    { Parser& s = p.sub("listen"); s.opt({"--captions"}).required(); s.opt({"--me"}).def("You").help("your name as it shows in the captions"); s.opt({"--follow"}).store_true(); }
    { Parser& s = p.sub("card"); s.opt({"--objection"}).required(); }
    { Parser& s = p.sub("review"); s.opt({"--captions"}).required(); s.opt({"--me"}).def("You"); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    const auto lib = load_objections();
    Val offer;
    try { offer = offer_from(S(a, "file")); } catch (const PipelineError& e) { eprint(e.what()); return 1; }
    if (cmd == "card") {
        const std::string key = S(a, "objection");
        std::vector<std::string> keys; for (const auto& o : lib) keys.push_back(o.key);
        for (const auto& o : lib) if (o.key == key) { print(o.label + "\nSay this next: " + fill(o.say, offer) + "\n  " + o.why); return 0; }
        eprint("No objection " + key + ". Try: " + join(keys, ", "));
        return 1;
    }
    const std::string me = S(a, "me");
    if (cmd == "listen" && truthy(get(a, "follow"))) { follow(S(a, "captions"), me, lib, offer); return 0; }
    const auto lines = splitlines(read_text(S(a, "captions")));
    if (cmd == "listen") {
        const auto cards = listen(lines, me, lib, offer);
        for (const Card& c : cards) print(show(c));
        if (cards.empty()) print("Nothing to coach yet.");
    } else {
        const Review r = review(lines, me, lib);
        print("You talked " + std::to_string(r.talk_share) + "% of the time" + (r.talk_share > 50 ? " (aim for under 50%)" : "") + ".");
        print("You asked " + std::to_string(r.questions_asked) + " questions.");
        for (const auto& [k, t] : r.objections) print("Objection (" + k + "): " + t);
        for (const auto& t : r.signals) print("Buying signal: " + t);
        print(std::string("Next step agreed: ") + (r.next_step ? "yes" : "NO. Send a recap with one clear next step."));
    }
    return 0;
}
PACKTOOL("sales-coach/coach", main);
}  // namespace packtools::closer_coach
