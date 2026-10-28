---
name: debug-detective
description: Systematically find and fix the root cause of a bug, crash, wrong output or failing test. Use when something "doesn't work", throws, hangs, or behaves differently than expected.
---

# Debug detective

Fix causes, not symptoms.

## Steps

1. **Pin down the failure.** Collect the exact error text, stack trace, input, expected vs actual output, environment (versions, OS) and when it started. Ask for anything essential that is missing.
2. **Reproduce it.** Write the smallest command, test or script that shows the failure every time. If it cannot be reproduced, add logging at the suspected boundaries and say what you are waiting to see.
3. **Narrow it down.** Read the stack trace bottom-up to the first frame in the project's own code. Bisect: inputs (smaller data), code (comment out halves), history (`git bisect` between a good and a bad commit).
4. **Form one hypothesis at a time** and design a check that could prove it wrong (print a value, add an assertion, run one case). Write down what you ruled out.
5. **Fix the root cause** with the smallest correct change. Check other call sites that share the same flawed assumption.
6. **Prove it.** The reproduction now passes, a regression test is added, and the existing tests still pass.

## Report

- Cause: one or two sentences, pointing at `file:line`.
- Fix: the diff.
- Proof: the command you ran and its result.
- Related risks you noticed but did not change.

## Rules

- Do not silence errors (`try/except: pass`, `catch {}`) or raise timeouts to make a symptom disappear.
- Do not change several things at once; you will not know which one mattered.
