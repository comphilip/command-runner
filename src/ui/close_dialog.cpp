#include "ui/close_dialog.h"

#include "resource.h"

#include <string>

namespace command_runner::ui {

CloseAction CloseDialog::show(HWND owner,
                              HINSTANCE instance,
                              std::size_t runningCount) {
    DialogState state{runningCount, CloseAction::CANCEL};
    DialogBoxParamW(instance,
                    MAKEINTRESOURCEW(IDD_CLOSE_DIALOG),
                    owner,
                    &CloseDialog::dialogProc,
                    reinterpret_cast<LPARAM>(&state));
    return state.mAction;
}

INT_PTR CALLBACK CloseDialog::dialogProc(HWND dialog,
                                         UINT message,
                                         WPARAM wParam,
                                         LPARAM lParam) {
    auto* state = reinterpret_cast<DialogState*>(
        GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<DialogState*>(lParam);
        if (state == nullptr) {
            return FALSE;
        }
        SetWindowLongPtrW(dialog,
                          DWLP_USER,
                          reinterpret_cast<LONG_PTR>(state));
        const std::wstring messageText =
            std::to_wstring(state->mRunningCount) +
            L" command(s) are still running. Choose an action.";
        SetDlgItemTextW(dialog, IDC_CLOSE_MESSAGE, messageText.c_str());
        SetFocus(GetDlgItem(dialog, IDC_CLOSE_CANCEL));
        return FALSE;
    }
    if (state == nullptr) {
        return FALSE;
    }

    if (message == WM_COMMAND) {
        switch (LOWORD(wParam)) {
        case IDC_CLOSE_STOP_AND_EXIT:
            state->mAction = CloseAction::EXIT;
            EndDialog(dialog, IDC_CLOSE_STOP_AND_EXIT);
            return TRUE;
        case IDC_CLOSE_CANCEL:
        case IDCANCEL:
            state->mAction = CloseAction::CANCEL;
            EndDialog(dialog, IDC_CLOSE_CANCEL);
            return TRUE;
        case IDC_CLOSE_TRAY:
            state->mAction = CloseAction::TRAY;
            EndDialog(dialog, IDC_CLOSE_TRAY);
            return TRUE;
        default:
            break;
        }
    }
    if (message == WM_CLOSE) {
        state->mAction = CloseAction::CANCEL;
        EndDialog(dialog, IDC_CLOSE_CANCEL);
        return TRUE;
    }
    return FALSE;
}

}  // namespace command_runner::ui
