#include "ui/command_dialog.h"

#include "resource.h"
#include "ui/win32xx_helpers.h"

#include <win32xx/wxx_folderdialog.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace command_runner::ui {
namespace {

constexpr int DIALOG_MINIMUM_WIDTH = 430;
constexpr int DIALOG_MARGIN = 8;
constexpr int DIALOG_COMMAND_LINE_MINIMUM_HEIGHT = 40;
constexpr int DIALOG_COMMAND_LINE_BOTTOM_GAP = 8;
constexpr int DIALOG_BUTTON_GAP = 6;

void positionControl(const Win32xx::CWnd& control,
                     int x,
                     int y,
                     int width,
                     int height) {
    if (control.IsWindow()) {
        control.SetWindowPos(nullptr,
                             x,
                             y,
                             width,
                             height,
                             SWP_NOZORDER | SWP_NOACTIVATE);
    }
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

    const auto attach = [this](Win32xx::CWnd& control, UINT id) {
        control.AttachDlgItem(id, *this);
    };
    attach(mNameLabel, IDC_COMMAND_NAME_LABEL);
    attach(mName, IDC_COMMAND_NAME);
    attach(mWorkingDirectoryLabel, IDC_COMMAND_WORKING_DIRECTORY_LABEL);
    attach(mWorkingDirectory, IDC_COMMAND_WORKING_DIRECTORY);
    attach(mBrowse, IDC_COMMAND_BROWSE);
    attach(mEncodingLabel, IDC_COMMAND_ENCODING_LABEL);
    attach(mEncoding, IDC_COMMAND_ENCODING);
    attach(mShell, IDC_COMMAND_SHELL);
    attach(mAutoStart, IDC_COMMAND_AUTO_START);
    attach(mCommandLineLabel, IDC_COMMAND_LINE_LABEL);
    attach(mCommandLine, IDC_COMMAND_LINE);
    attach(mSave, IDC_COMMAND_SAVE);
    attach(mCancel, IDC_COMMAND_CANCEL);

    mName.SetWindowText(mDraft.mName.c_str());
    mWorkingDirectory.SetWindowText(mDraft.mWorkingDirectory.c_str());
    mCommandLine.SetWindowText(mDraft.mCommandLine.c_str());
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
    mFixedWindowHeight = windowRect.Height();
    updateControlFont();
    layoutControls();
    mName.SetFocus();
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

void CommandDialog::OnDestroy() {}

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
                         *suggested,
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
    if (!IsWindow()) {
        return;
    }
    Win32xx::CFont newFont;
    createFont(newFont, GetHwnd(), 9, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
    const HFONT font = static_cast<HFONT>(newFont);
    const std::array<Win32xx::CWnd*, 13> controls{
        &mNameLabel,
        &mName,
        &mWorkingDirectoryLabel,
        &mWorkingDirectory,
        &mBrowse,
        &mEncodingLabel,
        &mEncoding,
        &mShell,
        &mAutoStart,
        &mCommandLineLabel,
        &mCommandLine,
        &mSave,
        &mCancel,
    };
    for (const Win32xx::CWnd* control : controls) {
        control->SetFont(font, TRUE);
    }
    mUiFont = newFont;
}

void CommandDialog::layoutControls() {
    if (!IsWindow()) {
        return;
    }
    const Win32xx::CRect client = GetClientRect();
    const int width = std::max(0, client.Width());
    const int height = std::max(0, client.Height());
    const UINT dpi = windowDpi(GetHwnd());
    const int margin = scaleForWindow(GetHwnd(), DIALOG_MARGIN);
    const int edge = GetSystemMetricsForDpi(SM_CYEDGE, dpi);
    const HFONT font = static_cast<HFONT>(mUiFont);
    const int currentFontHeight = measureFontHeight(GetHwnd(), font);
    const int labelHeight = std::max(1, currentFontHeight);
    const int inputHeight = std::max(1, currentFontHeight + 2 * edge);

    const std::array<const Win32xx::CWnd*, 4> labels{
        &mNameLabel,
        &mWorkingDirectoryLabel,
        &mEncodingLabel,
        &mCommandLineLabel,
    };
    int labelWidth = 0;
    for (const Win32xx::CWnd* label : labels) {
        labelWidth = std::max(labelWidth,
                              measureControlTextWidth(*label, font));
    }
    labelWidth = std::max(1, labelWidth);
    const int inputLeft = margin + labelWidth + margin;

    const auto buttonSize = [font, edge, inputHeight](
                                const Win32xx::CWnd& control) {
        const SIZE idealSize = preferredButtonSize(control);
        const int textWidth = measureControlTextWidth(control, font);
        return SIZE{
            idealSize.cx > 0 ? idealSize.cx
                             : std::max(1, textWidth + 2 * edge),
            idealSize.cy > 0 ? idealSize.cy : inputHeight,
        };
    };
    const SIZE browseSize = buttonSize(mBrowse);
    const SIZE saveSize = buttonSize(mSave);
    const SIZE cancelSize = buttonSize(mCancel);
    const int browseWidth = browseSize.cx;
    const int browseHeight = browseSize.cy;
    const int buttonWidth = std::max(saveSize.cx, cancelSize.cx);
    const int buttonHeight = std::max(saveSize.cy, cancelSize.cy);
    const int buttonGap = scaleForWindow(GetHwnd(), DIALOG_BUTTON_GAP);

    const int encodingTop = scaleForWindow(GetHwnd(), 60);
    const int optionTop = encodingTop + inputHeight + buttonGap;
    const SIZE shellSize = buttonSize(mShell);
    const SIZE autoStartSize = buttonSize(mAutoStart);
    const int optionHeight = std::max(shellSize.cy, autoStartSize.cy);
    const int commandLineTop = optionTop + optionHeight + margin;
    const int inputWidth = std::max(
        scaleForWindow(GetHwnd(), 80),
        width - inputLeft - margin);
    const int workingWidth = std::max(
        scaleForWindow(GetHwnd(), 80),
        width - inputLeft - margin - browseWidth -
            buttonGap);
    const int buttonY = std::max(0, height - margin - buttonHeight);
    const int commandLineHeight = std::max(
        scaleForWindow(GetHwnd(), DIALOG_COMMAND_LINE_MINIMUM_HEIGHT),
        buttonY - commandLineTop -
            scaleForWindow(GetHwnd(), DIALOG_COMMAND_LINE_BOTTOM_GAP));

    positionControl(mNameLabel,
                    margin,
                    scaleForWindow(GetHwnd(), 10),
                    labelWidth,
                    labelHeight);
    positionControl(mName,
                    inputLeft,
                    scaleForWindow(GetHwnd(), 8),
                    inputWidth,
                    inputHeight);
    positionControl(mWorkingDirectoryLabel,
                    margin,
                    scaleForWindow(GetHwnd(), 36),
                    labelWidth,
                    labelHeight);
    positionControl(mWorkingDirectory,
                    inputLeft,
                    scaleForWindow(GetHwnd(), 34),
                    workingWidth,
                    inputHeight);
    positionControl(mBrowse,
                    width - margin - browseWidth,
                    scaleForWindow(GetHwnd(), 34) +
                        (inputHeight - browseHeight) / 2,
                    browseWidth,
                    browseHeight);
    positionControl(mEncodingLabel,
                    margin,
                    scaleForWindow(GetHwnd(), 62),
                    labelWidth,
                    labelHeight);
    positionControl(mEncoding,
                    inputLeft,
                    scaleForWindow(GetHwnd(), 60),
                    scaleForWindow(GetHwnd(), 112),
                    scaleForWindow(GetHwnd(), 100));
    positionControl(mShell,
                    inputLeft,
                    optionTop,
                    shellSize.cx,
                    optionHeight);
    positionControl(mAutoStart,
                    inputLeft + shellSize.cx + buttonGap,
                    optionTop,
                    autoStartSize.cx,
                    optionHeight);
    positionControl(mCommandLineLabel,
                    margin,
                    commandLineTop,
                    labelWidth,
                    labelHeight);
    positionControl(mCommandLine,
                    inputLeft,
                    commandLineTop,
                    inputWidth,
                    commandLineHeight);
    positionControl(mCancel,
                    width - margin - buttonWidth,
                    buttonY,
                    buttonWidth,
                    buttonHeight);
    positionControl(mSave,
                    width - margin - buttonWidth - buttonGap - buttonWidth,
                    buttonY,
                    buttonWidth,
                    buttonHeight);
}

void CommandDialog::save() {
    const std::wstring name = trim(readText(mName));
    const std::wstring workingDirectory = trim(readText(mWorkingDirectory));
    const std::wstring commandLine = readText(mCommandLine);
    if (name.empty() || trim(commandLine).empty()) {
        MessageBox(L"Name and command line are required.",
                   L"Invalid Command",
                   MB_OK | MB_ICONWARNING);
        if (name.empty()) {
            mName.SetFocus();
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
        mWorkingDirectory.SetFocus();
        return;
    }

    const std::wstring encoding = selectedEncoding(mEncoding);
    mResult = CommandConfig(name,
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
    const Win32xx::CString currentDirectory = mWorkingDirectory.GetWindowText();
    if (!currentDirectory.IsEmpty()) {
        folderDialog.SetSelection(currentDirectory.c_str());
    }
    if (folderDialog.DoModal(GetHwnd()) == IDOK) {
        const Win32xx::CString folder = folderDialog.GetFolderPath();
        mWorkingDirectory.SetWindowText(folder.c_str());
    }
}

}  // namespace command_runner::ui
