from __future__ import annotations

import tkinter as tk
from dataclasses import dataclass
from datetime import datetime
from enum import Enum
from pathlib import Path
from typing import Protocol

from .config_store import ConfigStore
from .models import (
    CommandConfig,
    LogAdded,
    LogLine,
    Preferences,
    RuntimeSnapshot,
    State,
    StateChanged,
)
from .process_manager import ProcessManager


class CloseAction(str, Enum):
    EXIT = "exit"
    CANCEL = "cancel"
    TRAY = "tray"


@dataclass(frozen=True)
class CommandRow:
    id: str
    name: str
    state: State
    pid: int | None
    exit_code: int | None
    working_directory: str
    auto_start: bool


@dataclass(frozen=True)
class DisplayLogLine:
    sequence: int
    stream: str
    text: str


@dataclass(frozen=True)
class ValidationResult:
    value: CommandConfig | None = None
    error: str | None = None

    @property
    def valid(self) -> bool:
        return self.value is not None


class ProcessService(Protocol):
    def snapshot(self, command_id: str) -> RuntimeSnapshot: ...
    def drain_events(self) -> list[StateChanged | LogAdded]: ...
    def start(self, config: CommandConfig) -> None: ...
    def stop(self, command_id: str, timeout: float = 4.0) -> None: ...
    def restart(self, config: CommandConfig) -> None: ...
    def clear_logs(self, command_id: str) -> None: ...
    def running_ids(self) -> list[str]: ...


class ConfigurationService(Protocol):
    def load(self) -> tuple[list[CommandConfig], Preferences]: ...
    def save(
        self, commands: list[CommandConfig], preferences: Preferences
    ) -> None: ...


