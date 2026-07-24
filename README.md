# QiuckPrompts

Lightweight **Windows 10/11** tray app: global hotkeys grab text from your editor and drop it into an AI chat with a prepared prompt.

Pure Win32 + C++17. No third-party libraries. Built to debug, trace, and extend.

## What a hotkey does (Send-to-AI)

1. Release modifiers from the chord  
2. **Ctrl+A** / **Ctrl+C** in the focused editor  
3. Activate **Chrome Beta** (fallback: Chrome / Edge)  
4. **Ctrl+T** new tab → open AI URL (default [meta.ai](https://www.meta.ai/))  
5. Wait for page → **paste** `prompt + editor text`  
6. Restore your previous clipboard  

Insert-only mode (template paste, no browser) is available via tray toggle or `--insert-only`.

## Default hotkeys

Left hand holds **Ctrl+Alt**, right hand presses the letter:

| Hotkey | Action |
|--------|--------|
| `Ctrl+Alt+J` | Grammar check → AI |
| `Ctrl+Alt+K` | Fact check → AI |
| `Ctrl+Alt+L` | Summarize → AI |
| `Ctrl+Alt+I` | Explain simply → AI |
| `Ctrl+Alt+O` | Code review → AI |

Edit bindings / prompts / AI URL in **`include/config.hpp`**.

## Build

```bat
scripts\build.bat
:: or
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

Exe: `build\Debug\qiuckprompts.exe`

```bat
build\Debug\qiuckprompts.exe --self-test
build\Debug\qiuckprompts.exe --console --log-level=trace
```

## CLI

| Flag | Meaning |
|------|---------|
| `--console` | Live logs |
| `--log-level=...` | trace/debug/info/warn/error |
| `--ai-url=URL` | Default chat URL |
| `--browser-hint=TEXT` | Prefer matching window/path (default `Chrome Beta`) |
| `--page-title-hint=TEXT` | Title must contain this (auto from URL if empty) |
| `--page-ready-timeout=MS` | Max wait for page/input (default 15000) |
| `--hotkey-on-press` | Fire on key-down (default is on-release) |
| `--insert-only` | Skip browser; paste template only |
| `--self-test` | Headless checks |

## Hotkey hold / release

Default: **fire on release** (`ARMED` → release → `FIRE`).

## Collecting window titles (for future config)

Every run appends stable `TITLE_SAMPLE` lines to:

- `build\Debug\logs\titles.log`  (easy to mine)
- main `qiuckprompts.log` as well

**How to collect:**

1. Start the app  
2. Open meta.ai / Gemini / etc. tabs in Chrome Beta  
3. Tray → **Sample window titles now** (repeat after each site)  
4. Or run hotkeys a few times  
5. Tray → **Open titles.log**

Example line:

```text
TITLE_SAMPLE ts=... where=page_ready_poll hwnd=... fg=1 class='Chrome_WidgetWin_1' exe='chrome.exe' title='Meta AI - Chrome Beta' note='t=1200ms titleReady=1 hint=Meta'
```

`where=` tags to filter: `startup`, `tray_sample`, `hotkey_fire`, `workflow_after_navigate_enter`, `page_ready_poll`, `page_ready_ready`, `browser_selected`, …

## Layout

| Module | Role |
|--------|------|
| `config.hpp` | Templates, hotkeys, workflow timings/URL |
| `workflow` | Full send-to-AI pipeline |
| `browser` | Find/activate Chrome Beta |
| `page_ready` | Title + UIA wait |
| `title_sample` | `TITLE_SAMPLE` logging → `titles.log` |
| `input_sim` | Keys, clipboard, focus helpers |
| `injector` | Insert-only paste path |
| `hotkeys` / `tray` / `logger` | Shell |

## Tuning flaky steps

Page ready is **not** a fixed sleep anymore. After opening the AI URL we poll:

1. Tab title left “New Tab” and matches hint (auto from URL: meta.ai → `Meta`)  
2. **UI Automation** finds a non-omnibox `Edit` (chat box) and focuses it  

Chrome does **not** expose the chat box as a Win32 `HWND` — only a single render surface. UIA is the supported way to “see” the input.

```bat
qiuckprompts.exe --page-ready-timeout=20000
qiuckprompts.exe --page-title-hint=Meta
qiuckprompts.exe --no-uia
```

Logs: look for `page_ready: READY after Xms` in `<exe_dir>\logs\qiuckprompts.log`.

## License

Personal tooling — add a license when you care.
