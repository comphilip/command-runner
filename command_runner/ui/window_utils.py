import tkinter as tk


def center_over_parent(window: tk.Toplevel, parent: tk.Misc) -> None:
    """Position a dialog in the center of its parent window."""
    window.update_idletasks()

    width = window.winfo_reqwidth()
    height = window.winfo_reqheight()
    x = parent.winfo_rootx() + (parent.winfo_width() - width) // 2
    y = parent.winfo_rooty() + (parent.winfo_height() - height) // 2

    window.geometry(f"+{x}+{y}")
