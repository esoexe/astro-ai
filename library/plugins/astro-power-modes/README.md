# Power Modes

5 power modes: tournament (six strategies plus a judge), design-polish (anti-slop design audit), compress-prompts (shorter sub-agent briefs), self-critic (attack every claim before it ships) and idea-teardown (startup idea analysis).

Adapted from [claude-illegal-codes](https://github.com/Jakeschincariol/claude-illegal-codes) by Jake Schincariol (MIT). See NOTICE.md.

## Skills

| Skill | |
|---|---|
| `compress-prompts` | Session mode that translates every sub-agent prompt into Mandarin before dispatch (more meaning per character, fewer tokens) while keeping code, identifiers, and all user-facing output in English; invoke via… |
| `design-polish` | Maximum-aesthetic mode for any visual deliverable (Meta ads, slide decks, landing pages, social graphics, posters, UI) that bans every AI-slop tell, forces a real design direction, and audits the result before shipping;… |
| `idea-teardown` | Startup idea analyzer that channels a 24-year-old founder with Euro Summer photos and a $100M raise for a ChatGPT wrapper, runs the idea through a rigorous teardown underneath the bit, and assembles the strongest… |
| `self-critic` | Anti-hallucination mode where Astro drafts an answer and then becomes obsessed with proving itself wrong, attacking every claim with tools and counter-evidence and shipping only what survives; invoke via /self-critic on… |
| `tournament` | Runs the task through six agents in parallel, each locked to a different strategy, then a seventh judge ranks the six attempts and synthesizes a final answer that steals the strongest part of each; invoke via… |

## Use

Ask the agent for the job in plain words ("write chapters for this transcript"), or name the skill.
