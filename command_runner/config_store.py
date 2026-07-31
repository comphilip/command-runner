from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any

from .models import CommandConfig, Preferences


def default_config_path() -> Path:
    if os.name == "nt":
        root = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData/Local"))
    else:
        root = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
    return root / "CommandRunner" / "commands.json"


class ConfigStore:
    def __init__(self, path: Path | None = None) -> None:
        self.path = path or default_config_path()

    def load(self) -> tuple[list[CommandConfig], Preferences]:
        if not self.path.exists():
            return [], Preferences()
        try:
            payload = json.loads(self.path.read_text(encoding="utf-8"))
            commands = []
            for item in payload.get("commands", []):
                item = dict(item)
                item.pop("execution_mode", None)
                commands.append(CommandConfig(**item))
            prefs = payload.get("preferences", {})
            return commands, Preferences(
                wrap_lines=bool(prefs.get("wrap_lines", False)),
                auto_scroll=bool(prefs.get("auto_scroll", True)),
            )
        except (OSError, ValueError, TypeError) as exc:
            raise ValueError(f"Unable to read configuration file {self.path}: {exc}") from exc

    def save(
        self,
        commands: list[CommandConfig],
        preferences: Preferences | dict[str, Any],
    ) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.path.with_suffix(".json.tmp")
        payload = {
            "version": 1,
            "commands": [item.to_dict() for item in commands],
            "preferences": (
                preferences.to_dict()
                if isinstance(preferences, Preferences)
                else preferences
            ),
        }
        temporary.write_text(
            json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8"
        )
        os.replace(temporary, self.path)
