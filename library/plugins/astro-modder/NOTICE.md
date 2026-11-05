# Notice

**Modder Pack** is an Astro Pack: the skills and agents of **universal-modder** by Rehan and universal-modder
contributors (https://github.com/rehan-remade/universal-modder, commit 9811709d9509), used under the MIT License below.

Changes for A.S.T.R.O AI (made by tools/port_modder.cpp):
- `um <group>` commands point at the built-in C++ port (`${ASTRO_TOOL} um <group>`); install paragraphs say so.
- The Python helper scripts are built-in C++ commands instead: blitz3d-decomp/scripts/roblox -> `um b3d`,
  unpack_upk.py -> `um upk`, and the examples' sld.py / vpk.py / vtf.py -> `um sld` / `um vpk` / `um vtf`.
  `um render3d` uses a built-in software renderer in place of Blender. The skills that ran a script say so.
- Left out: the hooks and the knowledge base (the `um kb` tool syncs it from GitHub).
- A short A.S.T.R.O AI note at the top of the agents and of the skills whose tools changed.
- The guardrails are unchanged: mod only games the user owns, keep game files and decompiled code out of anything
  shared, no cheats against other players, back up saves first.

The `um` CLI's Space Grotesk and JetBrains Mono fonts (SIL Open Font License 1.1) are built into A.S.T.R.O AI and
written, with their licence texts, to the um data folder the first time a video needs them.

This pack is not made or endorsed by the universal-modder authors, fal, Sprixen, Voidless or ComfyUI. Report problems
with the A.S.T.R.O AI version to https://github.com/esoexe/astro-ai/issues.

---

MIT License

Copyright (c) 2026 Rehan and universal-modder contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
OR OTHER DEALINGS IN THE SOFTWARE.
