#include "ui/command_dialog.h"

#include "resource.h"

#include <shlobj.h>

#include <wil/resource.h>

#include <algorithm>
#include <cwctype>
#include <memory>
#include <string>
#include <vector>

namespace command_runner::ui {
namespace {

constexpr UINT WM_COMMAND_DIALOG_SAVE = WM_APP + 20;
constexpr int DIALOG_MINIMUM_WIDTH = 430;
constexpr int DIALOG_LABEL_WIDTH = 104;
constexpr int DIALOG_INPUT_LEFT = 120;
constexpr int DIALOG_MARGIN = 8;
constexpr int DIALOG_BUTTON_HEIGHT = 20;
constexpr int DIALOG_OPTIONS_HEIGHT = 68;

std::wstring readText(HWND control) {
    if (control == nullptr) {
        return {};
    }
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) {
        return {};
    }
    std::vector<wchar_t> buffer(static_cast<std::size_t>(length) + 1);
    const int written = GetWindowTextW(control,
                                       buffer.data(),
                                       static_cast<int>(buffer.size()));
    return std::wstring(buffer.data(), static_cast<std::size_t>(written));
}

std::wstring trim(std::wstring value) {
    const auto isSpace = [](wchar_t value) {
        return std::iswspace(static_cast<wint_t>(value)) != 0;
    };
    const auto first = std::ranges::find_if_not(value, isSpace);
    if (first == value.end()) {
        return {};
    }
    const auto last = std::ranges::find_if_not(value.rbegin(),
                                               value.rend(),
                                               isSpace)
                          .base();
    return std::wstring(first, last);
}

std::wstring defaultWorkingDirectory() {
    const DWORD required = GetCurrentDirectoryW(0, nullptr);
    if (required == 0) {
        return L".";
    }
    std::vector<wchar_t> buffer(static_cast<std::size_t>(required));
    const DWORD written = GetCurrentDirectoryW(required, buffer.data());
    if (written == 0 || written >= required) {
        return L".";
    }
    return std::wstring(buffer.data(), written);
}

std::wstring selectedEncoding(HWND combo) {
    const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (index == CB_ERR) {
        return L"auto";
    }
    const LRESULT length = SendMessageW(combo, CB_GETLBTEXTLEN, index, 0);
    if (length <= 0) {
        return L"auto";
    }
    std::vector<wchar_t> buffer(static_cast<std::size_t>(length) + 1);
    SendMessageW(combo,
                 CB_GETLBTEXT,
                 index,
                 reinterpret_cast<LPARAM>(buffer.data()));
    return std::wstring(buffer.data(), static_cast<std::size_t>(length));
}

std::string narrowEncoding(std::wstring_view value) {
    std::string result;
    result.reserve(value.size());
    for (const wchar_t character : value) {
        result.push_back(static_cast<char>(character));
    }
    return result;
}

}  // namespace

struct CommandDialog::DialogState {
    HINSTANCE mInstance{};
    HWND mDialog{};
    HWND mCommandLine{};
    WNDPROC mCommandLinePreviousProc{};
    int mFixedWindowHeight{};
    bool mEditing{};
    CommandConfig mDraft;
    std::optional<CommandConfig> mResult;
};

std::optional<CommandConfig> CommandDialog::show(
    HWND owner,
    HINSTANCE instance,
    const CommandConfig* initialValue) {
    CommandConfig draft;
    if (initialValue != nullptr) {
        draft = *initialValue;
    } else {
        draft = CommandConfig(L"", defaultWorkingDirectory(), L"");
    }

    DialogState state{instance,
                      nullptr,
                      nullptr,
                      nullptr,
                      0,
                      initialValue != nullptr,
                      std::move(draft),
                      std::nullopt};
    const INT_PTR result = DialogBoxParamW(instance,
                                           MAKEINTRESOURCEW(IDD_COMMAND_DIALOG),
                                           owner,
                                           &CommandDialog::dialogProc,
                                           reinterpret_cast<LPARAM>(&state));
    if (result == -1) {
        return std::nullopt;
    }
    return state.mResult;
}

