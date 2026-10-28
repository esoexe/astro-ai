---
name: explain-codebase
description: Explain an unfamiliar repository by mapping its purpose, entry points, main modules, data flow, build and test commands, and where to make a given change. Use when onboarding or before a large change.
---

# Explain a codebase

## Steps

1. **Survey.** Read the README, the top-level folder listing, and the manifest files (`package.json`, `pyproject.toml`, `Cargo.toml`, `go.mod`, `CMakeLists.txt`, `Makefile`, `Dockerfile`, CI workflows).
2. **Find the entry points:** `main` functions, server start-up, CLI definitions, route tables, exported library modules, scheduled jobs.
3. **Trace one real request or command end to end**, naming the files and functions it passes through.
4. **Identify the core modules** and how they depend on each other. Note shared utilities, data models and configuration.
5. **Note how to work on it:** install, build, run, test and lint commands (as defined in the project, not guessed), required environment variables, and conventions (style, branching, commit format).

## Output

- One-paragraph summary of what the project does.
- A short architecture map (a list or a small diagram in text).
- "Where to change X": for the user's goal, the files to touch and the tests to run.
- Open questions and risky areas (no tests, very large files, unclear ownership).

## Rules

- Cite file paths for every claim. If something is a guess, label it as one.
