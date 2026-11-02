# Notice

**Seed Pack** is an Astro Pack: an adaptation for A.S.T.R.O AI of **seed-skill** by Jake Schincariol
(https://github.com/Jakeschincariol/seed-skill, commit fde88957238b), used under the MIT License below.

Changes made for A.S.T.R.O AI:
- Python helpers were re-written in C++ and built into A.S.T.R.O AI itself (run with `${ASTRO_TOOL}`, A.S.T.R.O AI 1.9.2 or later); they print what the originals printed. The Python files are not included.
- Commands, paths and the assistant's name were adapted (`${ASTRO_SKILL_DIR}`, `${ASTRO_PACK_DIR}`, `~/.astro-packs/`).
- A note was added to the README and the Library listing: No promise of funding: the tools draft, research and do arithmetic, and investors decide. The tools never send anything: seed-closer writes drafts, and a batch goes out only after your own yes in the chat (through a Gmail MCP server you added, or as drafts you send yourself). Two helpers go online with no login or keys: seed-scout reads the App Store's public search and review feeds, and seed-investors searches SEC EDGAR Form D filings, which asks every request to say who is asking (sec_user_agent in seed/config.json). Not legal advice: have a startup lawyer read any SAFE or term sheet before you sign it. Not investment advice: the model and the market size are only as good as your assumptions. Cold email has to follow the law where you send it; every email carries your real name, your postal address and an opt-out, and a no, an opt-out or a bounce ends that thread for good.
- Tests, install instructions for other tools and repository files were left out.

This pack is not made or endorsed by Jake Schincariol. Report problems with the A.S.T.R.O AI version to https://github.com/esoexe/astro-ai/issues.

---

MIT License

Copyright (c) 2026 Jake Schincariol

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
