# Modder Pack

Mod a PC game you own with an agent, from the idea to working in the real game and on video. Adapted from
[universal-modder](https://github.com/rehan-remade/universal-modder) (MIT).

## What is inside

- **Skills (13):** asset-pipeline, blitz3d-decomp, fal-assets, game-automation, game-recon, game-research-websearch, mashup-mods, mod-any-game, port-a-mod, publish-mod, reverse-engineering, share-field-notes, showcase-video.
- **Agents (3):** mod-game-worker, mod-offline-worker, mod-orchestrator. The orchestrator plans and checks; one worker drives the game, one works offline.
- **fal MCP server** (`https://mcp.fal.ai/mcp`): needs your own fal key. Save it as `FAL_KEY` in Settings > Providers >
  Tool keys, or set it in the environment. fal is a paid service with its own prices.
- **The `um` tool, built into A.S.T.R.O AI** (C++, no Python): `scan`, `backup`, `publish`, `kb`, `sprite`, `render3d`, `video`,
  `fal`, `comfy`, `sprixen`, `voidless`, `win`, the Call of Duty 4 / World at War helpers `fastfile`, `iwi`, `weapon`, and
  the ported helper scripts: `b3d` (Blitz3D viewmodels to Roblox), `upk` (UE3 LZO packages), `sld` (AoE2 DE sprites),
  `vpk` and `vtf` (Source archives and textures).
  Run `${ASTRO_TOOL} um --help` from a skill, or `<engine> tool um --help` in a terminal.

## Limits, honestly

- `um render3d` is a software renderer, not Blender: same cameras and file names, simpler light than Cycles.
- The examples' one-off scripts (a mod's build scripts, the Minecraft passthrough hosts and video directors, the
  Portal 2 model converter) are case studies of one setup and are not included; the file-format tools are (`sld`, `vpk`, `vtf`).
- `um win` works on Windows and in WSL only. `um video` and `um win record` need ffmpeg (`um win setup` fetches a
  checksum-verified Windows build). Sprixen and Voidless are paid services with their own keys; ComfyUI runs locally.
- The knowledge base is synced from GitHub on first use (`um kb sync`); with no network and no clone, `um kb search` cannot help.
- The skills raise the floor; they do not make every game moddable. Online games with anti-cheat are out of bounds.

## Rules the pack keeps

Only games the user owns. Back up saves before touching anything. Never ship game files, extracted assets or
decompiled code; `um publish check` lints a mod folder for those and for secrets before sharing. No cheats or
advantages against other players.
