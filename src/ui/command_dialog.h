#pragma once

#include "core/models.h"

#include <optional>

#include <windows.h>
#include <shellapi.h>
#include <wxx_wincore.h>
#include <wxx_controls.h>
#include <wxx_dialog.h>
#include <wxx_stdcontrols.h>

namespace command_runner::ui {

class CommandDialog final : public Win32xx::CDialog {
public:
    [[nodiscard]] static std::optional<CommandConfig> show(
        HWND owner,
        HINSTANCE instance,
        const CommandConfig* initialValue = nullptr);

private:
    CommandDialog(CommandConfig draft, bool editing);

    INT_PTR DialogProc(UINT message, WPARAM wParam, LPARAM lParam) override;
    BOOL OnInitDialog() override;
    BOOL OnCommand(WPARAM wParam, LPARAM lParam) override;
    void OnCancel() override;
    void OnClose() override;
    void OnDestroy() override;
    BOOL PreTranslateMessage(MSG& message) override;

    void updateControlFont();
    void layoutControls();
    void save();
    void browseForDirectory();

    CommandConfig mDraft;
    std::optional<CommandConfig> mResult;
    Win32xx::CEdit mCommandLine;
    Win32xx::CComboBox mEncoding;
    Win32xx::CButton mShell;
    Win32xx::CButton mAutoStart;
    int mFixedWindowHeight{};
    bool mEditing{};
    HFONT mUiFont{};
};

}  // namespace command_runner::ui
