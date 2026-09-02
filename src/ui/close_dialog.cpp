#include "ui/close_dialog.h"

#include "resource.h"

#include <array>
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
        state->mDialog = dialog;
        SetWindowLongPtrW(dialog,
                          DWLP_USER,
                          reinterpret_cast<LONG_PTR>(state));
        const std::wstring messageText =
            std::to_wstring(state->mRunningCount) +
            L" command(s) are still running. Choose an action.";
        SetDlgItemTextW(dialog, IDC_CLOSE_MESSAGE, messageText.c_str());
        updateControlFont(*state);
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
    if (message == WM_DPICHANGED) {
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        if (suggested != nullptr) {
            SetWindowPos(dialog,
                         nullptr,
                         suggested->left,
                         suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        updateControlFont(*state);
        return TRUE;
    }
    if (message == WM_DESTROY) {
        if (state->mUiFont != nullptr) {
            DeleteObject(state->mUiFont);
            state->mUiFont = nullptr;
        }
        return TRUE;
    }
    return FALSE;
}

void CloseDialog::updateControlFont(DialogState& state) {
    if (state.mDialog == nullptr) {
        return;
    }
    UINT dpi = GetDpiForWindow(state.mDialog);
    if (dpi == 0) {
        dpi = 96;
    }
    const HFONT newFont = CreateFontW(-MulDiv(9,
                                               static_cast<int>(dpi),
                                               72),
                                      0,
                                      0,
                                      0,
                                      FW_NORMAL,
                                      FALSE,
                                      FALSE,
                                      FALSE,
                                      DEFAULT_CHARSET,
                                      OUT_DEFAULT_PRECIS,
                                      CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY,
                                      VARIABLE_PITCH | FF_SWISS,
                                      L"Segoe UI");
    if (newFont == nullptr) {
        return;
    }

    const HFONT oldFont = state.mUiFont;
    state.mUiFont = newFont;
    const std::array<int, 4> controlIds{
        IDC_CLOSE_MESSAGE,
        IDC_CLOSE_STOP_AND_EXIT,
        IDC_CLOSE_CANCEL,
        IDC_CLOSE_TRAY,
    };
    for (const int controlId : controlIds) {
        const HWND control = GetDlgItem(state.mDialog, controlId);
        if (control != nullptr) {
            SendMessageW(control,
                         WM_SETFONT,
                         reinterpret_cast<WPARAM>(state.mUiFont),
                         TRUE);
        }
    }
    if (oldFont != nullptr) {
        DeleteObject(oldFont);
    }
}

}  // namespace command_runner::ui
