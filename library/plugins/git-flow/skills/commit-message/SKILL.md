---
name: commit-message
description: Write a clear commit message from the actual staged diff, in Conventional Commits style. Use when the user asks for a commit message or to commit changes.
---

# Commit messages

## Steps

1. Read the real diff (`git diff --staged`, or `git diff` if nothing is staged). Never write a message from memory of the conversation alone.
2. Work out the single main purpose. If the diff mixes unrelated changes, suggest splitting it and show how.
3. Write the message:

```
<type>(<optional scope>): <summary in the imperative, max 72 chars>

<body: why the change was needed and anything non-obvious about how,
wrapped at 72 columns>

<footer: BREAKING CHANGE: ..., Fixes #123>
```

Types: `feat`, `fix`, `docs`, `refactor`, `perf`, `test`, `build`, `ci`, `chore`, `style`, `revert`.

## Rules

- The summary says what the commit does ("add retry to upload"), not what you did ("added", "changes").
- No trailing period in the summary. No filler such as "various fixes" or "update code".
- Mention breaking changes explicitly in the footer.
- Follow the repository's existing style if `git log` shows a different convention.
