# A.S.T.R.O AI coding benchmark

2026-10-29 23:35 UTC · astro 1.7.0 · mode auto · 12 tasks × 2

| Model | Passed | Rate | Avg time | Avg steps |
|---|---|---|---|---|
| auto | 24/24 | 100% | 143.8s* | 5.3 |

| Task | auto |
|---|---|
| js-off-by-one | 2/2 |
| js-add-slugify | 2/2 |
| js-async-foreach | 2/2 |
| js-make-tests-pass | 2/2 |
| js-rename-across-files | 2/2 |
| js-cli-flag | 2/2 |
| json-package-script | 2/2 |
| html-signup-form | 2/2 |
| c-reverse-bug | 2/2 |
| c-makefile | 2/2 |
| sh-count-lines | 2/2 |
| js-debug-from-error | 2/2 |

The checks are hidden from the agent and run after it finishes. Free models change often and have rate limits, so results vary between runs. A small set like this one shows trends; it is not a leaderboard.


*Timing note: the average includes the two js-add-slugify runs, which were measured across a pause of the test machine (about 1,400 s each, not real). Without them the average is 28.9 s; the median of all 24 runs is 20.8 s. Both json-package-script runs passed the hidden check but ended as "needs attention" (exit 4), correctly: the requested `npm test` script runs test.js, which does not exist in that task.
