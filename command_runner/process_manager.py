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

from .models import CommandConfig, LogLine, State
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


class ProcessManager:
    def __init__(self) -> None:
        self.runtimes: dict[str, Runtime] = {}
        self.events: queue.Queue[tuple] = queue.Queue()
        self._sequence = count(1)
        self._lock = threading.RLock()

    def runtime(self, command_id: str) -> Runtime:
        return self.runtimes.setdefault(command_id, Runtime())

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
            self.events.put(("state", config.id))
        threading.Thread(
            target=self._start_worker, args=(config, generation), daemon=True
        ).start()

    def _start_worker(self, config: CommandConfig, generation: int) -> None:
        runtime = self.runtime(config.id)
        try:
            cwd = Path(config.working_directory).expanduser()
            if not cwd.is_dir():
                raise FileNotFoundError(f"Working directory does not exist: {cwd}")
            shell = config.execution_mode == "shell"
            if os.name == "nt":
                args: str | list[str] = config.command_line
                flags = subprocess.CREATE_NEW_PROCESS_GROUP
            else:
                args = config.command_line if shell else shlex.split(config.command_line)
                flags = 0
            process = subprocess.Popen(
                args,
                cwd=str(cwd),
                shell=shell,
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
            self.events.put(("state", config.id))
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
            self.events.put(("state", config.id))

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
        self.events.put(("log", command_id, line))

    def _wait(self, command_id: str, generation: int, process: subprocess.Popen) -> None:
        code = process.wait()
        with self._lock:
            runtime = self.runtime(command_id)
            if runtime.generation != generation:
                return
            was_stopping = runtime.state == State.STOPPING
            runtime.exit_code, runtime.pid, runtime.process = code, None, None
            runtime.state = State.STOPPED if was_stopping else (
                State.EXITED if code == 0 else State.FAILED
            )
            if runtime.job:
                runtime.job.close()
                runtime.job = None
        self.events.put(("state", command_id))

    def stop(self, command_id: str, timeout: float = 4.0) -> None:
        with self._lock:
            runtime = self.runtime(command_id)
            if not runtime.process or runtime.process.poll() is not None:
                return
            runtime.state = State.STOPPING
            process, job, generation = runtime.process, runtime.job, runtime.generation
        self.events.put(("state", command_id))
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
            process.wait(timeout=timeout)
            return
        except (subprocess.TimeoutExpired, PermissionError, ProcessLookupError):
            pass
        try:
            if job:
                job.terminate()
            elif os.name == "nt":
                subprocess.run(
                    ["taskkill", "/PID", str(process.pid), "/T", "/F"],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False,
                )
            else:
                os.killpg(process.pid, signal.SIGKILL)
        except (OSError, ProcessLookupError):
            pass

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
