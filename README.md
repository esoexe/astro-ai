<div align="center">
  <a href="https://esoexe.github.io/astro-ai/">
    <img src="https://raw.githubusercontent.com/esoexe/astro-ai/main/website/src/static/assets/img/icon.svg" width="90" alt="A.S.T.R.O AI Icon">
  </a>
  <h1 align="center">A.S.T.R.O <em>AI</em></h1>
  <p align="center">
    <strong>A</strong>ll <strong>S</strong>ystems <strong>T</strong>hinking, <strong>R</strong>easoning as <strong>O</strong>ne.
  </p>
  <p align="center">
    A free AI workspace where many free models work as one team.<br>
    Ask it anything, build software with an agent that plans, writes and tests, or hand it jobs from your terminal.<br>
    <strong>One brain, many minds.</strong>
  </p>
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

* **Ask & learn:** Questions, explanations, research, writing and maths. Many models draft, check each other, and you get one clear answer. Talk to it with the voice button.
  > *“Explain how HTTPS works, simply.”*
* **Build software:** Open a folder. The agent reads your code, plans, writes, runs the tests and shows you a diff. You choose Plan, Ask or Auto, and every run can be undone.
  > *“Add login with email and a test.”*
* **Automate from the terminal:** The `astro` command brings the same agent to your shell, scripts and CI. One task, a live session, or JSON output for your own tools.
  > `astro -p "fix the failing test"`
* **Run many jobs at once:** Start up to eight agents in parallel. Each one works in its own isolated copy of the project, so they never step on each other. Merge what you like.
  > `astro parallel "A" "B" "C"`
* **Plug it into your tools:** A VS Code extension, a JavaScript SDK and an OpenAI-compatible API. It also reads your Claude Code, Cursor and OpenCode rules, skills and MCP servers.
* **Who it's for:** Students, developers, makers and small teams who want a capable AI without paying for one. It runs in the cloud, so an older laptop is fine.

---

## 02 / How It Works: Five Letters, One Idea

The name is the method. Every question and every coding job goes through the same five steps:

1. **A — All models:** Every free model you have access to joins in, including the slow ones. Free tiers have limits; when one provider is busy, the rest carry on.
2. **S — Systems:** The team can use real tools: your files, a terminal, web search, a code runner, MCP servers and skills.
3. **T — Thinking:** Each model works on its own draft or plan, at the same time, so you see different approaches, not one guess.
4. **R — Reasoning:** The drafts are compared and reviewed. A mistake one model makes is usually caught by another.
5. **O — One answer:** You get one clear answer, or one finished change with a diff. Press **Answer now** if you don't want to wait.

---

## 03 / New in 1.5 · Terminal App

Type `astro` in any folder and a full-screen app opens, in the spirit of Claude Code and OpenCode: a calm conversation view, a side panel you can toggle, approvals right in the terminal, a model picker and parallel agents.

### Ask Mode: Approve a Change
The agent stops before each change. `y` allows it once, `a` allows the rest of the run, `n` says no:

<div align="center">
  <img src="https://esoexe.github.io/astro-ai/assets/img/shots/v15-tui-ask.webp" alt="The astro terminal app in Ask mode asking: Allow this?" width="100%">
</div>

### Finished Run
A finished run: what changed, the check it ran and a short report. `/diff` shows the changes and `/undo` takes them back:

<div align="center">
  <img src="https://esoexe.github.io/astro-ai/assets/img/shots/v15-tui-done.webp" alt="The finished run in the terminal app" width="100%">
</div>

### Parallel Agents
`/parallel "task" "task" --merge`: each agent works in its own copy, then the results are merged:

<div align="center">
  <img src="https://esoexe.github.io/astro-ai/assets/img/shots/v15-tui-parallel.webp" alt="Parallel agents in the terminal app" width="100%">
</div>

### Model Picker
`Ctrl + O` picks a model, or leave it on auto and the engine fails over between free models:

<div align="center">
  <img src="https://esoexe.github.io/astro-ai/assets/img/shots/v15-tui-models.webp" alt="Model picker in terminal" width="100%">
</div>

### Shortcuts Overlay
Press `?` for every shortcut: `Shift + Tab` changes the mode, `Ctrl + B` toggles the side panel, `Ctrl + R` resumes a session:

<div align="center">
  <img src="https://esoexe.github.io/astro-ai/assets/img/shots/v15-tui-help.webp" alt="Keyboard shortcuts overlay" width="100%">
</div>

### Install Terminal CLI

One command downloads it from the official release, checks the SHA-256 and puts `astro` on your PATH:

