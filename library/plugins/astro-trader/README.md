# Trader Pack

11 investing research skills from public data (SEC EDGAR 13F filings, House disclosures, news): copy an investor's portfolio, watch news and Congress, size trades to your budget, risk, rebalancing and tax lots.

> **Read first.** Research and education only, not financial advice. Markets carry risk of loss and nothing here guarantees returns. The agent must never place, change or cancel an order without your explicit yes in the chat for that exact order; start with paper trading.

Adapted from [trader-skill](https://github.com/Jakeschincariol/trader-skill) by Jake Schincariol (MIT). See NOTICE.md.

## Skills

| Skill | |
|---|---|
| `trader-alerts` | Price Alerts. Rings when a price target, a stop or the opening bell hits: above / below / stop rules, percent moves from the previous close, and the NYSE opening bell, each ringing once so you are not spammed. Knows… |
| `trader-analyst` | The Earnings Analyst. Reads a company's reported numbers straight from SEC EDGAR (XBRL company facts from its 10-Qs and 10-Ks, plus the latest earnings 8-K and its press release) and sums up what changed: revenue,… |
| `trader-broker` | Order Executor. Places APPROVED orders through your broker's MCP server, one at a time, and never without your explicit yes. Shows each order as a plain ticket plus the exact JSON for Alpaca's place_stock_order, records… |
| `trader-budget` | Budget Manager. Sizes every position to your budget before anything is bought: keeps a cash buffer, caps any one stock (the excess is shared out to the others), drops slices under your minimum order, rounds so every… |
| `trader-copycat` | The Reverse Engineer. Reads a famous investor's public SEC filings (the quarterly 13F-HR, plus Form 4 if you want the insider trades) and rebuilds their portfolio as weights you can copy: every position, its share of… |
| `trader-news` | The News Monitor. Watches breaking market news and SEC filings for the stocks you hold (or are about to copy): Yahoo Finance headlines per ticker and each company's 8-K filings on SEC EDGAR (earnings, CEO changes,… |
| `trader-pelosi` | The Pelosi Tracker. Watches the stock trades members of Congress must disclose under the STOCK Act: reads the House Clerk's public Periodic Transaction Reports (PTRs) as soon as they are posted, alerts once per new… |
| `trader-rebalance` | Rebalancer. Brings the portfolio back to its target weights when it drifts: values every holding at current prices, works out each stock's drift from its target in percentage points, trades only the stocks outside a… |
| `trader-risk` | Risk Manager. Checks every proposed order against your limits before it can reach the broker and blocks anything over them: blocked tickers, max order size, minimum order, max position percent after the fill, the cash… |
| `trader-screener` | The Stock Screener. Checks a list of companies against the user's own rules (revenue growth, margins, EPS, P/E, earnings yield, sector, anything in the metrics file), says exactly which rule each company passes or fails… |
| `trader-tax` | Tax Optimizer. Tracks cost-basis lots for every position (FIFO, highest cost first, or specific lots), shows unrealized gains and losses by lot with short or long term, works out what a sale would realize before you… |

## Use

Ask the agent for the job in plain words ("write chapters for this transcript"), or name the skill.
Some skills run helper tools that are built into A.S.T.R.O AI (1.9.0 or later); nothing extra to install.
Some helpers read public web data (they say which site in their help text).
Some helpers call pdftotext if it is installed.
