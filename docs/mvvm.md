# Refactor Command Runner to Tkinter MVVM

## Summary

Perform a single coordinated rewrite that preserves current behavior while separating:

- **Model:** command configuration, persistence, process execution, runtime state, and log data.
- **ViewModel:** application state, validation, action availability, process-event handling, and UI commands.
- **View:** widget construction, dialogs, window lifecycle, and Tkinter-specific presentation.

ViewModels expose Tkinter variables and receive a Tk master explicitly, allowing tests to use `tkinter.Tcl()` without opening windows.

`BooleanVar` cannot bind directly to `ttk.Button.state`. A reusable View binding traces the variable and translates `True/False` into `!disabled/disabled`. Button actions continue to bind through `command=`.

## Implementation Changes

### MVVM infrastructure

- Add `bind_enabled(widget, BooleanVar)`, including initial synchronization and cleanup.
- Keep Tk variables and traces in ViewModels; keep widgets in Views.
- Define ViewModel commands as ordinary methods suitable for Tkinter `command` callbacks.
- Keep dialogs, message boxes, filedialogs, and widget manipulation out of ViewModels.

### Main window

Create `MainWindowViewModel`, injected with a Tk master, `ConfigStore`, and `ProcessManager`. It owns commands, projected rows, selection, preferences, log filtering, action availability, persistence, process-event projection, and shutdown state.

Use immutable presentation records for command rows and formatted log rows. Collection revision variables notify the View when Treeview or Text projections change.

Reduce `MainWindow` to widget construction, bindings, rendering projections, scheduling event polling, opening dialogs, displaying messages, and window/tray presentation.

### Dialogs and tray

- Add `CommandDialogViewModel` with form variables, normalization, validation, and result creation.
- Keep `CommandDialog` responsible only for controls, directory selection, validation display, and window lifetime.
- Keep `CloseDialog` presentation-only and replace string results with a `CloseAction` enum.
- Keep `TrayManager` as an infrastructure adapter wired through Tk's dispatcher.

### Model and service boundaries

- Retain `CommandConfig`, `LogLine`, `State`, `ConfigStore`, and `ProcessManager`.
- Add read-only runtime snapshots and typed process events.
- Keep queue details inside `ProcessManager` through an event-drain method.
- Add typed preferences while preserving the existing JSON format.
- Preserve process behavior and the 1,000-line retention limit.
- Use `app.py` as the composition root.

## Public Interfaces and Types

- `MainWindowViewModel(master, store, process_manager)`
- `CommandDialogViewModel(master, initial=None)`
- `Preferences(wrap_lines=False, auto_scroll=True)`
- `CommandRow(id, name, state, pid, exit_code, working_directory)`
- `RuntimeSnapshot`
- `StateChanged` and `LogAdded`
- `CloseAction`
- `bind_enabled()`

## Test Plan

- Test ViewModels headlessly with `tkinter.Tcl()` and injected fake services.
- Verify loading, preferences, persistence errors, selection, action availability, command dispatch, log projection, filtering, and clearing.
- Verify command-dialog validation, normalization, ID preservation, and ID generation.
- Verify `bind_enabled` synchronization and cleanup.
- Retain existing persistence, process, and accessibility tests.
- Run `pytest`, `compileall`, and Windows smoke tests for tray, Job Objects, close behavior, and process-tree termination.

## Assumptions

- Preserve the current layout, mnemonics, configuration schema, process behavior, log format, and tray behavior.
- Add no third-party MVVM library.
- Tkinter variables in ViewModels are intentional; tests supply `tkinter.Tcl()` as master.
- Treeview and Text projections use revision variables because Tkinter has no native collection binding.
- Views own all dialogs and visual operations.
