#include "application.h"

#include "ui/close_dialog.h"

#include <string>
#include <utility>

namespace command_runner {

Application::Application(HINSTANCE instance,
                         ConfigData configuration,
                         ConfigStore& store,
                         ProcessManager& processManager)
    : mInstance(instance),
      mConfiguration(std::move(configuration)),
      mStore(store),
      mProcessManager(processManager),
      mThreadId(GetCurrentThreadId()) {
    for (const auto& command : mConfiguration.mCommands) {
        if (command.mAutoStart) {
            mProcessManager.start(command);
        }
    }
}

Application::~Application() {
    if (mExitPollTimer != 0) {
        KillTimer(nullptr, mExitPollTimer);
        mExitPollTimer = 0;
    }
    mTray.reset();
    mWindow.reset();
}

std::expected<int, DWORD> Application::run(int showCommand) {
    MSG queuedMessage{};
    PeekMessageW(&queuedMessage,
                 nullptr,
                 WM_USER,
                 WM_USER,
                 PM_NOREMOVE);

    const auto created = createWindow(showCommand);
    if (!created) {
        return std::unexpected(created.error());
    }

    MSG message{};
    while (true) {
        const int result = GetMessageW(&message, nullptr, 0, 0);
        if (result == -1) {
            const DWORD error = GetLastError();
            return std::unexpected(error == ERROR_SUCCESS ? ERROR_FUNCTION_FAILED
                                                           : error);
        }
        if (result == 0) {
            return static_cast<int>(message.wParam);
        }
        if (message.hwnd == nullptr && message.message >= WM_APP &&
            message.message < WM_APP + 100) {
            handleDeferredMessage(message.message);
            continue;
        }
        if (message.hwnd == nullptr && message.message == WM_TIMER &&
            static_cast<UINT_PTR>(message.wParam) == mExitPollTimer) {
            if (mExitRequested && mProcessManager.runningIds().empty()) {
                finishExit();
            }
            continue;
        }
        if (mWindow != nullptr &&
            IsDialogMessageW(mWindow->window(), &message) != FALSE) {
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void Application::onMainWindowCloseRequested() {
    postDeferred(WM_APPLICATION_CLOSE);
}

void Application::onMainWindowMinimizeRequested() {
    postDeferred(WM_APPLICATION_MINIMIZE);
}

void Application::onTrayRestoreRequested() {
    postDeferred(WM_APPLICATION_RESTORE);
}

void Application::onTrayStartAllRequested() {
    postDeferred(WM_APPLICATION_START_ALL);
}

void Application::onTrayStopAllRequested() {
    postDeferred(WM_APPLICATION_STOP_ALL);
}

void Application::onTrayExitRequested() {
    postDeferred(WM_APPLICATION_CLOSE);
}

std::expected<void, DWORD> Application::createWindow(int showCommand) {
    if (mWindow != nullptr) {
        return {};
    }
    mWindow = std::make_unique<ui::MainWindow>(mInstance,
                                                mConfiguration,
                                                mStore,
                                                mProcessManager,
                                                *this);
    const auto created = mWindow->create(showCommand);
    if (!created) {
        const DWORD error = created.error();
        mWindow.reset();
        return std::unexpected(error);
    }
    return {};
}

bool Application::createTray() {
    if (mTray != nullptr) {
        return true;
    }
    auto tray = std::make_unique<platform::TrayIcon>(mInstance, *this);
    const auto created = tray->create();
    if (!created) {
        return false;
    }
    mTray = std::move(tray);
    return true;
}

void Application::handleDeferredMessage(UINT message) {
    switch (message) {
    case WM_APPLICATION_MINIMIZE:
        minimizeToTrayNow();
        break;
    case WM_APPLICATION_CLOSE:
        requestCloseNow();
        break;
    case WM_APPLICATION_RESTORE:
        restoreWindowNow();
        break;
    case WM_APPLICATION_START_ALL:
        startAll();
        break;
    case WM_APPLICATION_STOP_ALL:
        stopAll();
        break;
    default:
        break;
    }
}

void Application::requestCloseNow() {
    if (mExitRequested || mExitFinished) {
        return;
    }
    const auto running = mProcessManager.runningIds();
    if (running.empty() || mWindow == nullptr) {
        beginExit();
        return;
    }

    const ui::CloseAction action = ui::CloseDialog::show(
        mWindow->window(),
        running.size());
    switch (action) {
    case ui::CloseAction::EXIT:
        beginExit();
        break;
    case ui::CloseAction::TRAY:
        minimizeToTrayNow();
        break;
    case ui::CloseAction::CANCEL:
        break;
    }
}

void Application::minimizeToTrayNow() {
    if (mWindow == nullptr || mExitRequested || mExitFinished) {
        return;
    }
    if (!createTray()) {
        MessageBoxW(mWindow->window(),
                    L"The system tray icon could not be created.",
                    L"System Tray Unavailable",
                    MB_OK | MB_ICONERROR);
        return;
    }
    mWindow.reset();
}

void Application::restoreWindowNow() {
    if (mExitRequested || mExitFinished) {
        return;
    }
    if (mWindow != nullptr) {
        ShowWindow(mWindow->window(), SW_RESTORE);
        SetForegroundWindow(mWindow->window());
        return;
    }

    // The tray object is released before the main window is recreated so a
    // restore/minimize cycle never retains a stale menu or hidden HWND.
    mTray.reset();
    const auto created = createWindow(SW_SHOWNORMAL);
    if (!created) {
        MessageBoxW(nullptr,
                    L"Unable to restore the main window.",
                    L"Command Runner",
                    MB_OK | MB_ICONERROR);
        const bool trayCreated = createTray();
        (void)trayCreated;
    }
}

void Application::startAll() {
    for (const auto& command : mConfiguration.mCommands) {
        mProcessManager.start(command);
    }
}

void Application::stopAll() {
    for (const std::wstring& commandId : mProcessManager.runningIds()) {
        mProcessManager.stop(commandId);
    }
}

void Application::beginExit() {
    if (mExitFinished) {
        return;
    }
    mExitRequested = true;
    stopAll();
    if (mProcessManager.runningIds().empty()) {
        finishExit();
        return;
    }
    if (mWindow != nullptr) {
        mWindow->showStopping();
    }
    if (mExitPollTimer == 0) {
        // For a thread timer (hWnd == nullptr), Windows returns the timer ID
        // that must be used to recognize and destroy the timer.
        mExitPollTimer = SetTimer(nullptr,
                                  EXIT_POLL_TIMER_REQUEST,
                                  100,
                                  nullptr);
    }
}

void Application::finishExit() {
    if (mExitFinished) {
        return;
    }
    mExitFinished = true;
    if (mExitPollTimer != 0) {
        KillTimer(nullptr, mExitPollTimer);
        mExitPollTimer = 0;
    }
    const auto saved = mStore.save(mConfiguration);
    if (!saved) {
        MessageBoxW(mWindow != nullptr ? mWindow->window() : nullptr,
                    saved.error().c_str(),
                    L"Save Failed",
                    MB_OK | MB_ICONERROR);
    }
    mTray.reset();
    mWindow.reset();
    mProcessManager.close();
    PostQuitMessage(0);
}

void Application::postDeferred(UINT message) const noexcept {
    PostThreadMessageW(mThreadId, message, 0, 0);
}

}  // namespace command_runner
