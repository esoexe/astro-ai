# Security notes

## Two desktop editions

| Edition | App name | Port | Contains keys | Where it goes |
|---|---|---|---|---|
| **Clean** | A.S.T.R.O AI Clean | 47612 | No | The website, GitHub releases, anyone |
| **With keys** (personal) | A.S.T.R.O AI | 47611 | **Yes, in plain text** (`seed-keys.env`, `gateway.env`) | Only you. **Never publish it.** |

The with-keys edition is not encrypted. Anyone who gets the file can read the keys, which is why:

* `desktop/package.sh` refuses to build it on CI (`CI` or `GITHUB_ACTIONS` set). CI builds use `EDITIONS=clean`.
* After every build, `desktop/package.sh` scans the clean edition for every key value from the personal one, with a positive control. The build aborts if anything is found.
* `tools/export_repo.sh` copies only an allow-list into the public repo, and `tools/scan_secrets.js` must pass before a push.
* CI fails if a private file is ever tracked: `.workspace.env`, `.astro_keys.env`, `.astro_auth.json`, `seed-keys.env`, `gateway.env`, `desktop-builds/with-keys/`, `outreach/`, `uploads/`.

## Rotating keys

Rotate a provider key whenever a with-keys build may have left your hands, and do it at least every few months:

1. Create a new key in the provider's dashboard, then delete the old one there. Deleting is what actually revokes it.
2. Paste the new key in A.S.T.R.O AI: **Settings > Providers & keys**. It is saved to `.astro_keys.env`, which only your user can read.
3. Gateway keys: update the Supabase secret with `supabase secrets set NAME=value`, or in the dashboard. No redeploy is needed.
4. Rebuild the personal edition (`bash desktop/package.sh`) and delete old copies, including Gofile links.

## Opt-in feedback

The 👎 button opens "What went wrong?":

* The note, question and answer are always saved locally in `.astro_feedback.jsonl`.
* They are sent to the A.S.T.R.O AI team only if you tick **Share**. Sharing is off by default.
* Shared rows go to a Supabase table that the public key can insert into but not read. Length limits are enforced by the database.
* Keys, files and settings are never included.
* Set `ASTRO_FEEDBACK_SHARE=0` to turn sharing off completely.

## Background code check

Answers are shown first. Afterwards, A.S.T.R.O AI may compile or run self-contained code blocks on your computer:

* Each run has a 6-second limit, a temporary folder, and no API keys in the environment.
* Code that reads files, uses the network, starts processes, needs packages or asks for input is only syntax-checked, never run.
* You can turn the check off in **Settings > Brain > Check code in the background**.
