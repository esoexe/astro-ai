// port_modder.cpp - builds library/plugins/astro-modder/ (the Modder Pack) from a checkout of
// https://github.com/rehan-remade/universal-modder (MIT). C++ on purpose: A.S.T.R.O AI's tooling has no Python.
//
//   g++ -std=c++20 -O1 tools/port_modder.cpp -o /tmp/port_modder
//   /tmp/port_modder ~/.cache/um library/plugins/astro-modder
//
// What it does:
//   * copies skills/ and agents/ (Markdown) and the fal MCP config;
//   * leaves out the Python helper scripts (Library packs are text-only, and the A.S.T.R.O AI port has no Python);
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
    "> **A.S.T.R.O AI note:** `um render3d` is not built in. Upstream it scripts Blender in Python, and the\n"
    "> A.S.T.R.O AI port has no Python. To turn a GLB into sprite frames, render the frames in Blender yourself (or ask\n"
    "> the user to), then continue with `${ASTRO_TOOL} um sprite` (fit, pixelate, recolor, pack). Everything else in\n"
    "> this skill works as written.\n\n";

int main(int argc, char** argv) {
    if (argc != 3) { std::cerr << "usage: port_modder <universal-modder checkout> <out dir>\n"; return 2; }
    const fs::path src = argv[1], out = argv[2];
    if (!fs::is_regular_file(src / "skills/mod-any-game/SKILL.md") || !fs::is_regular_file(src / "LICENSE")) { std::cerr << "not a universal-modder checkout: " << src << "\n"; return 2; }
    std::string commit = "unknown";
    if (FILE* p = popen(("git -C \"" + src.string() + "\" rev-parse HEAD 2>/dev/null").c_str(), "r")) {
        char buf[64] = {0}; if (std::fgets(buf, sizeof buf, p)) { commit = buf; while (!commit.empty() && (commit.back() == '\n' || commit.back() == '\r')) commit.pop_back(); }
        pclose(p);
    }
    fs::remove_all(out);

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
             "`um` is the toolkit CLI. In A.S.T.R.O AI it is built in (a C++ port of universal-modder's CLI, every group but\n"
             "`render3d`): run it as `${ASTRO_TOOL} um ...`. Nothing to install.\n"},
            {"| Cut out / fit / pixelate / pack sprites; 3D model → sprite frames | `um sprite ...`, `um render3d ...` |",
             "| Cut out / fit / pixelate / pack sprites; 3D model → sprite frames | `um sprite ...` (render the 3D frames in Blender first: `render3d` is not in the A.S.T.R.O AI port) |"}}},
        {"skills/game-automation/SKILL.md", {
            {"From Python (for longer scripts), `from um.win import Drive, shot, Recorder`, then `d = Drive(\"AoE2DE_s\")`,\n"
             "`d.focus()`, `d.click(x, y)`, `d.key(\"0x0D\")`.\n",
             "For longer sequences, pass several WinDrive commands to one call:\n"
             "`um win drive --proc AoE2DE_s \"focus\" \"click 640 360\" \"key 0x0D\"`. (Upstream also has a Python API, `um.win`;\n"
             "the A.S.T.R.O AI port is the command line only.)\n"},
            {"(`um/ps1/WinDrive.ps1`;", "(`WinDrive.ps1`, which `um win setup` installs;"}}},
        {"skills/showcase-video/SKILL.md", {{"(`um/ps1/ProcLoopback.ps1`)", "(`ProcLoopback.ps1`, which `um win setup` installs)"}}},
        {"skills/mod-any-game/references/case-studies.md", {{"(`um/ps1/ProcLoopback.ps1`)", "(`ProcLoopback.ps1`)"}}},
        {"skills/blitz3d-decomp/SKILL.md", {}},
        {"skills/reverse-engineering/SKILL.md", {}},
    };
    const std::set<std::string> render3d_note = {"skills/asset-pipeline/SKILL.md", "skills/fal-assets/SKILL.md"};
    const std::regex um_cmd(R"((^|[^A-Za-z0-9_/.$-])um (scan|fal|comfy|sprixen|voidless|sprite|render3d|video|win|backup|publish|kb|fastfile|iwi|weapon)\b)");

    int files = 0; std::size_t bytes = 0; std::vector<std::string> skipped, skills, agents;
    for (const char* top : {"skills", "agents"}) {
        for (auto it = fs::recursive_directory_iterator(src / top); it != fs::recursive_directory_iterator(); ++it) {
            if (!it->is_regular_file()) continue;
            std::string rel = fs::relative(it->path(), src).generic_string();
            if (rel.ends_with(".py") || rel.find("/scripts/") != std::string::npos) { skipped.push_back(rel); continue; }
            std::string data = slurp(it->path()), dst = rel;
            if (rel.ends_with(".patch")) dst += ".txt";
            else if (!rel.ends_with(".md")) { skipped.push_back(rel); continue; }
            if (rel.ends_with(".md")) {
                if (auto e = edits.find(rel); e != edits.end()) for (const auto& [a, b] : e->second) must_replace(data, rel, a, b);
                if (rel.starts_with("skills/")) {
                    data = std::regex_replace(data, um_cmd, "$1$${ASTRO_TOOL} um $2");
                    // references to the dropped helper scripts / the renamed patch
                    data = replace_all(data, "b3ddecomp-skip-failures.patch", "b3ddecomp-skip-failures.patch.txt");
                    if (render3d_note.count(rel)) {
                        const auto fm = data.find("\n---\n", 4);
                        if (fm == std::string::npos) { std::cerr << "no front matter: " << rel << "\n"; ++failures; }
                        else data.insert(fm + 5, std::string("\n") + RENDER3D_NOTE);
                    }
                    if (rel.ends_with("/SKILL.md")) skills.push_back(fs::path(rel).parent_path().filename().string());
                } else {
                    const auto fm = data.find("\n---\n", 4);
                    if (fm == std::string::npos) { std::cerr << "no front matter: " << rel << "\n"; ++failures; }
                    else data.insert(fm + 5, "\n> In A.S.T.R.O AI the `um` commands below are built in: load the skill that covers the step and run them\n"
                                             "> as it shows (`${ASTRO_TOOL} um <group> ...`). `um render3d` is not in the A.S.T.R.O AI port.\n");
                    agents.push_back(fs::path(rel).stem().string());
                }
            }
            if (data.size() > 256 * 1024) { std::cerr << "too big for a pack: " << rel << "\n"; ++failures; }
            spit(out / dst, data); ++files; bytes += data.size();
        }
    }
    // notes about what was left out, so a skill that mentions a dropped script says so
    for (const auto& [skill, what] : std::vector<std::pair<std::string, std::string>>{
             {"blitz3d-decomp", "the Python helpers in `scripts/roblox/` (b3d_skin.py, vm_anim_data.py, vm_build.py, vm_render.py)"},
             {"reverse-engineering", "the Python helper `references/ue3-packages/unpack_upk.py`"}}) {
        const fs::path p = out / "skills" / skill / "SKILL.md";
        std::string d = slurp(p);
        const auto fm = d.find("\n---\n", 4);
        d.insert(fm + 5, "\n> **A.S.T.R.O AI note:** this pack leaves out " + what + " (Library packs are text-only).\n"
                         "> They are in the upstream repository, https://github.com/rehan-remade/universal-modder, if the user wants them.\n");
        spit(p, d);
    }
    std::sort(skills.begin(), skills.end()); std::sort(agents.begin(), agents.end());

    // fal's hosted MCP server; ${FAL_KEY} is filled in by A.S.T.R.O AI from Settings > Keys or the environment
    spit(out / ".mcp.json", slurp(src / ".mcp.json")); ++files;

    std::string pj = "{\n  \"name\": \"astro-modder\",\n  \"displayName\": \"Modder Pack\",\n  \"version\": \"1.0.0\",\n"
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
         "- Left out: the Python helper scripts (blitz3d-decomp/scripts, unpack_upk.py), the hooks, the knowledge base (the\n"
         "  `um kb` tool syncs it from GitHub) and `um render3d` (it scripts Blender in Python).\n"
         "- A short A.S.T.R.O AI note at the top of the agents and of the skills that mention render3d or a left-out script.\n"
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
        "- **The `um` tool, built into A.S.T.R.O AI** (C++, no Python): `scan`, `backup`, `publish`, `kb`, `sprite`, `video`, `fal`,\n"
        "  `comfy`, `sprixen`, `voidless`, `win`, and the Call of Duty 4 / World at War helpers `fastfile`, `iwi`, `weapon`.\n"
        "  Run `${ASTRO_TOOL} um --help` from a skill, or `<engine> tool um --help` in a terminal.\n\n"
        "## Limits, honestly\n\n"
        "- `um render3d` (GLB to sprite frames) is not ported: it scripts Blender in Python. Render frames in Blender, then use `um sprite`.\n"
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
