#pragma once

#include <cstddef>

#include <windows.h>

namespace command_runner::ui {

enum class CloseAction {
    EXIT,
    CANCEL,
    TRAY,
};

class CloseDialog final {
public:
    [[nodiscard]] static CloseAction show(HWND owner,
                                          HINSTANCE instance,
                                          std::size_t runningCount);

private:
    struct DialogState {
        std::size_t mRunningCount{};
        CloseAction mAction{CloseAction::CANCEL};
    };

    static INT_PTR CALLBACK dialogProc(HWND dialog,
                                       UINT message,
                                       WPARAM wParam,
                                       LPARAM lParam);
};

}  // namespace command_runner::ui
