from __future__ import annotations

import threading


class TrayManager:
    def __init__(self, dispatch, restore, start_all, stop_all, exit_app) -> None:
        self.dispatch = dispatch
        self.restore = restore
        self.start_all = start_all
        self.stop_all = stop_all
        self.exit_app = exit_app
        self.icon = None

    def show(self) -> bool:
        if self.icon:
            self.icon.visible = True
            return True
        try:
            import pystray
            from PIL import Image, ImageDraw
        except ImportError:
            return False
        image = Image.new("RGBA", (64, 64), "#2563eb")
        draw = ImageDraw.Draw(image)
        draw.rectangle((13, 15, 51, 49), outline="white", width=4)
        draw.line((21, 27, 29, 33, 21, 39), fill="white", width=4)
        draw.line((34, 39, 44, 39), fill="white", width=4)
        menu = pystray.Menu(
            pystray.MenuItem("打开", lambda: self.dispatch(self.restore), default=True),
            pystray.Menu.SEPARATOR,
            pystray.MenuItem("全部启动", lambda: self.dispatch(self.start_all)),
            pystray.MenuItem("全部停止", lambda: self.dispatch(self.stop_all)),
            pystray.Menu.SEPARATOR,
            pystray.MenuItem("退出", lambda: self.dispatch(self.exit_app)),
        )
        self.icon = pystray.Icon("CommandRunner", image, "Command Runner", menu)
        threading.Thread(target=self.icon.run, daemon=True).start()
        return True

    def hide(self) -> None:
        if self.icon:
            self.icon.visible = False

    def stop(self) -> None:
        if self.icon:
            self.icon.stop()
            self.icon = None
