#include "platform/tray_icon.h"

#include "resource.h"

#include <algorithm>
#include <array>
#include <iterator>

namespace command_runner::platform {
namespace {

DWORD effectiveError(DWORD error, DWORD fallback) {
    return error == ERROR_SUCCESS ? fallback : error;
}

}  // namespace

TrayIcon::TrayIcon(HINSTANCE instance, TrayIconListener& listener)
    : mInstance(instance),
      mListener(listener),
      mTaskbarCreated(RegisterWindowMessageW(L"TaskbarCreated")) {}

TrayIcon::~TrayIcon() {
    destroy();
}

std::expected<void, DWORD> TrayIcon::create() {
    if (mWindow != nullptr) {
        return {};
    }

    const auto registered = registerWindowClass();
    if (!registered) {
        return registered;
    }

    mIcon = LoadIconW(mInstance,
                      MAKEINTRESOURCEW(IDI_COMMAND_RUNNER));
    if (mIcon == nullptr) {
        mIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    if (mIcon == nullptr) {
        return std::unexpected(effectiveError(GetLastError(),
                                               ERROR_RESOURCE_NAME_NOT_FOUND));
    }

    mMenu = CreatePopupMenu();
    if (mMenu == nullptr) {
        return std::unexpected(effectiveError(GetLastError(),
                                               ERROR_NOT_ENOUGH_MEMORY));
    }
    AppendMenuW(mMenu, MF_STRING, TRAY_OPEN, L"&Open");
    AppendMenuW(mMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(mMenu, MF_STRING, TRAY_START_ALL, L"&Start All");
    AppendMenuW(mMenu, MF_STRING, TRAY_STOP_ALL, L"S&top All");
    AppendMenuW(mMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(mMenu, MF_STRING, TRAY_EXIT, L"E&xit");

    mWindow = CreateWindowExW(0,
                              WINDOW_CLASS_NAME,
                              L"Command Runner",
                              0,
                              0,
                              0,
                              0,
                              0,
                              HWND_MESSAGE,
                              nullptr,
                              mInstance,
                              this);
    if (mWindow == nullptr) {
        const DWORD error = effectiveError(GetLastError(),
                                           ERROR_FUNCTION_FAILED);
        destroy();
        return std::unexpected(error);
    }

    mNotifyData = {};
    mNotifyData.cbSize = sizeof(NOTIFYICONDATAW);
    mNotifyData.hWnd = mWindow;
    mNotifyData.uID = 1;
    mNotifyData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    mNotifyData.uCallbackMessage = TRAY_CALLBACK_MESSAGE;
    mNotifyData.hIcon = mIcon;
    constexpr wchar_t TIP[] = L"Command Runner";
    const std::size_t tipLength = std::min(std::size(TIP) - 1,
                                           std::size(mNotifyData.szTip) - 1);
    std::copy_n(TIP, tipLength, mNotifyData.szTip);
    mNotifyData.szTip[tipLength] = L'\0';

    if (!addIcon()) {
        const DWORD error = effectiveError(GetLastError(),
                                           ERROR_FUNCTION_FAILED);
        destroy();
        return std::unexpected(error);
    }
    return {};
}

void TrayIcon::destroy() noexcept {
    if (mIconAdded) {
        Shell_NotifyIconW(NIM_DELETE, &mNotifyData);
        mIconAdded = false;
    }
    if (mWindow != nullptr && IsWindow(mWindow) != FALSE) {
        DestroyWindow(mWindow);
    }
    mWindow = nullptr;
    if (mMenu != nullptr) {
        DestroyMenu(mMenu);
        mMenu = nullptr;
    }
    mIcon = nullptr;
    mNotifyData = {};
}

LRESULT CALLBACK TrayIcon::windowProc(HWND window,
                                      UINT message,
                                      WPARAM wParam,
                                      LPARAM lParam) {
    auto* self = reinterpret_cast<TrayIcon*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        if (createStruct == nullptr || createStruct->lpCreateParams == nullptr) {
            return FALSE;
        }
        self = static_cast<TrayIcon*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(window,
                          GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
    }
    if (self == nullptr) {
        return DefWindowProcW(window, message, wParam, lParam);
    }

    if (self->mTaskbarCreated != 0 && message == self->mTaskbarCreated) {
        self->mIconAdded = false;
        const bool restored = self->addIcon();
        (void)restored;
        return 0;
    }
    if (message == TRAY_CALLBACK_MESSAGE) {
        if (lParam == WM_LBUTTONDBLCLK) {
            self->mListener.onTrayRestoreRequested();
        } else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
            self->showMenu();
        }
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

std::expected<void, DWORD> TrayIcon::registerWindowClass() const {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.hInstance = mInstance;
    windowClass.lpfnWndProc = &TrayIcon::windowProc;
    windowClass.lpszClassName = WINDOW_CLASS_NAME;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (RegisterClassExW(&windowClass) == 0) {
        const DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            return std::unexpected(effectiveError(error,
                                                   ERROR_FUNCTION_FAILED));
        }
    }
    return {};
}

bool TrayIcon::addIcon() {
    if (mWindow == nullptr) {
        return false;
    }
    if (!Shell_NotifyIconW(NIM_ADD, &mNotifyData)) {
        return false;
    }
    mIconAdded = true;
    mNotifyData.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &mNotifyData);
    return true;
}

void TrayIcon::showMenu() {
    if (mMenu == nullptr || mWindow == nullptr) {
        return;
    }
    POINT cursor{};
    if (GetCursorPos(&cursor) == FALSE) {
        return;
    }
    SetForegroundWindow(mWindow);
    const UINT command = TrackPopupMenu(mMenu,
                                        TPM_RETURNCMD | TPM_NONOTIFY |
                                            TPM_RIGHTBUTTON,
                                        cursor.x,
                                        cursor.y,
                                        0,
                                        mWindow,
                                        nullptr);
    PostMessageW(mWindow, WM_NULL, 0, 0);
    dispatchMenuCommand(command);
}

void TrayIcon::dispatchMenuCommand(UINT command) {
    switch (command) {
    case TRAY_OPEN:
        mListener.onTrayRestoreRequested();
        break;
    case TRAY_START_ALL:
        mListener.onTrayStartAllRequested();
        break;
    case TRAY_STOP_ALL:
        mListener.onTrayStopAllRequested();
        break;
    case TRAY_EXIT:
        mListener.onTrayExitRequested();
        break;
    default:
        break;
    }
}

}  // namespace command_runner::platform
