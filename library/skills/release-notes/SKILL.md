---
name: release-notes
description: Turn git history, merged PRs or a diff between two tags into release notes and a CHANGELOG entry. Use when preparing a release or summarising what changed.
---

# Release notes

## Steps

1. Collect the changes: `git log --no-merges <previous-tag>..<new-ref> --pretty=format:'%h %s'`, plus merged PR titles if available.
2. Drop noise: formatting, internal refactors, CI tweaks and dependency bumps without user impact (mention security updates, though).
3. Group by what users care about:
   - **Highlights**: the one to three changes worth a sentence each.
   - **New**, **Improved**, **Fixed**, **Breaking changes** (with migration steps), **Security**.
4. Rewrite each entry for the reader: what they can now do or what no longer goes wrong, not the internal mechanism.
5. Match the existing CHANGELOG format (Keep a Changelog, date style, version heading) if the project has one.

## Rules

- Every breaking change gets an explicit "what to do" line.
- Do not claim improvements that are not in the history.
- Keep entries short; link the PR or commit for detail.
