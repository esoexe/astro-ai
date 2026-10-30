# Pro plan: plumbing only

**Status: not on sale.** Nothing in the app is locked. This page explains the scaffolding so a paid plan can be switched on later
without changing the app.

## What exists

| Piece | Where | What it does |
|---|---|---|
| `astro_licenses` table | Supabase migration `20261030_astro_licenses.sql` | One row per license: SHA-256 **hash** of the key, plan (`pro`/`team`), status, expiry, seller reference. Not readable with the public key. |
| `astro_license_check(p_key)` | same migration | Public RPC: returns `{valid, plan, expires_at}` or `{valid:false, reason}`. Needs the exact key, so it can't be used to list licenses. |
| `GET/POST /api/license`, `POST /api/license/remove` | `astro_quality.inc` | The engine checks a key, keeps it in `<data>/.astro_license.json`, re-checks every 7 days and allows 30 days offline. |
| Settings > Account & sync > *A.S.T.R.O AI Pro* | `web/app.js` | Waitlist link plus "I have a license key". |
| Website | `pro.html` (waitlist), `support.html` (donations) | The waitlist goes to `astro_reports` with `kind = 'waitlist'`. |

## To start selling (later, by the team)

1. Open a seller account with a **merchant of record** that handles VAT and pays out to the team's country: Lemon Squeezy or
   Paddle. Check first that it can pay out to the team (country, PayPal or Payoneer). Stripe does not onboard Algerian
   businesses. The agent does not create accounts.
2. Create a subscription product. In the seller dashboard, add a webhook pointing at a new Supabase edge function
   (`astro-license`) that verifies the webhook signature, generates a random key (`ASTRO-` + 24 random base32 characters),
   e-mails it to the buyer through the seller's license feature or receipt, and inserts `sha256(key)` with
   `seller = 'lemonsqueezy'|'paddle'` and the order id. On cancellation or refund it sets `status`.
3. Decide which features Pro adds. Gate them **in the engine** with `license_state(false)` → `active`, never by removing
   free features that exist today.
4. Update `pro.html` with the real price and terms, and add the seller to the privacy page.

## Donations

Set `support.github_sponsors`, `support.kofi` or `support.liberapay` in `website/site.json` once the account exists.
The Support page shows the buttons automatically, and `.github/FUNDING.yml` already points GitHub's Sponsor button at it.
