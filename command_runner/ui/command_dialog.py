from __future__ import annotations

import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from ..models import CommandConfig
from ..viewmodels import CommandDialogViewModel
from .accessibility import add_control_mnemonic, add_label_mnemonic
from .window_utils import center_over_parent


class CommandDialog(tk.Toplevel):
    def __init__(self, parent, value: CommandConfig | None = None) -> None:
        super().__init__(parent)
        self.title("Edit Command" if value else "Add Command")
        self.resizable(True, False)
        self.result: CommandConfig | None = None
        self.view_model = CommandDialogViewModel(self, value)
        frame = ttk.Frame(self, padding=14)
        frame.grid(sticky="nsew")
        name_label = ttk.Label(frame, text="Name")
        name_label.grid(row=0, column=0, sticky="w", pady=4)
        name = ttk.Entry(frame, textvariable=self.view_model.name, width=60)
        name.grid(row=0, column=1, columnspan=2, sticky="ew", pady=4)
        add_label_mnemonic(self, name_label, "Name", "N", name)
        directory_label = ttk.Label(frame, text="Working Directory")
        directory_label.grid(row=1, column=0, sticky="w", pady=4)
        directory = ttk.Entry(
            frame, textvariable=self.view_model.working_directory
        )
        directory.grid(
            row=1, column=1, sticky="ew", pady=4
        )
        add_label_mnemonic(
            self, directory_label, "Working Directory", "W", directory
        )
        browse = ttk.Button(frame, text="Browse…", command=self._browse)
        add_control_mnemonic(self, browse, "Browse…", "B")
        browse.grid(row=1, column=2, padx=(6, 0))
        command_label = ttk.Label(frame, text="Command Line")
        command_label.grid(row=2, column=0, sticky="w", pady=4)
        command_line = ttk.Entry(
            frame, textvariable=self.view_model.command_line
        )
        command_line.grid(
            row=2, column=1, columnspan=2, sticky="ew", pady=4
        )
        add_label_mnemonic(self, command_label, "Command Line", "L", command_line)
        encoding_label = ttk.Label(frame, text="Output Encoding")
        encoding_label.grid(row=3, column=0, sticky="w", pady=4)
        encoding = ttk.Combobox(
            frame, textvariable=self.view_model.encoding,
            values=("auto", "utf-8", "gbk", "system"), state="readonly", width=15
        )
        encoding.grid(row=3, column=1, sticky="w", pady=4)
        add_label_mnemonic(
            self, encoding_label, "Output Encoding", "O", encoding
        )
        buttons = ttk.Frame(frame)
        buttons.grid(row=4, column=0, columnspan=3, sticky="e", pady=(14, 0))
        cancel = ttk.Button(buttons, text="Cancel", command=self.destroy)
        add_control_mnemonic(self, cancel, "Cancel", "C")
        cancel.pack(side="right")
        save = ttk.Button(buttons, text="Save", command=self._save)
        add_control_mnemonic(self, save, "Save", "S")
        save.pack(side="right", padx=8)
        frame.columnconfigure(1, weight=1)
        self.transient(parent)
        center_over_parent(self, parent)
        self.grab_set()
        self.protocol("WM_DELETE_WINDOW", self.destroy)
        self.bind("<Escape>", lambda _e: self.destroy())
        name.focus_set()

    def _browse(self) -> None:
        path = filedialog.askdirectory(
            parent=self, initialdir=self.view_model.working_directory.get()
        )
        if path:
            self.view_model.working_directory.set(path)

    def _save(self) -> None:
        result = self.view_model.validate()
        if not result.valid:
            messagebox.showwarning("Invalid Command", result.error, parent=self)
            return
        self.result = result.value
        self.destroy()
