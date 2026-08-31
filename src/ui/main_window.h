#pragma once

#include "core/config_store.h"

#include <expected>

#include <windows.h>

namespace command_runner::ui {

class MainWindow final {
public:
    MainWindow(HINSTANCE instance, ConfigData configuration);

    [[nodiscard]] std::expected<void, DWORD> create(int showCommand);
    [[nodiscard]] std::expected<int, DWORD> runMessageLoop() const;

private:
    static constexpr int DEFAULT_DPI = 96;
    static constexpr int INITIAL_WINDOW_WIDTH = 900;
    static constexpr int INITIAL_WINDOW_HEIGHT = 620;
    static constexpr int MINIMUM_WINDOW_WIDTH = 640;
    static constexpr int MINIMUM_WINDOW_HEIGHT = 420;
    inline static constexpr wchar_t WINDOW_CLASS_NAME[] = L"CommandRunner.MainWindow";

    static LRESULT CALLBACK windowProc(HWND window, UINT message,
                                       WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    static int scaleForWindow(HWND window, int value);

    HINSTANCE mInstance{};
    HWND mWindow{};
    ConfigData mConfiguration;
};

}  // namespace command_runner::ui
