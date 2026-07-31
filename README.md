# QiuckPrompts

[![CI](https://github.com/summeroff/qiuckprompts/actions/workflows/ci.yml/badge.svg)](https://github.com/summeroff/qiuckprompts/actions/workflows/ci.yml)

Lightweight **Windows 10/11** tray app: global hotkeys grab text from your editor and send it to an AI chat with a prepared prompt.

Pure **Win32 + C++17**. No third-party libraries. Built to debug, trace, and extend.

> Windows tray app: hotkeys grab editor text and send it to an AI chat with a prepared prompt.

## Features

- System tray icon, single instance
- Ergonomic hotkeys (left hand `Ctrl+Alt`, right hand letter)
- **Fire on key release** so modifiers are up before automation runs
- Send-to-AI workflow:
  1. Select-all + copy from the focused editor  
  2. Activate Chrome (Dev/Beta/stable) / Edge  
  3. New tab → open AI URL (default [meta.ai](https://www.meta.ai/))  
  4. Adaptive wait (window title + **UI Automation** chat input)  
  5. Paste `prompt + editor text`  
  6. Restore clipboard  
- Insert-only mode (paste template into the current field)
- File + debugger logging; optional `titles.log` for mining real window titles
- Templates / hotkeys in `include/config.hpp` (file config planned later)


## Configuration file

**User config (edit this):** `%LOCALAPPDATA%\QiuckPrompts\qiuckprompts.ini`  
Seeded on first run from the install template (`<exe>\config\qiuckprompts.ini`).  
Updates never overwrite the user file. Backups live in `%LOCALAPPDATA%\QiuckPrompts\backups\`.

**Logs:** `%LOCALAPPDATA%\QiuckPrompts\logs\` (`qiuckprompts.log`, `titles.log`; rotated ~5 MiB × 4 files).

Tray → **Open config** / **Open data folder**. Override path: `--config=D:\path\qiuckprompts.ini`

Each `[section]` binds **hotkey + service URL + full prompt text**:

```ini
[grammar_meta]
label=Grammar quick (Meta)
service=meta
hotkey=Ctrl+Alt+J
url=https://www.meta.ai/
title_hint=Meta
capture_editor=1
prompt<<<
Light edit only. Fix grammar...
Message:
>>>
```

Multi-line prompts use `prompt<<<` … `>>>` (so you can use `;` and markdown ` ``` ` inside).  
Single-line `prompt=...` still works. External `prompt=file.txt` is optional if you really want a separate file.

| Hotkey | Service | Purpose |
|--------|---------|---------|
| `Ctrl+Alt+J` | Meta | Quick grammar / light sanity, keep your voice |
| `Ctrl+Alt+O` | Meta | Grammar in-thread; no over-explaining context |
| `Ctrl+Alt+K` | Gemini | Deep fact-check + sources |
| `Ctrl+Alt+L` | Grok | Collaborate on an idea |
| `Ctrl+Alt+I` | Meta | Screenshot on clipboard + review prompt |

Restart the app after edits. Override path: `--config=D:\path\qiuckprompts.ini`

Screenshot flow: Win+Shift+S, then `Ctrl+Alt+I`.

## Default hotkeys (from config)

| Hotkey | Action |
|--------|--------|
| `Ctrl+Alt+J` | Grammar check → AI |
| `Ctrl+Alt+K` | Fact check → AI |
| `Ctrl+Alt+L` | Summarize → AI |
| `Ctrl+Alt+I` | Explain simply → AI |
| `Ctrl+Alt+O` | Code review → AI |

Edit bindings, prompts, and AI URL in **`include/config.hpp`**, then rebuild.

## Requirements

- Windows 10 or 11 (x64)
- [CMake](https://cmake.org/) 3.20+
- Visual Studio 2022 with C++ desktop workload (MSVC)

## Build

```bat
scripts\build.bat
```

Or manually:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

| Output | Path |
|--------|------|
| Exe | `build\Debug\qiuckprompts.exe` |
| PDB | `build\Debug\qiuckprompts.pdb` |

Release-style local build:

```bat
cmake --build build --config RelWithDebInfo
```

## Run / test

```bat
build\Debug\qiuckprompts.exe
build\Debug\qiuckprompts.exe --console --log-level=debug
build\Debug\qiuckprompts.exe --self-test
```

## CLI

| Flag | Meaning |
|------|---------|
| `--console` | Live logs on a console |
| `--log-level=LEVEL` | `trace` \| `debug` \| `info` \| `warn` \| `error` |
| `--log-file=PATH` | Override log path |
| `--ai-url=URL` | Default AI chat URL |
| `--browser-hint=TEXT` | Prefer matching window/path (default `Chrome Dev`) |
| `--page-title-hint=TEXT` | Title must contain this (auto from URL if empty) |
| `--page-ready-timeout=MS` | Max wait for page/input (default 15000) |
| `--page-ready-min=MS` | Min wait after navigate (default 500) |
| `--no-uia` | Title-only wait (disable UI Automation) |
| `--no-extension` | Skip Chrome companion; UIA-only path |
| `--hotkey-on-press` | Fire on key-down (default is on-release) |
| `--hotkey-release-timeout=MS` | Max wait for key-up (default 3000) |
| `--insert-only` | Paste template only (no browser flow) |
| `--config=PATH` | Override user ini path |
| `--self-test` | Headless checks, exit 0/1 |
| `--help` | Help text |

## Chrome companion extension

Optional **MV3** extension gives reliable DOM paste (preferred over UIA when connected).

1. Run the tray app once (registers native messaging host under `%LOCALAPPDATA%\QiuckPrompts\nm\`).
2. Chrome/Edge → Developer mode → **Load unpacked** → `extension/` (or `build\Debug\extension`).
3. Keep extension enabled; id should be `aodehlngahndannepofbddnacfaldmih`.

Details: [extension/README.md](extension/README.md). Disable with `prefer_extension=0` or `--no-extension`.

## Releases

This project ships installable binaries via
[GitHub Releases](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases)
using **Velopack** (Setup + in-app updates) and a plain zip.

CI builds on every push/PR as **`0.0.0-dev`**. **Pushing a version tag** injects that
version into the binary (CMake `-DQP_RELEASE_VERSION=`), builds RelWithDebInfo, runs
`vpk pack`, and publishes a Release:

```bat
git checkout master
git pull
git tag v0.3.2
git push origin v0.3.2
```

| Item | Detail |
|------|--------|
| Trigger | Git tag matching `v*` |
| Version | Tag without `v` → PE + About + Velopack pack version |
| Local/PR | Always `0.0.0-dev` (not a release) |
| Build | `RelWithDebInfo` (MSVC x64) |
| Installer | `QiuckPrompts-win-Setup.exe` (Velopack one-click → `%LocalAppData%\QiuckPrompts`) |
| Portable | `QiuckPrompts-win-Portable.zip` (Velopack portable layout) |
| Feed | `releases.win.json` + `*-full.nupkg` (in-app update) |
| Zip | `qiuckprompts-<tag>-win-x64.zip` (raw exe + config + extension) |
| Notes | Auto-generated from commits since previous tag |

### Install / update

1. Download **QiuckPrompts-win-Setup.exe** and run (no admin).
2. Config/logs stay in `%LOCALAPPDATA%\QiuckPrompts\` (not wiped by updates).
3. Tray → **Check for updates…** fetches the latest feed and applies via `Update.exe`.

**Chrome companion after Setup:**

| Step | What |
|------|------|
| Install/update hook | Registers native messaging host; copies packaged `extension/` → `%LOCALAPPDATA%\QiuckPrompts\extension` (stable path) |
| You once | Chrome → Load unpacked → that **stable** folder (not `current\extension`) |
| After app update | Extension files are re-copied to the same path — click **Reload** on `chrome://extensions` if needed |
| Uninstall | NM registry keys removed; your config/logs/extension folder are left |

Local pack (needs [vpk](https://www.nuget.org/packages/vpk)):

```bat
dotnet tool install -g vpk
scripts\build.bat
cmake --build build --config RelWithDebInfo
scripts\pack-velopack.bat 0.3.2
```

Default feed URL: `https://github.com/summeroff/qiuckprompts/releases/latest/download`  
Override: `update_url=` in ini or `--update-url=`.

See [CONTRIBUTING.md](CONTRIBUTING.md) for branch/PR workflow and release checklist.

## Code style

See [CODING_STYLE.md](CODING_STYLE.md). Braces are Allman-style (`{` on its own line); `} else {` stays joined. CI runs `clang-format`.

```bat
scriptsormat.bat          :: apply
scriptsormat.bat --check  :: CI-style check
```

## Contributing

Use feature branches and squash-merge PRs into `master` — details in [CONTRIBUTING.md](CONTRIBUTING.md).

## Layout

| Path | Role |
|------|------|
| `include/config.hpp` | Templates, hotkeys, workflow knobs |
| `src/workflow.cpp` | Send-to-AI pipeline (extension → UIA fallback) |
| `src/ext_bridge.cpp` | Named pipe + native-messaging host relay |
| `extension/` | MV3 companion (DOM composer paste) |
| `src/browser.cpp` | Find / activate browser window |
| `src/page_ready.cpp` | Title + UI Automation wait |
| `src/title_sample.cpp` | `TITLE_SAMPLE` → `titles.log` |
| `src/hotkeys.cpp` | Global hotkeys, fire-on-release |
| `src/injector.cpp` | Insert-only paste path |
| `src/input_sim.cpp` | Keys, clipboard, focus helpers |
| `src/tray.cpp` | Notify icon + menu |
| `.github/workflows/ci.yml` | Build, test, tagged releases |

## Collecting window titles

For tuning `pageTitleHint` / browser matching:

1. Run the app  
2. Open AI tabs in Chrome  
3. Tray → **Sample window titles now**  
4. Tray → **Open titles.log** → `build\Debug\logs\titles.log`  

Lines are tagged `TITLE_SAMPLE` with stable `where=` fields.

## Design notes

- User data lives under **`%LOCALAPPDATA%\QiuckPrompts`** so installers/updates cannot clobber config or logs.
- Chrome does **not** expose the chat box as a Win32 `HWND`. Prefer the **MV3 companion** (DOM); readiness otherwise uses the **UI Automation** tree plus the tab title.
- Hotkeys **arm on press** and **run on release** so Ctrl/Alt are up before Select-all/Copy/Paste.
- No Qt/WPF/Electron — message-only window + tray only.

## License

MIT — see [LICENSE](LICENSE).
