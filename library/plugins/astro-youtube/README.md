# YouTube Pack

11 skills that run a YouTube channel: scripts with scored hooks, titles and packaging, chapters, retention and dead-air analysis, Shorts, SEO, comments and a weekly plan.

Adapted from [youtube-agent-skill](https://github.com/Jakeschincariol/youtube-agent-skill) by Jake Schincariol (MIT). See NOTICE.md.

## Skills

| Skill | |
|---|---|
| `yt-audit` | Audit a YouTube channel end to end - packaging, consistency, the first fifteen seconds, and what to fix first. Use for 'audit my channel', 'why isn't my channel growing', 'review my videos', or a pasted channel URL. |
| `yt-chapters` | Write YouTube chapters from a transcript, validated against YouTube's own rules so they actually render. Use for 'add chapters', 'timestamps', 'break this video into sections'. |
| `yt-comment` | Draft replies to YouTube comments in the creator's voice, triaged by which ones are worth answering. Use for 'reply to my comments', 'handle the comment section', 'someone asked X', or a pasted comment thread. |
| `yt-edit` | Turn a raw recording's transcript into an edit decision list - dead air, filler cues and retakes, with timecodes. Use for 'edit this', 'cut the dead space', 'tighten this video', 'I rambled', or any request to shorten… |
| `yt-package` | Write and lint the title and thumbnail for a YouTube video as one pairing, checking truncation, duplication and vagueness before publish. Use for 'title ideas', 'what should I call this', 'thumbnail text', 'my CTR is… |
| `yt-plan` | Plan a week or a month of YouTube uploads - what to post, when, and in what order, sized to the creator's actual capacity. Use for 'plan my week', 'content calendar', 'what should I post', 'I have no idea what to make… |
| `yt-retention` | Read a YouTube Studio audience-retention export and find where viewers actually leave, then say what to change. Use for 'why do people stop watching', 'my retention is bad', a pasted retention chart or CSV, or 'fix my… |
| `yt-script` | Write a YouTube video script from a raw idea - hook options off 21 formulas, scored, then the full spoken script with the retention beats marked. Use whenever the user wants a video script, a hook, an opening line,… |
| `yt-seo` | Write the description, tags and search-facing text for a YouTube video, aimed at the query a real person types. Use for 'write my description', 'tags', 'SEO', 'help this video get found', 'nobody is finding this'. |
| `yt-shorts` | Find the Shorts hiding inside a long video and write them, using the transcript to pick self-contained moments. Use for 'cut this into shorts', 'clip this', 'repurpose this video', 'what should I clip'. |
| `yt-viral` | Find what is actually working in the user's niche on YouTube and rank it by how far each video beat its own channel, then name the formula. Use for 'what's working right now', 'find viral videos in my niche', 'why did… |

## Use

Ask the agent for the job in plain words ("write chapters for this transcript"), or name the skill.
Some skills run helper tools that are built into A.S.T.R.O AI (1.9.0 or later); nothing extra to install.
