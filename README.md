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

## Default hotkeys

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
| `--browser-hint=TEXT` | Prefer matching window/path (default `Chrome Beta`) |
| `--page-title-hint=TEXT` | Title must contain this (auto from URL if empty) |
| `--page-ready-timeout=MS` | Max wait for page/input (default 15000) |
| `--page-ready-min=MS` | Min wait after navigate (default 500) |
| `--no-uia` | Title-only wait (disable UI Automation) |
| `--hotkey-on-press` | Fire on key-down (default is on-release) |
| `--hotkey-release-timeout=MS` | Max wait for key-up (default 3000) |
| `--insert-only` | Paste template only (no browser flow) |
| `--self-test` | Headless checks, exit 0/1 |
| `--help` | Help text |

## Releases

CI builds on every push/PR. Pushing a version tag creates a GitHub Release with a Windows x64 zip:

```bat
git tag v0.2.0
git push origin v0.2.0
```

Asset name: `qiuckprompts-<tag>-win-x64.zip` (contains `qiuckprompts.exe` + PDB).

## Layout

| Path | Role |
|------|------|
| `include/config.hpp` | Templates, hotkeys, workflow knobs |
| `src/workflow.cpp` | Send-to-AI pipeline |
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

- Chrome does **not** expose the chat box as a Win32 `HWND`. Readiness uses the **UI Automation** accessibility tree plus the tab title.
- Hotkeys **arm on press** and **run on release** so Ctrl/Alt are up before Select-all/Copy/Paste.
- No Qt/WPF/Electron — message-only window + tray only.

## License

MIT — see [LICENSE](LICENSE).
