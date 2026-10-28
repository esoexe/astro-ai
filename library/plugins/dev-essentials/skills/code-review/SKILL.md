---
name: code-review
description: Review code changes or files for real defects first (logic, edge cases, security, concurrency), then performance and readability. Use when the user asks for a review, a second opinion, or "is this right?".
---

# Code review

Review like a senior engineer who has to maintain this code next year. Find problems that matter; skip taste.

## Steps

1. **Understand the intent.** Read the diff or files and any linked issue. State in one sentence what the change is meant to do. If you cannot tell, say so before reviewing.
2. **Read the surroundings.** Open the callers and callees of every changed function. Most real bugs live at the boundary between the changed code and the code it touches.
3. **Hunt for defects, in this order:**
   - Wrong results: off-by-one, inverted conditions, wrong operator, unhandled `null`/empty/zero, integer overflow, float comparison, time zones, Unicode.
   - Error paths: exceptions swallowed, resources not closed, partial writes, retries that are not idempotent.
   - Security: untrusted input reaching SQL, shell, file paths, HTML or `eval`; missing authorization checks; secrets in code or logs.
   - Concurrency: shared state without locks, check-then-act races, async calls not awaited.
   - Contracts: public API or data format changed without updating callers, docs or migrations.
4. **Then performance:** only where it is visible (N+1 queries, work inside hot loops, unbounded memory).
5. **Then clarity:** names that lie, dead code, duplicated logic that will drift.

## Output

Group findings by severity and give each a location and a concrete fix:

- **Blocker**: will cause wrong behaviour, data loss or a security hole.
- **Should fix**: likely bug or maintenance trap.
- **Nit**: optional polish (keep these few).

For each finding: `file:line`, what goes wrong (with a concrete input that triggers it when possible), and the smallest correct fix as a code snippet.

End with a one-line verdict (ready / ready after blockers / needs rework) and list anything you could not verify, such as code you did not have or tests you could not run.

## Rules

- Never invent a bug to have something to say. "No blockers found" is a valid result.
- Quote the real code you are talking about; do not paraphrase it from memory.
- Prefer running the tests or a tiny reproduction over guessing, when tools allow it.
