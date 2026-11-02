// seed-closer/closer: The Closer. Investor outreach drafts (.eml), the batch the founder says yes to, the pipeline,
// business-day follow-ups, reply sorting, and the calendar invite. It never sends anything. A C++ port of
// seed-skill/seed-closer/closer.py (MIT, Jake Schincariol): same files, output and exit codes (0 done, 1 refused,
// nothing to do or an error, 2 bad arguments).
// Adapted for A.S.T.R.O AI: the printed hints call the assistant Astro and name what really sends mail here (a Gmail
// or calendar MCP server the user added; A.S.T.R.O AI's Allow / Deny card in Ask mode) instead of Claude's connectors.
#include "seed_closer_mail.inc"
#include "seed_closer_base.inc"
#include "seed_closer_cmds1.inc"
#include "seed_closer_cmds2.inc"
#include "seed_closer_inbox.inc"
#include "seed_closer_book.inc"
namespace packtools::seed_closer {

inline void base_args(Parser& s, bool today = true) {
    s.opt({"--dir"}).def("seed").help("the seed folder (default ./seed)");
    if (today) s.opt({"--today"}).help("YYYY-MM-DD (default: today on this computer)");
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("closer.py", "The Closer: investor outreach drafts, the pipeline, follow-ups and the invite. It never sends anything.");
    p.subcommands("cmd", false);
    { Parser& s = p.sub("draft"); s.help_line = "one .eml intro per investor from investors.json + the template";
      base_args(s);
      s.opt({"--ids"}).help("comma-separated investor ids (default: everyone not yet in the pipeline)");
      s.opt({"--top"}).integer().help("the N best fits");
      s.opt({"--min-fit"}).number();
      s.opt({"--template"});
      s.opt({"--dry-run"}).store_true().help("show what would be drafted, write nothing"); }
    { Parser& s = p.sub("batch"); s.help_line = "show the batch waiting for a yes";
      base_args(s);
      s.opt({"--ids"});
      s.opt({"--limit"}).integer();
      s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("approve"); s.help_line = "record the user's yes for one batch";
      base_args(s);
      s.opt({"--ids"});
      s.opt({"--limit"}).integer();
      s.opt({"--batch"}).required().help("the batch id that batch printed");
      s.opt({"--reply"}).required().help("the user's reply, word for word"); }
    { Parser& s = p.sub("outbox"); s.help_line = "the approved emails, exactly as Astro sends them";
      base_args(s, false);
      s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("log"); s.help_line = "record sent / denied / a reply / a note";
      base_args(s);
      s.opt({"--id"}).required().help("an id, or several comma separated");
      s.opt({"--event"}).required().choices(events());
      s.opt({"--date"}).help("YYYY-MM-DD (default --today)");
      s.opt({"--thread-id"});
      s.opt({"--note"}); }
    { Parser& s = p.sub("due"); s.help_line = "who gets a follow-up today";
      base_args(s);
      s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("followup"); s.help_line = "draft the next touch";
      base_args(s);
      s.opt({"--id"});
      s.opt({"--due"}).store_true().help("everyone due today");
      s.opt({"--extra"}).help("one short extra line for follow-up 1, like a demo link");
      s.opt({"--ask"}).help("the question for follow-up 2, like \"would Thursday at 10 work for 20 minutes?\"");
      s.opt({"--template"}); }
    { Parser& s = p.sub("reply"); s.help_line = "draft an answer in an ongoing conversation";
      base_args(s);
      s.opt({"--id"}).required();
      s.opt({"--body"});
      s.opt({"--body-file"}); }
    { Parser& s = p.sub("inbox"); s.help_line = "sort pasted replies by rules, and record them with --write";
      base_args(s);
      s.opt({"--file"});
      s.opt({"--text"});
      s.opt({"--id"});
      s.opt({"--from"}).dest("sender");
      s.opt({"--subject"});
      s.opt({"--date"});
      s.opt({"--write"}).store_true();
      s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("book"); s.help_line = "write calls/<id>.ics, a calendar invite";
      base_args(s);
      s.opt({"--id"}).required();
      s.opt({"--start"}).required().help("\"2026-11-05 10:00\" in --tz, or 2026-11-05T15:00Z");
      s.opt({"--tz"}).def("America/New_York");
      s.opt({"--minutes"}).integer().def(30);
      s.opt({"--end"}).help("HH:MM, instead of --minutes");
      s.opt({"--link"});
      s.opt({"--location"});
      s.opt({"--title"});
      s.opt({"--stamp"}).help("DTSTAMP in UTC, like 2026-11-02T15:04:00Z (default: now)"); }
    { Parser& s = p.sub("status"); s.help_line = "the funnel";
      base_args(s);
      s.opt({"--id"});
      s.opt({"--json"}).store_true(); }
    const Val a = parse_args(p, argv);
    const Val cmdv = pget(a, "cmd");
    const std::string cmd = truthy(cmdv) ? pystr(cmdv) : "";
    if (cmd.empty()) { flush(); const std::string t = ap::help_text(ap::norm(p, p.prog)); std::fwrite(t.data(), 1, t.size(), stdout); return 1; }
    try {
        if (cmd == "draft") return cmd_draft(a);
        if (cmd == "batch") return cmd_batch(a);
        if (cmd == "approve") return cmd_approve(a);
        if (cmd == "outbox") return cmd_outbox(a);
        if (cmd == "log") return cmd_log(a);
        if (cmd == "due") return cmd_due(a);
        if (cmd == "followup") return cmd_followup(a);
        if (cmd == "reply") return cmd_reply(a);
        if (cmd == "inbox") return cmd_inbox(a);
        if (cmd == "book") return cmd_book(a);
        return cmd_status(a);
    } catch (const CloserError& e) { flush(); eprint(std::string("closer.py: ") + e.what()); return 1; }
}
PACKTOOL("seed-closer/closer", main);
}  // namespace packtools::seed_closer
