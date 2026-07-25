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
| Push / PR to `master` | Debug build + `--self-test` + artifact upload |
| Push tag `v*` (e.g. `v0.2.0`) | RelWithDebInfo build, zip, **create GitHub Release** with the zip attached |

### Cut a release

After the changes you want are on `master`:

```bat
git checkout master
git pull origin master
git tag v0.2.0
git push origin v0.2.0
```

Then check:

- Actions: build + package must be green  
- https://github.com/summeroff/qiuckprompts/releases — new release `v0.2.0`  
- Asset: `qiuckprompts-v0.2.0-win-x64.zip` (`qiuckprompts.exe`, PDB, README, LICENSE)

Tag names must match `v*` (semver recommended: `v0.2.0`, `v1.0.0`).

Optional: create the release notes on GitHub UI after CI generates them, or edit the release body.

### Version numbers

- CMake `project(... VERSION ...)` in `CMakeLists.txt`  
- Tag name used for the zip and Release title  
- Keep them aligned when you ship (e.g. tag `v0.2.0` when project version is `0.2.0`)
