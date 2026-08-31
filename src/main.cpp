#include "core/config_store.h"
#include "ui/main_window.h"

#include <windows.h>

using command_runner::ConfigStore;
using command_runner::ui::MainWindow;

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    ConfigStore store;
    const auto loaded = store.load();
    if (!loaded.succeeded()) {
        MessageBoxW(nullptr,
                    loaded.error->c_str(),
                    L"Configuration Error",
                    MB_OK | MB_ICONWARNING);
    }

    MainWindow main_window(instance, loaded.data);
    if (!main_window.create(show_command)) {
        MessageBoxW(nullptr,
                    L"Unable to create the main window.",
                    L"Command Runner",
                    MB_OK | MB_ICONERROR);
        return static_cast<int>(GetLastError());
    }
    return main_window.run_message_loop();
}

