# Contributing

## Branch + PR workflow

Keep `master` clean:

1. Create a branch from up-to-date `master`  
   `git checkout master && git pull && git checkout -b feat/short-name`
2. Commit on the branch only  
3. Push and open a PR into `master`  
4. Merge with **Squash and merge** (one commit on `master`)  
5. Delete the branch after merge  

Suggested prefixes: `feat/`, `fix/`, `docs/`, `ci/`, `chore/`.

Do **not** push feature work straight to `master`.

## Releases (GitHub Releases)

This repo uses [GitHub Releases](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases).

CI (`.github/workflows/ci.yml`) already:

| Event | What happens |
|-------|----------------|
| Push / PR to `master` | Debug build + `--self-test` + artifact upload (**version `0.0.0-dev`**) |
| Push tag `v*` (e.g. `v0.3.2`) | Same, with `-DQP_RELEASE_VERSION=0.3.2`; RelWithDebInfo zip + GitHub Release |

### Cut a release

After the changes you want are on `master` — **do not edit CMake for the version**:

```bat
git checkout master
git pull origin master
git tag v0.3.2
git push origin v0.3.2
```

Then check:

- Actions: build + package must be green  
- https://github.com/summeroff/qiuckprompts/releases — new release `v0.3.2`  
- Asset: `qiuckprompts-v0.3.2-win-x64.zip` (`qiuckprompts.exe`, PDB, README, LICENSE, config)

Tag names must match `v*` (semver recommended: `v0.3.2`, `v1.0.0`).

Optional: edit the auto-generated release notes on GitHub after CI finishes.

### Version numbers

| Source | Role |
|--------|------|
| Git tag `vX.Y.Z` | **Only** release version knob |
| CMake `-DQP_RELEASE_VERSION=` | CI sets from the tag (no leading `v`) |
| Default `0.0.0-dev` | Local builds, PRs, untagged CI — never looks like a release |
| Git short SHA | Always appended as `+g<12hex>` (and `.dirty` if the tree is dirty) |
| `cmake/version_build.h.in` → generated header | Feeds C++ (`QP_VERSION_STRING`, `QP_GIT_HASH`) and `resources/app.rc` |

Examples: `0.0.0-dev+g5cbcf2531f0a.dirty` · `0.3.2+g5cbcf2531f0a`

Do **not** bump a version in `CMakeLists.txt` for shipping. Local Debug shows the `0.0.0-dev+g…` form in tray/About/logs.

### Crashes

Unhandled SEH / purecall / invalid parameter / `std::terminate` append a stack walk to the log file (`logs/qiuckprompts.log`) via dbghelp when PDBs are present. No Sentry yet — grep `CRASH` in the log.

**Dev-only tools** (`--crash-test`, …) are compiled **only into Debug** (CMake `QP_ENABLE_DEV_TOOLS`). Production **RelWithDebInfo** tag zips strip them. Override locally with `-DQP_DEV_TOOLS_IN_RELWITHDEBINFO=ON` if you need the flag on a Rel build.

`--crash-test` starts the app normally (tray/hotkeys), then a **worker thread** crashes after ~2s through a deep call ladder (`CrashLadder::Rung0…Rung7`) so the log stack is multi-frame.

