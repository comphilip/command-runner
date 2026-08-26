from __future__ import annotations

import tkinter as tk
from tkinter import filedialog, messagebox, simpledialog, ttk

from ..models import CommandConfig
from ..viewmodels import CommandDialogViewModel
from .accessibility import add_control_mnemonic, add_label_mnemonic


class CommandDialog(simpledialog.Dialog):
    def __init__(
        self,
        parent: tk.Misc,
        value: CommandConfig | None = None,
    ) -> None:
        self.view_model: CommandDialogViewModel
        self.initial_value = value
        super().__init__(parent, title="Edit Command" if value else "Add Command")

    def body(self, master: tk.Misc) -> tk.Widget:
        self.resizable(True, False)
        master.pack_configure(fill="both", expand=True)
        self.view_model = CommandDialogViewModel(self, self.initial_value)

        name_label = ttk.Label(master, text="Name")
        name_label.grid(row=0, column=0, sticky="w", pady=4)
        name = ttk.Entry(
            master, textvariable=self.view_model.name, width=60
        )
        name.grid(row=0, column=1, columnspan=2, sticky="ew", pady=4)
        add_label_mnemonic(self, name_label, "Name", "N", name)

        directory_label = ttk.Label(master, text="Working Directory")
        directory_label.grid(row=1, column=0, sticky="w", pady=4)
        directory = ttk.Entry(
            master, textvariable=self.view_model.working_directory
        )
        directory.grid(row=1, column=1, sticky="ew", pady=4)
        add_label_mnemonic(
            self, directory_label, "Working Directory", "W", directory
        )
        browse = ttk.Button(master, text="Browse…", command=self._browse)
        add_control_mnemonic(self, browse, "Browse…", "B")
        browse.grid(row=1, column=2, padx=(6, 0))

        command_label = ttk.Label(master, text="Command Line")
        command_label.grid(row=2, column=0, sticky="w", pady=4)
        command_editor_frame = ttk.Frame(master)
        command_editor_frame.grid(
            row=2, column=1, columnspan=2, sticky="nsew", pady=4
        )
        command_editor_frame.columnconfigure(0, weight=1)
        self._command_line_editor = tk.Text(
            command_editor_frame, height=4, wrap="word"
        )
        self._command_line_editor.insert(
            "1.0", self.view_model.command_line.get()
        )
        command_line = self._command_line_editor
        command_line.grid(row=0, column=0, sticky="nsew")
        command_scrollbar = ttk.Scrollbar(
            command_editor_frame,
            orient="vertical",
            command=command_line.yview,
        )
        command_scrollbar.grid(row=0, column=1, sticky="ns")
        command_line.configure(yscrollcommand=command_scrollbar.set)
        add_label_mnemonic(
            self, command_label, "Command Line", "L", command_line
        )

        encoding_label = ttk.Label(master, text="Output Encoding")
        encoding_label.grid(row=3, column=0, sticky="w", pady=4)
        encoding = ttk.Combobox(
            master,
            textvariable=self.view_model.encoding,
            values=("auto", "utf-8", "gbk", "system"),
            state="readonly",
            width=15,
        )
        encoding.grid(row=3, column=1, sticky="w", pady=4)
        add_label_mnemonic(
            self, encoding_label, "Output Encoding", "O", encoding
        )
        shell = ttk.Checkbutton(
            master,
            text="Shell",
            variable=self.view_model.shell,
        )
        shell.grid(row=4, column=1, sticky="w", pady=4)
        add_control_mnemonic(self, shell, "Shell", "H")
        auto_start = ttk.Checkbutton(
            master,
            text="Auto Start",
            variable=self.view_model.auto_start,
        )
        auto_start.grid(row=5, column=1, sticky="w", pady=4)
        add_control_mnemonic(self, auto_start, "Auto Start", "A")
        master.columnconfigure(1, weight=1)
        return name

    def buttonbox(self) -> None:
        buttons = ttk.Frame(self)
        buttons.pack(fill="x", padx=5, pady=(5, 10))
        cancel = ttk.Button(buttons, text="Cancel", command=self.cancel)
        add_control_mnemonic(self, cancel, "Cancel", "C")
        cancel.pack(side="right")
        save = ttk.Button(buttons, text="Save", command=self._save)
        add_control_mnemonic(self, save, "Save", "S")
        save.pack(side="right", padx=8)
        self.bind("<Escape>", self.cancel)
        self.bind("<Control-Return>", lambda _event: self._save())

    def _browse(self) -> None:
        path = filedialog.askdirectory(
            parent=self, initialdir=self.view_model.working_directory.get()
        )
        if path:
            self.view_model.working_directory.set(path)

    def _save(self) -> None:
        self.view_model.command_line.set(
            self._command_line_editor.get("1.0", "end-1c")
        )
        result = self.view_model.validate()
        if not result.valid:
            messagebox.showwarning("Invalid Command", result.error, parent=self)
            return
        self.result = result.value
        self.cancel()
