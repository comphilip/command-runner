import tkinter as tk
from tkinter import ttk


class CloseDialog(tk.Toplevel):
    def __init__(self, parent, count: int) -> None:
        super().__init__(parent)
        self.title("仍有命令正在运行")
        self.resizable(False, False)
        self.result = "cancel"
        box = ttk.Frame(self, padding=18)
        box.pack(fill="both", expand=True)
        ttk.Label(
            box, text=f"仍有 {count} 个命令正在运行，请选择操作。"
        ).pack(anchor="w", pady=(0, 16))
        buttons = ttk.Frame(box)
        buttons.pack()
        ttk.Button(
            buttons, text="关闭命令并退出", command=lambda: self._choose("exit")
        ).pack(side="left")
        cancel = ttk.Button(
            buttons, text="取消", command=lambda: self._choose("cancel")
        )
        cancel.pack(side="left", padx=8)
        ttk.Button(
            buttons, text="最小化到托盘", command=lambda: self._choose("tray")
        ).pack(side="left")
        self.transient(parent)
        self.grab_set()
        self.protocol("WM_DELETE_WINDOW", lambda: self._choose("cancel"))
        self.bind("<Escape>", lambda _e: self._choose("cancel"))
        self.bind("<Return>", lambda _e: self._choose("cancel"))
        cancel.focus_set()

    def _choose(self, value: str) -> None:
        self.result = value
        self.destroy()
