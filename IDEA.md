# Idea

Short prompt templates I type or copy too often (grammar check, fact check, ...).

Want a basic lightweight tray tool with notification-area icon and a few predefined
hotkeys that insert prepared templates into the current text input.

Start with MVP and see what makes sense later.

## Constraints (agreed)

- C++ / CMake / Visual Studio
- Windows 10 and 11 only
- No UI bloat / no extra libraries for cosmetics
- Tests nice but not critical
- Design for debug, trace, and improve
- POC: templates + hotkeys hardcoded in `include/config.hpp`
- Later: config via simple text file (prefer hand-rolled INI/`key=value` over JSON/XML deps)
