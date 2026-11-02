# Job Hunter Pack

11 skills that help you land a job: scout listings, tailor your resume and cover letter, research the company, LinkedIn and networking, applications, interview prep, salary negotiation, follow-ups and a tracker.

Adapted from [hunter-skill](https://github.com/Jakeschincariol/hunter-skill) by Jake Schincariol (MIT). See NOTICE.md.

## Skills

| Skill | |
|---|---|
| `hunter-apply` | The Application Assistant. Fills the user's job applications from their own saved answers and leaves every one as a draft for them to review and submit: maps each form question to hunter/profile.json (name, links, work… |
| `hunter-connector` | The Connector. Finds the hiring manager for a role (from the posting, the team page and a LinkedIn search the user runs), researches them from public professional information only, works out their likely work email with… |
| `hunter-cover` | Cover Letter Writer. Writes a specific cover letter for every application: addressed to the hiring manager or the team, opening with something real about the company or the team from the research brief, one paragraph of… |
| `hunter-followup` | Follow-up Manager. Sends polite follow-ups so no thread goes cold, and stops before anyone feels chased. Every morning it reads hunter/pipeline.json and lists what is due today: follow-ups on unanswered outreach,… |
| `hunter-interview` | Interview Coach. Runs mock interviews with the questions this company actually asks, from a question bank that cites each company's own published interview-prep pages (Google, Meta, NVIDIA, Netflix, Amazon) and marks… |
| `hunter-linkedin` | The LinkedIn Optimizer. Rewrites the user's LinkedIn headline, About, skills and experience for the job they want, using only what is true, then checks the draft against LinkedIn's field limits (headline, About, skills… |
| `hunter-research` | The Company Researcher. Researches each company the user is applying to and writes a one-page brief in hunter/companies/: the business from its own SEC filings (latest 10-K, 10-Q, recent 8-Ks with what each one is… |
| `hunter-resume` | Resume Writer. Rewrites your resume for each role so it reads like the job description, using only what is true: it mirrors the posting's own words for skills you really have, moves buried results into strong bullets (a… |
| `hunter-salary` | Salary Negotiator. Benchmarks the role and scripts your counter-offer. Turns an offer into total compensation year by year (base, bonus target, equity with its real vesting schedule including back-loaded ones, sign-on),… |
| `hunter-scout` | The Job Scout. Finds the best open roles for the user's profile and ranks them by fit: pulls live listings from public job boards that need no login (Greenhouse, Lever, Ashby, NVIDIA's Workday board, Netflix's careers… |
| `hunter-tracker` | Pipeline Tracker. Tracks every role, contact and next step in one pipeline: hunter/pipeline.json, the single file every hunter skill reads and writes. Adds and updates roles and contacts, moves roles through the stages… |

## Use

Ask the agent for the job in plain words ("write chapters for this transcript"), or name the skill.
Some skills run helper tools that are built into A.S.T.R.O AI (1.9.0 or later); nothing extra to install.
Some helpers read public web data (they say which site in their help text).
