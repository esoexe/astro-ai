---
name: refactor-safely
description: Restructure code without changing its behaviour, in small verified steps. Use for cleanups, extracting functions or modules, renaming, removing duplication or untangling large files.
---

# Refactor safely

## Steps

1. **Lock the behaviour.** Find the tests that cover the code. If coverage is thin, first add characterization tests that record what the code does today, including odd cases.
2. **Plan small steps.** Each step is one mechanical change (rename, extract function, move file, inline variable, replace conditional with lookup) that keeps the program working.
3. **After every step** build and run the tests. Commit or checkpoint when green.
4. **Keep the public surface stable.** If an exported name, file path, CLI flag or data format must change, keep a compatibility alias or list every caller you updated.
5. **Stop at the goal.** Do not mix in new features or bug fixes. If you find a bug, note it separately (or fix it in its own step with its own test, if the user agrees).

## Output

- The plan as a numbered list of steps.
- The changes, step by step.
- Proof: tests run after the final step, with the command and result.

## Rules

- Search the whole project for usages before renaming or deleting anything, including strings, configs, templates and docs.
- Prefer the project's existing patterns over introducing new abstractions.