* **Linux & macOS:**
  ```bash
  curl -fsSL https://esoexe.github.io/astro-ai/install.sh | sh
Windows (PowerShell):
PowerShell

irm https://esoexe.github.io/astro-ai/install.ps1 | iex
04 / New in 1.5 · Desktop: A Calmer Agent Page
The desktop Agent page is redesigned around three columns: your sessions on the left, the run in the middle, and a Session panel on the right that answers the questions you used to click around for.

Sessions with live status, time, files changed and mode
Flat, readable steps that end in a one-line recap: what changed and whether the tests passed
Session panel: context used, git branch, checkpoint and Undo all, rules and skills in use
Switch the project's MCP servers on and off for the agent
Turn stats: steps, actions, files and time for every run
<div align="center"> <img src="https://esoexe.github.io/astro-ai/assets/img/shots/v15-agent.webp" alt="Redesigned Agent page" width="100%"> </div>
Every Change, Reviewable
The Changes panel keeps a diff of each file, with Undo all one click away:

<div align="center"> <img src="https://esoexe.github.io/astro-ai/assets/img/shots/v15-agent-changes.webp" alt="The Changes panel" width="100%"> </div>
Your Tools, Your Call
Trust a project's MCP server once, then switch it on or off for the agent:

<div align="center"> <img src="https://esoexe.github.io/astro-ai/assets/img/shots/v15-agent-mcp.webp" alt="MCP server switches" width="100%"> </div>
05 / Parallel Agents
Three jobs, three copies, one project. Each agent gets its own git worktree, or a copy if the folder isn't a git repo. They work side by side and can't break each other's work:

Up to 8 agents at once, from the terminal or the Parallel panel
Edits to different parts of the same file are combined automatically
Real conflicts are shown to you and never overwritten
Every merge saves a checkpoint, so astro undo takes it back
06 / Permissions: You Decide How Much It Can Touch
Every folder you open has a permission mode. Change it any time from the composer or with /mode. A checkpoint is saved before each run, so you can undo the whole job.

Mode	Summary	What Happens
Plan	Look, don't touch.	Read-only. The agent explores, searches and runs safe commands, then hands you a plan to approve.
Ask	Ask before changes. (Default)	Reading is free. Writing files and running commands waits for your OK, once or for the whole run.
Auto	Full speed.	The agent works on its own, logs every step and keeps a checkpoint, so one click undoes everything.
07 / The Real Thing: Version 2.0.0
Real screenshots working on the todo-api project:

Active Run Execution
<div align="center"> <img src="https://esoexe.github.io/astro-ai/assets/img/shots/v15-agent-run.webp" alt="Real agent run" width="100%"> </div>
Official MCP Registry Browser
<div align="center"> <img src="https://esoexe.github.io/astro-ai/assets/img/shots/v11-mcp-registry.webp" alt="MCP registry browser" width="100%"> </div>
One Brain Multi-Model Consensus Chat
<div align="center"> <img src="https://esoexe.github.io/astro-ai/assets/img/shots/chat-answer.webp" alt="One Brain answer by 19 models" width="100%"> </div>
26 free models ready the first time you open it
50+ providers supported. Add free keys whenever you like.
8 agents at once, each in its own isolated copy
0 accounts, ads, trackers or telemetry
08 / Downloads
The models run in the cloud, so A.S.T.R.O AI stays light on your RAM, CPU and GPU.

Windows (10 & 11, 64-bit)
Installer (.exe) 20.1 MB · Recommended
Portable (.zip) 16.0 MB · No install
Linux (Ubuntu 22.04+, Debian 12+, Fedora 36+)
Debian / Ubuntu (.deb) 9.3 MB
Portable (.tar.gz) 12.6 MB · Any distro
macOS (11+, Apple Silicon & Intel)
Universal Disk Image (.dmg) 30.7 MB
09 / Good to Know (FAQ)
Is A.S.T.R.O AI really free?
Yes. The app is free, and so are the models it uses out of the box. They come from providers' free tiers, which have daily or per-minute limits. When one provider hits its limit, the rest of the team keeps going. Nothing asks you to pay.

What does A.S.T.R.O stand for?
All Systems Thinking, Reasoning as One. All models, using real tools (systems), each think the problem through, reason over each other's work, and come back with one answer.

<p align="center"> <sub>© 2026 A.S.T.R.O AI · Distributed under the <a href="LICENSE">MIT License</a> · <a href="https://esoexe.github.io/astro-ai/">Official Website</a></sub> </p> ```
