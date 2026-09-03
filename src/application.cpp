#include "application.h"

#include "ui/close_dialog.h"

#include <exception>
#include <string>
#include <utility>

namespace command_runner {
namespace {

[[nodiscard]] DWORD errorOr(DWORD fallback) noexcept {
    const DWORD error = GetLastError();
    return error == ERROR_SUCCESS ? fallback : error;
}

}  // namespace

Application::Application(HINSTANCE instance,
                         ConfigData configuration,
                         ConfigStore& store,
                         ProcessManager& processManager)
    : mInstance(instance),
      mConfiguration(std::move(configuration)),
      mStore(store),
      mProcessManager(processManager),
      mThreadId(GetCurrentThreadId()) {}

Application::~Application() {
    if (mExitPollTimer != 0) {
        if (mExitTimerOwner != nullptr) {
            mExitTimerOwner->KillTimer(mExitPollTimer);
        } else {
            ::KillTimer(nullptr, mExitPollTimer);
        }
        mExitPollTimer = 0;
        mExitTimerOwner = nullptr;
    }
    mTray.reset();
    mWindow.reset();
}

std::expected<int, DWORD> Application::run(int showCommand) {
    mShowCommand = showCommand;
    mStartupError = ERROR_SUCCESS;

    MSG queuedMessage{};
    PeekMessageW(&queuedMessage,
                 nullptr,
                 WM_USER,
                 WM_USER,
                 PM_NOREMOVE);

    const int result = CWinApp::Run();
    if (mStartupError != ERROR_SUCCESS) {
        return std::unexpected(mStartupError);
    }
    if (result < 0) {
        return std::unexpected(errorOr(ERROR_FUNCTION_FAILED));
    }
    return result;
}

BOOL Application::InitInstance() {
    try {
        for (const auto& command : mConfiguration.mCommands) {
            if (command.mAutoStart) {
                mProcessManager.start(command);
            }
        }
        const auto created = createWindow(mShowCommand);
        if (!created) {
            mStartupError = created.error();
            return FALSE;
        }
    } catch (const Win32xx::CException& exception) {
        mStartupError = exception.GetError() == ERROR_SUCCESS
                            ? ERROR_FUNCTION_FAILED
                            : exception.GetError();
        return FALSE;
    } catch (const std::bad_alloc&) {
        mStartupError = ERROR_NOT_ENOUGH_MEMORY;
        return FALSE;
    }
    return TRUE;
}

BOOL Application::PreTranslateMessage(MSG& message) {
    if (mExitPollTimer != 0 && message.message == WM_TIMER &&
        static_cast<UINT_PTR>(message.wParam) == mExitPollTimer) {
        const bool belongsToOwner =
            mExitTimerOwner == nullptr
                ? message.hwnd == nullptr
                : message.hwnd == static_cast<HWND>(*mExitTimerOwner);
        if (belongsToOwner) {
            if (mExitRequested && mProcessManager.runningIds().empty()) {
                finishExit();
            }
            return TRUE;
        }
    }

    if (message.hwnd == nullptr && message.message >= WM_APP &&
        message.message < WM_APP + 100) {
        handleDeferredMessage(message.message);
        return TRUE;
    }

    if (mWindow != nullptr &&
        IsDialogMessageW(mWindow->window(), &message) != FALSE) {
        return TRUE;
    }
    return CWinApp::PreTranslateMessage(message);
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
    try {
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
    } catch (const Win32xx::CException& exception) {
        mWindow.reset();
        const DWORD error = exception.GetError();
        return std::unexpected(error == ERROR_SUCCESS
                                   ? ERROR_FUNCTION_FAILED
                                   : error);
    } catch (const std::bad_alloc&) {
        mWindow.reset();
        return std::unexpected(ERROR_NOT_ENOUGH_MEMORY);
    }
    return {};
}

bool Application::createTray() {
    if (mTray != nullptr) {
        return true;
    }
    try {
        auto tray = std::make_unique<platform::TrayIcon>(mInstance, *this);
        const auto created = tray->create();
        if (!created) {
            return false;
        }
        mTray = std::move(tray);
    } catch (const Win32xx::CException&) {
        return false;
    } catch (const std::bad_alloc&) {
        return false;
    }
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
    if (running.empty()) {
        beginExit();
        return;
    }

    const HWND owner = mWindow != nullptr ? mWindow->window() : nullptr;
    const ui::CloseAction action = ui::CloseDialog::show(owner, running.size());
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
        ::MessageBoxW(mWindow->window(),
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
        mWindow->ShowWindow(SW_RESTORE);
        mWindow->SetForegroundWindow();
        return;
    }

    // Release the tray before recreating the main window so a restore/minimize
    // cycle never retains a stale menu, hidden window, or callback target.
    mTray.reset();
    const auto created = createWindow(SW_SHOWNORMAL);
    if (!created) {
        ::MessageBoxW(nullptr,
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
        if (mWindow != nullptr && mWindow->IsWindow()) {
            mExitTimerOwner = mWindow.get();
            mExitPollTimer = mWindow->SetTimer(EXIT_POLL_TIMER_REQUEST,
                                                100,
                                                nullptr);
        } else if (mTray != nullptr && mTray->IsWindow()) {
            mExitTimerOwner = mTray.get();
            mExitPollTimer = mTray->SetTimer(EXIT_POLL_TIMER_REQUEST,
                                              100,
                                              nullptr);
        }

        if (mExitPollTimer == 0) {
            // The shutdown state can outlive the main window. This small
            // thread timer is the narrow native exception when no CWnd owner
            // can accept a timer.
            mExitTimerOwner = nullptr;
            mExitPollTimer = ::SetTimer(nullptr,
                                        EXIT_POLL_TIMER_REQUEST,
                                        100,
                                        nullptr);
        }
    }
}

void Application::finishExit() {
    if (mExitFinished) {
        return;
    }
    mExitFinished = true;
    if (mExitPollTimer != 0) {
        if (mExitTimerOwner != nullptr) {
            mExitTimerOwner->KillTimer(mExitPollTimer);
        } else {
            ::KillTimer(nullptr, mExitPollTimer);
        }
        mExitPollTimer = 0;
        mExitTimerOwner = nullptr;
    }

    const auto saved = mStore.save(mConfiguration);
    if (!saved) {
        ::MessageBoxW(mWindow != nullptr ? mWindow->window() : nullptr,
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
