---
name: regex-builder
description: Build, explain and test regular expressions for a given language or engine, with examples that must and must not match. Use whenever a task needs a regex.
---

# Regex builder

## Steps

1. **Confirm the engine** (JavaScript, Python `re`, PCRE, Go RE2, POSIX, .NET, Java). Features such as lookbehind, named groups and Unicode classes differ.
2. **Collect examples.** At least five strings that must match and five that must not, including edge cases (empty string, extra spaces, Unicode, very long input).
3. **Write the pattern** anchored where appropriate (`^...$` or `\b`), with named groups for parts the code will use, and the right flags (case-insensitive, multiline, Unicode).
4. **Explain it** piece by piece in a short table.
5. **Test it** against every example, in code when tools allow, and show the results.

## Rules

- Avoid catastrophic backtracking: no nested quantifiers like `(a+)+` on untrusted input; prefer possessive or atomic groups or a parser when the engine allows.
- If a real parser would be safer (HTML, JSON, email per RFC, nested structures), say so and suggest the library to use.
