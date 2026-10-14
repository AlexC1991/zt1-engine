# Contributing to ZT1-Engine

Thanks for wanting to help! Contributions of any size and from any level of experience are welcome: a bug report, a typo fix, a mod, a new system, or a single number checked against the original. If you're new to C++, reverse engineering or open source, this is a good place to learn, so ask in an issue whenever you're stuck.

## The one big rule: the original game is the spec

The engine should do what Zoo Tycoon 1 does, exactly. When the engine and the original differ, the original is right. That means:

- **Don't guess.** If you don't know how the original does something, find out (see below) or mark your code as a stand-in.
- **Say where it came from.** Every gameplay, UI or drawing function says in its comment where its rules came from:
  - `zoo.exe 0x48d21d`: copied from the original's code at that address.
  - `measured: ...`: measured from the original running (say how).
  - `ours: ...`: the original has nothing like it (plumbing, tools).
  - `addition: ...`: a new feature, behind a switch (next point).
- **Additions are switches.** Anything the original doesn't do goes behind a switch in `src/Features.hpp`. Switched off, the game must behave exactly like the original.
- **No magic numbers.** A number the original hard-codes goes in `src/OriginalConstants.hpp` with its address. A number from the game's files is read from the game's files.

The ZT1 Lab's **Explore › Code 1:1 with zoo.exe** view lists every function and number that doesn't follow this yet. Those are good first tasks.

## Ways to help

- **Report differences from the original.** Open an issue with what you did, what the original does, what the engine does, and screenshots or a saved zoo. These are the most valuable reports there are.
- **Test the weekly alphas** and report crashes. A log or a saved zoo helps a lot.
- **Make mods** (see the README) and tell us what the mod system is missing.
- **Write code:** fix a difference, cite an uncited function, move a bare number into `OriginalConstants.hpp`, or add a formula check (below).
- **Write docs and guides,** especially for newcomers.

## Building

You need Windows 10/11 64-bit, Visual Studio 2022 (the "Desktop development with C++" workload), CMake 3.16+, Python 3 and your own Zoo Tycoon 1.

```bash
git clone --recurse-submodules https://github.com/AlexC1991/zt1-engine.git
cd zt1-engine
BUILD_ENGINE.bat
```

`BUILD_ENGINE.bat` offers a full build (the first time: it builds and imports your game files), a quick build that compiles changes and runs, and a verify build that only compiles. The engine ends up in `build/Release/`.

### Testing your change

```bash
build\Release\zt1-engine.exe --panel-shots <a folder>
```

This plays a freeform zoo by itself and checks over 230 behaviours against the original, saving a screenshot at each step. Results are in `<folder>/timings.txt`: every line should end in `ok`. Run it before opening a pull request.

## Finding out how the original does something

- **The game's own files** (`.ai`, `.cfg`, `.lyt` inside the `.ztd` archives) hold most numbers and settings. Read them first.
- **`zoo.exe` itself:** open it in [Ghidra](https://ghidra-sre.org/) (free), or use the **ZT1 Lab** (which bundles Ghidra and Java). The function names and fields worked out so far are in `re/knowledge.json`, and scripts in `re/ghidra_scripts/` apply them to your Ghidra project. Cite the addresses you used.
- **Sharing binary findings (with or without the ZT1 Lab):** you don't have to switch to the ZT1 Lab if you prefer your own Ghidra setup. You can sync findings through `python re/cloud/sync.py contribute` (see [`re/cloud/README.md`](re/cloud/README.md)) or open an issue/PR. Every address in the shared knowledge base is pinned to one specific retail Windows `zoo.exe` (`2,449,432` bytes, SHA-256 `61fcc6d1…`) so addresses never collide across builds. If you have symbol or class names from another binary (such as the Mac PowerPC build), class and struct names are keyed by name rather than address and can be cross-referenced onto the Windows build.
- **Measuring:** play the original and the engine side by side and compare. Write down how you measured in the `measured:` comment.

### Formula checks

`re/lab/formulas.json` lists checks that run one of the original's functions in an emulator (`re/lab/zoorun.py`) and ours on the same numbers, case by case. To add one:

1. Make our formula a function that both the game and the check call.
2. Add it to the engine's `formula` command in `src/Lab.cpp`.
3. Add the check to `formulas.json`: the original's address, how its objects are laid out, and the cases.

## Pull requests

1. **Scope, maintenance responsibility, and forks:** Pull requests for bug fixes, gameplay/UI accuracy cited to `zoo.exe` or measured against the original, formula checks, and docs are always welcome. However, I don't merge large subsystems or platform ports (such as Linux or macOS builds) that I would be left responsible for maintaining if the original contributor steps away. If you want to build a Linux, macOS, or custom version, **fork this repo** and own your build — and open an issue with your link so I can add it to the **Community Forks & Related Projects** table in [`README.md`](README.md#community-forks--related-projects) and send people your way (no matter what OS it runs on or how many forks exist).
2. Fork the repo and make a branch for your change. (This repo is itself a fork of sharkwouter's, so when you open the pull request, check that its base is **AlexC1991/zt1-engine** `main`. GitHub picks the original repo by default.)
3. Keep each pull request to one thing. Small is good.
4. **Build and test on Windows first:** make sure `BUILD_ENGINE.bat` compiles with zero errors on Windows and run `--panel-shots`. GitHub Actions workflows on fork PRs wait for maintainer approval before running any jobs, so an untested Windows break won't be caught automatically when you open the PR.
5. Cite where your rules came from (above), and in the description say what changed, how you checked it against the original, and what you couldn't check.

Someone will review it. Expect questions about where a rule came from: that's how the engine stays faithful.

### No Discord or chat server

There is no Discord server or social group for this project. Everything happens in the open through GitHub issues, pull requests, and the ZT1 Lab's shared knowledge base so anyone can contribute without needing an invite and the project never depends on one person moderating a server.

### Using AI

You're welcome to use AI tools, or not. If you do, the ZT1 Lab's MCP server lets your agent search the original, edit, build and test the engine through the Lab, where you can watch everything it does. Setup is in the README. Either way, **you're responsible for what you submit**. Understand it, test it, and cite where its rules came from like any other change. Never submit AI-generated art, audio or text for the game itself.

## Releases (for maintainers)

Builds are made in the cloud (`.github/workflows/`):

- **Every push and pull request** is built, to check that it compiles and packages.
- **Every push to `main`** is built too, and `main`'s code is mirrored to the `zt1-engine` project on CodeRook.
- **A weekly alpha** is an annotated tag named for its version, `v<year>.<month>.<day>` with no leading zeros. Its message is the release notes:

  ```bash
  git tag -a v2026.10.19 -F notes.md
  git push origin v2026.10.19
  ```

  The engine is built as an official release, the only kind that can update itself. Its installer, the installer's checksum and `latest.yml` (the in-game updater's feed) become a GitHub release and a named version of `zt1-engine-releases` on CodeRook.

`python engine-build-resources/make_release.py` builds the same installer on your own PC, into `dist/`. It needs Inno Setup 6. It isn't official unless you pass `--official`, so it never updates itself.

The cloud can't run the self-test or the formula checks, because they need Zoo Tycoon's own files. Run them in the ZT1 Lab before tagging an alpha.

## The game's files are not ours

Never commit anything from Zoo Tycoon itself: no art, sounds, maps, `.ztd` archives, `zoo.exe` or its decompiled code. The engine reads them from each player's own copy. Small facts about the original (an address, a field's meaning, a number) are fine and expected.

## Be kind

Be patient and welcoming, especially with newcomers. Critique code, not people. This is a hobby project made out of love for a game. Let's keep it a good place to be.
