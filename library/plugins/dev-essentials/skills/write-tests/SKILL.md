---
name: write-tests
description: Write focused automated tests for existing code using the project's own test framework and conventions. Use when asked to add tests, raise coverage, or protect a bug fix.
---

# Write tests

## Steps

1. **Find the conventions.** Look for the test runner and existing tests (`package.json` scripts, `pytest.ini`/`pyproject.toml`, `Cargo.toml`, `go test`, `CMakeLists.txt`, `*_test.*`, `tests/`, `__tests__/`). Copy their style, folder layout, naming and helpers. Do not add a new framework when one exists.
2. **Read the code under test** and list its behaviours: normal cases, boundaries (empty, one, many, max), invalid input, error paths, and any side effects (files, network, time, randomness).
3. **Pick the cases that would catch real regressions.** Aim for a few sharp tests over many shallow ones. For a bug fix, first write the test that fails on the old code.
4. **Keep tests deterministic.** Freeze time, seed randomness, use temporary directories, and fake the network at the boundary instead of mocking internals.
5. **Write the tests.** One behaviour per test, a name that reads as a sentence (`returns_empty_list_when_no_rows`), arrange / act / assert.
6. **Run them.** Run the new tests and the existing suite with the project's real command. Report the exact command and the result. If a test fails because the code is wrong, say so instead of weakening the test.

## Rules

- Never change production code just to make a test pass unless the user asked for the fix.
- Never assert on incidental details (full error strings from libraries, dict ordering, timestamps).
- If you could not run the tests, say that clearly and give the command to run.
