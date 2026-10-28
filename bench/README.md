# A.S.T.R.O AI coding benchmark

These are small, realistic repository tasks. They measure how the A.S.T.R.O AI agent does with free models. Each task:

1. starts from a few files in a fresh temporary folder,
2. gives the agent a short job, the way a person would phrase it,
3. after the agent finishes, copies in **hidden check files** and runs a check command. Exit code 0 means pass.

The agent never sees the checks, so it cannot fit its answer to them. `--self-test` proves that every check fails on the starting files and passes on a reference solution (`solutions.json`). `make test` runs the self-test.

| Task | What it tests |
|---|---|
| js-off-by-one | Fix a loop bound |
| js-add-slugify | Add a function to a module with exact edge cases (accents, separators) |
| js-async-foreach | Fix a classic async bug (`forEach` + `await`) |
| js-make-tests-pass | Implement a class from a test file without editing the test |
| js-rename-across-files | Rename a function in 3 files without changing behaviour |
| js-cli-flag | Add a flag without changing the default output |
| json-package-script | Edit `package.json` precisely |
| html-signup-form | Accessible form markup (label, required, minlength) |
| c-reverse-bug | Off-by-one and empty-string bug in C |
| c-makefile | Write a working Makefile with `clean` |
| sh-count-lines | Shell script with usage errors and exit codes |
| js-debug-from-error | Run the program, read the crash, fix the real cause |

## Run

```bash
make astro                                  # the terminal app
node bench/run.js --self-test               # validate the tasks (no model needed)
node bench/run.js                           # all tasks, the default model choice, 2 at a time
node bench/run.js --models "auto,router9:deepseek/deepseek-v4-pro" --repeat 3 --parallel 3
node bench/run.js --tasks js-off-by-one,c-makefile --review
```

You need a running A.S.T.R.O AI engine: the desktop app, `astro server start`, or `ASTRO_URL`. Results go to `bench/results/<time>.json` and `bench/results/latest.md`.

Needs: node, bash, a C compiler and make.

## Latest result

October 28, 2026: one run with the default choice of free models (`auto`). **12/12** tasks passed, averaging 39.5 s and 4.1 steps per task. The models that did the work included DeepSeek V4 (Pro and Flash), Codestral, Llama 3.3 70B and Qwen 3.5 397B, with automatic fallback between providers. See [results/latest.md](results/latest.md).

## Reading the numbers

- The tasks are small on purpose. They take minutes on free models and show whether the agent loop works: it reads, edits, runs and checks its own work.
- They are **not** a substitute for large public benchmarks such as SWE-bench. Do not compare them with numbers other tools publish on their own task sets.
- Free models change, get rate-limited and fall back to other models. Results vary from run to run. Use `--repeat` and compare runs made on the same day.
