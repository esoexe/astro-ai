---
name: agent-reach
description: Zero-API-key internet router based on Agent Reach (Panniantong/Agent-Reach). Unified access to search, scrape, and aggregate across 15+ platforms including Twitter/X, Reddit, YouTube transcripts, GitHub, Bilibili, V2EX, and general web.
---

# Agent Reach — Internet Capability Router for AI Agents

Agent Reach gives Astro AI eyes and ears across the public internet without requiring expensive API subscriptions or developer keys.

## Platform Routing Table

| Platform | Channel | Methodology | Typical Output |
|---|---|---|---|
| Web Search | search | Exa / DuckDuckGo / Brave | Top search snippets, URLs, titles |
| Web Pages | web | Jina Reader (`r.jina.ai/<url>`) | Clean Markdown text of articles and docs |
| Twitter / X | social | Public syndication & scrapers | Recent tweets, author handle, metrics |
| Reddit | social | Public JSON API (`/r/<sub>.json`) | Top posts, discussion threads, sentiment |
| YouTube | video | Transcript scraper / metadata | Video title, chapters, full spoken transcript |
| GitHub | dev | Public GitHub API & raw content | Repo stars, commits, issues, PR discussions |
| Bilibili | video | Public search & metadata | Video summaries, bullet comments, views |
| V2EX | dev | Hot topics JSON API | Developer discussions, trending tech topics |

## Usage Patterns
- **Deep Research**: Aggregate perspectives on a new library, technology, or topic across Reddit, Twitter, and GitHub in parallel.
- **Video Learning**: Extract complete spoken transcripts from conference talks or tutorials to extract code snippets and summaries.
- **Ecosystem Monitoring**: Track issues, sentiment, and recent releases for open-source frameworks.
