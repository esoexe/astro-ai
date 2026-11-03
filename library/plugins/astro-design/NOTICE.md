# Notice

**Design Pack** is an Astro Pack for A.S.T.R.O AI. It contains:

1. **ui-ux-pro-max**: an adaptation of **UI/UX Pro Max** by Next Level Builder
   (https://github.com/nextlevelbuilder/ui-ux-pro-max-skill, version 2.15.0, commit 713eef51b04e), used under the MIT License below.
   Changes made for A.S.T.R.O AI:
   - The Python search (scripts/search.py, core.py, design_system.py, reasoning_contract.py) was re-written in C++ and built into
     A.S.T.R.O AI (run with `${ASTRO_TOOL} ui-ux-pro-max/search`, A.S.T.R.O AI 1.9.5 or later). It uses the same ranking, tokenizer,
     domain detection, score floors, exact-identity lookups and design-system rules. The Python files are not included.
   - Three large reference files are not included: google-fonts.csv (every Google font; font questions are answered from the 74
     curated pairings in typography.csv), google-font-licenses.json and phosphor-icons-upstream.json. The stack version-number
     check is simpler (an explicit "legacy" or "deprecated" asks for older guidance), and `--format ascii` prints the same plain
     Markdown as `--format markdown`.
   - SKILL.md was built from templates/base/skill-content.md: the Python prerequisites were replaced by the built-in command, and
     the Quick Reference moved to references/quick-reference.md. The data/*.csv files are unchanged.
   - Tests, the installer CLI, other platforms' templates and repository files were left out.
2. **frontend-design**: **frontend-design** by Anthropic (https://github.com/anthropics/skills, skills/frontend-design, commit
   ca45fc496a34), included unchanged under the Apache License 2.0 (full text in skills/frontend-design/LICENSE.txt).
3. **design-director**: written for A.S.T.R.O AI by the Astro AI team (MIT OR Apache-2.0 OR GPL-3.0, like A.S.T.R.O AI).

This pack is not made or endorsed by Next Level Builder or Anthropic. Report problems with the A.S.T.R.O AI version to
https://github.com/esoexe/astro-ai/issues.

---

MIT License

Copyright (c) 2024 Next Level Builder

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
