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
.\build-windows.ps1
```

The script configures and builds the native C++23 Release preset, runs the
Windows regression tests, checks the PE architecture/imports and version
metadata, and reports the unsigned artifact size. The output is located at:

```text
out\build\windows-x64-release\Release\CommandRunner.exe
```

Use `-Configuration Debug` for a debug build or `-SkipTests` when iterating on
the executable. Release signing is opt-in and uses a certificate in the
current user's certificate store:

```powershell
.\build-windows.ps1 -CertificateThumbprint "..."
```

The description, file version, product name, product version, copyright, and
language shown in Windows file properties are defined in
`resources/CommandRunner.rc`. Update the resource version together with the
Git tag and `CHANGELOG.md` entry.

## Native C++ migration (phase five)

The native migration uses C++23, CMake, vcpkg manifest mode, MSVC, WIL, and
nlohmann/json. Phases three and four add the native main window with an action
bar, native add/edit/delete and close-confirmation dialogs, a Win32 system-tray
icon, and
multi-select ListView, draggable vertical splitter, log filtering controls,
RichEdit output, keyboard navigation, and DPI-aware responsive layout. The
Windows process manager from phase two continues to provide one shared IOCP
worker for stdout/stderr capture, Job Object process-tree cleanup, and direct
or shell command execution. The checked-in preset targets the installed Visual
Studio 18 toolset; a Visual Studio 2022 installation can use the same CMake
targets after selecting its generator/toolset. Set `VCPKG_ROOT` to a vcpkg
checkout, then configure and build the x64 Debug preset:

```powershell
cmake --preset windows-x64-debug
cmake --build --preset windows-x64-debug
ctest --test-dir out/build/windows-x64-debug -C Debug --output-on-failure
```

`CommandRunner.sln` is provided for Visual Studio users. The application state
survives repeated main-window destruction and recreation while minimized to the
system tray. `scripts/verify-release.ps1` also checks that a release does not
accidentally depend on Python, the .NET runtime, or a dynamic MSVC runtime.

Phase 5 compatibility validation must still be completed on a real Windows 11
x64 machine: move the window between 100%, 125%, 150%, 175%, and 200% DPI
monitors; exercise keyboard access keys and a screen reader; restart Explorer
and confirm the tray icon returns; and inspect the signed artifact with the
organization's antivirus submission process. These checks require an
interactive Windows desktop and are not replaced by the headless CI tests.

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

## Usage Notes

“Run through Shell” is suitable for `.bat` and `.cmd` files, pipes,
redirections, and `&&`. “Run directly” is suitable for ordinary executables and
their arguments. Commands run with the current user's permissions. Do not
import and execute untrusted configurations.

The native executable has no Python, .NET, or third-party GUI runtime
dependency. Windows tray, DPI, Job Object, and accessibility behavior must be
validated on Windows 11 x64.
