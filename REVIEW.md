# Review guide

For the Nexus Mods support team, or anyone checking what ZARModManager does. Everything below can be confirmed in the
code in this repository; file names point to where.

## What it is

Two parts, built from this source:

1. **ZARModManager.exe** (`gui/`): a desktop app. It looks up the game folder (Steam's install path from the registry,
   `gui/Core/Game.cs`), copies the runtime into it as `version.dll` (`gui/Core/Runtime.cs`), copies mod `.lua` files
   into `<game folder>\ZAMods\` (`gui/Core/Mods.cs`), and starts the game through Steam (`steam://rungameid/...`).
2. **The runtime, `version.dll`** (`src/`): a proxy for the game's `version.dll`. Windows loads it from the game folder
   because the game's `UnityPlayer.dll` imports `VERSION.dll`. All 17 real functions are forwarded to
   `C:\Windows\System32\version.dll` (`src/forward.asm`, `src/exports.def`). **Outside `zombieassault.exe` it does
   nothing.** Inside the game it runs Lua mods.

## Network, accounts and personal data

- **No network code at all.** Search the source for sockets, WinHTTP, `HttpClient`, `WebClient` or URLs: there are
  none besides XML namespaces. The app does not check for updates, send telemetry or download anything.
- It does not read or write browser data, saved passwords, tokens, the Steam account or any file outside the game
  folder, the app's own folder and the user's temp folder (`gui/Core/Runtime.cs` writes and deletes one temporary file
  to read the runtime's version).
- Files the runtime writes, all inside `<game folder>\ZAMods\`: `loader.log` (its own log), one `<mod>.settings` file
  per mod (the setting values), `classes.txt` and `schema.txt` (class and field names of the game's own code, written at
  start-up), and, only if the user presses the debug keys F8 or F9, a snapshot or entity report (`src/loader.cpp`).
- Registry: read-only, one value (`HKCU\Software\Valve\Steam\SteamPath`) to find the game.
- Processes: only `steam://rungameid/<id>` and `explorer.exe <folder>` (the Open Folder button) are started
  (`gui/Core/Game.cs`).
- The app never replaces a `version.dll` that is not its own (`Runtime.IsOurs`).

## What the runtime does inside the game

- Reads the game's data through the game's own exported IL2CPP API (`il2cpp_*`). Every read of game memory is
  guarded with structured exception handling, so a bad pointer is skipped, not a crash.
- Lets Lua mods read game objects and show on-screen text (`src/runtime.inl`, `src/ui.inl`).
- Patches a small number of game functions in memory, only the ones a mod needs (virtual-table slot swaps and
  call-site patches, using `VirtualProtect`; search `src/system.inl` for `VirtualProtect`). Examples: the text setter
  (so the Name Hider mod can replace the player's own name on their own screen), the boss count when a boss nest is
  destroyed (Nightmare Pods), and the point in the game's input update where ability presses are made.
- Samples keyboard state to give mods hotkeys: `sample_input` in `src/system.inl` calls `GetAsyncKeyState`, **only
  while the game window has focus**, keeps press counts in memory and exposes them to Lua as `za.input.down` and
  `za.input.presses`. Nothing is recorded, written to disk or sent anywhere. The same state lets the user bind a key
  on the Settings screen.
- **Input API for mods:** `za.input.fire` and `za.input.ability` let a mod press the player's weapon trigger and
  ability buttons through the game's own input controller (as a fallback, mouse clicks with `SendInput`, with the
  cursor kept inside the game window using `ClipCursor` while firing). These exist in the runtime; which mods use
  them is up to the mod. None of the mods in `mods/` uses them.
- The loader's own debug keys: F7 (labels), F8 (a snapshot of live objects), F9 (an entity report), only while the
  game window has focus.

## Lua sandbox

Mods run in Lua 5.4.7 states with only the base, `table`, `string`, `math` and `utf8` libraries. There is **no `io`,
`os`, `package` or `debug` library, and `dofile`, `loadfile`, `load` and `require` are removed**
(`src/runtime.inl`, around `luaL_requiref`). A mod cannot touch the file system, start a process or open a connection
itself. It can only use the `za.*` functions the runtime provides, which are all in `src/`.

## The mods in `mods/`

Plain Lua text, one file per mod, each published on its own Nexus page: Minimalistic Mod (a cleaner HUD), Net Stats
(FPS and ping panel), Mission Re-roller (re-rolls the mission list until it matches), HP Numbers (health of what you
aim at), Impatient Player (skips end-of-mission screens), Nightmare Pods (more bosses from a boss nest). They are not
compiled into the executable: the release build leaves them out (`BundleMods=false` in `publish.cmd`), and each is
downloaded separately.

## Checking the executable against this source

- `publish.cmd` builds the release (`dotnet publish`, self-contained, single file). The project file sets
  `Deterministic`, no debug symbols and a neutral path map, so the build machine's folder names are not embedded.
- The runtime inside the exe is the `version.dll` that `build.cmd` produces from `src/` and `third_party/lua-5.4.7`.
- Lua is unmodified; compare it with the official tarball <https://www.lua.org/ftp/lua-5.4.7.tar.gz>.
- The release zip contains only `ZARModManager.exe`, `LICENSES.txt` and the `licenses` folder.
- Each release page on Nexus lists the SHA-256 of its zip.

## Why it is a `version.dll` proxy

The game ships no mod support. A proxy DLL is the standard way to get code loaded in this kind of Unity game without
modifying any game file: the game folder's `version.dll` is found before the system one. Removing the file restores
the game exactly.
