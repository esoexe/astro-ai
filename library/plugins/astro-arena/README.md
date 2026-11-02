# Arena

When an answer isn't good enough: many sub-agents solve the same task with different strategy cards, then attack, defend and are judged in a single-elimination bracket until one solution survives. Uses A.S.T.R.O AI's task tool (capped to your budget).

Adapted from [arena-skill](https://github.com/Jakeschincariol/arena-skill) by Jake Schincariol (MIT). See NOTICE.md.

## Skills

| Skill | |
|---|---|
| `arena` | Make 100 versions of Astro fight to the death over one task. Spins up N sub-agents (default 100, --quick for 16), gives every one the exact same task plus a different strategy card (reasoning mode, workflow, strategy),… |

## Use

Ask the agent for the job in plain words ("write chapters for this transcript"), or name the skill.
Some skills run helper tools that are built into A.S.T.R.O AI (1.9.0 or later); nothing extra to install.
