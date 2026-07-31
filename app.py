from __future__ import annotations

import logging
import sys
import tkinter as tk

from command_runner.config_store import default_config_path
from command_runner.application import Application


def install_crash_log() -> None:
    path = default_config_path().parent / "logs" / "crash.log"
    path.parent.mkdir(parents=True, exist_ok=True)
    logging.basicConfig(
        filename=path, level=logging.ERROR,
        format="%(asctime)s %(levelname)s %(message)s", encoding="utf-8"
    )

    def handle(exc_type, exc_value, traceback) -> None:
        logging.exception("Unhandled exception", exc_info=(exc_type, exc_value, traceback))
        sys.__excepthook__(exc_type, exc_value, traceback)

    sys.excepthook = handle


def main() -> None:
    install_crash_log()
    root = tk.Tk()
    Application(root)
    root.mainloop()


if __name__ == "__main__":
    main()