INT_PTR CALLBACK CommandDialog::dialogProc(HWND dialog,
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
        SetWindowTextW(dialog,
                       state->mEditing ? L"Edit Command" : L"Add Command");
        SetDlgItemTextW(dialog, IDC_COMMAND_NAME, state->mDraft.mName.c_str());
        SetDlgItemTextW(dialog,
                        IDC_COMMAND_WORKING_DIRECTORY,
                        state->mDraft.mWorkingDirectory.c_str());
        SetDlgItemTextW(dialog,
                        IDC_COMMAND_LINE,
                        state->mDraft.mCommandLine.c_str());

        const HWND encoding = GetDlgItem(dialog, IDC_COMMAND_ENCODING);
        for (const wchar_t* value : {L"auto", L"gbk", L"utf-8", L"system"}) {
            SendMessageW(encoding,
                         CB_ADDSTRING,
                         0,
                         reinterpret_cast<LPARAM>(value));
        }
        const std::wstring encodingValue(state->mDraft.mEncoding.begin(),
                                         state->mDraft.mEncoding.end());
        const LRESULT encodingIndex = SendMessageW(
            encoding,
            CB_FINDSTRINGEXACT,
            static_cast<WPARAM>(-1),
            reinterpret_cast<LPARAM>(encodingValue.c_str()));
        SendMessageW(encoding,
                     CB_SETCURSEL,
                     encodingIndex == CB_ERR ? 0 : encodingIndex,
                     0);
        CheckDlgButton(dialog,
                       IDC_COMMAND_SHELL,
                       state->mDraft.mShell ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog,
                       IDC_COMMAND_AUTO_START,
                       state->mDraft.mAutoStart ? BST_CHECKED : BST_UNCHECKED);

        state->mCommandLine = GetDlgItem(dialog, IDC_COMMAND_LINE);
        SetWindowLongPtrW(state->mCommandLine,
                          GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(state));
        SetLastError(ERROR_SUCCESS);
        state->mCommandLinePreviousProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(state->mCommandLine,
                              GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(
                                  &CommandDialog::commandLineProc)));
        RECT windowRect{};
        GetWindowRect(dialog, &windowRect);
        state->mFixedWindowHeight = windowRect.bottom - windowRect.top;
        layoutControls(*state);
        SetFocus(GetDlgItem(dialog, IDC_COMMAND_NAME));
        return FALSE;
    }
    if (state == nullptr) {
        return FALSE;
    }

    switch (message) {
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        if (limits != nullptr) {
            RECT windowRect{};
            GetWindowRect(dialog, &windowRect);
            const int currentWidth = windowRect.right - windowRect.left;
            limits->ptMinTrackSize.x = std::max(
                currentWidth,
                DIALOG_MINIMUM_WIDTH);
            limits->ptMinTrackSize.y = state->mFixedWindowHeight;
            limits->ptMaxTrackSize.y = state->mFixedWindowHeight;
            return TRUE;
        }
        break;
    }
    case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        if (suggested != nullptr) {
            SetWindowPos(dialog,
                         nullptr,
                         suggested->left,
                         suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            state->mFixedWindowHeight = suggested->bottom - suggested->top;
        }
        layoutControls(*state);
        return TRUE;
    }
    case WM_SIZE:
        layoutControls(*state);
        return TRUE;
    case WM_COMMAND_DIALOG_SAVE:
        save(*state);
        return TRUE;
    case WM_COMMAND: {
        const int controlId = LOWORD(wParam);
        if (controlId == IDC_COMMAND_SAVE || controlId == IDOK) {
            save(*state);
            return TRUE;
        }
        if (controlId == IDC_COMMAND_CANCEL || controlId == IDCANCEL) {
            EndDialog(dialog, IDC_COMMAND_CANCEL);
            return TRUE;
        }
        if (controlId == IDC_COMMAND_BROWSE) {
            browseForDirectory(*state);
            return TRUE;
        }
        break;
    }
    case WM_CLOSE:
        EndDialog(dialog, IDC_COMMAND_CANCEL);
        return TRUE;
    }
    return FALSE;
}

LRESULT CALLBACK CommandDialog::commandLineProc(HWND control,
                                                UINT message,
                                                WPARAM wParam,
                                                LPARAM lParam) {
    auto* state = reinterpret_cast<DialogState*>(
        GetWindowLongPtrW(control, GWLP_USERDATA));
    if (state != nullptr && message == WM_KEYDOWN) {
        const bool controlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (wParam == VK_RETURN && controlPressed) {
            PostMessageW(state->mDialog, WM_COMMAND_DIALOG_SAVE, 0, 0);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            PostMessageW(state->mDialog,
                         WM_COMMAND,
                         MAKEWPARAM(IDC_COMMAND_CANCEL, BN_CLICKED),
                         0);
            return 0;
        }
    }
    if (state == nullptr || state->mCommandLinePreviousProc == nullptr) {
        return DefWindowProcW(control, message, wParam, lParam);
    }
    return CallWindowProcW(state->mCommandLinePreviousProc,
                           control,
                           message,
                           wParam,
                           lParam);
}

