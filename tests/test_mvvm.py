from pathlib import Path

import pytest

pytest.importorskip("_tkinter")

import tkinter as tk

from command_runner.models import (
    CommandConfig,
    LogAdded,
    LogLine,
    Preferences,
    RuntimeSnapshot,
    State,
    StateChanged,
)
from command_runner.ui.bindings import bind_enabled
from command_runner.viewmodels import CommandDialogViewModel, MainWindowViewModel


def snapshot(
    state=State.STOPPED,
    *,
    pid=None,
    exit_code=None,
    stdout=(),
    stderr=(),
    combined=(),
):
    return RuntimeSnapshot(
        state, pid, exit_code, tuple(stdout), tuple(stderr), tuple(combined), 0
    )


class FakeStore:
    def __init__(self, commands=None, preferences=None):
        self.commands = list(commands or [])
        self.preferences = preferences or Preferences()
        self.saved = []

    def load(self):
        return list(self.commands), self.preferences

    def save(self, commands, preferences):
        self.saved.append((list(commands), preferences))


class FakeManager:
    def __init__(self):
        self.snapshots = {}
        self.events = []
        self.calls = []

    def snapshot(self, command_id):
        return self.snapshots.get(command_id, snapshot())

    def drain_events(self):
        events, self.events = self.events, []
        return events

    def start(self, config):
        self.calls.append(("start", config.id))

    def stop(self, command_id, timeout=4.0):
        self.calls.append(("stop", command_id))

    def restart(self, config):
        self.calls.append(("restart", config.id))

    def clear_logs(self, command_id):
        self.calls.append(("clear", command_id))
        self.snapshots[command_id] = snapshot()

    def running_ids(self):
        return [
            command_id
            for command_id, value in self.snapshots.items()
            if value.state in {State.STARTING, State.RUNNING, State.STOPPING}
        ]


class FakeButton:
    def __init__(self):
        self.states = []

    def state(self, statespec):
        self.states.append(statespec)


def test_bind_enabled_synchronizes_and_can_unbind():
    master = tk.Tcl()
    enabled = tk.BooleanVar(master, False)
    button = FakeButton()

    unbind = bind_enabled(button, enabled)
    assert button.states[-1] == ["disabled"]
    enabled.set(True)
    assert button.states[-1] == ["!disabled"]

    unbind()
    enabled.set(False)
    assert button.states[-1] == ["!disabled"]


def test_main_view_model_selection_and_commands(tmp_path: Path):
    master = tk.Tcl()
    first = CommandConfig("one", str(tmp_path), "echo one")
    second = CommandConfig("two", str(tmp_path), "echo two")
    manager = FakeManager()
    manager.snapshots[second.id] = snapshot(State.RUNNING, pid=123)
    store = FakeStore([first, second])
    view_model = MainWindowViewModel(master, store, manager)

    view_model.set_selection([first.id, second.id])

    assert view_model.action_enabled["Edit"].get() is False
    assert view_model.action_enabled["Delete"].get() is False
    assert view_model.action_enabled["Start"].get() is True
    assert view_model.action_enabled["Stop"].get() is True
    assert view_model.action_enabled["Restart"].get() is True

    view_model.start_selected()
    view_model.stop_selected()
    view_model.restart_selected()
    assert manager.calls == [
        ("start", first.id),
        ("stop", second.id),
        ("restart", first.id),
        ("restart", second.id),
    ]


def test_main_view_model_persistence_and_delete(tmp_path: Path):
    master = tk.Tcl()
    command = CommandConfig("one", str(tmp_path), "echo one")
    store = FakeStore([command])
    view_model = MainWindowViewModel(master, store, FakeManager())
    view_model.set_selection([command.id])

    assert view_model.delete_selected() is None
    assert view_model.commands == []
    assert store.saved[-1][1] == Preferences(False, True)


def test_main_view_model_starts_only_automatic_commands(tmp_path: Path):
    master = tk.Tcl()
    automatic = CommandConfig("auto", str(tmp_path), "echo auto", auto_start=True)
    manual = CommandConfig("manual", str(tmp_path), "echo manual")
    manager = FakeManager()
    view_model = MainWindowViewModel(
        master, FakeStore([automatic, manual]), manager
    )

    view_model.start_automatic()

    assert manager.calls == [("start", automatic.id)]
    assert [row.auto_start for row in view_model.rows] == [True, False]


def test_main_view_model_projects_typed_log_events(tmp_path: Path):
    master = tk.Tcl()
    command = CommandConfig("one", str(tmp_path), "echo one")
    manager = FakeManager()
    line = LogLine(1, 0, "stdout", "hello")
    manager.snapshots[command.id] = snapshot(stdout=(line,), combined=(line,))
    view_model = MainWindowViewModel(master, FakeStore([command]), manager)
    view_model.set_selection([command.id])

    assert view_model.visible_logs[0].text.endswith("[stdout] hello\n")
    view_model.log_view.set("stderr")
    assert view_model.visible_logs == ()

    manager.snapshots[command.id] = snapshot(State.RUNNING, pid=10)
    manager.events = [StateChanged(command.id), LogAdded(command.id, line)]
    view_model.poll_events()
    assert view_model.rows[0].state == State.RUNNING


def test_command_dialog_view_model_validates_and_preserves_id(tmp_path: Path):
    master = tk.Tcl()
    command = CommandConfig("old", str(tmp_path), "echo old")
    view_model = CommandDialogViewModel(master, command)
    view_model.name.set("  new  ")
    view_model.command_line.set("  echo new  ")
    view_model.auto_start.set(True)

    result = view_model.validate()

    assert result.valid
    assert result.value is not None
    assert result.value.id == command.id
    assert result.value.name == "new"
    assert result.value.command_line == "echo new"
    assert result.value.auto_start is True


def test_command_dialog_view_model_rejects_invalid_values(tmp_path: Path):
    master = tk.Tcl()
    view_model = CommandDialogViewModel(master)
    view_model.working_directory.set(str(tmp_path / "missing"))

    assert view_model.validate().error == "Name and command line are required."

    view_model.name.set("demo")
    view_model.command_line.set("echo demo")
    assert view_model.validate().error == "The working directory does not exist."
