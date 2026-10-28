---
description: Write a Conventional Commits message from the staged diff and commit it.
---
Read `git diff --staged` (or `git diff` if nothing is staged; ask before staging everything). Write a Conventional Commits message: `type(scope): summary` (imperative, at most 72 characters), a body that explains why, and a `BREAKING CHANGE:` footer when needed. If the diff mixes unrelated changes, propose splitting it first. Show the message, then commit with it. Extra notes from the user: $ARGUMENTS
