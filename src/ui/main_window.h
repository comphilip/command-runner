#pragma once

#include "core/config_store.h"

#include <windows.h>

namespace command_runner::ui {

class MainWindow final {
public:
    MainWindow(HINSTANCE instance, ConfigData configuration);

    [[nodiscard]] bool create(int show_command);
    [[nodiscard]] int run_message_loop() const;

private:
    static constexpr wchar_t kWindowClassName[] = L"CommandRunner.MainWindow";

    static LRESULT CALLBACK window_proc(HWND window, UINT message,
                                        WPARAM wparam, LPARAM lparam);
    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam);
    static int scale_for_window(HWND window, int value);

    HINSTANCE instance_{};
    HWND window_{};
    ConfigData configuration_;
};

}  // namespace command_runner::ui

