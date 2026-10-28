---
name: dependency-upgrade
description: Upgrade project dependencies safely, one step at a time, reading changelogs for breaking changes and verifying with tests. Use for outdated packages, security advisories or framework major-version upgrades.
---

# Dependency upgrades

## Steps

1. **Inventory.** List outdated packages with the ecosystem tool (`npm outdated`, `pip list --outdated`, `cargo outdated`, `go list -m -u all`) and any security advisories (`npm audit`, `pip-audit`, `cargo audit`, `govulncheck`).
2. **Prioritize.** Security fixes first, then patch and minor versions, then majors one at a time.
3. **For each major upgrade,** read the release notes and migration guide between the current and target versions. List the breaking changes that affect this project by searching for the APIs involved.
4. **Upgrade and adapt.** Change one package (or one tightly coupled group) at a time, update the lock file, fix the code for breaking changes.
5. **Verify.** Build, run the test suite and a quick manual smoke test of the affected features after each step.

## Output

A table: package, from, to, why, breaking changes found, code changes made, test result. Mention anything held back and why.

## Rules

- Never delete the lock file to "fix" conflicts.
- Pin exact versions where the project already does; respect existing version policies.
