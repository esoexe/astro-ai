---
description: Add or update tests for the given code using the project's own test framework, then run them.
---
Write tests for $ARGUMENTS using the project's existing test framework, layout and naming. Cover normal cases, boundaries and error paths that would catch real regressions. Keep them deterministic.

Run the new tests and the existing suite with the project's real test command, and report the exact command and its result. If a test exposes a bug in the code, report it instead of weakening the test.
