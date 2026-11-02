# Console Pack

11 skills to port PlayStation 2 games you own to PC with the open-source PS2Recomp recompiler: reverse engineering, graphics, sound, physics, saves, controllers, levels, testing, mods and builds for other platforms.

> **Read first.** Only for games you own, using your own disc dumps and your own console's BIOS. Astro will not download, share or help find ROMs, ISOs or BIOS files, and will not help bypass copy protection. Respect the game publisher's rights when sharing mods.

Adapted from [console-skill](https://github.com/Jakeschincariol/console-skill) by Jake Schincariol (MIT). See NOTICE.md.

## Skills

| Skill | |
|---|---|
| `console-anywhere` | Play Anywhere. Builds the port for Windows, Mac, Linux, Steam Deck, Android and PS Vita and packages it without any game files inside. doctor checks this machine's tools for a target and prints the exact install command… |
| `console-backend` | Backend Engineer. Builds the engine room the recompiled game runs on: syscalls, kernel stubs, file and disc I/O, the IOP. Reads PS2Recomp runtime logs and turns every 'Unimplemented PS2 stub', '[Syscall TODO]', missing… |
| `console-controller` | Controller Mapper. Hooks the game up to any controller: DualSense, Xbox, Switch Pro, keyboard or touch, remapped how you like. Reads a mapping in console/controls.json (a layout preset plus per-button overrides, radial… |
| `console-graphics` | Graphics Artist. Rebuilds the graphics: dumps every texture, lets you repaint or replace them with your own art, finds the widescreen fix. Converts PS2 TIM2 textures (.tm2, 32/24/16-bit and 8/4-bit indexed) from the… |
| `console-levels` | Level Designer. Maps the disc's data files, unpacks the archives the levels live in, and swaps in your edits. Inventories an extracted PS2 disc (size, type by magic, entropy, a table-of-contents guess with a confidence… |
| `console-mod` | Mod Maker. Describe any change in plain English and Astro builds it into the game: patches, cheats, new behaviour, as code that never touches the original files. Ports PCSX2 .pnach patches to the PS2Recomp port (code… |
| `console-physics` | Physics Tuner. Finds the numbers that decide how the game moves (speed, gravity, grip, jump height, frame rate) and tunes them. Scans the user's own game ELF for a float32 value stored in data or built in code (lui, ori… |
| `console-reverse` | Reverse Engineer. Opens your own disc image, finds the game's code and maps how it works: the boot ELF, every function, the design doc. Drives the whole port and is the entry point of the Console pack. Reads ISO 9660… |
| `console-saves` | Save Manager. Brings your old memory card saves over, backs them up and syncs them to the cloud so you continue on any device. Reads PS2 memory card images (PCSX2 .ps2 and .mcd, raw 8 MB dumps, with or without ECC) and… |
| `console-sound` | Sound Designer. Rips the music and sound effects and puts your own tracks back in. Decodes PS2 VAG files and raw SPU2 ADPCM from sound banks (.bd) to WAV, finds VAGs hidden inside archives and disc files, and encodes… |
| `console-tester` | Playtester. Runs the port again and again, reads every crash, ranks what to fix next and checks nothing broke. run starts ps2EntryRunner with your game's ELF under a hard timeout, kills the whole process tree, keeps the… |

## Use

Ask the agent for the job in plain words ("write chapters for this transcript"), or name the skill.
Some skills run helper tools that are built into A.S.T.R.O AI (1.9.0 or later); nothing extra to install.
