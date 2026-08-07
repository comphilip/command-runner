from __future__ import annotations

import locale
import os
import queue
import shlex
import signal
import subprocess
import threading
from collections import deque
from dataclasses import dataclass, field
from itertools import count
from pathlib import Path

from .models import (
    CommandConfig,
    LogAdded,
    LogLine,
    ProcessEvent,
    RuntimeSnapshot,
    State,
    StateChanged,
)
from .windows_job import WindowsJob


@dataclass
class Runtime:
    state: State = State.STOPPED
    process: subprocess.Popen[str] | None = None
    pid: int | None = None
    exit_code: int | None = None
    generation: int = 0
    job: WindowsJob | None = None
    stdout: deque[LogLine] = field(default_factory=lambda: deque(maxlen=1000))
    stderr: deque[LogLine] = field(default_factory=lambda: deque(maxlen=1000))
    combined: deque[LogLine] = field(default_factory=lambda: deque(maxlen=1000))
    cleared_through: int = 0


class ProcessManager:
    def __init__(self) -> None:
        self.runtimes: dict[str, Runtime] = {}
        self.events: queue.Queue[ProcessEvent] = queue.Queue()
        self._sequence = count(1)
        self._lock = threading.RLock()

    def runtime(self, command_id: str) -> Runtime:
        return self.runtimes.setdefault(command_id, Runtime())

    def snapshot(self, command_id: str) -> RuntimeSnapshot:
        with self._lock:
            runtime = self.runtime(command_id)
            return RuntimeSnapshot(
                state=runtime.state,
                pid=runtime.pid,
                exit_code=runtime.exit_code,
                stdout=tuple(runtime.stdout),
                stderr=tuple(runtime.stderr),
                combined=tuple(runtime.combined),
                cleared_through=runtime.cleared_through,
            )

    def drain_events(self) -> list[ProcessEvent]:
        events: list[ProcessEvent] = []
        while True:
            try:
                events.append(self.events.get_nowait())
            except queue.Empty:
                return events

    def _encoding(self, value: str) -> str:
        return {
            "auto": locale.getpreferredencoding(False),
            "system": locale.getpreferredencoding(False),
            "gbk": "gbk",
            "utf-8": "utf-8",
        }.get(value.lower(), value)

    def start(self, config: CommandConfig) -> None:
        with self._lock:
            runtime = self.runtime(config.id)
            if runtime.state in {State.STARTING, State.RUNNING, State.STOPPING}:
                return
            runtime.state = State.STARTING
            runtime.exit_code = None
            runtime.generation += 1
            generation = runtime.generation
            self.events.put(StateChanged(config.id))
        threading.Thread(
            target=self._start_worker, args=(config, generation), daemon=True
        ).start()

    def _start_worker(self, config: CommandConfig, generation: int) -> None:
        runtime = self.runtime(config.id)
        try:
            cwd = Path(config.working_directory).expanduser()
            if not cwd.is_dir():
                raise FileNotFoundError(f"Working directory does not exist: {cwd}")
            if os.name == "nt":
                flags = subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.CREATE_NO_WINDOW
            else:
                flags = 0
            command = config.command_line
            if os.name != 'nt':
                command = shlex.split(config.command_line)
            process = subprocess.Popen(
                command,
                cwd=str(cwd),
                shell=False,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding=self._encoding(config.encoding),
                errors="replace",
                bufsize=1,
                creationflags=flags,
                start_new_session=os.name != "nt",
            )
            job = None
            if os.name == "nt":
                try:
                    job = WindowsJob()
                    job.assign(int(process._handle))  # type: ignore[attr-defined]
                except Exception as exc:
                    if job:
                        job.close()
                    job = None
                    self._append_log(
                        config.id, "stderr",
                        f"[Command Runner] Job Object unavailable; using taskkill as a fallback: {exc}",
                    )
            with self._lock:
                if runtime.generation != generation:
                    process.terminate()
                    return
                runtime.process, runtime.job = process, job
                runtime.pid, runtime.state = process.pid, State.RUNNING
            self.events.put(StateChanged(config.id))
            for stream_name, pipe in (("stdout", process.stdout), ("stderr", process.stderr)):
                threading.Thread(
                    target=self._read_pipe,
                    args=(config.id, generation, stream_name, pipe),
                    daemon=True,
                ).start()
            threading.Thread(
                target=self._wait, args=(config.id, generation, process), daemon=True
            ).start()
        except Exception as exc:
            self._append_log(config.id, "stderr", f"[Command Runner] Failed to start: {exc}")
            with self._lock:
                runtime.state, runtime.exit_code = State.FAILED, -1
            self.events.put(StateChanged(config.id))

    def _read_pipe(self, command_id: str, generation: int, stream: str, pipe) -> None:
        if pipe is None:
            return
        try:
            for line in iter(pipe.readline, ""):
                if self.runtime(command_id).generation != generation:
                    break
                self._append_log(command_id, stream, line)
        finally:
            pipe.close()

    def _append_log(self, command_id: str, stream: str, text: str) -> None:
        line = LogLine.create(next(self._sequence), stream, text)
        with self._lock:
            runtime = self.runtime(command_id)
            getattr(runtime, stream).append(line)
            runtime.combined.append(line)
        self.events.put(LogAdded(command_id, line))

    def clear_logs(self, command_id: str) -> None:
        with self._lock:
            runtime = self.runtime(command_id)
            if runtime.combined:
                runtime.cleared_through = runtime.combined[-1].sequence
            runtime.stdout.clear()
            runtime.stderr.clear()
            runtime.combined.clear()

    def _wait(self, command_id: str, generation: int, process: subprocess.Popen) -> None:
        code = process.wait()
        self._finalize_process(command_id, generation, process, code)

    def _finalize_process(
        self,
        command_id: str,
        generation: int,
        process: subprocess.Popen,
        code: int,
    ) -> None:
        with self._lock:
            runtime = self.runtime(command_id)
            if runtime.generation != generation or runtime.process is not process:
                return
            was_stopping = runtime.state == State.STOPPING
            runtime.exit_code, runtime.pid, runtime.process = code, None, None
            runtime.state = State.STOPPED if was_stopping else (
                State.EXITED if code == 0 else State.FAILED
            )
            if runtime.job:
                runtime.job.close()
                runtime.job = None
        self.events.put(StateChanged(command_id))

    def stop(self, command_id: str, timeout: float = 4.0) -> None:
        with self._lock:
            runtime = self.runtime(command_id)
            if not runtime.process or runtime.process.poll() is not None:
                return
            runtime.state = State.STOPPING
            process, job, generation = runtime.process, runtime.job, runtime.generation
        self.events.put(StateChanged(command_id))
        threading.Thread(
            target=self._stop_worker,
            args=(command_id, generation, process, job, timeout),
            daemon=True,
        ).start()

    def _stop_worker(self, command_id, generation, process, job, timeout) -> None:
        try:
            if os.name == "nt":
                process.send_signal(signal.CTRL_BREAK_EVENT)
            else:
                os.killpg(process.pid, signal.SIGTERM)
            code = process.wait(timeout=timeout)
            self._finalize_process(command_id, generation, process, code)
            return
        except (OSError, subprocess.SubprocessError):
            pass

        try:
            if os.name == "nt":
                # Always ask Windows to kill the complete PID tree. A child can
                # start before the shell has been assigned to the Job Object.
                subprocess.run(
                    ["taskkill", "/PID", str(process.pid), "/T", "/F"],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False,
                )
                # Also terminate the Job Object to catch descendants that have
                # already been assigned but are no longer reachable from the PID.
                if job:
                    job.terminate()
            else:
                os.killpg(process.pid, signal.SIGKILL)
        except (OSError, subprocess.SubprocessError):
            pass

        try:
            code = process.wait(timeout=max(1.0, timeout))
        except subprocess.TimeoutExpired:
            try:
                process.kill()
                code = process.wait(timeout=2.0)
            except (OSError, subprocess.SubprocessError):
                self._stop_failed(command_id, generation, process)
                return
        self._finalize_process(command_id, generation, process, code)

    def _stop_failed(
        self, command_id: str, generation: int, process: subprocess.Popen
    ) -> None:
        with self._lock:
            runtime = self.runtime(command_id)
            if (
                runtime.generation != generation
                or runtime.process is not process
                or runtime.state != State.STOPPING
            ):
                return
            runtime.state = State.RUNNING
        self._append_log(
            command_id,
            "stderr",
            "[Command Runner] Unable to stop the command process.",
        )
        self.events.put(StateChanged(command_id))

    def restart(self, config: CommandConfig) -> None:
        runtime = self.runtime(config.id)
        if runtime.process and runtime.process.poll() is None:
            self.stop(config.id)
            threading.Thread(target=self._restart_wait, args=(config,), daemon=True).start()
        else:
            self.start(config)

    def _restart_wait(self, config: CommandConfig) -> None:
        process = self.runtime(config.id).process
        if process:
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                return
        self.start(config)

    def running_ids(self) -> list[str]:
        return [
            key for key, value in self.runtimes.items()
            if value.process is not None and value.process.poll() is None
        ]
