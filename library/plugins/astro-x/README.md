# X Pack

11 skills that run an X account: posts, threads, replies, comments, DMs, profile, repurposing, a plain-voice style editor, a weekly plan and a virality study of what works in your niche.

> **Read first.** Two helpers go online, with no login and no keys: the x-viral/pull tool reads public posts you chose through X's documented embed endpoint (one per second, capped per run, it never crawls or searches), and the x-plan/weights tool reads X's public ranking file on GitHub. Everything else runs offline.

Adapted from [x-agent-skill](https://github.com/Jakeschincariol/x-agent-skill) by Jake Schincariol (MIT). See NOTICE.md.

## Skills

| Skill | |
|---|---|
| `x-audit` | "Post-mortem on what the user has already published on X - which posts actually worked, why, and what to stop doing - from their own analytics export. Use when the user shares their X analytics CSV or past posts and asks |
| `x-comment` | "Write replies on other people's X posts and threads that read as a person with something to add, not a reply guy. Use when the user pastes a post and wants a reply, says 'reply to this', 'comment on this thread', 'engag |
| `x-dm` | "Write X DMs that get answered - the first message, the two follow-ups - and triage the user's DM inbox and message requests. Use when the user says 'DM this person', 'cold DM', 'outreach on X', 'how do I follow up', 'wh |
| `x-human` | "Strip the machine fingerprint out of any draft - em dashes, AI slop words, invisible watermark characters, thread boilerplate - and score it against a five-check detection panel before it goes out. Use whenever text nee |
| `x-plan` | "Plan the user's week on X - what to post, when to post it, and who to engage with. Use when the user says 'plan my week', 'what should I post', 'content calendar', 'posting schedule', 'I have nothing to post about', or  |
| `x-post` | "Write an X (Twitter) post from a raw idea using 21 proven hook formulas, in the user's own voice, counted the way X counts characters and humanized so it does not read as AI. Use whenever the user wants a tweet, an X po |
| `x-profile` | "Score an X profile out of 100 against a 12-part rubric and rewrite the parts that lose points - bio, pinned post, display name, header, link. Use when the user says 'optimize my profile', 'fix my X bio', 'score my Twitt |
| `x-reply` | "Handle the replies under the user's own X posts - sort every one, then draft answers in the order that matters. Use when the user pastes the replies on their post, says 'reply to these', 'handle my replies', 'someone sa |
| `x-repurpose` | "Turn one long asset - a YouTube video, podcast, newsletter, blog post, transcript, LinkedIn post or client call - into a week of X posts and one thread. Use when the user says 'repurpose this', 'turn this into tweets',  |
| `x-thread` | "Build an X thread: the first post that sells the whole thing, one idea per post, cut to fit X's weighted character count, and a last post that people act on. Use when the user says 'thread', 'write a thread', 'turn this |
| `x-viral` | "The virality engine. Find the posts that are actually outperforming in the user's niche on X, rank them by how far each beat its own account, break each one down into hook, structure and format, then recreate the winner |

## Use

Ask the agent for the job in plain words ("write chapters for this transcript"), or name the skill.
Some skills run helper tools that are built into A.S.T.R.O AI (1.9.0 or later); nothing extra to install.
Some helpers read public web data (they say which site in their help text).
