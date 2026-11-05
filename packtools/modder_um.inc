// modder_um.inc - the `um` command line of the Universal Modder pack: a C++ port of universal-modder's um/cli.py
// (Rehan and universal-modder contributors, MIT). One entry point for every group, so a skill says
//   ${ASTRO_TOOL} um <group> <cmd> ...      (upstream: um <group> <cmd> ...)
namespace packtools::modder {
using namespace pyrt;

inline const char* UM_DOC =
    "universal-modder: tools any AI agent (Claude Code, Codex, Cursor, Gemini CLI, ...) uses to mod games.\n"
    "Built into A.S.T.R.O AI (a C++ port of the MIT-licensed `um` CLI; run it as ${ASTRO_TOOL} um ...).\n\n"
    "Subcommands (see `um --help`):\n"
    "  scan      find installed games and fingerprint one: engine, runtime, anti-cheat, mod loaders, routes\n"
    "  fal       generate game assets with fal (sprites, textures, PBR, 3D, rigs, SFX, music, voice, video)\n"
    "  comfy     generate images locally with ComfyUI (no API key)\n"
    "  sprixen   2D sprites with Sprixen: one style per project, animation sheets, terrain tiles\n"
    "  voidless  2D sprites with Voidless: 8-way turns and animation sets on one asset\n"
    "  sprite    cut out, fit, pixelate, recolor and pack 2D sprites\n"
    "  render3d  (not in this C++ port: it scripts Blender in Python. Render GLB frames in Blender yourself, then use `sprite`)\n"
    "  video     compile styled showcase videos, trim, mux\n"
    "  win       Windows (and WSL): screenshots, recording with game-only audio, input, processes\n"
    "  backup    snapshot and restore save folders before you touch them\n"
    "  publish   lint a mod folder before sharing: game files, decompiled code, secrets, credits\n"
    "  kb        the knowledge base: search prior field notes, write your own, check it, open a PR\n"
    "  fastfile  Call of Duty 4 / World at War: list and dump the rawfiles (GSC, CSC, menus) in your own .ff zones\n"
    "  iwi       Call of Duty 4 / World at War: PNG to IWI v6 textures (DXT1/DXT5) and back\n"
    "  weapon    Call of Duty 4 / World at War: read, edit, clone and diff weapon files\n";

struct Group { const char* name; void (*reg)(Parser&); int (*run)(const Val&); };
inline std::vector<Group>& groups() { static std::vector<Group> g; return g; }

inline int um_main(const std::vector<std::string>& argv) {
    Parser p("um", UM_DOC); p.raw_description = true;
    p.opt({"--version"}).version("universal-modder " + UM_VERSION + " (A.S.T.R.O AI built-in)");
    p.subcommands("group", false); p.sub_metavar = "<group>";
    for (const auto& g : groups()) g.reg(p);
    const Val a = parse_args(p, argv);
    const std::string group = S(a, "group");
    const Group* hit = nullptr; for (const auto& g : groups()) if (group == g.name) hit = &g;
    // a group without a command: show that group's help (exit 0); no group at all: the top help, exit 1
    bool has_cmd = true;
    if (hit) for (const auto& s : p.subs) if (s.first == group && s.second.has_sub) { const Val c = get(a, s.second.sub_dest); if (c.is_none() || (c.t == Val::STR && c.s.empty())) has_cmd = false; }
    if (!hit || !has_cmd) {
        if (hit) { parse_args(p, {group, "--help"}); }
        try { parse_args(p, {"--help"}); } catch (const Exit&) {}
        throw Exit{1};
    }
    return hit->run(a);
}
}  // namespace packtools::modder
namespace packtools::modder {
inline int um_tool(const std::vector<std::string>& argv) {
    groups() = {   // upstream GROUPS order (render3d needs Blender's Python and is not ported)
        {"scan", scan::reg, scan::main_},
        {"fal", fal::reg, fal::main_},
        {"comfy", comfy::reg, comfy::main_},
        {"sprixen", sprixen::reg, sprixen::main_},
        {"voidless", voidless::reg, voidless::main_},
        {"sprite", sprite::reg, sprite::main_},
        {"video", video::reg, video::main_},
        {"win", win::reg, win::main_},
        {"backup", backup::reg, backup::main_},
        {"publish", publish::reg, publish::main_},
        {"kb", kb::reg, kb::main_},
        {"fastfile", fastfile::reg, fastfile::main_},
        {"iwi", iwi::reg, iwi::main_},
        {"weapon", weapon::reg, weapon::main_},
    };
    return um_main(argv);
}
PACKTOOL("um", um_tool);
}  // namespace packtools::modder
