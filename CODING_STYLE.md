# Coding style

Small pure **Win32 / C++17** tray app. Keep it readable, debuggable, and boring.

Automated check: `clang-format` via `.clang-format` (CI job **Format**).

```bat
scripts\format.bat          :: rewrite sources in place
scripts\format.bat --check  :: dry-run, non-zero if drift (CI)
```

## Braces (project rule)

**Default — Allman:** `{` and `}` on their own lines, same indent as the controlling statement.

```cpp
if (ok)
{
    DoWork();
}

void Foo()
{
    ...
}
```

**Exception — joined else:** keep `} else {` / `} else if (...) {` on one spine so the chain reads as a unit.

```cpp
if (a)
{
    One();
}
else if (b)
{
    Two();
}
else
{
    Three();
}
```

`clang-format` encodes this as `AfterControlStatement: Always` + `BeforeElse: false`.

One-liners without braces are discouraged for `if`/`for`/`while` (except empty blocks).

## Other conventions (match existing code)

| Topic | Rule |
|--------|------|
| Indent | 4 spaces, no tabs |
| Line length | soft ~100 columns |
| Pointers | `Type* name`, `Type& name` |
| Names | `snake_case` functions/vars, `PascalCase` types, `kCamel` / `UPPER` constants as already used |
| Headers | project headers first (`"app.hpp"`), then system/`windows.h` |
| Logging | `QP_LOG_*` with useful context; no spam at INFO in hot loops |
| Errors | return `bool` + optional `std::wstring* error`; log on fail |
| Win32 | wide APIs (`W` suffix), `UNICODE`, no third-party UI libs |
| Config | prefer simple text (INI) over JSON/XML deps |

## Scope of the formatter

Checked paths: `src/**/*.cpp`, `include/**/*.hpp`

Not formatted: `resources/*`, generated build trees, prompt/config text files.

## When CI fails

```bat
scripts\format.bat
git add -u
git commit -m "style: clang-format"
```
