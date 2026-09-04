# Command Runner

A desktop command runner for Windows 11. It can save multiple commands, start,
stop, and restart them in batches, and display stdout, stderr, or combined logs
in real time. Each log view retains up to the latest 1,000 lines.

## Features

- Add, edit, and delete multiple command configurations
- Start, stop, and restart multiple selected commands
- Shell and direct execution modes
- stdout, stderr, and combined log views
- Word wrapping, intelligent automatic scrolling, and jump to latest
- Atomic configuration saves to `%LOCALAPPDATA%\CommandRunner\commands.json`
- Minimize to the system tray
- When commands are running, closing the window offers three choices: stop
  commands and exit, cancel, or minimize to the system tray
- Process-tree management using Windows Job Objects; stopping first attempts a
  graceful exit, then forces termination after a timeout

Commands do not support interactive stdin. To view output from a Python
subprocess in real time, use `python -u`.

## Building on Windows

Use a Visual Studio 2022/2026 x64 developer shell with CMake and vcpkg
available. Set `VCPKG_ROOT` to the vcpkg checkout, then run:

```powershell
# debug build
cmake --preset windows-x64-debug
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug --output-on-failure

# relesea build
cmake --preset windows-x64-release
cmake --build --preset windows-x64-release
ctest --preset windows-x64-release --output-on-failure
```

## Automated GitHub Releases

When a tag matching `v*` is pushed, GitHub Actions configures the native x64
Release preset, runs the C++ regression tests, verifies the PE artifact, saves
the PDB as a build artifact, creates a GitHub Release with the same name, and
uploads the EXE.

Before publishing, add a level-two heading to `CHANGELOG.md` that exactly
matches the tag:

```markdown
## v1.2.0

### Added

- xxx

### Fixed

- yyy
```

The workflow uses only the content between that version heading and the next
`##` heading as the release notes. If the matching section cannot be found or
is empty, the workflow stops without creating a release.

Example release:

```bash
git tag v1.2.0
git push origin v1.2.0
```