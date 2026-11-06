<div align="center">
  <a href="https://esoexe.github.io/astro-ai/">
    <img src="assets/hero.svg" alt="A.S.T.R.O AI: One Brain, Many Minds" width="100%">
  </a>
</div>

<p align="center">
  <strong>All Systems Thinking, Reasoning as One.</strong><br>
  A free AI workspace where many free models work as one team. Ask it anything, build software with an agent that plans, writes and tests, or hand it jobs from your terminal. One brain, many minds.
</p>

<p align="center">
  <a href="https://github.com/esoexe/astro-ai/releases"><img src="https://img.shields.io/github/v/release/esoexe/astro-ai?color=00f0ff&label=release&style=flat-square" alt="Release"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-30363d?style=flat-square" alt="License"></a>
  <img src="https://img.shields.io/badge/pricing-100%25%20free-38ef7d?style=flat-square" alt="Free">
  <img src="https://img.shields.io/badge/account-none%20needed-7000ff?style=flat-square" alt="No Account">
  <img src="https://img.shields.io/badge/telemetry-zero-00f0ff?style=flat-square" alt="No Telemetry">
</p>

---

## 01 / Live Showcase: The Desktop Agent

Here is the native Astro AI agent working live in `Auto` mode: reading files, building consensus across 19 models, writing code, and verifying tests:

<div align="center">
  <img src="assets/agent-live.svg" alt="Live Animated Desktop Agent Workspace" width="100%">
</div>

---

## 02 / Live Showcase: The Terminal App

Type `astro` in any directory for a full-screen interactive TUI with approvals, model picking, and test execution:

<div align="center">
  <img src="assets/tui-live.svg" alt="Live Animated Terminal TUI" width="100%">
</div>

```bash
# Start full-screen terminal app
astro

# One-shot task execution with report
astro -p "fix failing test in routes/todos.js"

# Run parallel agents in isolated worktrees
astro parallel "Refactor backend" "Add OpenAPI schema" --merge

# Revert any changes instantly
astro undo
