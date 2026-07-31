from __future__ import annotations

import tkinter as tk

from .tray_manager import TrayManager
from .ui.main_window import MainWindow
from .viewmodels import MainWindowViewModel


class Application:
    """Own application state independently from disposable UI resources."""

    def __init__(
        self,
        root: tk.Tk,
        view_model: MainWindowViewModel | None = None,
    ) -> None:
        self.root = root
        self.root.withdraw()
        self.view_model = view_model or MainWindowViewModel(root)
        self.window: MainWindow | None = None
        self.tray: TrayManager | None = None
        self._exit_poll: str | None = None
        self.show_window()

    def show_window(self) -> None:
        if self.window is not None:
            self.window.show()
            return
        self._destroy_tray()
        self.view_model.poll_events()
        toplevel = tk.Toplevel(self.root)
        self.window = MainWindow(
            toplevel,
            self.view_model,
            on_minimize=self.minimize_to_tray,
            on_exit=self.request_exit,
            on_dispose=self._window_disposed,
        )

    def minimize_to_tray(self) -> bool:
        if self.tray is None:
            tray = TrayManager(
                lambda function: self.root.after(0, function),
                self.show_window,
                self.view_model.start_all,
                self.view_model.stop_all,
                self.request_exit,
            )
            if not tray.show():
                return False
            self.tray = tray
        if self.window is not None:
            self.window.dispose()
        return True

    def request_exit(self) -> None:
        if self.view_model.request_exit():
            self._finish_exit()
            return
        if self.window is not None:
            self.window.show_stopping()
        elif self._exit_poll is None:
            self._exit_poll = self.root.after(100, self._poll_exit)

    def _poll_exit(self) -> None:
        self._exit_poll = None
        if self.view_model.poll_events():
            self._finish_exit()
        else:
            self._exit_poll = self.root.after(100, self._poll_exit)

    def _finish_exit(self) -> None:
        if self._exit_poll is not None:
            self.root.after_cancel(self._exit_poll)
            self._exit_poll = None
        self.view_model.save_preferences()
        self._destroy_tray()
        if self.window is not None:
            self.window.dispose()
        self.view_model.dispose()
        self.root.destroy()

    def _destroy_tray(self) -> None:
        if self.tray is not None:
            self.tray.stop()
            self.tray = None

    def _window_disposed(self) -> None:
        self.window = None
