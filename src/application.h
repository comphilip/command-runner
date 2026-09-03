#pragma once

#include "core/config_store.h"
#include "core/process_manager.h"
#include "platform/tray_icon.h"
#include "ui/main_window.h"

#include <expected>
#include <memory>

#include <windows.h>
#include <win32xx/wxx_appcore.h>

namespace command_runner {

class Application final : public Win32xx::CWinApp,
                          public ui::MainWindowHost,
                          public platform::TrayIconListener {
public:
    Application(HINSTANCE instance,
                ConfigData configuration,
                ConfigStore& store,
                ProcessManager& processManager);
    ~Application() override;

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    [[nodiscard]] std::expected<int, DWORD> run(int showCommand);

private:
    static constexpr UINT WM_APPLICATION_MINIMIZE = WM_APP + 50;
    static constexpr UINT WM_APPLICATION_CLOSE = WM_APP + 51;
    static constexpr UINT WM_APPLICATION_RESTORE = WM_APP + 52;
    static constexpr UINT WM_APPLICATION_START_ALL = WM_APP + 53;
    static constexpr UINT WM_APPLICATION_STOP_ALL = WM_APP + 54;
    static constexpr UINT_PTR EXIT_POLL_TIMER_REQUEST = 1;

    void onMainWindowCloseRequested() override;
    void onMainWindowMinimizeRequested() override;
    void onTrayRestoreRequested() override;
    void onTrayStartAllRequested() override;
    void onTrayStopAllRequested() override;
    void onTrayExitRequested() override;

    [[nodiscard]] std::expected<void, DWORD> createWindow(int showCommand);
    [[nodiscard]] bool createTray();
    void handleDeferredMessage(UINT message);
    void requestCloseNow();
    void minimizeToTrayNow();
    void restoreWindowNow();
    void startAll();
    void stopAll();
    void beginExit();
    void finishExit();
    void postDeferred(UINT message) const noexcept;

    HINSTANCE mInstance{};
    ConfigData mConfiguration;
    ConfigStore& mStore;
    ProcessManager& mProcessManager;
    std::unique_ptr<ui::MainWindow> mWindow;
    std::unique_ptr<platform::TrayIcon> mTray;
    Win32xx::CWnd* mExitTimerOwner{};
    DWORD mThreadId{};
    UINT_PTR mExitPollTimer{};
    int mShowCommand{SW_SHOWNORMAL};
    DWORD mStartupError{};
    bool mExitRequested{};
    bool mExitFinished{};

protected:
    BOOL InitInstance() override;
    BOOL PreTranslateMessage(MSG& message) override;
};

}  // namespace command_runner
