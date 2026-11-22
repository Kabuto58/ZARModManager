# ZARModManager

A mod loader and launcher for **Zombie Assault: Resurgence** (Unity, IL2CPP, Windows). It installs a small runtime
into the game folder, loads Lua mods when the game starts, and puts every mod's settings on the game's own Settings
screen (a MODS tab).

This repository is the complete source of the program published on Nexus Mods as "ZARModManager - Mod Loader and
Manager". **If you are reviewing it, start with [REVIEW.md](REVIEW.md).**

## What is in here

| Path | What it is |
|---|---|
| `gui/` | The desktop app (C#, .NET 9, WPF): finds the game through Steam, installs or removes the runtime, manages mod files, launches the game |
| `src/` | The runtime (C++17, built as `version.dll`): `loader.cpp` (IL2CPP access, start-up), `runtime.inl` (Lua host and the `za.*` API), `system.inl` (input, settings, hooks), `ui.inl` (on-screen UI), `system.lua` (the MODS tab on the Settings screen), `forward.asm` and `exports.def` (forwarding the real `version.dll` functions) |
| `third_party/lua-5.4.7/` | Lua 5.4.7, unmodified, from <https://www.lua.org/ftp/lua-5.4.7.tar.gz> |
| `mods/` | The mods published on Nexus Mods (plain `.lua` text files, readable as they are) |
| `build.cmd`, `publish.cmd`, `package.ps1`, `check_mods.ps1`, `build_luac.cmd` | Build, publish and packaging scripts |
| `LICENSES.txt`, `licenses/` | Third-party licence notices |
| `VERSION` | The one version number used by the app and the runtime |

## Building

Needs Windows, Visual Studio 2022 Build Tools (C++ workload) and the .NET 9 SDK.

```
build.cmd       build\version.dll (the runtime) and build\dist\ZARModManager.exe (development build)
publish.cmd     the public release: self-contained single-file release\ZARModManager.exe, zipped by package.ps1
```

`build.cmd` compiles Lua 5.4.7 from `third_party/`, the runtime from `src/`, then the app. The runtime is embedded in
the app as a resource and written to the game folder as `version.dll` when the user clicks Install.

## Uninstall

Click Uninstall in the app, or delete `version.dll` from the game folder (and the `ZAMods` folder if you like). The
game is then vanilla.

## Licence

The project's own code is under the MIT License (`LICENSE`). Third-party components keep their own licences (see `LICENSES.txt`).
