from __future__ import annotations

from collections.abc import Callable
from typing import Any, Protocol


class Underlined(Protocol):
    def __setitem__(self, key: str, value: object) -> None: ...


class Invokable(Underlined, Protocol):
    def invoke(self) -> object: ...


class Focusable(Protocol):
    def focus_set(self) -> object: ...


def _underline_index(text: str, key: str) -> int:
    """Return the mnemonic's character index, rejecting invalid definitions."""
    if len(key) != 1:
        raise ValueError("A mnemonic must be a single character.")
    index = text.lower().find(key.lower())
    if index < 0:
        raise ValueError(f"Mnemonic {key!r} is not present in {text!r}.")
    return index


def _bind_alt(window: Any, key: str, action: Callable[[], object]) -> None:
    def activate(_event=None) -> str:
        action()
        return "break"

    window.bind(f"<Alt-{key.lower()}>", activate, add="+")
    window.bind(f"<Alt-{key.upper()}>", activate, add="+")


def add_control_mnemonic(
    window: Any, widget: Invokable, text: str, key: str
) -> None:
    """Underline a control mnemonic and invoke the control with Alt+key."""
    widget["underline"] = _underline_index(text, key)
    _bind_alt(window, key, widget.invoke)


def add_label_mnemonic(
    window: Any,
    label: Underlined,
    text: str,
    key: str,
    target: Focusable,
) -> None:
    """Underline a label mnemonic and focus its field with Alt+key."""
    label["underline"] = _underline_index(text, key)
    _bind_alt(window, key, target.focus_set)
