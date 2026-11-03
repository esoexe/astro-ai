# A.S.T.R.O AI Library data

This repository publishes the community catalog of the [A.S.T.R.O AI](https://github.com/esoexe/astro-ai) Library at
<https://esoexe.github.io/astro-library/v3/>. It holds no code of its own: a daily workflow checks out
`esoexe/astro-ai`, runs `library/import/index_all.js --format v3 --extra` and deploys the output with GitHub Pages.

What is in it (every entry links to its source and licence):

- every server in the official MCP Registry;
- npm packages tagged as MCP servers that start a command (pinned to an exact version);
- Hugging Face Spaces tagged `mcp-server` (Gradio Spaces as remote servers, others as links);
- Docker's MCP catalog and GitHub repositories with an MCP topic (links);
- Open VSX editor extensions (installed through the A.S.T.R.O AI Code tool);
- Agent Skills and Claude Code plugins from public GitHub repositories, found through topics, open lists and GitHub
  code search for `SKILL.md` files, pinned to a commit with a SHA-256 per file;
- AI command-line tools and an AI app directory.

Community entries are indexed automatically and are not reviewed. Entries whose name or summary looks like adult
content, piracy or malware are left out. PyPI is not crawled because its robots.txt disallows its JSON APIs.

Format: see the comment at the top of `web/lib3core.js` in esoexe/astro-ai. Edits to this repository are not needed;
the source of truth is in esoexe/astro-ai (`library/site/`).
