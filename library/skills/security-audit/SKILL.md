---
name: security-audit
description: Audit code or a project for security weaknesses such as injection, broken authentication or authorization, leaked secrets, unsafe deserialization, SSRF, path traversal and vulnerable dependencies. Use for security reviews, before releases, or when handling untrusted input.
---

# Security audit

Defensive review only: find weaknesses and explain fixes. Do not write working exploits against systems the user does not own.

## Steps

1. **Map the attack surface.** List entry points (HTTP routes, CLI args, file uploads, message queues, webhooks), who can reach them, and what data or privileges sit behind them.
2. **Follow untrusted input** from each entry point to sensitive sinks:
   - SQL/NoSQL queries (parameterize), shell commands (no string concatenation; argument arrays), file paths (normalize and confine to a root), HTML (context-aware escaping), template engines, `eval`/dynamic imports, deserializers (pickle, YAML load, Java serialization), outbound URLs (SSRF: block private and metadata addresses), redirects.
3. **Check identity and access.** Authentication on every route, authorization on every object (no IDOR), session and token lifetime, CSRF for cookie sessions, password hashing (argon2/bcrypt/scrypt), rate limiting on login and expensive endpoints.
4. **Secrets and data.** Search for keys and tokens in code, history and logs; check that secrets come from the environment or a vault; verify TLS, encryption at rest where required, and that logs do not contain personal data.
5. **Dependencies and config.** Run the ecosystem's audit tool when available (`npm audit`, `pip-audit`, `cargo audit`, `govulncheck`). Check security headers, CORS, debug modes, default credentials, open admin panels and permissive file permissions.

## Report

A table of findings: severity (Critical/High/Medium/Low), location, impact in one sentence, and the fix. Then the top three actions in order. State what was out of scope or not checked.

## Rules

- Rate severity by real exploitability and impact, not by pattern matching.
- Never print real secrets you find; show only the location and the first few characters.