void CommandDialog::layoutControls(DialogState& state) {
    if (state.mDialog == nullptr) {
        return;
    }
    RECT client{};
    GetClientRect(state.mDialog, &client);
    const int width = std::max(0L, client.right - client.left);
    const int height = std::max(0L, client.bottom - client.top);
    const int inputWidth = std::max(
        80,
        width - DIALOG_INPUT_LEFT - DIALOG_MARGIN);
    const int browseWidth = 70;
    const int workingWidth = std::max(
        80,
        width - DIALOG_INPUT_LEFT - DIALOG_MARGIN - browseWidth - 6);
    const int optionsY = std::max(140, height - DIALOG_OPTIONS_HEIGHT);
    const int lineHeight = std::max(40, optionsY - 68);
    const int buttonY = std::max(0, height - DIALOG_MARGIN - DIALOG_BUTTON_HEIGHT);

    SetWindowPos(GetDlgItem(state.mDialog, IDC_COMMAND_NAME),
                 nullptr,
                 DIALOG_INPUT_LEFT,
                 8,
                 inputWidth,
                 20,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(state.mDialog, IDC_COMMAND_WORKING_DIRECTORY),
                 nullptr,
                 DIALOG_INPUT_LEFT,
                 34,
                 workingWidth,
                 20,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(state.mDialog, IDC_COMMAND_BROWSE),
                 nullptr,
                 width - DIALOG_MARGIN - browseWidth,
                 33,
                 browseWidth,
                 22,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(state.mCommandLine,
                 nullptr,
                 DIALOG_INPUT_LEFT,
                 60,
                 inputWidth,
                 lineHeight,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(state.mDialog, IDC_COMMAND_ENCODING),
                 nullptr,
                 DIALOG_INPUT_LEFT,
                 optionsY - 26,
                 112,
                 100,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(state.mDialog, IDC_COMMAND_SHELL),
                 nullptr,
                 DIALOG_INPUT_LEFT,
                 optionsY,
                 80,
                 20,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(state.mDialog, IDC_COMMAND_AUTO_START),
                 nullptr,
                 DIALOG_INPUT_LEFT + 90,
                 optionsY,
                 100,
                 20,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    const int cancelWidth = 52;
    const int saveWidth = 52;
    SetWindowPos(GetDlgItem(state.mDialog, IDC_COMMAND_CANCEL),
                 nullptr,
                 width - DIALOG_MARGIN - cancelWidth,
                 buttonY,
                 cancelWidth,
                 DIALOG_BUTTON_HEIGHT,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(state.mDialog, IDC_COMMAND_SAVE),
                 nullptr,
                 width - DIALOG_MARGIN - cancelWidth - 6 - saveWidth,
                 buttonY,
                 saveWidth,
                 DIALOG_BUTTON_HEIGHT,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void CommandDialog::save(DialogState& state) {
    const std::wstring name = trim(
        readText(GetDlgItem(state.mDialog, IDC_COMMAND_NAME)));
    const std::wstring workingDirectory = trim(
        readText(GetDlgItem(state.mDialog, IDC_COMMAND_WORKING_DIRECTORY)));
    const std::wstring commandLine = readText(
        GetDlgItem(state.mDialog, IDC_COMMAND_LINE));
    if (name.empty() || trim(commandLine).empty()) {
        MessageBoxW(state.mDialog,
                    L"Name and command line are required.",
                    L"Invalid Command",
                    MB_OK | MB_ICONWARNING);
        if (name.empty()) {
            SetFocus(GetDlgItem(state.mDialog, IDC_COMMAND_NAME));
        } else {
            SetFocus(GetDlgItem(state.mDialog, IDC_COMMAND_LINE));
        }
        return;
    }

    const DWORD attributes = GetFileAttributesW(workingDirectory.c_str());
    if (workingDirectory.empty() || attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        MessageBoxW(state.mDialog,
                    L"The working directory does not exist.",
                    L"Invalid Command",
                    MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(state.mDialog, IDC_COMMAND_WORKING_DIRECTORY));
        return;
    }

    const std::wstring encoding = selectedEncoding(
        GetDlgItem(state.mDialog, IDC_COMMAND_ENCODING));
    state.mResult = CommandConfig(
        name,
        workingDirectory,
        commandLine,
        narrowEncoding(encoding),
        state.mDraft.mId,
        IsDlgButtonChecked(state.mDialog, IDC_COMMAND_AUTO_START) == BST_CHECKED,
        IsDlgButtonChecked(state.mDialog, IDC_COMMAND_SHELL) == BST_CHECKED);
    EndDialog(state.mDialog, IDC_COMMAND_SAVE);
}

void CommandDialog::browseForDirectory(DialogState& state) {
    wchar_t displayName[MAX_PATH]{};
    BROWSEINFOW browseInfo{};
    browseInfo.hwndOwner = state.mDialog;
    browseInfo.pszDisplayName = displayName;
    browseInfo.lpszTitle = L"Select Working Directory";
    browseInfo.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    using Pidl = std::unique_ptr<ITEMIDLIST __unaligned,
                                  decltype(&CoTaskMemFree)>;
    Pidl selected(
        SHBrowseForFolderW(&browseInfo),
        &CoTaskMemFree);
    if (!selected) {
        return;
    }
    wchar_t path[MAX_PATH]{};
    if (SHGetPathFromIDListW(selected.get(), path) != FALSE) {
        SetDlgItemTextW(state.mDialog, IDC_COMMAND_WORKING_DIRECTORY, path);
    }
}

}  // namespace command_runner::ui
