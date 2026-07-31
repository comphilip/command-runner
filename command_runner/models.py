from __future__ import annotations

from dataclasses import asdict, dataclass
from enum import Enum
from time import time
from uuid import uuid4


class State(str, Enum):
    STOPPED = "STOPPED"
    STARTING = "STARTING"
    RUNNING = "RUNNING"
    STOPPING = "STOPPING"
    EXITED = "EXITED"
    FAILED = "FAILED"


@dataclass(frozen=True)
class Preferences:
    wrap_lines: bool = False
    auto_scroll: bool = True

    def to_dict(self) -> dict[str, bool]:
        return asdict(self)


@dataclass
class CommandConfig:
    name: str
    working_directory: str
    command_line: str
    encoding: str = "auto"
    id: str = ""

    def __post_init__(self) -> None:
        if not self.id:
            self.id = str(uuid4())

    def to_dict(self) -> dict:
        return asdict(self)


@dataclass(frozen=True)
class LogLine:
    sequence: int
    timestamp: float
    stream: str
    text: str

    @classmethod
    def create(cls, sequence: int, stream: str, text: str) -> "LogLine":
        return cls(sequence, time(), stream, text.rstrip("\r\n"))


@dataclass(frozen=True)
class RuntimeSnapshot:
    state: State
    pid: int | None
    exit_code: int | None
    stdout: tuple[LogLine, ...]
    stderr: tuple[LogLine, ...]
    combined: tuple[LogLine, ...]
    cleared_through: int


@dataclass(frozen=True)
class StateChanged:
    command_id: str


@dataclass(frozen=True)
class LogAdded:
    command_id: str
    line: LogLine


ProcessEvent = StateChanged | LogAdded
