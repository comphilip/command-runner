#pragma once

#include "core/models.h"

#include <optional>

#include <windows.h>

namespace command_runner::ui {

class CommandDialog final {
public:
    [[nodiscard]] static std::optional<CommandConfig> show(
        HWND owner,
        HINSTANCE instance,
        const CommandConfig* initialValue = nullptr);

private:
    struct DialogState;

    static INT_PTR CALLBACK dialogProc(HWND dialog,
                                       UINT message,
                                       WPARAM wParam,
                                       LPARAM lParam);
    static LRESULT CALLBACK commandLineProc(HWND control,
                                            UINT message,
                                            WPARAM wParam,
                                            LPARAM lParam);
    static void updateControlFont(DialogState& state);
    static void layoutControls(DialogState& state);
    static void save(DialogState& state);
    static void browseForDirectory(DialogState& state);
};

}  // namespace command_runner::ui
