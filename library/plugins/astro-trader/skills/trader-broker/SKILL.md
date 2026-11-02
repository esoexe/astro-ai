---
name: trader-broker
description: "Order Executor. Places APPROVED orders through your broker's MCP server, one at a time, and never without your explicit yes. Shows each order as a plain ticket plus the exact JSON for Alpaca's place_stock_order, records your yes word for word, then A.S.T.R.O AI's own Money action card asks again for that exact call. Covers setting up Alpaca's official MCP server in A.S.T.R.O AI (paper trading by default, keys in the MCP environment, never in chat) and placing orders by hand with any other broker. Use when the user says 'place the orders', 'buy it', 'execute', 'send it to my broker', 'connect Alpaca', 'connect Interactive Brokers', 'set up paper trading', or after /trader-risk passes."
argument-hint: "[order number]"
---

> **A.S.T.R.O AI safety rule.** Research and education only, not financial advice. Markets carry risk of loss and nothing here guarantees returns. The agent must never place, change or cancel an order without your explicit yes in the chat for that exact order; start with paper trading.

# trader-broker

The Order Executor is the only skill in this pack that can lead to a real
order, so it is the strictest. An order reaches the market only after three
separate gates, every time, for every order:

1. **trader-risk passed this exact order today** (matched by fingerprint).
2. **The user typed an explicit yes for this one order** in the chat.
3. **The user approved A.S.T.R.O AI's Money action card** for that one call,
   which appears for every order tool call, in every mode.

Two yeses from the user, one PASS from the rules. No yes, no order.

`${ASTRO_SKILL_DIR}` below is this skill's folder (where this SKILL.md lives).

```bash
${ASTRO_TOOL} trader-broker/ticket show                     # every ticket, READY or not
${ASTRO_TOOL} trader-broker/ticket approve --index 1 --reply "yes"
${ASTRO_TOOL} trader-broker/ticket result --index 1 --status placed --broker-id ID
${ASTRO_TOOL} trader-broker/ticket fill --symbol AAPL --side buy --qty 3.6628 --price 341.27
${ASTRO_TOOL} trader-broker/ticket log
```

`ticket` never talks to a broker. It renders tickets, checks that a reply
is a real yes for that order, and keeps the log in `trader/state/broker.json`.

## Setup A: Alpaca (official MCP server, in A.S.T.R.O AI)

Alpaca's own server is github.com/alpacahq/alpaca-mcp-server (MIT). It runs on
the user's computer as a local MCP server inside A.S.T.R.O AI, and it trades on a
**paper** account unless told otherwise.

1. The user makes an Alpaca account and creates **paper trading** API keys in
   the Alpaca dashboard. Paper keys are separate from live keys.
2. The user installs `uv` (it provides `uvx`), from docs.astral.sh/uv.
3. In A.S.T.R.O AI: **Extensions > MCP > Add server**. The user fills in, themselves:
   - Name: `alpaca`
   - Command: `uvx alpaca-mcp-server`
   - Environment, one per line: `ALPACA_API_KEY=…`, `ALPACA_SECRET_KEY=…`,
     `ALPACA_PAPER_TRADE=true`, `ALPACA_TOOLSETS=account,trading,stock-data`

   The same block, ready to merge into A.S.T.R.O AI's global `mcp.json`, is in
   `alpaca.mcp.example.json` in this skill's folder. The environment values stay
   on the user's own A.S.T.R.O AI server and go to the MCP server as environment
   variables. **Keys never go in the chat.** Never ask for them, never read the
   MCP settings or `mcp.json` back into the conversation, and if the user pastes a
   key anyway, tell them to delete that message and make new keys in the Alpaca
   dashboard. `ALPACA_PAPER_TRADE` is already true by default; writing it makes it
   visible. `ALPACA_TOOLSETS` is optional and hides the tools they do not need.
4. A.S.T.R.O AI starts the server. Check it: call the read-only account tool
   (`get_account_info`) and confirm in the Alpaca dashboard that it is the paper account.

