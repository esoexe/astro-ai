# Seed Pack

11 skills that take a startup from idea to funded: competitor research from one-star reviews, market sizing, investor matching from public sources and SEC filings, a business plan, a financial model, a pitch deck, investor updates, call coaching, a term-sheet reader, a data room, and a closer that drafts investor emails and books the calls.

> **Read first.** No promise of funding: the tools draft, research and do arithmetic, and investors decide. The tools never send anything: seed-closer writes drafts, and a batch goes out only after your own yes in the chat (through a Gmail MCP server you added, or as drafts you send yourself). Two helpers go online with no login or keys: seed-scout reads the App Store's public search and review feeds, and seed-investors searches SEC EDGAR Form D filings, which asks every request to say who is asking (sec_user_agent in seed/config.json). Not legal advice: have a startup lawyer read any SAFE or term sheet before you sign it. Not investment advice: the model and the market size are only as good as your assumptions. Cold email has to follow the law where you send it; every email carries your real name, your postal address and an opt-out, and a no, an opt-out or a bounce ends that thread for good.

Adapted from [seed-skill](https://github.com/Jakeschincariol/seed-skill) by Jake Schincariol (MIT). See NOTICE.md.

## Skills

| Skill | |
|---|---|
| `seed-closer` | The Closer. Sends personalised outreach emails to investors, manages the conversations and keeps following up until an investor call is booked, and nothing leaves the user's inbox without their yes. Drafts one RFC 5322… |
| `seed-coach` | Pitch Coach. Rehearses the investor call with you: drills the hard questions investors ask (market, competition, traction, unit economics, team, why now, risks, the ask), tailored to your own numbers from the competitor… |
| `seed-dataroom` | Data Room Builder. Assembles the data room investors ask for and tells you what is missing. Lays out the nine standard folders (01 Company, 02 Pitch deck, 03 Financials, 04 Market, 05 Product, 06 Team, 07 Legal, 08 Cap… |
| `seed-deck` | Pitch Deck Designer. Turns the business plan into a 10-slide pitch deck in the order investors expect: title, problem, solution, market, competition, business model, go-to-market, team, financials, the ask. Writes ONE… |
| `seed-investors` | Investor Finder. Researches the best investors for your idea from public sources only (the firm's own site, public profiles found through web search, SEC EDGAR Form D filings, the press) and ranks them by fit, 0 to 100,… |
| `seed-market` | Market Sizer. Sizes your market the way investors check it: TAM top-down (population x share x spend a year), SAM bottom-up (customers you can reach x purchases a year x price) and SOM (the share of SAM you can win in N… |
| `seed-model` | Financial Modeler. Builds the 36-month financial model behind your raise: customers, orders, GMV, revenue (GMV x your take rate), contribution, payroll, marketing (new customers x CAC), opex, net and cash, month by… |
| `seed-plan` | Business Planner. Turns your idea into a business plan investors can read: the nine sections (executive summary, problem, solution, market, competition, business model, go-to-market, financial plan, use of funds),… |
| `seed-scout` | Competitor Scout. Finds the weak points of your competitors in their own App Store reviews: searches the App Store for the apps people already use for your idea, reads up to the 500 most recent reviews of each, sorts… |
| `seed-terms` | Term Sheet Reader. Reads a SAFE or a term sheet in plain English, does the dilution math and flags the clauses to ask a lawyer about. Works out what a post-money SAFE owns at its cap and what it converts into at a… |
| `seed-updates` | Investor Updates. Writes the monthly investor update that keeps investors warm: a TL;DR, every metric with its month-over-month growth, highlights, lowlights, the asks, and the runway worked out from your cash and your… |

## Use

Ask the agent for the job in plain words ("write chapters for this transcript"), or name the skill.
Some skills run helper tools that are built into A.S.T.R.O AI (1.9.2 or later); nothing extra to install.
Some helpers read public web data (they say which site in their help text).
