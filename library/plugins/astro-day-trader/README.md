# Day Trader Pack

11 day-trading research skills from free public data (Yahoo Finance charts, Nasdaq symbol lists, SEC, House): chart scanner, news, entries and exits, budget, risk, broker tickets and a time machine that replays outcomes.

> **Read first.** Research and education only, not financial advice. Day trading is high risk and most day traders lose money; nothing here guarantees returns. The agent must never place, change or cancel an order without your explicit yes in the chat for that exact order; replay and paper trade first.

Adapted from [day-trader-skill](https://github.com/Jakeschincariol/day-trader-skill) by Jake Schincariol (MIT). See NOTICE.md.

## Skills

| Skill | |
|---|---|
| `daytrade-alerts` | Price Alerts. Rings when a price, a stop or the opening bell hits. Turns the trade plan into three alerts in one go (the stop, the target and 'flat by close' at your flat-by time), plus above / below / stop / target… |
| `daytrade-analyst` | The Earnings Analyst. Reads a company's reported numbers straight from SEC EDGAR (XBRL company facts from its 10-Qs and 10-Ks, plus the latest earnings 8-K and its press release) and sums up what changed: revenue,… |
| `daytrade-broker` | Order Executor. Sends approved orders to your broker, paper trading by default, never without your yes. Shows each order as a plain ticket (a bracket shows its stop-loss, take-profit and the max loss at the stop) plus… |
| `daytrade-budget` | Budget Manager. Sizes every position to your budget and your max loss per trade. Reads the trade plan from daytrade-exit (entry, stop, target), finds the whole shares that fit inside four limits (max loss per trade,… |
| `daytrade-entry` | Entry Timer. Watches a setup bar by bar and tells you the moment to get in. Reads free Yahoo 1-minute or 5-minute bars, builds the opening range (the first 15 minutes by default) and calls the entry on the first… |
| `daytrade-exit` | Exit Planner. Sets your stop and your target before you enter, so you know when to get out. Reads the entry from daytrade-entry (or a price you give), puts the stop at the opening-range midpoint by default (or the range… |
| `daytrade-news` | News Monitor. Tracks breaking news on every stock you watch and flags what moves it: Yahoo Finance headlines per ticker, each company's 8-K filings on SEC EDGAR (earnings, CEO changes, deals, restatements, bankruptcy)… |
| `daytrade-pelosi` | The Pelosi Tracker. Watches the stock trades members of Congress must disclose under the STOCK Act: reads the House Clerk's public Periodic Transaction Reports (PTRs) as soon as they are posted, alerts once per new… |
| `daytrade-risk` | Risk Manager. Checks every trade against your limits and stops anything over them, before it can reach the broker. For a day trade: a stop is required, the loss at the stop must fit your max loss per trade (measured… |
| `daytrade-scanner` | Chart Scanner. Scans thousands of charts for setups: opening-range breakouts, flags, oversold bounces. Builds the list of US-listed common stocks, ordinary shares and ADRs from Nasdaq Trader's symbol directories, runs a… |
| `daytrade-timemachine` | Time Machine. Replays every possible outcome of a trade before you risk a dollar: 10,000 simulated sessions plus every past time the setup fired. Takes the plan from daytrade-exit (entry, stop, target), rebuilds the… |

## Use

Ask the agent for the job in plain words ("write chapters for this transcript"), or name the skill.
Some skills run helper tools that are built into A.S.T.R.O AI (1.9.0 or later); nothing extra to install.
Some helpers read public web data (they say which site in their help text).
Some helpers call pdftotext if it is installed.