**How A.S.T.R.O AI guards orders.** Any MCP tool whose name places, changes,
cancels or closes an order or moves money (`place_stock_order`,
`cancel_order_by_id`, `close_position`, transfers and so on) gets a **Money
action** card for every single call, in every mode, Auto included: "alpaca →
place_stock_order. Approve this exact call?" with the exact arguments. "Allow all"
never covers it, and in a run nobody is watching the call is blocked outright.
Read-only tools (`get_account_info`, `get_all_positions`, `get_stock_snapshot`,
`get_stock_latest_quote`) run without a card. Never call `place_option_order`,
`place_crypto_order`, `close_all_positions`, `cancel_all_orders` or
`exercise_options_position` unless the user has asked for that exact action.

## Setup B: any other broker (the user places the order)

For Interactive Brokers or any broker without a local MCP server the user has set
up, `ticket` still renders every ticket, and the user places the order
themselves in their broker's own app or website, then tells you what happened so
you can record it with `ticket result` and `ticket fill`. Set
`"broker": "manual"` (or `"ibkr"`) in `trader/config.json`, and size with
`--whole-shares` if the broker wants share counts. Never sign in to a broker for
the user and never ask for their password.

## Steps, for each order

1. `ticket show`. Only READY tickets go to the user. A ticket that is NOT
   READY or BLOCKED goes back to `/trader-risk`.
2. Show ONE ticket and ask: "Place this order? Reply yes or no." One order
   per question. Never offer "yes to all".
3. Run `ticket approve --index N --reply "<the user's reply, word for word>"`.
   If it prints "Not recorded", the reply was not an explicit yes for this
   order (for example "sure", "yes to all", or a yes that names a different
   stock or amount). Ask again. Never turn a reply into a yes yourself.
4. Only after "Recorded your yes", call `place_stock_order` with exactly the
   JSON the ticket printed. A.S.T.R.O AI's Money action card appears: Allow or Deny.
5. Record what happened: `ticket result --index N --status placed
   --broker-id <id from the response>`, or `--status denied` if they pressed
   Deny, or `--status failed` with `--note` for an error. A failed order is
   not retried without a new check and a new yes.
6. When the fill is known (the tool's response, or later from
   `get_all_positions`), `ticket fill` adds it to `trader/portfolio.json`
   as a tax lot for trader-tax. Then the next ticket.

## Example: the reel

```
ORDER 1 of 5, PAPER account, Alpaca
  BUY $1,250 of AAPL, market order, good for today
  READY: trader-risk PASS, waiting for your yes
  Astro will call place_stock_order with exactly:
    {"symbol": "AAPL", "side": "buy", "notional": "1250",
     "type": "market", "time_in_force": "day"}
```

The user replies "yes". Astro records it, calls `place_stock_order`, and the
Money action card reads "alpaca → place_stock_order. Approve this exact call?"
with that JSON and two buttons: Allow, Deny. Allow sends it to the paper account.

## What it never does

- Never places an order without all three gates, and never two orders on one yes.
- Never places orders from a scheduled or background task: nobody is there to say yes.
- Never asks for, reads, stores or repeats API keys or passwords.
- Never switches `ALPACA_PAPER_TRADE` or `"paper"` to live. The user does that themselves.
- Never calls `close_all_positions`, `cancel_all_orders`, option or crypto order tools on its own.

## Safety

- Education and tooling, not financial advice. No order here is a recommendation, and nothing promises returns.
- Paper trading by default (`"paper": true`, `ALPACA_PAPER_TRADE` true). Going live is the user's own decision.
- Nothing in this pack places an order except trader-broker, and only when all three hold: trader-risk
  passed that exact order today, the user typed an explicit yes for that one order in the chat, and the
  user approved A.S.T.R.O AI's Money action card for that one call (it appears for every order tool
  call in every mode, and "Allow all" never covers it).
- No options, margin, short selling or leverage unless the config allows it and the user confirms.
- Broker keys live in environment variables or A.S.T.R.O AI's MCP settings, never in the chat.
