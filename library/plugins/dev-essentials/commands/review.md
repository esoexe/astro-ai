---
description: Review the current changes (or the given files) for real bugs, security problems and maintainability.
---
Review $ARGUMENTS. If nothing is named, review the uncommitted changes (`git diff` and `git diff --staged`).

Use the code-review skill if it is installed. Read the callers and callees of changed functions before judging them. Report findings as Blocker / Should fix / Nit with `file:line`, the input that breaks it, and the smallest fix. Say "No blockers found" if that is the truth, and list what you could not check.
