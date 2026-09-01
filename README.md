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

## Running in a Windows Development Environment

```powershell
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
.\.venv\Scripts\python.exe app.py
```

If you use the Microsoft Store version of Python, you can replace `py -3` with
`python`.

## Building on Windows

After copying the entire directory to Windows, double-click
`build-windows.bat`, or run:

```powershell
.\build-windows.ps1
```

The output is located at:

```text
dist\CommandRunner.exe
```

The current spec produces a single-file application. If UPX is installed on the
Windows build machine, PyInstaller further compresses supported binary
components. PyInstaller is not a cross-compiler, so the production Windows EXE
should be built on Windows (or with Windows Python under Wine). Because this
project uses the system tray and Job Objects, always test the final build on a
real Windows 11 system.

The description, file version, product name, product version, copyright, and
language shown in Windows file properties are defined in `version_info.txt`.
When releasing a new version, update `filevers`, `prodvers`, `FileVersion`, and
`ProductVersion` together.

## Native C++ migration (phase four)

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
system tray.

## Automated GitHub Releases

When a tag matching `v*` is pushed, GitHub Actions installs the dependencies on
a Windows VM, builds `CommandRunner.exe` with PyInstaller, creates a GitHub
Release with the same name, and uploads the EXE.

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

Minimizing the application requires `pystray` and Pillow. Both are listed in
`requirements.txt`.

## Validation on Linux

Linux can validate configuration handling, logging, and POSIX process-group
cleanup, but it cannot validate the Windows system tray or Job Objects:

```bash
python3 -m pytest
python3 -m compileall app.py command_runner
```
