#include "ui/close_dialog.h"

#include <string>

#include <shellapi.h>
#include <win32xx/wxx_taskdialog.h>

namespace command_runner::ui {

CloseAction CloseDialog::show(HWND owner,
                              std::size_t runningCount) {
    constexpr int STOP_AND_EXIT = 1;
    constexpr int MINIMIZE_TO_TRAY = 2;
    constexpr int KEEP_OPEN = 3;

    if (Win32xx::CTaskDialog::IsSupported()) {
        Win32xx::CTaskDialog taskDialog;
        taskDialog.SetWindowTitle(L"Commands Are Still Running");
        taskDialog.SetMainInstruction(L"Commands are still running");
        taskDialog.SetContent((std::to_wstring(runningCount) +
                               L" command(s) are still running.")
                                  .c_str());
        taskDialog.AddCommandControl(STOP_AND_EXIT,
                                      L"Stop commands and exit");
        taskDialog.AddCommandControl(MINIMIZE_TO_TRAY,
                                      L"Minimize to tray");
        taskDialog.AddCommandControl(KEEP_OPEN,
                                      L"Cancel and keep the application open");
        taskDialog.SetDefaultButton(KEEP_OPEN);
        taskDialog.SetOptions(TDF_ALLOW_DIALOG_CANCELLATION |
                              TDF_USE_COMMAND_LINKS);
        try {
            taskDialog.DoModal(owner);
            switch (taskDialog.GetSelectedButtonID()) {
            case STOP_AND_EXIT:
                return CloseAction::EXIT;
            case MINIMIZE_TO_TRAY:
                return CloseAction::TRAY;
            case KEEP_OPEN:
            default:
                return CloseAction::CANCEL;
            }
        } catch (const Win32xx::CException&) {
            // Keep the native fallback below for systems where the task dialog
            // API is present but cannot be created.
        }
    }

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
