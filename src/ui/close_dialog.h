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
                                          std::size_t runningCount);
};

}  // namespace command_runner::ui
