// port_modder.cpp - builds library/plugins/astro-modder/ (the Modder Pack) from a checkout of
// https://github.com/rehan-remade/universal-modder (MIT). C++ on purpose: A.S.T.R.O AI's tooling has no Python.
//
//   g++ -std=c++20 -O1 tools/port_modder.cpp -o /tmp/port_modder
//   /tmp/port_modder ~/.cache/um library/plugins/astro-modder
//   /tmp/port_modder ~/.cache/um <edition>/ --standalone    # skills/ + agents/ for universal-modder-cpp (plain `um`)
//
// What it does:
//   * copies skills/ and agents/ (Markdown) and the fal MCP config;
//   * leaves out the Python helper scripts: each one is a built-in C++ command now (b3d_skin.py / vm_*.py -> um b3d,
//     unpack_upk.py -> um upk, the examples' sld.py / vpk.py / vtf.py -> um sld / vpk / vtf), and the skills say so;
//     a .patch reference is kept as .patch.txt;
//   * points every `um <group>` command at the built-in C++ port: `${ASTRO_TOOL} um <group>`;
//   * rewrites the few install / Python-API paragraphs (each rewrite is asserted, so an upstream change fails loudly);
//   * writes plugin.json, NOTICE.md (with the upstream MIT licence) and README.md.
// The guardrails (own copy of the game, no game files or decompiled dumps shipped, no cheating other players,
// backups first) are upstream text and are copied unchanged.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) { std::cerr << "cannot read " << p << "\n"; std::exit(1); }
    std::ostringstream s; s << in.rdbuf(); return s.str();
}
static void spit(const fs::path& p, const std::string& d) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc); out << d;
    if (!out) { std::cerr << "cannot write " << p << "\n"; std::exit(1); }
}
static int failures = 0;
static void must_replace(std::string& s, const std::string& file, const std::string& from, const std::string& to) {
    const auto at = s.find(from);
    if (at == std::string::npos) { std::cerr << "port_modder: expected text not found in " << file << ":\n  " << from.substr(0, 120) << "\n"; ++failures; return; }
    s.replace(at, from.size(), to);
}
static std::string replace_all(std::string s, const std::string& a, const std::string& b) {
    for (std::size_t i = 0; (i = s.find(a, i)) != std::string::npos; i += b.size()) s.replace(i, a.size(), b);
    return s;
}

static const char* RENDER3D_NOTE =
    "> **A.S.T.R.O AI note:** `um render3d` is built in as C++: a software renderer in place of Blender, with the same\n"
    "> presets, motions, flags and file names. `--samples` sets the anti-aliasing, `--engine` is accepted and ignored, and\n"
    "> the light is a sun plus ambient (simpler than Cycles), so look at the first frame. AoE2 DE frames pack into an\n"
    "> .sld with `${ASTRO_TOOL} um sld write`.\n\n";

// --standalone: the same skills for universal-modder-cpp, the C++ edition as a program of its own (`um` on PATH or bin/um)
static std::string edition_text(std::string d, const std::string& rel) {
    const std::vector<std::pair<std::string, std::string>> swaps = {
        {"In A.S.T.R.O AI, `um` is built in (a C++ port of universal-modder's CLI): run it as `${ASTRO_TOOL} um ...`.\nNothing to install.\n",
         "`um` lives at `bin/um` in this repo: the C++ edition, built by `make` (or take a release binary). Anywhere else, put a release\nbinary on PATH.\n"},
        {"In A.S.T.R.O AI, `um` is built in: run it as `${ASTRO_TOOL} um ...`. Outside a clone,",
         "`um` lives at `bin/um` in this repo (C++; `make` builds it). Outside a clone,"},
        {"`um` is the toolkit CLI. In A.S.T.R.O AI it is built in (a C++ port of universal-modder's CLI, every group,\n"
         "plus `b3d`, `upk`, `sld`, `vpk` and `vtf` in place of the Python helper scripts): run it as `${ASTRO_TOOL} um ...`.\n"
         "Nothing to install.\n",
         "`um` is the toolkit CLI (`bin/um` in the repo). This edition is C++: every group, plus `b3d`, `upk`, `sld`, `vpk` and\n"
         "`vtf` in place of the Python helper scripts. If `command -v um` finds nothing, use `bin/um` in a clone or a release binary.\n"},
        {"the A.S.T.R.O AI port is the command line only.)", "the C++ edition is the command line only.)"},
        {"In A.S.T.R.O AI the four scripts of upstream's", "In this edition the four scripts of upstream's"},
        {"**A.S.T.R.O AI note:**", "**C++ edition note:**"},
        {"${ASTRO_TOOL} um", "um"},
    };
    for (const auto& [a, b] : swaps) d = replace_all(d, a, b);
    if (d.find("A.S.T.R.O") != std::string::npos || d.find("ASTRO_TOOL") != std::string::npos) { std::cerr << "port_modder: A.S.T.R.O AI text left in the standalone " << rel << "\n"; ++failures; }
    return d;
}

