#include "application.h"

#include <windows.h>

#include <utility>

using command_runner::ConfigData;
using command_runner::ConfigStore;
using command_runner::ProcessManager;
using command_runner::Application;

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

    ProcessManager processManager;
    Application application(instance,
                            std::move(configuration),
                            store,
                            processManager);
    const auto result = application.run(showCommand);
    if (!result) {
        processManager.close();
        MessageBoxW(nullptr,
                    L"Unable to start Command Runner.",
                    L"Command Runner",
                    MB_OK | MB_ICONERROR);
        return static_cast<int>(result.error());
    }
    processManager.close();
    return *result;
}
