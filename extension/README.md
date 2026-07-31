# QiuckPrompts Chrome / Edge companion (MV3)

Gives the tray app reliable **DOM** access to AI chat composers (Meta, Gemini, Grok, …)
via **native messaging**. No AI API keys — it drives pages you already use.

## Install (unpacked)

### Velopack install (Setup.exe)

1. Run **QiuckPrompts-win-Setup.exe** — install hook seeds config, registers the
   native messaging host, and copies the extension to a **stable** folder:
   `%LOCALAPPDATA%\QiuckPrompts\extension`
2. Chrome / Chrome Dev / Edge → `chrome://extensions` (or `edge://extensions`)
3. Enable **Developer mode**
4. **Load unpacked** → select  
   `%LOCALAPPDATA%\QiuckPrompts\extension`  
   (not `current\extension` — that path is replaced on every app update)
5. Confirm extension id is **`aodehlngahndannepofbddnacfaldmih`**
6. Companion version should match the app major (e.g. **1.0.0**). After updates, click
   **Reload** on the extension if the version or paste path looks stale.
7. Start the tray app. After **app updates**, open `chrome://extensions` and click
   **Reload** on QiuckPrompts if paste stops working (files change under the same path).

### Dev / portable (repo build)

1. Build/run **QiuckPrompts** once so it registers the native host  
   (`%LOCALAPPDATA%\QiuckPrompts\nm\` + HKCU registry).
2. Chrome / Chrome Dev / Edge → `chrome://extensions` (or `edge://extensions`)
3. Enable **Developer mode**
4. **Load unpacked** → select this `extension` folder  
   (from the repo, or `build\Debug\extension` after build).
5. Confirm extension id is **`aodehlngahndannepofbddnacfaldmih`**  
   (fixed via manifest `key`). If it differs, set `extension_id=` in  
   `%LOCALAPPDATA%\QiuckPrompts\qiuckprompts.ini` and restart the tray app.
6. Leave the extension **enabled**. Start the tray app **before** or reload the extension
   so the native port connects.

## Check

- Tray log should show `ext_bridge: native-host client connected` after the extension loads.
- Hotkey workflow prefers the extension when connected; falls back to UIA if not.

## Commands (native ↔ extension)

| cmd | direction | purpose |
|-----|-----------|---------|
| `ping` | tray → ext | liveness |
| `prepareAndPaste` | tray → ext | focus/open URL tab, wait for composer, paste text |
| `prepare` | tray → ext | open/focus + find composer only |

## Privacy

- Host permissions are limited to listed AI sites.
- Text you paste is whatever the tray app already captured from your editor.
- No network calls except the pages you open.
