#include "ui/close_dialog.h"

#include <string>

namespace command_runner::ui {

CloseAction CloseDialog::show(HWND owner,
                              std::size_t runningCount) {
    const std::wstring message =
        std::to_wstring(runningCount) +
        L" command(s) are still running.\n\n"
        L"Yes: Stop commands and exit\n"
        L"No: Minimize to tray\n"
        L"Cancel: Keep Command Runner open.";
    const int result = MessageBoxW(owner,
                                   message.c_str(),
                                   L"Commands Are Still Running",
                                   MB_YESNOCANCEL | MB_DEFBUTTON3);
    switch (result) {
    case IDYES:
        return CloseAction::EXIT;
    case IDNO:
        return CloseAction::TRAY;
    case IDCANCEL:
    default:
        return CloseAction::CANCEL;
    }
}

}  // namespace command_runner::ui
