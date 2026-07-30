from __future__ import annotations

import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

from ..models import CommandConfig


class CommandDialog(tk.Toplevel):
    def __init__(self, parent, value: CommandConfig | None = None) -> None:
        super().__init__(parent)
        self.title("编辑命令" if value else "添加命令")
        self.resizable(True, False)
        self.result: CommandConfig | None = None
        self.original_id = value.id if value else ""
        self.vars = {
            "name": tk.StringVar(value=value.name if value else ""),
            "working_directory": tk.StringVar(
                value=value.working_directory if value else str(Path.home())
            ),
            "command_line": tk.StringVar(value=value.command_line if value else ""),
            "execution_mode": tk.StringVar(
                value=value.execution_mode if value else "shell"
            ),
            "encoding": tk.StringVar(value=value.encoding if value else "auto"),
        }
        frame = ttk.Frame(self, padding=14)
        frame.grid(sticky="nsew")
        ttk.Label(frame, text="名称").grid(row=0, column=0, sticky="w", pady=4)
        name = ttk.Entry(frame, textvariable=self.vars["name"], width=60)
        name.grid(row=0, column=1, columnspan=2, sticky="ew", pady=4)
        ttk.Label(frame, text="工作目录").grid(row=1, column=0, sticky="w", pady=4)
        ttk.Entry(frame, textvariable=self.vars["working_directory"]).grid(
            row=1, column=1, sticky="ew", pady=4
        )
        ttk.Button(frame, text="浏览…", command=self._browse).grid(row=1, column=2, padx=(6, 0))
        ttk.Label(frame, text="命令行").grid(row=2, column=0, sticky="w", pady=4)
        ttk.Entry(frame, textvariable=self.vars["command_line"]).grid(
            row=2, column=1, columnspan=2, sticky="ew", pady=4
        )
        ttk.Label(frame, text="执行方式").grid(row=3, column=0, sticky="w", pady=4)
        modes = ttk.Frame(frame)
        modes.grid(row=3, column=1, columnspan=2, sticky="w")
        ttk.Radiobutton(
            modes, text="通过 Shell（支持 .bat、管道、重定向）",
            variable=self.vars["execution_mode"], value="shell"
        ).pack(side="left")
        ttk.Radiobutton(
            modes, text="直接执行", variable=self.vars["execution_mode"], value="direct"
        ).pack(side="left", padx=12)
        ttk.Label(frame, text="输出编码").grid(row=4, column=0, sticky="w", pady=4)
        ttk.Combobox(
            frame, textvariable=self.vars["encoding"],
            values=("auto", "utf-8", "gbk", "system"), state="readonly", width=15
        ).grid(row=4, column=1, sticky="w", pady=4)
        buttons = ttk.Frame(frame)
        buttons.grid(row=5, column=0, columnspan=3, sticky="e", pady=(14, 0))
        ttk.Button(buttons, text="取消", command=self.destroy).pack(side="right")
        ttk.Button(buttons, text="保存", command=self._save).pack(side="right", padx=8)
        frame.columnconfigure(1, weight=1)
        self.transient(parent)
        self.grab_set()
        self.protocol("WM_DELETE_WINDOW", self.destroy)
        self.bind("<Escape>", lambda _e: self.destroy())
        name.focus_set()

    def _browse(self) -> None:
        path = filedialog.askdirectory(
            parent=self, initialdir=self.vars["working_directory"].get()
        )
        if path:
            self.vars["working_directory"].set(path)

    def _save(self) -> None:
        values = {key: value.get().strip() for key, value in self.vars.items()}
        if not values["name"] or not values["command_line"]:
            messagebox.showwarning("缺少内容", "名称和命令行不能为空。", parent=self)
            return
        if not Path(values["working_directory"]).expanduser().is_dir():
            messagebox.showwarning("目录无效", "工作目录不存在。", parent=self)
            return
        self.result = CommandConfig(id=self.original_id, **values)
        self.destroy()
