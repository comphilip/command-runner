from __future__ import annotations

import queue
import tkinter as tk
from datetime import datetime
from tkinter import messagebox, ttk

from ..config_store import ConfigStore
from ..models import CommandConfig, State
from ..process_manager import ProcessManager
from ..tray_manager import TrayManager
from .accessibility import add_control_mnemonic, add_label_mnemonic
from .close_dialog import CloseDialog
from .command_dialog import CommandDialog


class MainWindow:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("Command Runner")
        self.root.geometry("1050x700")
        self.store = ConfigStore()
        try:
            self.commands, prefs = self.store.load()
        except ValueError as exc:
            messagebox.showerror("Configuration Error", str(exc))
            self.commands, prefs = [], {}
        self.manager = ProcessManager()
        self.wrap = tk.BooleanVar(value=prefs.get("wrap_lines", False))
        self.auto_scroll = tk.BooleanVar(value=prefs.get("auto_scroll", True))
        self.log_view = tk.StringVar(value="combined")
        self.active_id: str | None = None
        self._rendered_sequences: set[int] = set()
        self._exiting = False
        self._build()
        self.tray = TrayManager(
            lambda fn: self.root.after(0, fn), self.restore, self.start_all,
            self.stop_all, self.request_exit
        )
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)
        self.root.bind("<Unmap>", self._on_unmap)
        self.root.after(100, self._poll_events)
        self._refresh_rows()

    def _build(self) -> None:
        toolbar = ttk.Frame(self.root, padding=(8, 8, 8, 4))
        toolbar.pack(fill="x")
        self.action_buttons: dict[str, ttk.Button] = {}
        actions = (
            ("Add", "A", self.add), ("Edit", "E", self.edit),
            ("Delete", "D", self.delete), ("Start", "S", self.start_selected),
            ("Stop", "T", self.stop_selected),
            ("Restart", "R", self.restart_selected),
        )
        for _, (text, key, command) in enumerate(actions):
            button = ttk.Button(toolbar, text=text, command=command)
            add_control_mnemonic(self.root, button, text, key)
            button.pack(side="left")
            self.action_buttons[text] = button
        pane = ttk.Panedwindow(self.root, orient="vertical")
        pane.pack(fill="both", expand=True, padx=8, pady=(4, 8))
        top = ttk.Frame(pane)
        bottom = ttk.Frame(pane)
        pane.add(top, weight=2)
        pane.add(bottom, weight=3)
        columns = ("name", "state", "pid", "exit", "cwd")
        self.tree = ttk.Treeview(
            top, columns=columns, show="headings", selectmode="extended"
        )
        labels = {
            "name": "Name", "state": "Status", "pid": "PID",
            "exit": "Exit Code", "cwd": "Working Directory",
        }
        widths = {"name": 180, "state": 100, "pid": 80, "exit": 70, "cwd": 500}
        for column in columns:
            self.tree.heading(column, text=labels[column])
            self.tree.column(column, width=widths[column], anchor="w")
        sy = ttk.Scrollbar(top, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=sy.set)
        self.tree.pack(side="left", fill="both", expand=True)
        sy.pack(side="right", fill="y")
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
                options, text=text, value=value, variable=self.log_view,
                command=self._render_full_log
            )
            add_control_mnemonic(self.root, radio, text, key)
            radio.pack(side="left")
        auto_scroll = ttk.Checkbutton(
            options, text="Auto-scroll", variable=self.auto_scroll, command=self._save
        )
        add_control_mnemonic(self.root, auto_scroll, "Auto-scroll", "L")
        auto_scroll.pack(side="right")
        word_wrap = ttk.Checkbutton(
            options, text="Word wrap", variable=self.wrap, command=self._toggle_wrap
        )
        add_control_mnemonic(self.root, word_wrap, "Word wrap", "W")
        word_wrap.pack(side="right", padx=10)
        jump = ttk.Button(
            options, text="Jump to Latest", command=lambda: self.text.see("end")
        )
        add_control_mnemonic(self.root, jump, "Jump to Latest", "J")
        jump.pack(side="right")
        clear = ttk.Button(options, text="Clear", command=self._clear_logs)
        add_control_mnemonic(self.root, clear, "Clear", "C")
        clear.pack(side="right", padx=(0, 6))
        log_frame = ttk.Frame(bottom)
        log_frame.pack(fill="both", expand=True, pady=(5, 0))
        log_label = ttk.Label(log_frame, text="Log output")
        log_label.grid(row=0, column=0, sticky="w")
        self.text = tk.Text(
            log_frame, state="disabled", wrap="word" if self.wrap.get() else "none",
            font=("Consolas", 10), undo=False
        )
        add_label_mnemonic(self.root, log_label, "Log output", "G", self.text)
        self.text.tag_configure("stderr", foreground="#c62828")
        vy = ttk.Scrollbar(log_frame, orient="vertical", command=self.text.yview)
        hx = ttk.Scrollbar(log_frame, orient="horizontal", command=self.text.xview)
        self.text.configure(yscrollcommand=vy.set, xscrollcommand=hx.set)
        self.text.grid(row=1, column=0, sticky="nsew")
        vy.grid(row=1, column=1, sticky="ns")
        hx.grid(row=2, column=0, sticky="ew")
        log_frame.rowconfigure(1, weight=1)
        log_frame.columnconfigure(0, weight=1)

    def by_id(self, command_id: str) -> CommandConfig | None:
        return next((item for item in self.commands if item.id == command_id), None)

    def selected(self) -> list[CommandConfig]:
        return [item for key in self.tree.selection() if (item := self.by_id(key))]

    def add(self) -> None:
        dialog = CommandDialog(self.root)
        self.root.wait_window(dialog)
        if dialog.result:
            self.commands.append(dialog.result)
            self._save()
            self._refresh_rows()

    def edit(self) -> None:
        selected = self.selected()
        if len(selected) != 1:
            messagebox.showinfo("Edit Command", "Please select a command.")
            return
        runtime = self.manager.runtime(selected[0].id)
        if runtime.state in {State.STARTING, State.RUNNING, State.STOPPING}:
            messagebox.showwarning("Cannot Edit", "Stop this command before editing it.")
            return
        dialog = CommandDialog(self.root, selected[0])
        self.root.wait_window(dialog)
        if dialog.result:
            self.commands[self.commands.index(selected[0])] = dialog.result
            self._save()
            self._refresh_rows()

    def delete(self) -> None:
        selected = self.selected()
        if not selected:
            return
        if any(item.id in self.manager.running_ids() for item in selected):
            messagebox.showwarning(
                "Cannot Delete", "Stop the selected running commands before deleting them."
            )
            return
        if messagebox.askyesno(
            "Delete Commands",
            f"Are you sure you want to delete {len(selected)} command(s)?",
        ):
            ids = {item.id for item in selected}
            self.commands = [item for item in self.commands if item.id not in ids]
            self._save()
            self._refresh_rows()

    def start_selected(self) -> None:
        for item in self.selected():
            if self.manager.runtime(item.id).state in {
                State.STOPPED, State.EXITED, State.FAILED
            }:
                self.manager.start(item)

    def stop_selected(self) -> None:
        for item in self.selected():
            if self.manager.runtime(item.id).state == State.RUNNING:
                self.manager.stop(item.id)

    def restart_selected(self) -> None:
        for item in self.selected():
            if self.manager.runtime(item.id).state in {
                State.STOPPED, State.EXITED, State.FAILED, State.RUNNING
            }:
                self.manager.restart(item)

    def start_all(self) -> None:
        for item in self.commands:
            self.manager.start(item)

    def stop_all(self) -> None:
        for command_id in self.manager.running_ids():
            self.manager.stop(command_id)

    def _refresh_rows(self) -> None:
        existing = set(self.tree.get_children())
        for item in self.commands:
            runtime = self.manager.runtime(item.id)
            values = (
                item.name, runtime.state.value, runtime.pid or "",
                "" if runtime.exit_code is None else runtime.exit_code,
                item.working_directory,
            )
            if item.id in existing:
                self.tree.item(item.id, values=values)
                existing.remove(item.id)
            else:
                self.tree.insert("", "end", iid=item.id, values=values)
        for item_id in existing:
            self.tree.delete(item_id)
        self._update_action_buttons()

    def _selection_changed(self, _event=None) -> None:
        selection = self.tree.selection()
        new_id = selection[0] if len(selection) == 1 else None
        if new_id != self.active_id:
            self.active_id = new_id
            self._render_full_log()
        self._update_action_buttons()

    def _row_double_clicked(self, event) -> None:
        command_id = self.tree.identify_row(event.y)
        if not command_id:
            return
        self.tree.selection_set(command_id)
        if self.manager.runtime(command_id).state in {
            State.STOPPED, State.EXITED, State.FAILED
        }:
            self.edit()

    def _update_action_buttons(self) -> None:
        """Enable actions only when at least one selected command can use them."""
        selected = self.selected()
        states = [self.manager.runtime(item.id).state for item in selected]
        inactive = {State.STOPPED, State.EXITED, State.FAILED}

        availability = {
            "Add": True,
            "Edit": len(selected) == 1 and states[0] in inactive,
            "Delete": bool(selected) and all(state in inactive for state in states),
            "Start": any(state in inactive for state in states),
            "Stop": any(state == State.RUNNING for state in states),
            "Restart": any(
                state in inactive or state == State.RUNNING for state in states
            ),
        }
        for name, enabled in availability.items():
            self.action_buttons[name].state(["!disabled"] if enabled else ["disabled"])

    def _format_line(self, line) -> str:
        stamp = datetime.fromtimestamp(line.timestamp).strftime("%H:%M:%S")
        return f"{stamp} [{line.stream}] {line.text}\n"

    def _render_full_log(self) -> None:
        self.text.configure(state="normal")
        self.text.delete("1.0", "end")
        self._rendered_sequences.clear()
        if self.active_id:
            runtime = self.manager.runtime(self.active_id)
            lines = getattr(runtime, self.log_view.get())
            for line in lines:
                self.text.insert("end", self._format_line(line), line.stream)
                self._rendered_sequences.add(line.sequence)
        self.text.configure(state="disabled")
        self.text.see("end")

    def _append_visible(self, line) -> None:
        if line.sequence in self._rendered_sequences:
            return
        if self.active_id and line.sequence <= self.manager.runtime(
            self.active_id
        ).cleared_through:
            return
        if self.log_view.get() not in ("combined", line.stream):
            return
        at_bottom = self.text.yview()[1] >= 0.999
        self.text.configure(state="normal")
        if len(self._rendered_sequences) >= 1000:
            self.text.delete("1.0", "2.0")
            self._rendered_sequences.discard(min(self._rendered_sequences))
        self.text.insert("end", self._format_line(line), line.stream)
        self.text.configure(state="disabled")
        self._rendered_sequences.add(line.sequence)
        if self.auto_scroll.get() and at_bottom:
            self.text.see("end")

    def _clear_logs(self) -> None:
        if self.active_id:
            self.manager.clear_logs(self.active_id)
            self._render_full_log()

    def _poll_events(self) -> None:
        dirty = False
        while True:
            try:
                event = self.manager.events.get_nowait()
            except queue.Empty:
                break
            if event[0] == "state":
                dirty = True
            elif event[0] == "log" and event[1] == self.active_id:
                self._append_visible(event[2])
        if dirty:
            self._refresh_rows()
        if self._exiting and not self.manager.running_ids():
            self._finish_exit()
            return
        self.root.after(100, self._poll_events)

    def _toggle_wrap(self) -> None:
        self.text.configure(wrap="word" if self.wrap.get() else "none")
        self._save()

    def _save(self) -> None:
        try:
            self.store.save(
                self.commands,
                {"wrap_lines": self.wrap.get(), "auto_scroll": self.auto_scroll.get()},
            )
        except OSError as exc:
            messagebox.showerror("Save Failed", str(exc))

    def _on_unmap(self, _event=None) -> None:
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
        running = self.manager.running_ids()
        if not running:
            self._finish_exit()
            return
        dialog = CloseDialog(self.root, len(running))
        self.root.wait_window(dialog)
        if dialog.result == "exit":
            self.request_exit()
        elif dialog.result == "tray":
            self.minimize_to_tray()

    def request_exit(self) -> None:
        if not self.manager.running_ids():
            self._finish_exit()
            return
        self._exiting = True
        self.stop_all()
        self.root.title("Command Runner — Stopping all commands…")

    def _finish_exit(self) -> None:
        self._save()
        self.tray.stop()
        self.root.destroy()
