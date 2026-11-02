# Duel

Make an answer defend itself against a second model: a critic model challenges it, the agent defends or revises claim by claim, up to three rounds, with a record of what changed.

Adapted from [duel-skill](https://github.com/Jakeschincariol/duel-skill) by Jake Schincariol (MIT). See NOTICE.md.

## Skills

| Skill | |
|---|---|
| `duel` | Have an independent critic model (OpenAI, or any OpenAI-compatible provider the user configures) challenge Astro's answer, then defend or revise it through a short evidence-led debate. Use for "duel this with ChatGPT",… |

## Use

Ask the agent for the job in plain words ("write chapters for this transcript"), or name the skill.
Some skills run helper tools that are built into A.S.T.R.O AI (1.9.0 or later); nothing extra to install.
Some helpers read public web data (they say which site in their help text).
