# Changelog

All notable changes to this project are documented in this file.

## v0.1.2
* change version to 0.1.2.0
* resource saving: remove gui resource on minimal to tray. remvoe tray resource on show main window
* command_dialog.py: using tkinter.simpledialog
* close_dialog.py: using tkinter.simpledialog
* fix pytest error
* migrate to mvvm pattern
* add mvvm.md: mvvm design

## v0.1.1
* change version to 0.1.1.0, translate README.md to English
* toolbar: remove seperator
* toolbar: using compact button group
* fix startup error
* fix pyright errors and warnings
* set `Mnemonic / Access key` to all UI controls
* window: kill process tree
* Fix: click stop button, command stuck at STOPPING state
* Double click in command row in table, open edit dialog if it is ediable
* Turn off Word Wrap by default <br> Add "Clear" to left of "Jump to Latest": clear logs <br> Remove "Execution Mode", only shell supported(need stdout/stderr redirection)
* dialog popup in center of main window
* UI in english

## v0.1.0
- Multiple command configurations with working directory and execution mode.
- Start, stop, and restart actions for single or multiple selections.
- Live stdout, stderr, and combined log views with a 1000-line limit.
- Windows system tray integration and close-window choices.
- Windows process-tree cleanup through Job Objects with a `taskkill` fallback.
- Single-file Windows build with version metadata.
