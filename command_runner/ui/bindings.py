from __future__ import annotations

import tkinter as tk
from collections.abc import Callable
from typing import Protocol


class StatefulWidget(Protocol):
    def state(self, statespec: list[str]) -> object: ...


def bind_enabled(widget: StatefulWidget, variable: tk.BooleanVar) -> Callable[[], None]:
    """Bind a BooleanVar to a ttk widget's enabled state."""

    def synchronize(*_args: object) -> None:
        widget.state(["!disabled"] if variable.get() else ["disabled"])

    trace_id = variable.trace_add("write", synchronize)
    synchronize()

    def unbind() -> None:
        try:
            variable.trace_remove("write", trace_id)
        except tk.TclError:
            pass

    return unbind
