---
name: pr-description
description: Write a pull request title and description from the branch's commits and diff. Use when opening or updating a PR or merge request.
---

# Pull request writer

## Steps

1. Read `git log <base>..HEAD` and `git diff <base>...HEAD --stat`, then the important hunks.
2. Check for a PR template (`.github/pull_request_template.md`, `.gitlab/merge_request_templates/`) and follow it if present.
3. Write:

```
## Why
<the problem or goal, linked issue>

## What changed
- <user-visible or architectural change, grouped by area>

## How it was tested
- <commands run and results, manual steps, screenshots for UI>

## Risks and rollout
- <migrations, config, feature flags, backwards compatibility, what to watch after merge>
```

4. Title: short, imperative, under 70 characters, with the scope if the repository uses one.

## Rules

- Only claim tests that were really run. If none were run, say "Not tested yet" and list what should be.
- Call out anything reviewers must look at closely (security-sensitive code, data migrations, deleted behaviour).
