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