class MainWindowViewModel:
    INACTIVE = {State.STOPPED, State.EXITED, State.FAILED}

    def __init__(
        self,
        master: tk.Misc,
        store: ConfigurationService | None = None,
        process_manager: ProcessService | None = None,
    ) -> None:
        self.store = store or ConfigStore()
        self.manager = process_manager or ProcessManager()
        self.startup_error: str | None = None
        try:
            self.commands, preferences = self.store.load()
        except ValueError as exc:
            self.commands, preferences = [], Preferences()
            self.startup_error = str(exc)

        self.wrap_lines = tk.BooleanVar(master, preferences.wrap_lines)
        self.auto_scroll = tk.BooleanVar(master, preferences.auto_scroll)
        self.log_view = tk.StringVar(master, "combined")
        self.rows_revision = tk.IntVar(master, 0)
        self.logs_revision = tk.IntVar(master, 0)
        self.action_enabled = {
            name: tk.BooleanVar(master, name == "Add")
            for name in ("Add", "Edit", "Delete", "Start", "Stop", "Restart", "Clear")
        }
        self.selected_ids: tuple[str, ...] = ()
        self.active_id: str | None = None
        self.rows: tuple[CommandRow, ...] = ()
        self.visible_logs: tuple[DisplayLogLine, ...] = ()
        self.exiting = False
        self._log_view_trace = self.log_view.trace_add(
            "write", self._log_view_changed
        )
        self.refresh_rows()

    def dispose(self) -> None:
        try:
            self.log_view.trace_remove("write", self._log_view_trace)
        except tk.TclError:
            pass
        close = getattr(self.manager, "close", None)
        if close is not None:
            close()

    def by_id(self, command_id: str) -> CommandConfig | None:
        return next((item for item in self.commands if item.id == command_id), None)

    def selected(self) -> list[CommandConfig]:
        return [
            item
            for command_id in self.selected_ids
            if (item := self.by_id(command_id)) is not None
        ]

    def set_selection(self, command_ids: tuple[str, ...] | list[str]) -> None:
        self.selected_ids = tuple(command_ids)
        new_active = self.selected_ids[0] if len(self.selected_ids) == 1 else None
        if new_active != self.active_id:
            self.active_id = new_active
            self.refresh_logs()
        self._update_action_availability()

    def add(self, command: CommandConfig) -> str | None:
        self.commands.append(command)
        return self._save_and_refresh()

    def replace_selected(self, command: CommandConfig) -> str | None:
        selected = self.selected()
        if len(selected) != 1 or not self.action_enabled["Edit"].get():
            return "Stop this command before editing it."
        self.commands[self.commands.index(selected[0])] = command
        return self._save_and_refresh()

    def delete_selected(self) -> str | None:
        if not self.action_enabled["Delete"].get():
            return "Stop the selected running commands before deleting them."
        selected_ids = set(self.selected_ids)
        self.commands = [
            item for item in self.commands if item.id not in selected_ids
        ]
        self.set_selection(())
        return self._save_and_refresh()

    def start_selected(self) -> None:
        for item in self.selected():
            if self.manager.snapshot(item.id).state in self.INACTIVE:
                self.manager.start(item)

    def stop_selected(self) -> None:
        for item in self.selected():
            if self.manager.snapshot(item.id).state == State.RUNNING:
                self.manager.stop(item.id)

    def restart_selected(self) -> None:
        for item in self.selected():
            state = self.manager.snapshot(item.id).state
            if state in self.INACTIVE or state == State.RUNNING:
                self.manager.restart(item)

    def start_all(self) -> None:
        for item in self.commands:
            self.manager.start(item)

    def start_automatic(self) -> None:
        for item in self.commands:
            if item.auto_start:
                self.manager.start(item)

    def stop_all(self) -> None:
        for command_id in self.manager.running_ids():
            self.manager.stop(command_id)

    def clear_logs(self) -> None:
        if self.active_id:
            self.manager.clear_logs(self.active_id)
            self.refresh_logs()

    def refresh_rows(self) -> None:
        self.rows = tuple(
            CommandRow(
                id=item.id,
                name=item.name,
                state=(runtime := self.manager.snapshot(item.id)).state,
                pid=runtime.pid,
                exit_code=runtime.exit_code,
                working_directory=item.working_directory,
                auto_start=item.auto_start,
            )
            for item in self.commands
        )
        self.rows_revision.set(self.rows_revision.get() + 1)
        self._update_action_availability()

    def refresh_logs(self) -> None:
        lines: tuple[LogLine, ...] = ()
        if self.active_id:
            runtime = self.manager.snapshot(self.active_id)
            lines = getattr(runtime, self.log_view.get())
        self.visible_logs = tuple(self._display_line(line) for line in lines)
        self.logs_revision.set(self.logs_revision.get() + 1)
        self._update_action_availability()

    def poll_events(self) -> bool:
        rows_dirty = False
        logs_dirty = False
        for event in self.manager.drain_events():
            if isinstance(event, StateChanged):
                rows_dirty = True
            elif isinstance(event, LogAdded) and event.command_id == self.active_id:
                logs_dirty = True
        if rows_dirty:
            self.refresh_rows()
        if logs_dirty:
            self.refresh_logs()
        return self.exiting and not self.manager.running_ids()

    def save_preferences(self) -> str | None:
        return self._save()

    def request_exit(self) -> bool:
        if not self.manager.running_ids():
            return True
        self.exiting = True
        self.stop_all()
        return False

    def _save_and_refresh(self) -> str | None:
        error = self._save()
        self.refresh_rows()
        return error

    def _save(self) -> str | None:
        try:
            self.store.save(
                self.commands,
                Preferences(self.wrap_lines.get(), self.auto_scroll.get()),
            )
        except OSError as exc:
            return str(exc)
        return None

    def _update_action_availability(self) -> None:
        selected = self.selected()
        states = [self.manager.snapshot(item.id).state for item in selected]
        values = {
            "Add": True,
            "Edit": len(selected) == 1 and states[0] in self.INACTIVE,
            "Delete": bool(selected) and all(s in self.INACTIVE for s in states),
            "Start": any(s in self.INACTIVE for s in states),
            "Stop": any(s == State.RUNNING for s in states),
            "Restart": any(s in self.INACTIVE or s == State.RUNNING for s in states),
            "Clear": self.active_id is not None,
        }
        for name, value in values.items():
            self.action_enabled[name].set(value)

    def _log_view_changed(self, *_args: object) -> None:
        self.refresh_logs()

    @staticmethod
    def _display_line(line: LogLine) -> DisplayLogLine:
        stamp = datetime.fromtimestamp(line.timestamp).strftime("%H:%M:%S")
        return DisplayLogLine(
            line.sequence, line.stream, f"{stamp} [{line.stream}] {line.text}\n"
        )


class CommandDialogViewModel:
    def __init__(
        self,
        master: tk.Misc,
        value: CommandConfig | None = None,
    ) -> None:
        self.original_id = value.id if value else ""
        self.name = tk.StringVar(master, value.name if value else "")
        self.working_directory = tk.StringVar(
            master, value.working_directory if value else str(Path.home())
        )
        self.command_line = tk.StringVar(master, value.command_line if value else "")
        self.encoding = tk.StringVar(master, value.encoding if value else "auto")
        self.auto_start = tk.BooleanVar(master, value.auto_start if value else False)
        self.shell = tk.BooleanVar(master, value.shell if value else False)

    def validate(self) -> ValidationResult:
        values = {
            "name": self.name.get().strip(),
            "working_directory": self.working_directory.get().strip(),
            "command_line": self.command_line.get().strip(),
            "encoding": self.encoding.get().strip(),
            "auto_start": self.auto_start.get(),
            "shell": self.shell.get(),
        }
        if not values["name"] or not values["command_line"]:
            return ValidationResult(error="Name and command line are required.")
        if not Path(values["working_directory"]).expanduser().is_dir():
            return ValidationResult(error="The working directory does not exist.")
        return ValidationResult(
            value=CommandConfig(id=self.original_id, **values)
        )
