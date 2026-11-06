<div align="center">
  <a href="https://esoexe.github.io/astro-ai/">
    <img src="https://raw.githubusercontent.com/esoexe/astro-ai/main/website/src/static/assets/img/icon.svg" width="90" alt="Astro AI Icon">
  </a>
  <h1 align="center">A.S.T.R.O <em>AI</em></h1>
  <p align="center"><strong>All Systems Thinking, Reasoning as One.</strong></p>
  <p align="center">A free AI workspace where many free models work as one team.<br>One brain, many minds.</p>
</div>

<p align="center">
  <a href="https://github.com/esoexe/astro-ai/releases"><img src="https://img.shields.io/github/v/release/esoexe/astro-ai?color=00f0ff&label=release&style=flat-square" alt="Release"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-30363d?style=flat-square" alt="License"></a>
  <img src="https://img.shields.io/badge/pricing-100%25%20free-38ef7d?style=flat-square" alt="Free">
  <img src="https://img.shields.io/badge/account-none%20needed-7000ff?style=flat-square" alt="No Account">
  <img src="https://img.shields.io/badge/telemetry-zero-00f0ff?style=flat-square" alt="No Telemetry">
</p>

---

## 01 / What It's For

One app for the things people use AI for every day, with a whole team of free models behind every answer. No subscription, no sign-in.

* **Ask & learn:** Questions, explanations, research, writing and maths. Many models draft, check each other, and you get one clear answer. Talk to it with the live voice button.
* **Build software:** Open a folder. The agent reads your code, plans, writes, runs the tests and shows you a diff. You choose `Plan`, `Ask` or `Auto`, and every run can be undone.
* **Automate from the terminal:** The `astro` command brings the same agent to your shell, scripts and CI. One task, a live session, or JSON output for your own tools.
* **Run many jobs at once:** Start up to eight agents in parallel. Each one works in its own isolated copy of the project, so they never step on each other. Merge what you like.
* **Plug it into your tools:** A VS Code extension, a JavaScript SDK, and an OpenAI-compatible API. Reads your Claude Code, Cursor, and OpenCode rules, skills, and MCP servers.

---

## 02 / Inside Astro AI: App Showcase

A look directly inside the native Astro AI workspace:

### The Redesigned Agent Page
A 3-column developer layout: session history on the left, step-by-step test execution run in the center, and the Session panel on the right with git worktrees, Undo all, and MCP server controls:

<div align="center">
  <img src="https://esoexe.github.io/astro-ai/assets/img/shots/v15-agent.webp" alt="Astro Desktop Agent Page" width="100%">
</div>

### Reviewable Diffs & Undo All
Every single change made by the agent is recorded with syntax-highlighted diffs. One click rolls back the entire run instantly:

<div align="center">
  <img src="https://esoexe.github.io/astro-ai/assets/img/shots/v15-agent-changes.webp" alt="Agent Changes and Unified Diffs" width="100%">
</div>

### Model Context Protocol (MCP) Registry & Extensions
Connect custom tools, local databases, and external APIs directly from the built-in MCP manager:

<div align="center">
  <img src="https://esoexe.github.io/astro-ai/assets/img/shots/v11-mcp-registry.webp" alt="MCP Server Registry" width="100%">
</div>

---

## 03 / Terminal App: In Your Shell

Type `astro` in any directory for a full-screen TUI with approvals, model picking, and multi-agent parallelism:

<div align="center">
  <img src="https://esoexe.github.io/astro-ai/assets/img/shots/v15-tui-ask.webp" alt="Terminal App Ask Mode" width="100%">
</div>

```bash
# Start full-screen terminal app
astro

# One-shot task execution
astro -p "fix failing test in routes/todos.js"

# Parallel agents running in isolated copies
astro parallel "Refactor backend" "Add OpenAPI schema" --merge

# Diff and Undo
astro diff
astro undo
