# Security policy

## Supported versions

Only the latest release of A.S.T.R.O AI gets security fixes. The app checks for updates and tells you when a new
version is out (Settings > General > Updates). Older versions are not patched.

## Reporting a vulnerability

Please **do not open a public issue** for a security problem.

- Use GitHub's private reporting: **Security > Report a vulnerability** on
  https://github.com/esoexe/astro-ai/security/advisories/new
- Include the version (Settings > General), your system, the steps to reproduce and what an attacker could do.

We aim to reply within 7 days, and to ship a fix or a workaround within 30 days for confirmed problems. We will credit
you in the release notes unless you prefer not to be named. Please give us a reasonable time to fix the problem before
you publish details.

## Scope

In scope:
- the local engine (`workspace_server`) and its HTTP API, which listens on 127.0.0.1 only;
- the desktop apps, the terminal app (`astro`), the Android app and the VS Code extension;
- the Agent's safety checks (risky-command blocking, Plan/Ask/Auto permissions, isolated runs, checkpoints);
- the website and the A.S.T.R.O API (`/functions/v1/astro-api`).

Out of scope:
- bugs in third-party AI platforms, models or the MCP servers, plugins and coding CLIs you install yourself;
- answers that are wrong or unsafe because of the model (report those with *Send feedback*);
- attacks that need someone who already controls your computer or user account;
- the Agent doing what you asked in **Auto** mode. Auto runs commands with your permissions and is not an operating
  system sandbox; see https://esoexe.github.io/astro-ai/limits.html

## How A.S.T.R.O AI protects you

- **Keys stay local.** Provider keys are stored on your computer (or, for the optional A.S.T.R.O API, encrypted with
  AES-256-GCM). The CLEAN installers contain no keys.
- **Local-only engine.** The engine binds to 127.0.0.1 and requires an `X-Astro-Client` header on API calls (blocks
  cross-site requests from web pages). Optional password sign-in (PBKDF2) protects shared machines.
- **Android: app-only engine.** On Android every launch gets a new random port and a 256-bit token that only the app's
  WebView receives; the engine refuses every request without it, so other apps on the phone cannot use it. The
  bridge calls that reach the microphone, files or other apps need a per-page secret that frames don't get. Release
  APKs are signed; the certificate fingerprint is in `android/signing-cert.sha256`.
- **Agent guard rails.** Checkpoint before every job (one-click undo), risky commands blocked or approval-gated,
  Plan (read-only) and Ask (approve each change) modes, isolated runs in a copy or git worktree, and a finish audit that
  refuses unverified "done" claims.
- **Network guard.** Server-side fetches refuse private and loopback addresses (SSRF protection).
- **Opt-in reports only.** Nothing is reported unless you turn on error reports or press Send in the feedback form; text
  is scrubbed of keys, tokens, e-mail addresses and personal paths first. See https://esoexe.github.io/astro-ai/privacy.html
- **Checksums.** Every release lists SHA-256 checksums (`SHA256SUMS.txt`). Code signing is not active yet; until it is,
  verify the checksum of what you download.
