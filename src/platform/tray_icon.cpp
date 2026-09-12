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
    if (GetHwnd() != nullptr) {
        return {};
    }

    mIcon = GetApp()->LoadIcon(IDI_COMMAND_RUNNER);
    if (mIcon == nullptr) {
        mIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    if (mIcon == nullptr) {
        return std::unexpected(effectiveError(GetLastError(),
                                               ERROR_RESOURCE_NAME_NOT_FOUND));
    }

    try {
        mMenu.CreatePopupMenu();
        mMenu.AppendMenu(MF_STRING, TRAY_OPEN, L"&Open");
        mMenu.AppendMenu(MF_SEPARATOR, 0, static_cast<LPCTSTR>(nullptr));
        mMenu.AppendMenu(MF_STRING, TRAY_STOP_ALL, L"S&top All");
        mMenu.AppendMenu(MF_STRING, TRAY_START_ALL, L"&Start All");
        mMenu.AppendMenu(MF_SEPARATOR, 0, static_cast<LPCTSTR>(nullptr));
        mMenu.AppendMenu(MF_STRING, TRAY_EXIT, L"E&xit");
        Create();
    } catch (const Win32xx::CException& exception) {
        const DWORD error = effectiveError(exception.GetError(),
                                           ERROR_FUNCTION_FAILED);
        destroy();
        return std::unexpected(error);
    }

    mNotifyData = {};
    mNotifyData.cbSize = sizeof(NOTIFYICONDATAW);
    mNotifyData.hWnd = GetHwnd();
    mNotifyData.uID = 1;
    mNotifyData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
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
    if (GetHwnd() != nullptr && ::IsWindow(GetHwnd()) != FALSE) {
        Destroy();
    }
    mMenu.Destroy();
    mIcon = nullptr;
    mNotifyData = {};
}

LRESULT TrayIcon::WndProc(UINT message, WPARAM wParam, LPARAM lParam) {
    if (mTaskbarCreated != 0 && message == mTaskbarCreated) {
        mIconAdded = false;
        const bool restored = addIcon();
        (void)restored;
        return 0;
    }
    if (message == TRAY_CALLBACK_MESSAGE) {
        const UINT trayEvent = LOWORD(lParam);
        if (trayEvent == WM_LBUTTONUP ||
            trayEvent == WM_LBUTTONDBLCLK || trayEvent == NIN_SELECT ||
            trayEvent == NIN_KEYSELECT) {
            mListener.onTrayRestoreRequested();
        } else if (trayEvent == WM_RBUTTONUP ||
                   trayEvent == WM_CONTEXTMENU) {
            showMenu();
        }
        return 0;
    }
    return WndProcDefault(message, wParam, lParam);
}

void TrayIcon::PreRegisterClass(WNDCLASS& windowClass) {
    windowClass.lpszClassName = WINDOW_CLASS_NAME;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
}

void TrayIcon::PreCreate(CREATESTRUCT& createStruct) {
    createStruct.dwExStyle = WS_EX_TOOLWINDOW;
    createStruct.style = WS_POPUP;
    createStruct.x = 0;
    createStruct.y = 0;
    createStruct.cx = 0;
    createStruct.cy = 0;
    createStruct.lpszName = L"Command Runner";
}

bool TrayIcon::addIcon() {
    if (GetHwnd() == nullptr) {
        return false;
    }
    if (mIconAdded) {
        return true;
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
    if (mMenu == nullptr || GetHwnd() == nullptr) {
        return;
    }
    const Win32xx::CPoint cursor = Win32xx::GetCursorPos();
    SetForegroundWindow();
    const UINT command = mMenu.TrackPopupMenu(
        TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        cursor.x,
        cursor.y,
        GetHwnd(),
        nullptr);
    PostMessage(WM_NULL);
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
