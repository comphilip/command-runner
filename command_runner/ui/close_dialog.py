import tkinter as tk
from tkinter import ttk

from ..viewmodels import CloseAction
from .accessibility import add_control_mnemonic
from .window_utils import center_over_parent


class CloseDialog(tk.Toplevel):
    def __init__(self, parent, count: int) -> None:
        super().__init__(parent)
        self.title("Commands Are Still Running")
        self.resizable(False, False)
        self.result = CloseAction.CANCEL
        box = ttk.Frame(self, padding=18)
        box.pack(fill="both", expand=True)
        ttk.Label(
            box, text=f"{count} command(s) are still running. Choose an action."
        ).pack(anchor="w", pady=(0, 16))
        buttons = ttk.Frame(box)
        buttons.pack()
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
        self.transient(parent)
        center_over_parent(self, parent)
        self.grab_set()
        self.protocol("WM_DELETE_WINDOW", lambda: self._choose(CloseAction.CANCEL))
        self.bind("<Escape>", lambda _e: self._choose(CloseAction.CANCEL))
        self.bind("<Return>", lambda _e: self._choose(CloseAction.CANCEL))
        cancel.focus_set()

    def _choose(self, value: CloseAction) -> None:
        self.result = value
        self.destroy()
