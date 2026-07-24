# QiuckPrompts

Lightweight **Windows 10/11** tray app: global hotkeys insert prepared prompt templates into whatever text field is focused.

POC / MVP — no third-party libraries. Pure Win32 + C++17. Built to be easy to debug, trace, and extend.

## Status

- [x] System tray icon + context menu  
- [x] Global hotkeys (`RegisterHotKey`)  
- [x] Template injection via clipboard swap + `Ctrl+V`  
- [x] File + `OutputDebugString` logging (DebugView / VS Output)  
- [x] Hardcoded templates & hotkeys in `include/config.hpp`  
- [ ] Load config from text file (later — prefer simple INI/`key=value`, no deps)  
- [ ] Custom tray icon asset  

## Default hotkeys (POC)

| Hotkey | Template |
|--------|----------|
| `Ctrl+Alt+1` | grammar_check |
| `Ctrl+Alt+2` | fact_check |
| `Ctrl+Alt+3` | summarize |
| `Ctrl+Alt+4` | explain_simple |
| `Ctrl+Alt+5` | code_review |

Change bindings / prompt text in **`include/config.hpp`** and rebuild.

## Build (Visual Studio 2022 + CMake)

From a **x64 Native Tools** prompt, or after `vcvars64.bat`:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

Exe: `build\Debug\qiuckprompts.exe`  
PDB next to it for debugging.

RelWithDebInfo (optimized + symbols):

```bat
cmake --build build --config RelWithDebInfo
```

Self-test (headless):

```bat
build\Debug\qiuckprompts.exe --self-test
```

## Run

```bat
build\Debug\qiuckprompts.exe
build\Debug\qiuckprompts.exe --console --log-level=trace
```

- Lives in the **notification area** (tray).  
- Log file: `<exe_dir>\logs\qiuckprompts.log`  
- Single instance only.  
- Tray menu: List hotkeys / Open log / About / Exit.

### CLI

| Flag | Meaning |
|------|---------|
| `--console` | Allocate a console and mirror logs to stdout |
| `--log-level=trace\|debug\|info\|warn\|error` | Default: `debug` |
| `--log-file=PATH` | Override log path |
| `--paste-delay=MS` | Wait after paste before restoring clipboard (default 200) |
| `--self-test` | Headless checks, exit code 0/1 |
| `--help` | Help text |

### Debugging tips

1. Run under Visual Studio: set debugger working dir to the exe folder; breakpoints in `TextInjector::Inject`, `HotkeyManager::OnWmHotkey`.  
2. Or attach **DebugView** (Sysinternals) and filter `qiuckprompts` — all levels go to `OutputDebugString`.  
3. `--console --log-level=trace` for live step traces (clipboard open/set/paste/restore).  
4. If a hotkey fails to register, another app owns it — check the log and change `GetBuiltInBindings()`.

## Layout

```
include/          public headers (config.hpp = POC knobs)
src/              app, tray, hotkeys, injector, logger, util
resources/        version.rc, resource.h
```

| Module | Role |
|--------|------|
| `config.hpp` | Built-in templates + hotkey table (edit here) |
| `hotkeys` | `RegisterHotKey` / `WM_HOTKEY` |
| `injector` | Save clipboard → set text → `SendInput` Ctrl+V → restore |
| `tray` | Notify icon + menu |
| `logger` | Timestamped file + debugger |
| `app` | Message-only window, wiring, self-test |

## Design notes

- **Injection**: clipboard paste is more reliable than per-character `SendInput` for long Unicode prompts. Clipboard is restored after a short delay.  
- **No UI framework**: message-only HWND + tray.  
- **Future config file**: when we outgrow compile-time tables, a tiny hand-rolled `key=value` / INI reader is enough — avoid JSON/XML libs unless needed.  
- Target: **Windows 10 and 11 only** (`WINVER=0x0A00`).

## License

Use freely for personal tooling; add a license file when you care.
