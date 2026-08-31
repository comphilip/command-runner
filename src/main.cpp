#include "core/config_store.h"
#include "ui/main_window.h"

#include <windows.h>

#include <utility>

using command_runner::ConfigStore;
using command_runner::ConfigData;
using command_runner::ui::MainWindow;

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    ConfigStore store;
    auto loaded = store.load();
    ConfigData configuration;
    if (!loaded) {
        MessageBoxW(nullptr,
                    loaded.error().c_str(),
                    L"Configuration Error",
                    MB_OK | MB_ICONWARNING);
    } else {
        configuration = std::move(*loaded);
    }

    MainWindow mainWindow(instance, std::move(configuration));
    const auto created = mainWindow.create(showCommand);
    if (!created) {
        MessageBoxW(nullptr,
                    L"Unable to create the main window.",
                    L"Command Runner",
                    MB_OK | MB_ICONERROR);
        return static_cast<int>(created.error());
    }

    const auto messageLoop = mainWindow.runMessageLoop();
    if (!messageLoop) {
        MessageBoxW(nullptr,
                    L"The message loop terminated unexpectedly.",
                    L"Command Runner",
                    MB_OK | MB_ICONERROR);
        return static_cast<int>(messageLoop.error());
    }
    return *messageLoop;
}
