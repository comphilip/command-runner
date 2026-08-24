from __future__ import annotations

import asyncio
import codecs
import locale
import os
import queue
import shlex
import signal
import subprocess
import threading
from collections import deque
from collections.abc import Coroutine
from dataclasses import dataclass, field
from itertools import count
from pathlib import Path
from typing import Any

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
    process: asyncio.subprocess.Process | None = None
    pid: int | None = None
    exit_code: int | None = None
    generation: int = 0
    job: WindowsJob | None = None
    stdout: deque[LogLine] = field(default_factory=lambda: deque(maxlen=1000))
    stderr: deque[LogLine] = field(default_factory=lambda: deque(maxlen=1000))
    combined: deque[LogLine] = field(default_factory=lambda: deque(maxlen=1000))
    cleared_through: int = 0


class ProcessManager:
    """Manage every child process on one shared asyncio worker thread."""

    def __init__(self) -> None:
        self.runtimes: dict[str, Runtime] = {}
        self.events: queue.Queue[ProcessEvent] = queue.Queue()
        self._sequence = count(1)
        self._lock = threading.RLock()
        self._loop: asyncio.AbstractEventLoop | None = None
        self._loop_ready = threading.Event()
        self._closed = False
        self._thread = threading.Thread(
            target=self._run_loop,
            name="command-runner-asyncio",
            daemon=True,
        )
        self._thread.start()
        self._loop_ready.wait()

    def _run_loop(self) -> None:
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        self._loop = loop
        loop.call_soon(self._keep_loop_responsive)
        self._loop_ready.set()
        loop.run_forever()
        loop.close()

    def _keep_loop_responsive(self) -> None:
        """Bound wake-up latency where cross-thread loop wakeups are restricted."""
        if not self._closed and self._loop is not None:
            self._loop.call_later(0.1, self._keep_loop_responsive)

    def _submit(self, coroutine: Coroutine[Any, Any, object]) -> None:
        if self._closed:
            coroutine.close()
            return
        assert self._loop is not None
        asyncio.run_coroutine_threadsafe(coroutine, self._loop)

    def close(self) -> None:
        """Release the worker after all managed commands have stopped."""
        with self._lock:
            if self._closed:
                return
            self._closed = True
        loop = self._loop
        assert loop is not None
        shutdown = asyncio.run_coroutine_threadsafe(self._shutdown_loop(), loop)
        try:
            shutdown.result(timeout=2.0)
        except TimeoutError:
            loop.call_soon_threadsafe(loop.stop)
        if threading.current_thread() is not self._thread:
            self._thread.join(timeout=2.0)

    async def _shutdown_loop(self) -> None:
        current = asyncio.current_task()
        tasks = [task for task in asyncio.all_tasks() if task is not current]
        for task in tasks:
            task.cancel()
        if tasks:
            await asyncio.gather(*tasks, return_exceptions=True)
        assert self._loop is not None
        self._loop.call_soon(self._loop.stop)

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
            if self._closed or runtime.state in {
                State.STARTING,
                State.RUNNING,
                State.STOPPING,
            }:
                return
            runtime.state = State.STARTING
            runtime.exit_code = None
            runtime.generation += 1
            generation = runtime.generation
            self.events.put(StateChanged(config.id))
        self._submit(self._start_process(config, generation))

    async def _start_process(self, config: CommandConfig, generation: int) -> None:
        runtime = self.runtime(config.id)
        try:
            cwd = Path(config.working_directory).expanduser()
            if not cwd.is_dir():
                raise FileNotFoundError(f"Working directory does not exist: {cwd}")
            encoding = self._encoding(config.encoding)
            codecs.lookup(encoding)
            command = self._split_command(config.command_line)
            flags = 0
            if os.name == "nt":
                flags = subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.CREATE_NO_WINDOW
            process = await asyncio.create_subprocess_exec(
                *command,
                cwd=str(cwd),
                stdin=subprocess.DEVNULL,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
                creationflags=flags,
                start_new_session=os.name != "nt",
            )
            job = self._create_job(config.id, process)
            with self._lock:
                if runtime.generation != generation or self._closed:
                    process.terminate()
                    if job:
                        job.close()
                    return
                runtime.process, runtime.job = process, job
                runtime.pid, runtime.state = process.pid, State.RUNNING
            self.events.put(StateChanged(config.id))
            asyncio.create_task(
                self._monitor_process(config.id, generation, process, encoding)
            )
        except Exception as exc:
            self._append_log(
                config.id, "stderr", f"[Command Runner] Failed to start: {exc}"
            )
            with self._lock:
                if runtime.generation != generation:
                    return
                runtime.state, runtime.exit_code = State.FAILED, -1
            self.events.put(StateChanged(config.id))

    def _split_command(self, command_line: str) -> list[str]:
        if os.name != "nt":
            return shlex.split(command_line)

        import ctypes
        from ctypes import wintypes

        shell32 = ctypes.WinDLL("shell32", use_last_error=True)
        shell32.CommandLineToArgvW.argtypes = [
            wintypes.LPCWSTR,
            ctypes.POINTER(ctypes.c_int),
        ]
        shell32.CommandLineToArgvW.restype = ctypes.POINTER(wintypes.LPWSTR)
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.LocalFree.argtypes = [wintypes.HLOCAL]
        kernel32.LocalFree.restype = wintypes.HLOCAL
        argc = ctypes.c_int()
        argv = shell32.CommandLineToArgvW(command_line, ctypes.byref(argc))
        if not argv:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            return [argv[index] for index in range(argc.value)]
        finally:
            kernel32.LocalFree(argv)

    def _create_job(
        self, command_id: str, process: asyncio.subprocess.Process
    ) -> WindowsJob | None:
        if os.name != "nt":
            return None
        job = None
        try:
            job = WindowsJob()
            transport = getattr(process, "_transport")
            raw_process = transport.get_extra_info("subprocess")
            job.assign(int(getattr(raw_process, "_handle")))
            return job
        except Exception as exc:
            if job:
                job.close()
            self._append_log(
                command_id,
                "stderr",
                "[Command Runner] Job Object unavailable; using taskkill as a "
                f"fallback: {exc}",
            )
            return None

    async def _monitor_process(
        self,
        command_id: str,
        generation: int,
        process: asyncio.subprocess.Process,
        encoding: str,
    ) -> None:
        readers = (
            asyncio.create_task(
                self._read_pipe(
                    command_id, generation, "stdout", process.stdout, encoding
                )
            ),
            asyncio.create_task(
                self._read_pipe(
                    command_id, generation, "stderr", process.stderr, encoding
                )
            ),
        )
        code = await process.wait()
        self._finalize_process(command_id, generation, process, code)
        await asyncio.gather(*readers, return_exceptions=True)

    async def _read_pipe(
        self,
        command_id: str,
        generation: int,
        stream: str,
        pipe: asyncio.StreamReader | None,
        encoding: str,
    ) -> None:
        if pipe is None:
            return
        decoder = codecs.getincrementaldecoder(encoding)(errors="replace")
        buffered = ""
        while True:
            chunk = await pipe.read(8192)
            if not chunk:
                buffered += decoder.decode(b"", final=True)
                if buffered and self.runtime(command_id).generation == generation:
                    self._append_log(command_id, stream, buffered)
                return
            buffered += decoder.decode(chunk)
            while "\n" in buffered:
                line, buffered = buffered.split("\n", 1)
                if self.runtime(command_id).generation != generation:
                    return
                self._append_log(command_id, stream, line)

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

    def _finalize_process(
        self,
        command_id: str,
        generation: int,
        process: asyncio.subprocess.Process,
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
            process = runtime.process
            if not process or process.returncode is not None:
                return
            runtime.state = State.STOPPING
            job, generation = runtime.job, runtime.generation
        self.events.put(StateChanged(command_id))
        self._submit(self._stop_process(command_id, generation, process, job, timeout))

    async def _stop_process(
        self,
        command_id: str,
        generation: int,
        process: asyncio.subprocess.Process,
        job: WindowsJob | None,
        timeout: float,
    ) -> bool:
        try:
            if os.name == "nt":
                process.send_signal(signal.CTRL_BREAK_EVENT)
            else:
                os.killpg(process.pid, signal.SIGTERM)
            code = await asyncio.wait_for(asyncio.shield(process.wait()), timeout)
            self._finalize_process(command_id, generation, process, code)
            return True
        except (OSError, ProcessLookupError, asyncio.TimeoutError):
            pass

        try:
            if os.name == "nt":
                killer = await asyncio.create_subprocess_exec(
                    "taskkill",
                    "/PID",
                    str(process.pid),
                    "/T",
                    "/F",
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )
                await killer.wait()
                if job:
                    job.terminate()
            else:
                os.killpg(process.pid, signal.SIGKILL)
        except (OSError, ProcessLookupError):
            pass

        try:
            code = await asyncio.wait_for(
                asyncio.shield(process.wait()), max(1.0, timeout)
            )
        except asyncio.TimeoutError:
            try:
                process.kill()
                code = await asyncio.wait_for(asyncio.shield(process.wait()), 2.0)
            except (OSError, ProcessLookupError, asyncio.TimeoutError):
                self._stop_failed(command_id, generation, process)
                return False
        self._finalize_process(command_id, generation, process, code)
        return True

    def _stop_failed(
        self,
        command_id: str,
        generation: int,
        process: asyncio.subprocess.Process,
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
        job: WindowsJob | None = None
        generation = 0
        with self._lock:
            runtime = self.runtime(config.id)
            process = runtime.process
            if not process or process.returncode is not None:
                process = None
            else:
                runtime.state = State.STOPPING
                job, generation = runtime.job, runtime.generation
        if process is None:
            self.start(config)
            return
        self.events.put(StateChanged(config.id))
        self._submit(self._restart_process(config, generation, process, job))

    async def _restart_process(
        self,
        config: CommandConfig,
        generation: int,
        process: asyncio.subprocess.Process,
        job: WindowsJob | None,
    ) -> None:
        stopped = await self._stop_process(
            config.id, generation, process, job, timeout=4.0
        )
        if stopped:
            self.start(config)

    def running_ids(self) -> list[str]:
        with self._lock:
            return [
                key
                for key, value in self.runtimes.items()
                if value.process is not None and value.process.returncode is None
            ]
