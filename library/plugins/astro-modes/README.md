# Work Modes

25 one-word working modes for the agent: deep-rigor, minimal-diff, fix-all-errors, incident, distill, cost-cutter, build-plan, idea-storm, speedrun, long-haul and more. Plain prompt modes, no scripts.

Adapted from [unhinged-claude-skills](https://github.com/Jakeschincariol/unhinged-claude-skills) by Jake Schincariol (MIT). See NOTICE.md.

## Skills

| Skill | |
|---|---|
| `apollo-13` | Works a critical failure under hard constraints — limited options, no clean fix — to get the broken thing home with whatever you have; invoke via /apollo-13 when something important is down, the usual tools are unavailab |
| `autopilot` | Takes the whole task and runs it end to end without stopping to ask, leaving the user to review at the finish line; invoke via /autopilot when you want a fully autonomous run and will inspect the result once it's done. |
| `build-plan` | Turns a vague idea into a clear build plan — short PRD, task list, user flows, constraints, and acceptance criteria — so A.S.T.R.O AI knows exactly what to build; invoke via /build-plan when you know what you want but As |
| `compress-brief` | Compresses long, repeated sub-agent instructions by translating them into Mandarin (more meaning per character) to slash the token bill, then dispatches the compressed brief to a fleet of sub-agents while preserving orig |
| `cost-cutter` | Hunts cost everywhere — trims tokens, kills redundant calls, and reports the cheapest path to the same result; invoke via /cost-cutter when you want aggressive (but technically legal) cost optimization of an AI workflow, |
| `decide-and-go` | Makes the judgment calls itself and keeps moving instead of pausing at every fork — fast and opinionated, best on low-stakes work; invoke via /decide-and-go when you'd rather get a decisive result than answer twenty clar |
| `deep-rigor` | Obsessive-detail mode that reads every relevant file, checks every edge case, questions every assumption, and chases every dependency before answering; slower on purpose and far harder to fool — invoke via /deep-rigor wh |
| `distill` | Workflow and system optimizer that takes a messy system, bloated prompt, broken automation, or slow process and strips it to the fastest, cheapest, cleanest version that still works; invoke via /distill when something ru |
| `exhaustive` | Goes wide and exhaustive — every option, every edge, every step spelled out, the opposite of a compressor; invoke via /exhaustive when you want maximum thoroughness and nothing left implicit. |
| `fix-all-errors` | Attacks the error list in one relentless pass — fix, rebuild, repeat, no commentary — until it builds clean; invoke via /fix-all-errors when you have a wall of build/type/lint errors and just want them gone. |
| `heads-down` | Drops the small talk and goes heads-down — no preamble, just code and short status lines until done; invoke via /heads-down when you want execution with minimal chatter. |
| `idea-storm` | Fires out ideas at full speed — twenty rough directions in a minute for the user to cherry-pick; invoke via /idea-storm when you're brainstorming and want raw quantity and range over polish. |
| `incident` | Triages a live incident — stop the bleeding first, smallest safe fix next, post-mortem once stable; invoke via /incident when something is on fire in production and you need to restore service before understanding everyt |
| `long-haul` | Holds the thread across a big multi-step build without losing the plot — long-haul endurance and context-keeping; invoke via /long-haul when a task spans many steps and hours and you need consistency from start to finish |
| `macgyver` | Improvises a solution from primitives — standard library and cleverness instead of reaching for a new framework; invoke via /macgyver when the elegant answer is a few smart lines of plain code rather than another heavy d |
| `minimal-diff` | Makes the smallest possible change — surgical edits, minimal diff, nothing touched that didn't need to be; invoke via /minimal-diff when you want a precise fix with the tiniest footprint and zero collateral changes. |
| `pragmatic` | Picks the solution that works over the one that's technically correct — pragmatic, slightly cursed, perfect for prototypes; invoke via /pragmatic when you need a result that functions now and don't care that purists woul |
| `ruthless-focus` | Cuts the backlog to the one or two things that actually move the needle and ignores the rest; invoke via /ruthless-focus when you have too much to do and need ruthless prioritization, not a longer list. |
| `simplest-first` | Anti-gold-plating mode that builds the simplest functional version first, skips overengineering, and ships something usable today because working beats perfect; invoke via /simplest-first when you need it live tonight, n |
| `source-dive` | Goes underground to reverse-engineer internals, read the source nobody reads, and explain how something actually works; invoke via /source-dive when documentation is missing or lying and you need ground truth from the im |
| `speedrun` | Finds the shortest path to done and takes it — no detours, no nice-to-haves; invoke via /speedrun when the only goal is to complete the task as fast as possible with nothing extra. |
| `stopgap` | Ships the quick patch that unblocks you right now and clearly flags it as debt to revisit later; invoke via /stopgap when you're blocked and need a working stopgap immediately, accepting that it's temporary. |
| `ugly-first` | Gets it working ugly first — no naming debates, no structure until structure earns its place; invoke via /ugly-first when you want raw forward progress and will refactor later, if ever. |
| `use-whats-here` | Solves the problem using only what is already in the repo — no new libraries, no installs, a fix built from parts on hand; invoke via /use-whats-here when you can't or won't add dependencies and need a solution from exis |
| `wild-ideas` | Connects the dots nobody asked for — wild, lateral architecture ideas linking things you'd never pair on purpose; invoke via /wild-ideas when you want unconventional cross-domain inspiration, with the explicit understand |

## Use

Ask the agent for the job in plain words ("write chapters for this transcript"), or name the skill.
