import sys
import time
from pathlib import Path

from command_runner.config_store import ConfigStore
from command_runner.models import CommandConfig, Preferences, State
from command_runner.process_manager import ProcessManager


def wait_until(predicate, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.03)
    raise AssertionError("timed out")


def test_config_round_trip(tmp_path: Path):
    store = ConfigStore(tmp_path / "commands.json")
    command = CommandConfig("demo", str(tmp_path), "echo hello", auto_start=True)
    store.save([command], {"wrap_lines": False, "auto_scroll": True})
    commands, preferences = store.load()
    assert commands == [command]
    assert preferences == Preferences(wrap_lines=False, auto_scroll=True)


def test_capture_stdout_and_stderr(tmp_path: Path):
    manager = ProcessManager()
    code = "import sys;print('out',flush=True);print('err',file=sys.stderr,flush=True)"
    command = CommandConfig(
        "capture", str(tmp_path), f'{sys.executable} -c "{code}"', "utf-8"
    )
    manager.start(command)
    wait_until(lambda: manager.runtime(command.id).state in {State.EXITED, State.FAILED})
    runtime = manager.runtime(command.id)
    assert runtime.exit_code == 0
    assert [line.text for line in runtime.stdout] == ["out"]
    assert [line.text for line in runtime.stderr] == ["err"]
    assert len(runtime.combined) == 2


def test_command_line_is_split_without_shell(tmp_path: Path):
    manager = ProcessManager()
    command = CommandConfig(
        "args",
        str(tmp_path),
        f'{sys.executable} -c "import sys; print(repr(sys.argv[1:]))" "hello world" "&& echo unsafe"',
        "utf-8",
    )
    manager.start(command)
    wait_until(lambda: manager.runtime(command.id).state in {State.EXITED, State.FAILED})
    runtime = manager.runtime(command.id)
    assert runtime.exit_code == 0
    assert runtime.stdout[0].text == "['hello world', '&& echo unsafe']"


def test_stop_process(tmp_path: Path):
    manager = ProcessManager()
    code = "import time;print('ready',flush=True);time.sleep(30)"
    command = CommandConfig(
        "sleeper", str(tmp_path), f'{sys.executable} -c "{code}"', "utf-8"
    )
    manager.start(command)
    wait_until(lambda: manager.runtime(command.id).state == State.RUNNING)
    manager.stop(command.id, timeout=0.2)
    wait_until(lambda: manager.runtime(command.id).state == State.STOPPED)
    assert manager.runtime(command.id).pid is None
