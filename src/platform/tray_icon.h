#pragma once

#include <expected>

#include <windows.h>
#include <shellapi.h>

namespace command_runner::platform {

class TrayIconListener {
public:
    virtual ~TrayIconListener() = default;

    virtual void onTrayRestoreRequested() = 0;
    virtual void onTrayStartAllRequested() = 0;
    virtual void onTrayStopAllRequested() = 0;
    virtual void onTrayExitRequested() = 0;
};

class TrayIcon final {
public:
    TrayIcon(HINSTANCE instance, TrayIconListener& listener);
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    [[nodiscard]] std::expected<void, DWORD> create();
    void destroy() noexcept;

private:
    static constexpr UINT TRAY_OPEN = 1;
    static constexpr UINT TRAY_START_ALL = 2;
    static constexpr UINT TRAY_STOP_ALL = 3;
    static constexpr UINT TRAY_EXIT = 4;
    static constexpr UINT TRAY_CALLBACK_MESSAGE = WM_APP + 40;

    inline static constexpr wchar_t WINDOW_CLASS_NAME[] =
        L"CommandRunner.TrayIcon";

    static LRESULT CALLBACK windowProc(HWND window,
                                       UINT message,
                                       WPARAM wParam,
                                       LPARAM lParam);
    [[nodiscard]] std::expected<void, DWORD> registerWindowClass() const;
    [[nodiscard]] bool addIcon();
    void showMenu();
    void dispatchMenuCommand(UINT command);

    HINSTANCE mInstance{};
    TrayIconListener& mListener;
    HWND mWindow{};
    HMENU mMenu{};
    HICON mIcon{};
    NOTIFYICONDATAW mNotifyData{};
    UINT mTaskbarCreated{};
    bool mIconAdded{};
};

}  // namespace command_runner::platform
