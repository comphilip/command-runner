import tkinter as tk
from tkinter import simpledialog, ttk

from ..viewmodels import CloseAction
from .accessibility import add_control_mnemonic


class CloseDialog(simpledialog.Dialog):
    def __init__(self, parent: tk.Misc, count: int) -> None:
        self.count = count
        super().__init__(parent, title="Commands Are Still Running")

    def body(self, master: tk.Misc) -> None:
        ttk.Label(
            master,
            text=f"{self.count} command(s) are still running. Choose an action.",
        ).pack(anchor="w", padx=8, pady=8)

    def buttonbox(self) -> None:
        buttons = ttk.Frame(self)
        buttons.pack(padx=8, pady=(0, 8))
        stop_and_exit = ttk.Button(
            buttons, text="Stop Commands and Exit",
            command=lambda: self._choose(CloseAction.EXIT)
        )
        add_control_mnemonic(self, stop_and_exit, "Stop Commands and Exit", "S")
        stop_and_exit.pack(side="left")
        cancel = ttk.Button(
            buttons, text="Cancel", command=lambda: self._choose(CloseAction.CANCEL)
        )
        add_control_mnemonic(self, cancel, "Cancel", "C")
        cancel.pack(side="left", padx=8)
        minimize = ttk.Button(
            buttons, text="Minimize to Tray",
            command=lambda: self._choose(CloseAction.TRAY)
        )
        add_control_mnemonic(self, minimize, "Minimize to Tray", "M")
        minimize.pack(side="left")
        self.bind("<Escape>", self.cancel)
        self.bind("<Return>", self.cancel)
        cancel.focus_set()

    def _choose(self, value: CloseAction) -> None:
        self.result = value
        self.cancel()

    def cancel(self, event: tk.Event | None = None) -> None:
        if self.result is None:
            self.result = CloseAction.CANCEL
        super().cancel(event)