int main(int argc, char** argv) {
    const bool standalone = argc == 4 && std::string(argv[3]) == "--standalone";
    if (argc != 3 && !standalone) { std::cerr << "usage: port_modder <universal-modder checkout> <out dir> [--standalone]\n"; return 2; }
    const fs::path src = argv[1], out = argv[2];
    if (!fs::is_regular_file(src / "skills/mod-any-game/SKILL.md") || !fs::is_regular_file(src / "LICENSE")) { std::cerr << "not a universal-modder checkout: " << src << "\n"; return 2; }
    std::string commit = "unknown";
    if (FILE* p = popen(("git -C \"" + src.string() + "\" rev-parse HEAD 2>/dev/null").c_str(), "r")) {
        char buf[64] = {0}; if (std::fgets(buf, sizeof buf, p)) { commit = buf; while (!commit.empty() && (commit.back() == '\n' || commit.back() == '\r')) commit.pop_back(); }
        pclose(p);
    }
    if (standalone) { fs::remove_all(out / "skills"); fs::remove_all(out / "agents"); } else fs::remove_all(out);

    // file-specific rewrites, applied before the generic `um <group>` rewrite
    const std::map<std::string, std::vector<std::pair<std::string, std::string>>> edits = {
        {"skills/game-recon/SKILL.md", {
            {"`um` lives at `bin/um` in the universal-modder repo (plugins and clones put it on PATH). Anywhere else:\n"
             "`uv tool install git+https://github.com/rehan-remade/universal-modder`.\n",
             "In A.S.T.R.O AI, `um` is built in (a C++ port of universal-modder's CLI): run it as `${ASTRO_TOOL} um ...`.\n"
             "Nothing to install.\n"}}},
        {"skills/share-field-notes/SKILL.md", {
            {"`um` lives at `bin/um` in the repo. Anywhere else: `uv tool install git+https://github.com/rehan-remade/universal-modder`.\n",
             "In A.S.T.R.O AI, `um` is built in: run it as `${ASTRO_TOOL} um ...`. Outside a clone, `um kb search` reads a copy of\n"
             "the knowledge base synced from GitHub (refreshed with `um kb sync`).\n"}}},
        {"skills/mod-any-game/SKILL.md", {
            {"`um` is the toolkit CLI (`bin/um` in the repo). The Claude Code plugin and a clone opened in Claude Code put it\n"
             "on PATH; other agents' plugin installs don't. If `command -v um` finds nothing, use `bin/um` in a clone, or\n"
             "install it once: `uv tool install git+https://github.com/rehan-remade/universal-modder` (or `pipx install ...`).\n",
             "`um` is the toolkit CLI. In A.S.T.R.O AI it is built in (a C++ port of universal-modder's CLI, every group,\n"
             "plus `b3d`, `upk`, `sld`, `vpk` and `vtf` in place of the Python helper scripts): run it as `${ASTRO_TOOL} um ...`.\n"
             "Nothing to install.\n"}}},
        {"skills/asset-pipeline/SKILL.md", {
            {"- Then pack with `um sprite sheet`, or with an engine writer (e.g. `examples/aoe2-de-civ/sld.py`).\n",
             "- Then pack with `um sprite sheet`, or with an engine writer (AoE2 DE: `um sld write`, the port of `examples/aoe2-de-civ/sld.py`).\n"},
            {"- Needs Blender (`blender` on PATH or `BLENDER=...`). Cycles uses the GPU when available.\n",
             "- No Blender needed: the built-in renderer uses every CPU core (`UM_RENDER_THREADS` sets how many).\n"}}},
        {"skills/blitz3d-decomp/references/b3d-to-roblox.md", {
            {"Scripts in `scripts/roblox/` (Python 3, Blender 5 for the build/render steps). Each has a\n"
             "docstring with its usage; run them with `python -I`.\n",
             "In A.S.T.R.O AI the four scripts of upstream's `scripts/roblox/` are built-in commands (C++, no Python or\n"
             "Blender): `um b3d dump`, `um b3d anim`, `um b3d build` and `um b3d render`; `um b3d --help` shows their usage.\n"},
            {"`b3d_skin.py MODEL.b3d OUT.json` dumps", "`um b3d dump MODEL.b3d OUT.json` (b3d_skin.py) dumps"},
            {"`vm_build.py` and `vm_anim_data.py` take the frame range", "`um b3d build` and `um b3d anim` take the frame range"},
            {"`vm_build.py` (Blender, headless) makes an armature with a bone for **every** node",
             "`um b3d build` (vm_build.py) makes a skinned glTF with a bone for **every** node"},
            {"and a preview action. `vm_render.py`\nrenders frames", "and a preview animation (`NAME.preview.glb`). `um b3d render`\nrenders frames"},
            {"Upload the FBX as a Model (Open Cloud).",
             "Upload the model as a Model (Open Cloud). Upstream's Blender script wrote FBX; `um b3d build` writes glTF (GLB), so\n"
             "check one bone's position against the JSON after importing, and convert in Blender if your import path needs FBX."},
            {"`vm_anim_data.py IN.json OUT.luau --scale S --name NAME` writes", "`um b3d anim IN.json OUT.luau --scale S --name NAME` (vm_anim_data.py) writes"}}},
        {"skills/reverse-engineering/references/ue3-packages/README.md", {
            {"`unpack_upk.py` is a small Python fallback for **LZO** packages",
             "`um upk unpack` (the C++ port of upstream's `unpack_upk.py`) is a small fallback for **LZO** packages"},
            {"so the script auto-detects it", "so the tool auto-detects it"},
            {"uv run --python 3.12 --with minilzo python unpack_upk.py <in.upk> <out.upk>\n", "um upk unpack <in.upk> <out.upk>\n"},
            {"```python\nimport struct\np = 'DishonoredGame.fixed.upk'; OFF = 0x79   # printed by the script\n"
             "d = bytearray(open(p, 'rb').read()); struct.pack_into('<I', d, OFF, 0)\nopen(p, 'wb').write(d)\n```\n",
             "```bash\num upk clear-flags DishonoredGame.fixed.upk 0x79   # the offset unpack printed\n```\n\n"
             "(or pass `--clear-flags` to `um upk unpack`).\n"},
            {"- Depends on the PyPI `minilzo` package (GPL-2.0), which `uv` fetches at run time. It's a C extension\n"
             "  published only as source, so `uv` builds it and needs a C compiler (on Windows, the MSVC Build Tools).\n",
             "- Nothing to install: the LZO1X decompressor is written in C++ (the safe variant: every read and write is\n"
             "  bounds-checked). zlib and LZX packages are refused; use Gildor's decompressor for those.\n"}}},
        {"skills/game-automation/SKILL.md", {
            {"From Python (for longer scripts), `from um.win import Drive, shot, Recorder`, then `d = Drive(\"AoE2DE_s\")`,\n"
             "`d.focus()`, `d.click(x, y)`, `d.key(\"0x0D\")`.\n",
             "For longer sequences, pass several WinDrive commands to one call:\n"
             "`um win drive --proc AoE2DE_s \"focus\" \"click 640 360\" \"key 0x0D\"`. (Upstream also has a Python API, `um.win`;\n"
             "the A.S.T.R.O AI port is the command line only.)\n"},
            {"(`um/ps1/WinDrive.ps1`;", "(`WinDrive.ps1`, which `um win setup` installs;"}}},
        {"skills/showcase-video/SKILL.md", {{"(`um/ps1/ProcLoopback.ps1`)", "(`ProcLoopback.ps1`, which `um win setup` installs)"}}},
        {"skills/mod-any-game/references/case-studies.md", {{"(`um/ps1/ProcLoopback.ps1`)", "(`ProcLoopback.ps1`)"}}},
        {"skills/mod-any-game/references/engines/genie-aoe2.md", {{"Read/write with `examples/aoe2-de-civ/sld.py`,", "Read/write with `um sld` (the port of `examples/aoe2-de-civ/sld.py`),"}}},
        {"skills/blitz3d-decomp/SKILL.md", {
            {"its scripts are in `scripts/roblox/`.", "its steps are the built-in `um b3d` commands (`dump`, `anim`, `build`, `render`)."}}},
        {"skills/reverse-engineering/SKILL.md", {
            {"or, for LZO packages, with\n    [`unpack_upk.py`](references/ue3-packages/README.md).",
             "or, for LZO packages, with\n    `um upk unpack` ([notes](references/ue3-packages/README.md))."},
            {"0.9/255 mean error (`examples/aoe2-de-civ/sld.py`).", "0.9/255 mean error (`examples/aoe2-de-civ/sld.py`; built in here as `um sld`, check yours with `um sld roundtrip`)."}}},
    };
    const std::set<std::string> render3d_note = {"skills/asset-pipeline/SKILL.md", "skills/fal-assets/SKILL.md"};
    const std::regex um_cmd(R"((^|[^A-Za-z0-9_/.$-])um (scan|fal|comfy|sprixen|voidless|sprite|render3d|video|win|backup|publish|kb|fastfile|iwi|weapon|b3d|upk|sld|vpk|vtf)\b)");

    int files = 0; std::size_t bytes = 0; std::vector<std::string> skipped, skills, agents;
    for (const char* top : {"skills", "agents"}) {
        for (auto it = fs::recursive_directory_iterator(src / top); it != fs::recursive_directory_iterator(); ++it) {
            if (!it->is_regular_file()) continue;
            std::string rel = fs::relative(it->path(), src).generic_string();
            if (rel.ends_with(".py") || rel.find("/scripts/") != std::string::npos) { skipped.push_back(rel); continue; }
            std::string data = slurp(it->path()), dst = rel;
            if (rel.ends_with(".patch")) { if (!standalone) dst += ".txt"; }
            else if (!rel.ends_with(".md")) { skipped.push_back(rel); continue; }
            if (rel.ends_with(".md")) {
                if (auto e = edits.find(rel); e != edits.end()) for (const auto& [a, b] : e->second) must_replace(data, rel, a, b);
                if (rel.starts_with("skills/")) {
                    data = std::regex_replace(data, um_cmd, "$1$${ASTRO_TOOL} um $2");
                    // references to the dropped helper scripts / the renamed patch
                    if (!standalone) data = replace_all(data, "b3ddecomp-skip-failures.patch", "b3ddecomp-skip-failures.patch.txt");
                    if (render3d_note.count(rel)) {
                        const auto fm = data.find("\n---\n", 4);
                        if (fm == std::string::npos) { std::cerr << "no front matter: " << rel << "\n"; ++failures; }
                        else data.insert(fm + 5, std::string("\n") + RENDER3D_NOTE);
                    }
                    if (rel.ends_with("/SKILL.md")) skills.push_back(fs::path(rel).parent_path().filename().string());
                } else if (standalone) {
                    agents.push_back(fs::path(rel).stem().string());
                } else {
                    const auto fm = data.find("\n---\n", 4);
                    if (fm == std::string::npos) { std::cerr << "no front matter: " << rel << "\n"; ++failures; }
                    else data.insert(fm + 5, "\n> In A.S.T.R.O AI the `um` commands below are built in: load the skill that covers the step and run them\n"
                                             "> as it shows (`${ASTRO_TOOL} um <group> ...`).\n");
                    agents.push_back(fs::path(rel).stem().string());
                }
            }
            if (standalone && rel.ends_with(".md") && !rel.starts_with("agents/")) { /* notes are added below; swaps after */ }
            if (data.size() > 256 * 1024) { std::cerr << "too big for a pack: " << rel << "\n"; ++failures; }
            spit(out / dst, data); ++files; bytes += data.size();
        }
    }
    // the skills whose Python helpers became built-in commands say so at the top
    for (const auto& [skill, what] : std::vector<std::pair<std::string, std::string>>{
             {"blitz3d-decomp", "upstream's Python helpers in `scripts/roblox/` are built in here as `${ASTRO_TOOL} um b3d`: `dump` (b3d_skin.py),\n"
                                "> `anim` (vm_anim_data.py), `build` (vm_build.py) and `render` (vm_render.py), with no Python or Blender. `build` writes\n"
                                "> glTF (`NAME.glb`, `NAME.preview.glb`) where upstream wrote FBX."},
             {"reverse-engineering", "upstream's Python helper `unpack_upk.py` is built in here as `${ASTRO_TOOL} um upk unpack` (and\n"
                                     "> `um upk clear-flags`), with its own LZO decompressor: no Python, no minilzo. The AoE2 `sld.py` and Source\n"
                                     "> `vpk.py` / `vtf.py` example tools are `um sld`, `um vpk` and `um vtf`."}}) {
        const fs::path p = out / "skills" / skill / "SKILL.md";
        std::string d = slurp(p);
        const auto fm = d.find("\n---\n", 4);
        if (fm == std::string::npos) { std::cerr << "no front matter: " << p << "\n"; ++failures; continue; }
        d.insert(fm + 5, "\n> **A.S.T.R.O AI note:** " + what + "\n");
        spit(p, d);
    }
    std::sort(skills.begin(), skills.end()); std::sort(agents.begin(), agents.end());
    if (standalone) {
        int n = 0;
        for (const char* top : {"skills", "agents"})
            for (auto it = fs::recursive_directory_iterator(out / top); it != fs::recursive_directory_iterator(); ++it)
                if (it->is_regular_file() && it->path().extension() == ".md") { const std::string rel = fs::relative(it->path(), out).generic_string(); spit(it->path(), edition_text(slurp(it->path()), rel)); ++n; }
        std::cout << "universal-modder-cpp: " << files << " files (" << n << " Markdown), " << skills.size() << " skills, " << agents.size() << " agents, commit " << commit.substr(0, 12) << "\n";
        if (failures) { std::cerr << failures << " problem(s)\n"; return 1; }
        return 0;
    }

    // fal's hosted MCP server; ${FAL_KEY} is filled in by A.S.T.R.O AI from Settings > Keys or the environment
    spit(out / ".mcp.json", slurp(src / ".mcp.json")); ++files;

    std::string pj = "{\n  \"name\": \"astro-modder\",\n  \"displayName\": \"Modder Pack\",\n  \"version\": \"1.1.0\",\n"
        "  \"description\": \"Mod PC games you own with an agent: recon (engine, anti-cheat, loaders), a safe lab with save backups, reverse engineering, fal-generated art, 3D and audio, in-game testing on Windows, showcase videos, a publish lint and a shared knowledge base of field notes. Skills and agents from universal-modder; its um CLI is built into A.S.T.R.O AI as C++.\",\n"
        "  \"author\": {\n    \"name\": \"The Astro AI team\"\n  },\n  \"license\": \"MIT\",\n"
        "  \"source\": {\n    \"repo\": \"https://github.com/rehan-remade/universal-modder\",\n    \"author\": \"Rehan and universal-modder contributors\",\n    \"license\": \"MIT\",\n    \"commit\": \"" + commit + "\"\n  },\n  \"skills\": [\n";
    for (std::size_t i = 0; i < skills.size(); ++i) pj += "    \"" + skills[i] + "\"" + (i + 1 < skills.size() ? ",\n" : "\n");
    pj += "  ],\n  \"agents\": [\n";
    for (std::size_t i = 0; i < agents.size(); ++i) pj += "    \"" + agents[i] + "\"" + (i + 1 < agents.size() ? ",\n" : "\n");
    pj += "  ]\n}\n";
    spit(out / "plugin.json", pj); ++files;

    spit(out / "NOTICE.md",
         "# Notice\n\n**Modder Pack** is an Astro Pack: the skills and agents of **universal-modder** by Rehan and universal-modder\n"
         "contributors (https://github.com/rehan-remade/universal-modder, commit " + commit.substr(0, 12) + "), used under the MIT License below.\n\n"
         "Changes for A.S.T.R.O AI (made by tools/port_modder.cpp):\n"
         "- `um <group>` commands point at the built-in C++ port (`${ASTRO_TOOL} um <group>`); install paragraphs say so.\n"
         "- The Python helper scripts are built-in C++ commands instead: blitz3d-decomp/scripts/roblox -> `um b3d`,\n"
         "  unpack_upk.py -> `um upk`, and the examples' sld.py / vpk.py / vtf.py -> `um sld` / `um vpk` / `um vtf`.\n"
         "  `um render3d` uses a built-in software renderer in place of Blender. The skills that ran a script say so.\n"
         "- Left out: the hooks and the knowledge base (the `um kb` tool syncs it from GitHub).\n"
         "- A short A.S.T.R.O AI note at the top of the agents and of the skills whose tools changed.\n"
         "- The guardrails are unchanged: mod only games the user owns, keep game files and decompiled code out of anything\n"
         "  shared, no cheats against other players, back up saves first.\n\n"
         "The `um` CLI's Space Grotesk and JetBrains Mono fonts (SIL Open Font License 1.1) are built into A.S.T.R.O AI and\n"
         "written, with their licence texts, to the um data folder the first time a video needs them.\n\n"
         "This pack is not made or endorsed by the universal-modder authors, fal, Sprixen, Voidless or ComfyUI. Report problems\n"
         "with the A.S.T.R.O AI version to https://github.com/esoexe/astro-ai/issues.\n\n---\n\n" + slurp(src / "LICENSE"));
    ++files;
    std::string readme =
        "# Modder Pack\n\n"
        "Mod a PC game you own with an agent, from the idea to working in the real game and on video. Adapted from\n"
        "[universal-modder](https://github.com/rehan-remade/universal-modder) (MIT).\n\n"
        "## What is inside\n\n"
        "- **Skills (" + std::to_string(skills.size()) + "):** ";
    for (std::size_t i = 0; i < skills.size(); ++i) readme += (i ? ", " : "") + skills[i];
    readme += ".\n- **Agents (" + std::to_string(agents.size()) + "):** ";
    for (std::size_t i = 0; i < agents.size(); ++i) readme += (i ? ", " : "") + agents[i];
    readme += ". The orchestrator plans and checks; one worker drives the game, one works offline.\n"
        "- **fal MCP server** (`https://mcp.fal.ai/mcp`): needs your own fal key. Save it as `FAL_KEY` in Settings > Providers >\n"
        "  Tool keys, or set it in the environment. fal is a paid service with its own prices.\n"
        "- **The `um` tool, built into A.S.T.R.O AI** (C++, no Python): `scan`, `backup`, `publish`, `kb`, `sprite`, `render3d`, `video`,\n"
        "  `fal`, `comfy`, `sprixen`, `voidless`, `win`, the Call of Duty 4 / World at War helpers `fastfile`, `iwi`, `weapon`, and\n"
        "  the ported helper scripts: `b3d` (Blitz3D viewmodels to Roblox), `upk` (UE3 LZO packages), `sld` (AoE2 DE sprites),\n"
        "  `vpk` and `vtf` (Source archives and textures).\n"
        "  Run `${ASTRO_TOOL} um --help` from a skill, or `<engine> tool um --help` in a terminal.\n\n"
        "## Limits, honestly\n\n"
        "- `um render3d` is a software renderer, not Blender: same cameras and file names, simpler light than Cycles.\n"
        "- The examples' one-off scripts (a mod's build scripts, the Minecraft passthrough hosts and video directors, the\n"
        "  Portal 2 model converter) are case studies of one setup and are not included; the file-format tools are (`sld`, `vpk`, `vtf`).\n"
        "- `um win` works on Windows and in WSL only. `um video` and `um win record` need ffmpeg (`um win setup` fetches a\n"
        "  checksum-verified Windows build). Sprixen and Voidless are paid services with their own keys; ComfyUI runs locally.\n"
        "- The knowledge base is synced from GitHub on first use (`um kb sync`); with no network and no clone, `um kb search` cannot help.\n"
        "- The skills raise the floor; they do not make every game moddable. Online games with anti-cheat are out of bounds.\n\n"
        "## Rules the pack keeps\n\n"
        "Only games the user owns. Back up saves before touching anything. Never ship game files, extracted assets or\n"
        "decompiled code; `um publish check` lints a mod folder for those and for secrets before sharing. No cheats or\n"
        "advantages against other players.\n";
    spit(out / "README.md", readme); ++files;

    std::cout << "astro-modder: " << files << " files, " << bytes << " bytes of skills/agents, " << skills.size() << " skills, " << agents.size()
              << " agents, commit " << commit.substr(0, 12) << "\n";
    for (const auto& s : skipped) std::cout << "  left out: " << s << "\n";
    if (failures) { std::cerr << failures << " problem(s)\n"; return 1; }
    return 0;
}
