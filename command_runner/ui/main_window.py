from __future__ import annotations

import tkinter as tk
from collections.abc import Callable
from tkinter import messagebox, ttk

from ..tray_manager import TrayManager
from ..viewmodels import CloseAction, MainWindowViewModel
from .accessibility import add_control_mnemonic, add_label_mnemonic
from .bindings import bind_enabled
from .close_dialog import CloseDialog
from .command_dialog import CommandDialog


class MainWindow:
    """Tkinter View: widgets and presentation only."""

    def __init__(
        self,
        root: tk.Tk,
        view_model: MainWindowViewModel | None = None,
    ) -> None:
        self.root = root
        self.view_model = view_model or MainWindowViewModel(root)
        self.root.title("Command Runner")
        self.root.geometry("1050x700")
        self._unbinders: list[Callable[[], None]] = []
        self._build()
        self.tray = TrayManager(
            lambda fn: self.root.after(0, fn),
            self.restore,
            self.view_model.start_all,
            self.view_model.stop_all,
            self.request_exit,
        )
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)
        self.root.bind("<Unmap>", self._on_unmap)
        self._rows_trace = self.view_model.rows_revision.trace_add(
            "write", self._render_rows
        )
        self._logs_trace = self.view_model.logs_revision.trace_add(
            "write", self._render_logs
        )
        self._render_rows()
        self._render_logs()
        if self.view_model.startup_error:
            self.root.after(
                0,
                lambda: messagebox.showerror(
                    "Configuration Error", self.view_model.startup_error
                ),
            )
        self.root.after(100, self._poll_events)

    def _build(self) -> None:
        toolbar = ttk.Frame(self.root, padding=(8, 8, 8, 4))
        toolbar.pack(fill="x")
        actions = (
            ("Add", "A", self.add),
            ("Edit", "E", self.edit),
            ("Delete", "D", self.delete),
            ("Start", "S", self.view_model.start_selected),
            ("Stop", "T", self.view_model.stop_selected),
            ("Restart", "R", self.view_model.restart_selected),
        )
        for text, key, command in actions:
            button = ttk.Button(toolbar, text=text, command=command)
            add_control_mnemonic(self.root, button, text, key)
            button.pack(side="left")
            self._unbinders.append(
                bind_enabled(button, self.view_model.action_enabled[text])
            )

        pane = ttk.Panedwindow(self.root, orient="vertical")
        pane.pack(fill="both", expand=True, padx=8, pady=(4, 8))
        top, bottom = ttk.Frame(pane), ttk.Frame(pane)
        pane.add(top, weight=2)
        pane.add(bottom, weight=3)
        columns = ("name", "state", "pid", "exit", "cwd")
        self.tree = ttk.Treeview(
            top, columns=columns, show="headings", selectmode="extended"
        )
        labels = {
            "name": "Name",
            "state": "Status",
            "pid": "PID",
            "exit": "Exit Code",
            "cwd": "Working Directory",
        }
        widths = {"name": 180, "state": 100, "pid": 80, "exit": 70, "cwd": 500}
        for column in columns:
            self.tree.heading(column, text=labels[column])
            self.tree.column(column, width=widths[column], anchor="w")
        scrollbar = ttk.Scrollbar(top, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=scrollbar.set)
        self.tree.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")
        self.tree.bind("<<TreeviewSelect>>", self._selection_changed)
        self.tree.bind("<Double-1>", self._row_double_clicked)

        options = ttk.Frame(bottom)
        options.pack(fill="x")
        for text, key, value in (
            ("Combined", "M", "combined"),
            ("stdout", "U", "stdout"),
            ("Standard error", "N", "stderr"),
        ):
            radio = ttk.Radiobutton(
                options,
                text=text,
                value=value,
                variable=self.view_model.log_view,
            )
            add_control_mnemonic(self.root, radio, text, key)
            radio.pack(side="left")

        auto_scroll = ttk.Checkbutton(
            options,
            text="Auto-scroll",
            variable=self.view_model.auto_scroll,
            command=self._save_preferences,
        )
        add_control_mnemonic(self.root, auto_scroll, "Auto-scroll", "L")
        auto_scroll.pack(side="right")
        word_wrap = ttk.Checkbutton(
            options,
            text="Word wrap",
            variable=self.view_model.wrap_lines,
            command=self._toggle_wrap,
        )
        add_control_mnemonic(self.root, word_wrap, "Word wrap", "W")
        word_wrap.pack(side="right", padx=10)
        jump = ttk.Button(options, text="Jump to Latest", command=self._jump_latest)
        add_control_mnemonic(self.root, jump, "Jump to Latest", "J")
        jump.pack(side="right")
        clear = ttk.Button(
            options, text="Clear", command=self.view_model.clear_logs
        )
        add_control_mnemonic(self.root, clear, "Clear", "C")
        clear.pack(side="right", padx=(0, 6))
        self._unbinders.append(
            bind_enabled(clear, self.view_model.action_enabled["Clear"])
        )

        log_frame = ttk.Frame(bottom)
        log_frame.pack(fill="both", expand=True, pady=(5, 0))
        log_label = ttk.Label(log_frame, text="Log output")
        log_label.grid(row=0, column=0, sticky="w")
        self.text = tk.Text(
            log_frame,
            state="disabled",
            wrap="word" if self.view_model.wrap_lines.get() else "none",
            font=("Consolas", 10),
            undo=False,
        )
        add_label_mnemonic(self.root, log_label, "Log output", "G", self.text)
        self.text.tag_configure("stderr", foreground="#c62828")
        vertical = ttk.Scrollbar(
            log_frame, orient="vertical", command=self.text.yview
        )
        horizontal = ttk.Scrollbar(
            log_frame, orient="horizontal", command=self.text.xview
        )
        self.text.configure(
            yscrollcommand=vertical.set, xscrollcommand=horizontal.set
        )
        self.text.grid(row=1, column=0, sticky="nsew")
        vertical.grid(row=1, column=1, sticky="ns")
        horizontal.grid(row=2, column=0, sticky="ew")
        log_frame.rowconfigure(1, weight=1)
        log_frame.columnconfigure(0, weight=1)

    def add(self) -> None:
        dialog = CommandDialog(self.root)
        self.root.wait_window(dialog)
        if dialog.result:
            self._show_save_error(self.view_model.add(dialog.result))

    def edit(self) -> None:
        selected = self.view_model.selected()
        if len(selected) != 1:
            messagebox.showinfo("Edit Command", "Please select a command.")
            return
        if not self.view_model.action_enabled["Edit"].get():
            messagebox.showwarning(
                "Cannot Edit", "Stop this command before editing it."
            )
            return
        dialog = CommandDialog(self.root, selected[0])
        self.root.wait_window(dialog)
        if dialog.result:
            self._show_save_error(self.view_model.replace_selected(dialog.result))

    def delete(self) -> None:
        selected = self.view_model.selected()
        if not selected:
            return
        if not self.view_model.action_enabled["Delete"].get():
            messagebox.showwarning(
                "Cannot Delete",
                "Stop the selected running commands before deleting them.",
            )
            return
        if messagebox.askyesno(
            "Delete Commands",
            f"Are you sure you want to delete {len(selected)} command(s)?",
        ):
            self._show_save_error(self.view_model.delete_selected())

    def _selection_changed(self, _event: object = None) -> None:
        self.view_model.set_selection(list(self.tree.selection()))

    def _row_double_clicked(self, event: tk.Event) -> None:
        command_id = self.tree.identify_row(event.y)
        if command_id:
            self.tree.selection_set(command_id)
            self.view_model.set_selection([command_id])
            if self.view_model.action_enabled["Edit"].get():
                self.edit()

    def _render_rows(self, *_args: object) -> None:
        selection = set(self.tree.selection())
        existing = set(self.tree.get_children())
        for row in self.view_model.rows:
            values = (
                row.name,
                row.state.value,
                row.pid or "",
                "" if row.exit_code is None else row.exit_code,
                row.working_directory,
            )
            if row.id in existing:
                self.tree.item(row.id, values=values)
                existing.remove(row.id)
            else:
                self.tree.insert("", "end", iid=row.id, values=values)
        for command_id in existing:
            self.tree.delete(command_id)
        retained = [item for item in selection if self.tree.exists(item)]
        if retained:
            self.tree.selection_set(retained)

    def _render_logs(self, *_args: object) -> None:
        position, bottom = self.text.yview()
        at_bottom = bottom >= 0.999
        self.text.configure(state="normal")
        self.text.delete("1.0", "end")
        for line in self.view_model.visible_logs:
            self.text.insert("end", line.text, line.stream)
        self.text.configure(state="disabled")
        if self.view_model.auto_scroll.get() and at_bottom:
            self.text.see("end")
        elif not at_bottom:
            self.text.yview_moveto(position)

    def _poll_events(self) -> None:
        if self.view_model.poll_events():
            self._finish_exit()
            return
        self.root.after(100, self._poll_events)

    def _toggle_wrap(self) -> None:
        self.text.configure(
            wrap="word" if self.view_model.wrap_lines.get() else "none"
        )
        self._save_preferences()

    def _save_preferences(self) -> None:
        self._show_save_error(self.view_model.save_preferences())

    def _jump_latest(self) -> None:
        self.text.see("end")

    def _show_save_error(self, error: str | None) -> None:
        if error:
            messagebox.showerror("Save Failed", error)

    def _on_unmap(self, _event: object = None) -> None:
        self.root.after(100, self._minimize_if_iconic)

    def _minimize_if_iconic(self) -> None:
        if self.root.state() == "iconic":
            self.minimize_to_tray()

    def minimize_to_tray(self) -> None:
        if self.tray.show():
            self.root.withdraw()
        else:
            self.root.state("normal")
            messagebox.showerror(
                "System Tray Unavailable",
                "Install the required packages first: pip install pystray pillow",
            )

    def restore(self) -> None:
        self.tray.hide()
        self.root.deiconify()
        self.root.state("normal")
        self.root.lift()

    def on_close(self) -> None:
        running = self.view_model.manager.running_ids()
        if not running:
            self._finish_exit()
            return
        dialog = CloseDialog(self.root, len(running))
        if dialog.result == CloseAction.EXIT:
            self.request_exit()
        elif dialog.result == CloseAction.TRAY:
            self.minimize_to_tray()

    def request_exit(self) -> None:
        if self.view_model.request_exit():
            self._finish_exit()
        else:
            self.root.title("Command Runner — Stopping all commands…")

    def _finish_exit(self) -> None:
        self._show_save_error(self.view_model.save_preferences())
        self.tray.stop()
        for unbind in self._unbinders:
            unbind()
        try:
            self.view_model.rows_revision.trace_remove("write", self._rows_trace)
            self.view_model.logs_revision.trace_remove("write", self._logs_trace)
        except tk.TclError:
            pass
        self.view_model.dispose()
        self.root.destroy()
