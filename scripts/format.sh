#!/usr/bin/env bash
# format.sh [--check]  — used by CI (Linux) and git-bash.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

CHECK=0
case "${1:-}" in
  --check|-n|--dry-run) CHECK=1 ;;
esac

CF="${CLANG_FORMAT:-clang-format}"
if ! command -v "$CF" >/dev/null 2>&1; then
  for c in clang-format-18 clang-format-17 clang-format-16 clang-format-15; do
    if command -v "$c" >/dev/null 2>&1; then CF="$c"; break; fi
  done
fi
if ! command -v "$CF" >/dev/null 2>&1; then
  echo "clang-format not found" >&2
  exit 2
fi

mapfile -t FILES < <(find src include -type f \( -name '*.cpp' -o -name '*.hpp' \) | sort)
if [[ ${#FILES[@]} -eq 0 ]]; then
  echo "No source files"
  exit 1
fi

if [[ "$CHECK" -eq 1 ]]; then
  fail=0
  for f in "${FILES[@]}"; do
    if ! "$CF" --dry-run --Werror --style=file "$f" 2>/dev/null; then
      # older clang-format may lack --dry-run: fall back to diff
      if ! diff -u "$f" <("$CF" --style=file "$f") >/dev/null; then
        echo "NEED FORMAT: $f"
        fail=1
      fi
    fi
  done
  if [[ "$fail" -ne 0 ]]; then
    echo "Format check FAILED (${#FILES[@]} files). Run: scripts/format.sh"
    exit 1
  fi
  echo "Format check OK (${#FILES[@]} files) via $CF"
  exit 0
fi

for f in "${FILES[@]}"; do
  "$CF" -i --style=file "$f"
done
echo "Formatted ${#FILES[@]} files via $CF"
