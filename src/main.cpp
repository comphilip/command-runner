#include "application.h"

#include <windows.h>

#include <wil/resource.h>

#include <utility>

namespace {

constexpr wchar_t SINGLE_INSTANCE_MUTEX_NAME[] =
    L"Local\\CommandRunner.SingleInstance";

}  // namespace

using command_runner::ConfigData;
using command_runner::ConfigStore;
using command_runner::ProcessManager;
using command_runner::Application;

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    // Keep this handle alive for the lifetime of the application. A named
    // mutex prevents a second process from creating competing command pipes
    // or running the same auto-start commands.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, SINGLE_INSTANCE_MUTEX_NAME);
    const DWORD mutexStatus = GetLastError();
    wil::unique_handle singleInstance(mutex);
    if (!singleInstance) {
        const DWORD error = mutexStatus == ERROR_SUCCESS
                                ? ERROR_FUNCTION_FAILED
                                : mutexStatus;
        MessageBoxW(nullptr,
                    L"Unable to ensure that only one Command Runner instance "
                    L"is running.",
                    L"Command Runner",
                    MB_OK | MB_ICONERROR);
        return static_cast<int>(error);
    }
    if (mutexStatus == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr,
                    L"Command Runner is already running.",
                    L"Command Runner",
                    MB_OK | MB_ICONINFORMATION);
        return 0;
    }

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
