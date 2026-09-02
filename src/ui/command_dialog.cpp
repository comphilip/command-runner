#include "ui/command_dialog.h"

#include "resource.h"

#include <wxx_folderdialog.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace command_runner::ui {
namespace {

constexpr int DEFAULT_DPI = 96;
constexpr int DIALOG_MINIMUM_WIDTH = 430;
constexpr int DIALOG_LABEL_WIDTH = 104;
constexpr int DIALOG_INPUT_LEFT = 120;
constexpr int DIALOG_MARGIN = 8;
constexpr int DIALOG_BUTTON_HEIGHT = 20;
constexpr int DIALOG_LABEL_HEIGHT = 14;
constexpr int DIALOG_INPUT_HEIGHT = 20;
constexpr int DIALOG_BROWSE_HEIGHT = 22;
constexpr int DIALOG_BROWSE_WIDTH = 70;
constexpr int DIALOG_COMMAND_LINE_TOP = 114;
constexpr int DIALOG_COMMAND_LINE_MINIMUM_HEIGHT = 40;
constexpr int DIALOG_COMMAND_LINE_BOTTOM_GAP = 8;
constexpr int DIALOG_BUTTON_GAP = 6;
constexpr int DIALOG_BUTTON_WIDTH = 52;

int scaleForWindow(HWND window, int value) {
    UINT dpi = window == nullptr ? GetDpiForSystem() : GetDpiForWindow(window);
    if (dpi == 0) {
        dpi = DEFAULT_DPI;
    }
    return MulDiv(value, static_cast<int>(dpi), DEFAULT_DPI);
}

void positionControl(const Win32xx::CWnd& control,
                     int x,
                     int y,
                     int width,
                     int height) {
    if (!control.IsWindow()) {
        return;
    }
    control.SetWindowPos(nullptr,
                         x,
                         y,
                         width,
                         height,
                         SWP_NOZORDER | SWP_NOACTIVATE);
}

std::wstring readText(const Win32xx::CWnd& control) {
    if (!control.IsWindow()) {
        return {};
    }
    const Win32xx::CString text = control.GetWindowText();
    return std::wstring(text.c_str());
}

std::wstring trim(std::wstring value) {
    const auto isSpace = [](wchar_t character) {
        return std::iswspace(static_cast<wint_t>(character)) != 0;
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

std::wstring selectedEncoding(const Win32xx::CComboBox& combo) {
    const int index = combo.GetCurSel();
    if (index == CB_ERR) {
        return L"auto";
    }
    const int length = combo.GetLBTextLen(index);
    if (length <= 0) {
        return L"auto";
    }
    std::vector<wchar_t> buffer(static_cast<std::size_t>(length) + 1);
    combo.GetLBText(index, buffer.data());
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

std::optional<CommandConfig> CommandDialog::show(
    HWND owner,
    HINSTANCE instance,
    const CommandConfig* initialValue) {
    (void)instance;
    CommandConfig draft;
    if (initialValue != nullptr) {
        draft = *initialValue;
    } else {
        draft = CommandConfig(L"", defaultWorkingDirectory(), L"");
    }

    CommandDialog dialog(std::move(draft), initialValue != nullptr);
    try {
        const INT_PTR result = dialog.DoModal(owner);
        if (result == -1) {
            return std::nullopt;
        }
    } catch (const Win32xx::CException&) {
        return std::nullopt;
    }
    return dialog.mResult;
}

CommandDialog::CommandDialog(CommandConfig draft, bool editing)
    : CDialog(IDD_COMMAND_DIALOG),
      mDraft(std::move(draft)),
      mEditing(editing) {}

BOOL CommandDialog::OnInitDialog() {
    CDialog::OnInitDialog();
    SetWindowText(mEditing ? L"Edit Command" : L"Add Command");
    SetDlgItemText(IDC_COMMAND_NAME, mDraft.mName.c_str());
    SetDlgItemText(IDC_COMMAND_WORKING_DIRECTORY,
                   mDraft.mWorkingDirectory.c_str());
    SetDlgItemText(IDC_COMMAND_LINE, mDraft.mCommandLine.c_str());

    mEncoding.AttachDlgItem(IDC_COMMAND_ENCODING, *this);
    mShell.AttachDlgItem(IDC_COMMAND_SHELL, *this);
    mAutoStart.AttachDlgItem(IDC_COMMAND_AUTO_START, *this);
    mCommandLine.AttachDlgItem(IDC_COMMAND_LINE, *this);

    for (const wchar_t* value : {L"auto", L"gbk", L"utf-8", L"system"}) {
        mEncoding.AddString(value);
    }
    const std::wstring encodingValue(mDraft.mEncoding.begin(),
                                     mDraft.mEncoding.end());
    const int encodingIndex = mEncoding.FindStringExact(-1,
                                                        encodingValue.c_str());
    mEncoding.SetCurSel(encodingIndex == CB_ERR ? 0 : encodingIndex);
    mShell.SetCheck(mDraft.mShell ? BST_CHECKED : BST_UNCHECKED);
    mAutoStart.SetCheck(mDraft.mAutoStart ? BST_CHECKED : BST_UNCHECKED);

    const Win32xx::CRect windowRect = GetWindowRect();
    mFixedWindowHeight = windowRect.bottom - windowRect.top;
    updateControlFont();
    layoutControls();
    GetDlgItem(IDC_COMMAND_NAME).SetFocus();
    return FALSE;
}

BOOL CommandDialog::OnCommand(WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    if (HIWORD(wParam) != BN_CLICKED) {
        return FALSE;
    }
    switch (LOWORD(wParam)) {
    case IDC_COMMAND_SAVE:
        save();
        return TRUE;
    case IDC_COMMAND_CANCEL:
        OnCancel();
        return TRUE;
    case IDC_COMMAND_BROWSE:
        browseForDirectory();
        return TRUE;
    default:
        return FALSE;
    }
}

void CommandDialog::OnCancel() {
    EndDialog(IDC_COMMAND_CANCEL);
}

void CommandDialog::OnClose() {
    OnCancel();
}

void CommandDialog::OnDestroy() {
    if (mUiFont != nullptr) {
        DeleteObject(mUiFont);
        mUiFont = nullptr;
    }
}

BOOL CommandDialog::PreTranslateMessage(MSG& message) {
    if (message.hwnd == mCommandLine.GetHwnd() &&
        message.message == WM_KEYDOWN) {
        const bool controlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (message.wParam == VK_RETURN && controlPressed) {
            save();
            return TRUE;
        }
        if (message.wParam == VK_ESCAPE) {
            OnCancel();
            return TRUE;
        }
    }
    return CDialog::PreTranslateMessage(message);
}

INT_PTR CommandDialog::DialogProc(UINT message,
                                  WPARAM wParam,
                                  LPARAM lParam) {
    switch (message) {
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        if (limits != nullptr) {
            limits->ptMinTrackSize.x = scaleForWindow(
                GetHwnd(), DIALOG_MINIMUM_WIDTH);
            limits->ptMinTrackSize.y = mFixedWindowHeight;
            limits->ptMaxTrackSize.y = mFixedWindowHeight;
            return TRUE;
        }
        break;
    }
    case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        if (suggested != nullptr) {
            SetWindowPos(nullptr,
                         suggested->left,
                         suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            mFixedWindowHeight = suggested->bottom - suggested->top;
        }
        updateControlFont();
        layoutControls();
        return TRUE;
    }
    case WM_SIZE:
        layoutControls();
        return TRUE;
    default:
        break;
    }
    return CDialog::DialogProc(message, wParam, lParam);
}

void CommandDialog::updateControlFont() {
    UINT dpi = GetDpiForWindow(GetHwnd());
    if (dpi == 0) {
        dpi = DEFAULT_DPI;
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

    const HFONT oldFont = mUiFont;
    mUiFont = newFont;
    const std::array<int, 13> controlIds{
        IDC_COMMAND_NAME_LABEL,
        IDC_COMMAND_NAME,
        IDC_COMMAND_WORKING_DIRECTORY_LABEL,
        IDC_COMMAND_WORKING_DIRECTORY,
        IDC_COMMAND_BROWSE,
        IDC_COMMAND_ENCODING_LABEL,
        IDC_COMMAND_ENCODING,
        IDC_COMMAND_SHELL,
        IDC_COMMAND_AUTO_START,
        IDC_COMMAND_LINE_LABEL,
        IDC_COMMAND_LINE,
        IDC_COMMAND_SAVE,
        IDC_COMMAND_CANCEL,
    };
    for (const int controlId : controlIds) {
        const Win32xx::CWnd control = GetDlgItem(controlId);
        if (control.IsWindow()) {
            control.SetFont(mUiFont, TRUE);
        }
    }
    if (oldFont != nullptr) {
        DeleteObject(oldFont);
    }
}

void CommandDialog::layoutControls() {
    const Win32xx::CRect client = GetClientRect();
    const int width = std::max(0L, client.right - client.left);
    const int height = std::max(0L, client.bottom - client.top);
    const int margin = scaleForWindow(GetHwnd(), DIALOG_MARGIN);
    const int labelWidth = scaleForWindow(GetHwnd(), DIALOG_LABEL_WIDTH);
    const int labelHeight = scaleForWindow(GetHwnd(), DIALOG_LABEL_HEIGHT);
    const int inputLeft = scaleForWindow(GetHwnd(), DIALOG_INPUT_LEFT);
    const int inputHeight = scaleForWindow(GetHwnd(), DIALOG_INPUT_HEIGHT);
    const int browseWidth = scaleForWindow(GetHwnd(), DIALOG_BROWSE_WIDTH);
    const int browseHeight = scaleForWindow(GetHwnd(), DIALOG_BROWSE_HEIGHT);
    const int inputWidth = std::max(
        scaleForWindow(GetHwnd(), 80),
        width - inputLeft - margin);
    const int workingWidth = std::max(
        scaleForWindow(GetHwnd(), 80),
        width - inputLeft - margin - browseWidth -
            scaleForWindow(GetHwnd(), DIALOG_BUTTON_GAP));
    const int commandLineTop = scaleForWindow(
        GetHwnd(), DIALOG_COMMAND_LINE_TOP);
    const int buttonHeight = scaleForWindow(
        GetHwnd(), DIALOG_BUTTON_HEIGHT);
    const int buttonY = std::max(0, height - margin - buttonHeight);
    const int commandLineHeight = std::max(
        scaleForWindow(GetHwnd(), DIALOG_COMMAND_LINE_MINIMUM_HEIGHT),
        buttonY - commandLineTop -
            scaleForWindow(GetHwnd(), DIALOG_COMMAND_LINE_BOTTOM_GAP));
    const int buttonWidth = scaleForWindow(GetHwnd(), DIALOG_BUTTON_WIDTH);
    const int buttonGap = scaleForWindow(GetHwnd(), DIALOG_BUTTON_GAP);

    positionControl(GetDlgItem(IDC_COMMAND_NAME_LABEL),
                    margin,
                    scaleForWindow(GetHwnd(), 10),
                    labelWidth,
                    labelHeight);
    positionControl(GetDlgItem(IDC_COMMAND_NAME),
                    inputLeft,
                    scaleForWindow(GetHwnd(), 8),
                    inputWidth,
                    inputHeight);
    positionControl(GetDlgItem(IDC_COMMAND_WORKING_DIRECTORY_LABEL),
                    margin,
                    scaleForWindow(GetHwnd(), 36),
                    labelWidth,
                    labelHeight);
    positionControl(GetDlgItem(IDC_COMMAND_WORKING_DIRECTORY),
                    inputLeft,
                    scaleForWindow(GetHwnd(), 34),
                    workingWidth,
                    inputHeight);
    positionControl(GetDlgItem(IDC_COMMAND_BROWSE),
                    width - margin - browseWidth,
                    scaleForWindow(GetHwnd(), 33),
                    browseWidth,
                    browseHeight);
    positionControl(GetDlgItem(IDC_COMMAND_ENCODING_LABEL),
                    margin,
                    scaleForWindow(GetHwnd(), 62),
                    labelWidth,
                    labelHeight);
    positionControl(GetDlgItem(IDC_COMMAND_ENCODING),
                    inputLeft,
                    scaleForWindow(GetHwnd(), 60),
                    scaleForWindow(GetHwnd(), 112),
                    scaleForWindow(GetHwnd(), 100));
    positionControl(GetDlgItem(IDC_COMMAND_SHELL),
                    inputLeft,
                    scaleForWindow(GetHwnd(), 87),
                    scaleForWindow(GetHwnd(), 80),
                    inputHeight);
    positionControl(GetDlgItem(IDC_COMMAND_AUTO_START),
                    inputLeft + scaleForWindow(GetHwnd(), 90),
                    scaleForWindow(GetHwnd(), 87),
                    scaleForWindow(GetHwnd(), 100),
                    inputHeight);
    positionControl(GetDlgItem(IDC_COMMAND_LINE_LABEL),
                    margin,
                    scaleForWindow(GetHwnd(), 116),
                    labelWidth,
                    labelHeight);
    positionControl(mCommandLine,
                    inputLeft,
                    commandLineTop,
                    inputWidth,
                    commandLineHeight);
    positionControl(GetDlgItem(IDC_COMMAND_CANCEL),
                    width - margin - buttonWidth,
                    buttonY,
                    buttonWidth,
                    buttonHeight);
    positionControl(GetDlgItem(IDC_COMMAND_SAVE),
                    width - margin - buttonWidth - buttonGap - buttonWidth,
                    buttonY,
                    buttonWidth,
                    buttonHeight);
}

void CommandDialog::save() {
    const std::wstring name = trim(
        readText(GetDlgItem(IDC_COMMAND_NAME)));
    const std::wstring workingDirectory = trim(
        readText(GetDlgItem(IDC_COMMAND_WORKING_DIRECTORY)));
    const std::wstring commandLine = readText(mCommandLine);
    if (name.empty() || trim(commandLine).empty()) {
        MessageBox(L"Name and command line are required.",
                   L"Invalid Command",
                   MB_OK | MB_ICONWARNING);
        if (name.empty()) {
            GetDlgItem(IDC_COMMAND_NAME).SetFocus();
        } else {
            mCommandLine.SetFocus();
        }
        return;
    }

    const DWORD attributes = GetFileAttributesW(workingDirectory.c_str());
    if (workingDirectory.empty() || attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        MessageBox(L"The working directory does not exist.",
                   L"Invalid Command",
                   MB_OK | MB_ICONWARNING);
        GetDlgItem(IDC_COMMAND_WORKING_DIRECTORY).SetFocus();
        return;
    }

    const std::wstring encoding = selectedEncoding(mEncoding);
    mResult = CommandConfig(
        name,
        workingDirectory,
        commandLine,
        narrowEncoding(encoding),
        mDraft.mId,
        mAutoStart.GetCheck() == BST_CHECKED,
        mShell.GetCheck() == BST_CHECKED);
    EndDialog(IDC_COMMAND_SAVE);
}

void CommandDialog::browseForDirectory() {
    Win32xx::CFolderDialog folderDialog;
    folderDialog.SetTitle(L"Select Working Directory");
    const Win32xx::CString currentDirectory =
        GetDlgItemText(IDC_COMMAND_WORKING_DIRECTORY);
    if (!currentDirectory.IsEmpty()) {
        folderDialog.SetSelection(currentDirectory.c_str());
    }
    if (folderDialog.DoModal(GetHwnd()) == IDOK) {
        const Win32xx::CString folder = folderDialog.GetFolderPath();
        SetDlgItemText(IDC_COMMAND_WORKING_DIRECTORY, folder.c_str());
    }
}

}  // namespace command_runner::ui
