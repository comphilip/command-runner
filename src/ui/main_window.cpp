#include "ui/main_window.h"

#include "resource.h"
#include "ui/command_dialog.h"
#include "ui/toolbar_icons.h"
#include "ui/win32xx_helpers.h"

#include <CommCtrl.h>
#include <Richedit.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <ctime>
#include <iomanip>
#include <new>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace command_runner::ui {
namespace {

constexpr std::size_t ACTION_BUTTON_COUNT = 6;

constexpr std::array<const wchar_t*, ACTION_BUTTON_COUNT>
    ACTION_BUTTON_LABELS{
        L"&Add",
        L"&Edit",
        L"&Delete",
        L"&Start",
        L"S&top",
        L"&Restart",
    };

constexpr std::array<int, ACTION_BUTTON_COUNT>
    ACTION_BUTTON_IDS{
        1001,
        1002,
        1003,
        1004,
        1005,
        1006,
    };

constexpr std::array<const wchar_t*, 6> COLUMN_LABELS{
    L"Name",
    L"Status",
    L"PID",
    L"Exit Code",
    L"Auto Start",
    L"Working Directory",
};

constexpr std::array<int, 5> FIXED_COLUMN_WIDTHS{180, 100, 80, 70, 80};
constexpr COLORREF STDERR_COLOR = RGB(198, 40, 40);

[[nodiscard]] std::wstring numberOrEmpty(
    const std::optional<std::uint32_t>& value) {
    return value ? std::to_wstring(*value) : std::wstring{};
}

[[nodiscard]] std::wstring exitCodeOrEmpty(
    const std::optional<std::int32_t>& value) {
    return value ? std::to_wstring(*value) : std::wstring{};
}

[[nodiscard]] std::wstring stateText(const RuntimeSnapshot& runtime) {
    return stateToString(runtime.mState);
}

}  // namespace

LRESULT CommandListView::WndProc(UINT message,
                                 WPARAM wParam,
                                 LPARAM lParam) {
    if (message == WM_LBUTTONDOWN) {
        const int item = HitTest(Win32xx::CPoint(lParam));
        if (item >= 0 && (GetKeyState(VK_SHIFT) & 0x8000) == 0) {
            mSelectionAnchorIndex = item;
        }
    }

    if (message == WM_KEYDOWN) {
        const bool controlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        switch (wParam) {
        case 'A':
            if (controlPressed) {
                const int count = GetItemCount();
                if (count > 0) {
                    SetItemState(-1, LVIS_SELECTED, LVIS_SELECTED);
                    SetItemState(0, LVIS_FOCUSED, LVIS_FOCUSED);
                    mSelectionAnchorIndex = 0;
                    notifySelectionChanged();
                }
                return 0;
            }
            break;
        case VK_UP:
            if (shiftPressed) {
                selectRange(focusedItem() - 1);
                return 0;
            }
            if (controlPressed) {
                moveFocus(-1);
                return 0;
            }
            break;
        case VK_DOWN:
            if (shiftPressed) {
                selectRange(focusedItem() + 1);
                return 0;
            }
            if (controlPressed) {
                moveFocus(1);
                return 0;
            }
            break;
        case VK_HOME:
            moveToBoundary(false, !controlPressed);
            return 0;
        case VK_END:
            moveToBoundary(true, !controlPressed);
            return 0;
        case VK_RETURN:
            if (mListener != nullptr) {
                mListener->onListActivated();
            }
            return 0;
        case VK_DELETE:
            if (mListener != nullptr && GetSelectedCount() != 0) {
                mListener->onListDeleteRequested();
            }
            return 0;
        case VK_SPACE:
            toggleFocusedSelection();
            return 0;
        default:
            break;
        }
    }

    return WndProcDefault(message, wParam, lParam);
}

int CommandListView::focusedItem() const {
    if (!IsWindow()) {
        return -1;
    }
    return GetNextItem(-1, LVNI_FOCUSED);
}

void CommandListView::notifySelectionChanged() {
    if (mListener != nullptr) {
        mListener->onListSelectionChanged();
    }
}

void CommandListView::selectRange(int targetIndex) {
    const int count = GetItemCount();
    if (count == 0) {
        return;
    }

    const int currentIndex = focusedItem();
    const int anchor = mSelectionAnchorIndex >= 0 &&
                               mSelectionAnchorIndex < count
                           ? mSelectionAnchorIndex
                           : (currentIndex >= 0 ? currentIndex : 0);
    const int target = std::clamp(targetIndex, 0, count - 1);

    SetItemState(-1, 0, LVIS_SELECTED);
    for (int index = std::min(anchor, target);
         index <= std::max(anchor, target);
         ++index) {
        SetItemState(index, LVIS_SELECTED, LVIS_SELECTED);
    }
    SetItemState(target, LVIS_FOCUSED, LVIS_FOCUSED);
    EnsureVisible(target, FALSE);
    notifySelectionChanged();
}

void CommandListView::moveFocus(int direction) {
    const int count = GetItemCount();
    if (count == 0) {
        return;
    }
    const int current = focusedItem();
    const int start = current >= 0 ? current : 0;
    const int target = std::clamp(start + direction, 0, count - 1);
    SetItemState(target, LVIS_FOCUSED, LVIS_FOCUSED);
    EnsureVisible(target, FALSE);
}

void CommandListView::moveToBoundary(bool last, bool select) {
    const int count = GetItemCount();
    if (count == 0) {
        return;
    }
    const int target = last ? count - 1 : 0;
    if (select) {
        SetItemState(-1, 0, LVIS_SELECTED);
        SetItemState(target, LVIS_SELECTED, LVIS_SELECTED);
        notifySelectionChanged();
    }
    SetItemState(target, LVIS_FOCUSED, LVIS_FOCUSED);
    mSelectionAnchorIndex = target;
    EnsureVisible(target, FALSE);
}

void CommandListView::toggleFocusedSelection() {
    const int item = focusedItem();
    if (item < 0) {
        return;
    }
    const bool selected = (GetItemState(item, LVIS_SELECTED) &
                           LVIS_SELECTED) != 0;
    SetItemState(item,
                 (selected ? 0U : LVIS_SELECTED) | LVIS_FOCUSED,
                 LVIS_SELECTED | LVIS_FOCUSED);
    mSelectionAnchorIndex = item;
    EnsureVisible(item, FALSE);
    notifySelectionChanged();
}

LRESULT Splitter::WndProc(UINT message,
                          WPARAM wParam,
                          LPARAM lParam) {
    (void)wParam;
    switch (message) {
    case WM_SETCURSOR:
        ::SetCursor(::LoadCursorW(nullptr, IDC_SIZENS));
        return TRUE;
    case WM_LBUTTONDOWN:
        mDragging = true;
        SetCapture();
        return 0;
    case WM_MOUSEMOVE:
        if (mDragging && mListener != nullptr) {
            Win32xx::CPoint point(lParam);
            if (ClientToScreen(point) != FALSE) {
                mListener->onSplitterMoved(point);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        mDragging = false;
        if (static_cast<HWND>(GetCapture()) == GetHwnd()) {
            ::ReleaseCapture();
        }
        return 0;
    case WM_CAPTURECHANGED:
        mDragging = false;
        return 0;
    case WM_PAINT:
        try {
            Win32xx::CPaintDC paint(GetHwnd());
            const Win32xx::CRect client = GetClientRect();
            paint.FillRect(client,
                           static_cast<HBRUSH>(
                               ::GetStockObject(COLOR_3DFACE + 1)));
            paint.DrawEdge(client, EDGE_RAISED, BF_RECT);
        } catch (const Win32xx::CException&) {
            return 0;
        }
        return 0;
    default:
        return WndProcDefault(message, wParam, lParam);
    }
}

void Splitter::PreRegisterClass(WNDCLASS& windowClass) {
    windowClass.lpszClassName = L"CommandRunner.HorizontalSplitter";
    windowClass.hCursor = ::LoadCursorW(nullptr, IDC_SIZENS);
}

void Splitter::PreCreate(CREATESTRUCT& createStruct) {
    createStruct.style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
    createStruct.dwExStyle = 0;
    createStruct.x = 0;
    createStruct.y = 0;
    createStruct.cx = 0;
    createStruct.cy = 0;
    createStruct.lpszName = L"Horizontal splitter";
}

MainWindow::MainWindow(HINSTANCE instance,
                       ConfigData& configuration,
                       ConfigStore& store,
                       ProcessManager& processManager,
                       MainWindowHost& host)
    : mInstance(instance),
      mConfiguration(configuration),
      mStore(store),
      mProcessManager(processManager),
      mHost(host) {
    mListView.setListener(*this);
    mSplitter.setListener(*this);
}

MainWindow::~MainWindow() {
    dispose();
}

void MainWindow::dispose() noexcept {
    if (mDisposed) {
        return;
    }
    mDisposed = true;
    if (IsWindow()) {
        KillTimer(PROCESS_EVENT_TIMER);
    }
    destroyControls();
    if (IsWindow()) {
        Destroy();
    }
}

std::expected<void, DWORD> MainWindow::create(int showCommand) {
    mDisposed = false;
    try {
        if (!IsWindow()) {
            Create();
        }

        const auto controls = createControls();
        if (!controls) {
            const DWORD error = controls.error();
            dispose();
            return std::unexpected(error);
        }

        updateControlFonts();
        updateLogFont();
        updateActionToolBarMetrics();
        layoutControls();
        refreshRows();
        refreshLogs();
        updateLogOptions();
        if (SetTimer(PROCESS_EVENT_TIMER,
                     PROCESS_EVENT_INTERVAL_MILLISECONDS,
                     nullptr) == 0) {
            const DWORD error = lastWin32ErrorOr(ERROR_FUNCTION_FAILED);
            dispose();
            return std::unexpected(error);
        }
        ShowWindow(showCommand);
        UpdateWindow();
    } catch (const Win32xx::CException& exception) {
        const DWORD error = exception.GetError() == ERROR_SUCCESS
                                ? ERROR_FUNCTION_FAILED
                                : exception.GetError();
        dispose();
        return std::unexpected(error);
    }
    return {};
}

void MainWindow::PreRegisterClass(WNDCLASS& windowClass) {
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpszClassName = WINDOW_CLASS_NAME;
    windowClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = GetApp()->LoadIcon(IDI_COMMAND_RUNNER);
    if (windowClass.hIcon == nullptr) {
        windowClass.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
    }
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
}

void MainWindow::PreCreate(CREATESTRUCT& createStruct) {
    createStruct.dwExStyle = WS_EX_CONTROLPARENT;
    createStruct.style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN |
                         WS_CLIPSIBLINGS;
    createStruct.x = CW_USEDEFAULT;
    createStruct.y = CW_USEDEFAULT;
    createStruct.cx = scaleForWindow(nullptr, INITIAL_WINDOW_WIDTH);
    createStruct.cy = scaleForWindow(nullptr, INITIAL_WINDOW_HEIGHT);
    createStruct.lpszName = L"Command Runner";
}

std::expected<void, DWORD> MainWindow::createControls() {
    constexpr DWORD childStyle = WS_CHILD | WS_VISIBLE;
    constexpr DWORD actionToolBarStyle =
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS |
        WS_TABSTOP | TBSTYLE_TOOLTIPS | TBSTYLE_FLAT | TBSTYLE_LIST |
        CCS_NODIVIDER | CCS_NORESIZE | CCS_NOPARENTALIGN;

    auto create = [this](auto& control,
                         DWORD exStyle,
                         LPCWSTR className,
                         LPCWSTR text,
                         DWORD style,
                         UINT id) {
        return createChild(control,
                           GetHwnd(),
                           exStyle,
                           className,
                           text,
                           style,
                           0,
                           0,
                           0,
                           0,
                           id);
    };

    try {
        mActionReBar.Create(GetHwnd());
        const HMENU actionToolBarId = reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(IDC_ACTION_TOOLBAR));
        mActionToolBar.CreateEx(0,
                                TOOLBARCLASSNAME,
                                nullptr,
                                actionToolBarStyle,
                                0,
                                0,
                                0,
                                0,
                                mActionReBar.GetHwnd(),
                                actionToolBarId);
    } catch (const Win32xx::CException& exception) {
        const DWORD error = exception.GetError();
        return std::unexpected(error == ERROR_SUCCESS
                                   ? ERROR_FUNCTION_FAILED
                                   : error);
    }
    if (!mActionReBar.IsWindow() || !mActionToolBar.IsWindow()) {
        return std::unexpected(lastWin32ErrorOr(ERROR_FUNCTION_FAILED));
    }

    const auto actionImages = createToolbarImageLists(
        mInstance, GetDpiForWindow(GetHwnd()));
    if (!actionImages) {
        return std::unexpected(actionImages.error());
    }
    mActionToolBar.SetImageList(actionImages->mNormalImages.GetHandle());
    mActionToolBar.SetDisableImageList(
        actionImages->mDisabledImages.GetHandle());
    mActionImages = std::move(actionImages->mNormalImages);
    mActionDisabledImages = std::move(actionImages->mDisabledImages);

    for (std::size_t index = 0; index < ACTION_BUTTON_LABELS.size(); ++index) {
        if (mActionToolBar.AddButton(ACTION_BUTTON_IDS[index],
                                     TRUE,
                                     static_cast<int>(index)) == FALSE ||
            mActionToolBar.SetButtonText(ACTION_BUTTON_IDS[index],
                                         ACTION_BUTTON_LABELS[index]) == FALSE) {
            return std::unexpected(lastWin32ErrorOr(ERROR_FUNCTION_FAILED));
        }
    }

    const Win32xx::CSize actionToolBarSize = mActionToolBar.GetMaxSize();
    REBARBANDINFO actionBand{};
    actionBand.fMask = RBBIM_STYLE | RBBIM_CHILD | RBBIM_ID |
                       RBBIM_CHILDSIZE | RBBIM_SIZE;
    actionBand.fStyle = RBBS_NOGRIPPER | RBBS_FIXEDSIZE;
    actionBand.hwndChild = mActionToolBar.GetHwnd();
    actionBand.wID = IDC_ACTION_TOOLBAR;
    actionBand.cxMinChild = static_cast<UINT>(actionToolBarSize.cx);
    actionBand.cyMinChild = static_cast<UINT>(actionToolBarSize.cy);
    actionBand.cyMaxChild = static_cast<UINT>(actionToolBarSize.cy);
    actionBand.cx = static_cast<UINT>(actionToolBarSize.cx);
    if (mActionReBar.InsertBand(-1, actionBand) == FALSE) {
        return std::unexpected(lastWin32ErrorOr(ERROR_FUNCTION_FAILED));
    }

    if (const auto result = create(mOptionsBar,
                                   0,
                                   L"Static",
                                   nullptr,
                                   childStyle,
                                   0);
        !result) {
        return result;
    }

    try {
        mSplitter.Create(GetHwnd());
    } catch (const Win32xx::CException& exception) {
        const DWORD error = exception.GetError();
        return std::unexpected(error == ERROR_SUCCESS
                                   ? ERROR_FUNCTION_FAILED
                                   : error);
    }

    if (const auto result = create(mListView,
                                   WS_EX_CLIENTEDGE,
                                   WC_LISTVIEWW,
                                   nullptr,
                                   childStyle | WS_TABSTOP | LVS_REPORT |
                                       LVS_SHOWSELALWAYS,
                                   0);
        !result) {
        return result;
    }
    if (const auto result = create(mLogLabel,
                                   0,
                                   L"Static",
                                   L"Lo&g output",
                                   childStyle | SS_LEFT | SS_NOTIFY,
                                   IDC_LOG_LABEL);
        !result) {
        return result;
    }
    if (const auto result = create(mLogEdit,
                                   WS_EX_CLIENTEDGE,
                                   MSFTEDIT_CLASS,
                                   nullptr,
                                   childStyle | WS_TABSTOP | ES_MULTILINE |
                                       ES_READONLY | ES_AUTOVSCROLL |
                                       ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL,
                                   0);
        !result) {
        return result;
    }

    const std::array<std::pair<Win32xx::CButton*, std::pair<LPCWSTR, UINT>>, 7>
        optionControls{
            std::pair{&mCombinedRadio,
                      std::pair{L"Co&mbined", static_cast<UINT>(IDC_LOG_COMBINED)}},
            std::pair{&mStdoutRadio,
                      std::pair{L"stdo&ut", static_cast<UINT>(IDC_LOG_STDOUT)}},
            std::pair{&mStderrRadio,
                      std::pair{L"std&err", static_cast<UINT>(IDC_LOG_STDERR)}},
            std::pair{&mClearButton,
                      std::pair{L"&Clear", static_cast<UINT>(IDC_CLEAR)}},
            std::pair{&mJumpLatestButton,
                      std::pair{L"&Jump to Latest",
                                static_cast<UINT>(IDC_JUMP_LATEST)}},
            std::pair{&mWrapLinesCheck,
                      std::pair{L"&Word wrap", static_cast<UINT>(IDC_WRAP_LINES)}},
            std::pair{&mAutoScrollCheck,
                      std::pair{L"Auto-scro&ll",
                                static_cast<UINT>(IDC_AUTO_SCROLL)}},
        };

    for (std::size_t index = 0; index < optionControls.size(); ++index) {
        const auto [control, definition] = optionControls[index];
        const auto [text, id] = definition;
        const DWORD style = childStyle | WS_TABSTOP |
                            (index < 3 ? BS_AUTORADIOBUTTON
                                       : (index < 5 ? BS_PUSHBUTTON
                                                    : BS_AUTOCHECKBOX));
        const DWORD radioGroup = index == 0 ? WS_GROUP : 0;
        if (const auto result = create(*control,
                                       0,
                                       L"Button",
                                       text,
                                       style | radioGroup,
                                       id);
            !result) {
            return result;
        }
    }

    mListView.SetExtendedStyle(mListView.GetExtendedStyle() |
                               LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES |
                               LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    setListViewUnicodeFormat(mListView);
    for (std::size_t index = 0; index < COLUMN_LABELS.size(); ++index) {
        const int width = scaleForWindow(
            GetHwnd(),
            index < FIXED_COLUMN_WIDTHS.size()
                ? FIXED_COLUMN_WIDTHS[index]
                : MINIMUM_WORKING_DIRECTORY_WIDTH);
        mListView.InsertColumn(static_cast<int>(index),
                               COLUMN_LABELS[index],
                               LVCFMT_LEFT,
                               width,
                               static_cast<int>(index));
    }

    mLogEdit.LimitText(1'048'576);
    mLogEdit.HideSelection(TRUE, FALSE);
    mLogEdit.SetBackgroundColor(TRUE, RGB(255, 255, 255));
    return {};
}

void MainWindow::destroyControls() noexcept {
    mLogEdit.Destroy();
    mLogLabel.Destroy();
    mListView.Destroy();
    mSplitter.Destroy();
    mCombinedRadio.Destroy();
    mStdoutRadio.Destroy();
    mStderrRadio.Destroy();
    mClearButton.Destroy();
    mJumpLatestButton.Destroy();
    mWrapLinesCheck.Destroy();
    mAutoScrollCheck.Destroy();
    mOptionsBar.Destroy();
    mActionToolBar.Destroy();
    mActionReBar.Destroy();
}

void MainWindow::layoutControls() {
    if (!IsWindow() || !mListView.IsWindow()) {
        return;
    }

    const Win32xx::CRect client = GetClientRect();
    const int width = std::max(0, client.Width());
    const int height = std::max(0, client.Height());
    const UINT dpi = windowDpi(GetHwnd());
    const int controlPadding = GetSystemMetricsForDpi(SM_CYEDGE, dpi);
    const int currentFontHeight = measureFontHeight(
        GetHwnd(), static_cast<HFONT>(mUiFont));
    const int fallbackControlHeight = currentFontHeight > 0
                                          ? currentFontHeight + 2 * controlPadding
                                          : GetSystemMetricsForDpi(SM_CYMENU, dpi);
    const std::array<const Win32xx::CWnd*, 7> optionControls{
        &mCombinedRadio,
        &mStdoutRadio,
        &mStderrRadio,
        &mClearButton,
        &mJumpLatestButton,
        &mWrapLinesCheck,
        &mAutoScrollCheck,
    };
    int optionControlHeight = fallbackControlHeight;
    for (const Win32xx::CWnd* control : optionControls) {
        const SIZE idealSize = preferredButtonSize(*control);
        optionControlHeight = std::max(
            optionControlHeight,
            idealSize.cy > 0 ? static_cast<int>(idealSize.cy)
                             : fallbackControlHeight);
    }
    const int optionsBarHeight = optionControlHeight + 2 * controlPadding;
    const int logLabelHeight = std::max(
        1, currentFontHeight + 2 * controlPadding);
    const int actionBarHeight = std::max(
        0,
        static_cast<int>(mActionReBar.SendMessage(RB_GETBARHEIGHT, 0, 0)));
    const int margin = scaleForWindow(GetHwnd(), CONTENT_MARGIN);
    const int contentTop = actionBarHeight +
                           scaleForWindow(GetHwnd(), CONTENT_TOP_PADDING);
    const int contentBottom = std::max(
        contentTop,
        height - scaleForWindow(GetHwnd(), CONTENT_BOTTOM_PADDING));
    const int splitterHeight = scaleForWindow(GetHwnd(), SPLITTER_HEIGHT);
    const int availableHeight = std::max(
        0,
        contentBottom - contentTop - splitterHeight);

    const int minimumListHeight = scaleForWindow(
        GetHwnd(), MINIMUM_LIST_HEIGHT);
    const int minimumBottomHeight = optionsBarHeight + logLabelHeight +
                                    scaleForWindow(GetHwnd(), 60);
    const int maximumTopHeight = std::max(
        0,
        availableHeight - minimumBottomHeight);
    const int minimumTopHeight = std::min(minimumListHeight,
                                          maximumTopHeight);
    const int topHeight = std::clamp(
        MulDiv(availableHeight, mSplitterPercent, 100),
        minimumTopHeight,
        maximumTopHeight);
    const int splitterY = contentTop + topHeight;
    const int bottomY = splitterY + splitterHeight;
    const int contentWidth = std::max(0, width - 2 * margin);
    const UINT positionFlags = SWP_NOZORDER | SWP_NOACTIVATE;

    const auto position = [positionFlags](const Win32xx::CWnd& control,
                                          int x,
                                          int y,
                                          int controlWidth,
                                          int controlHeight) {
        if (control.IsWindow()) {
            control.SetWindowPos(nullptr,
                                 x,
                                 y,
                                 controlWidth,
                                 controlHeight,
                                 positionFlags);
        }
    };

    position(mActionReBar, 0, 0, width, actionBarHeight);
    position(mOptionsBar,
             margin,
             bottomY,
             contentWidth,
             optionsBarHeight);
    position(mListView, margin, contentTop, contentWidth, topHeight);
    position(mSplitter,
             margin,
             splitterY,
             contentWidth,
             splitterHeight);

    const int optionsY = bottomY + controlPadding;
    int optionX = margin + scaleForWindow(GetHwnd(), 2);
    const int optionGap = scaleForWindow(GetHwnd(), 2);
    const std::array<std::pair<Win32xx::CWnd*, int>, 3> radios{
        std::pair{static_cast<Win32xx::CWnd*>(&mCombinedRadio), 78},
        std::pair{static_cast<Win32xx::CWnd*>(&mStdoutRadio), 72},
        std::pair{static_cast<Win32xx::CWnd*>(&mStderrRadio), 110},
    };
    for (const auto [control, logicalWidth] : radios) {
        const int controlWidth = scaleForWindow(GetHwnd(), logicalWidth);
        position(*control,
                 optionX,
                 optionsY,
                 controlWidth,
                 optionControlHeight);
        optionX += controlWidth + optionGap;
    }

    const int rightGap = scaleForWindow(GetHwnd(), 4);
    int rightX = width - margin;
    const std::array<std::pair<Win32xx::CWnd*, int>, 4> rightControls{
        std::pair{static_cast<Win32xx::CWnd*>(&mAutoScrollCheck), 88},
        std::pair{static_cast<Win32xx::CWnd*>(&mWrapLinesCheck), 82},
        std::pair{static_cast<Win32xx::CWnd*>(&mJumpLatestButton), 108},
        std::pair{static_cast<Win32xx::CWnd*>(&mClearButton), 60},
    };
    for (const auto [control, logicalWidth] : rightControls) {
        const int controlWidth = scaleForWindow(GetHwnd(), logicalWidth);
        rightX -= controlWidth;
        position(*control,
                 rightX,
                 optionsY,
                 controlWidth,
                 scaleForWindow(GetHwnd(), 26));
        rightX -= rightGap;
    }

    const int labelY = bottomY + optionsBarHeight;
    position(mLogLabel,
             margin,
             labelY,
             contentWidth,
             logLabelHeight);
    const int logY = labelY + logLabelHeight;
    position(mLogEdit,
             margin,
             logY,
             contentWidth,
             std::max(0, height - logY - margin));

    updateListColumns();
    RedrawWindow(RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void MainWindow::updateActionToolBarMetrics() {
    if (!mActionToolBar.IsWindow() || !mActionReBar.IsWindow()) {
        return;
    }

    const Win32xx::CSize actionToolBarSize = mActionToolBar.GetMaxSize();
    if (actionToolBarSize.cx <= 0 || actionToolBarSize.cy <= 0) {
        return;
    }

    REBARBANDINFO actionBand{};
    actionBand.cbSize = sizeof(actionBand);
    actionBand.fMask = RBBIM_CHILDSIZE | RBBIM_SIZE;
    actionBand.cxMinChild = static_cast<UINT>(actionToolBarSize.cx);
    actionBand.cyMinChild = static_cast<UINT>(actionToolBarSize.cy);
    actionBand.cyMaxChild = static_cast<UINT>(actionToolBarSize.cy);
    actionBand.cx = static_cast<UINT>(actionToolBarSize.cx);
    mActionReBar.SendMessage(RB_SETBANDINFO,
                             0,
                             reinterpret_cast<LPARAM>(&actionBand));
}

void MainWindow::updateActionToolBarImages(UINT dpi) {
    if (!mActionToolBar.IsWindow()) {
        return;
    }

    try {
        const auto actionImages = createToolbarImageLists(mInstance, dpi);
        if (!actionImages) {
            return;
        }
        mActionToolBar.SetImageList(actionImages->mNormalImages.GetHandle());
        mActionToolBar.SetDisableImageList(
            actionImages->mDisabledImages.GetHandle());
        mActionImages = std::move(actionImages->mNormalImages);
        mActionDisabledImages = std::move(actionImages->mDisabledImages);
    } catch (const Win32xx::CException&) {
        // Keep the previous image list if a DPI-specific list cannot be built.
    } catch (const std::bad_alloc&) {
        // Keep the previous image list if the process is out of memory.
    }
}

void MainWindow::updateListColumns() {
    if (!mListView.IsWindow()) {
        return;
    }
    const int availableWidth = std::max(0, mListView.GetClientRect().Width());
    int fixedWidth = 0;
    for (std::size_t index = 0; index < FIXED_COLUMN_WIDTHS.size(); ++index) {
        const int width = scaleForWindow(GetHwnd(), FIXED_COLUMN_WIDTHS[index]);
        mListView.SetColumnWidth(static_cast<int>(index), width);
        fixedWidth += width;
    }
    const int workingDirectoryWidth = std::max(
        scaleForWindow(GetHwnd(), MINIMUM_WORKING_DIRECTORY_WIDTH),
        availableWidth - fixedWidth);
    mListView.SetColumnWidth(static_cast<int>(FIXED_COLUMN_WIDTHS.size()),
                             workingDirectoryWidth);
}

void MainWindow::updateLogFont() {
    if (!mLogEdit.IsWindow()) {
        return;
    }
    Win32xx::CFont newFont;
    createFont(newFont, GetHwnd(), 10, FIXED_PITCH | FF_MODERN, L"Consolas");
    mLogEdit.SetFont(static_cast<HFONT>(newFont), TRUE);
    mLogFont = newFont;
}

void MainWindow::updateControlFonts() {
    if (!IsWindow()) {
        return;
    }
    Win32xx::CFont newFont;
    createFont(newFont,
               GetHwnd(),
               9,
               VARIABLE_PITCH | FF_SWISS,
               L"Segoe UI");
    const HFONT font = static_cast<HFONT>(newFont);
    mActionToolBar.SetFont(font, TRUE);
    mOptionsBar.SetFont(font, TRUE);
    mListView.SetFont(font, TRUE);
    mLogLabel.SetFont(font, TRUE);
    mCombinedRadio.SetFont(font, TRUE);
    mStdoutRadio.SetFont(font, TRUE);
    mStderrRadio.SetFont(font, TRUE);
    mClearButton.SetFont(font, TRUE);
    mJumpLatestButton.SetFont(font, TRUE);
    mWrapLinesCheck.SetFont(font, TRUE);
    mAutoScrollCheck.SetFont(font, TRUE);
    mUiFont = newFont;
}

void MainWindow::updateLogOptions() {
    if (!mCombinedRadio.IsWindow()) {
        return;
    }
    mCombinedRadio.SetCheck(mLogView == LogView::COMBINED ? BST_CHECKED
                                                          : BST_UNCHECKED);
    mStdoutRadio.SetCheck(mLogView == LogView::STDOUT ? BST_CHECKED
                                                       : BST_UNCHECKED);
    mStderrRadio.SetCheck(mLogView == LogView::STDERR ? BST_CHECKED
                                                       : BST_UNCHECKED);
    mWrapLinesCheck.SetCheck(mConfiguration.mPreferences.mWrapLines
                                 ? BST_CHECKED
                                 : BST_UNCHECKED);
    mAutoScrollCheck.SetCheck(mConfiguration.mPreferences.mAutoScroll
                                  ? BST_CHECKED
                                  : BST_UNCHECKED);
    mLogEdit.SetTargetDevice(nullptr,
                             mConfiguration.mPreferences.mWrapLines ? 0 : 1);
}

void MainWindow::updateActionAvailability() {
    if (!mActionToolBar.IsWindow()) {
        return;
    }
    std::vector<State> states;
    states.reserve(mSelectedCommandIds.size());
    for (const std::wstring& commandId : mSelectedCommandIds) {
        states.push_back(mProcessManager.snapshot(commandId).mState);
    }

    const bool editEnabled = states.size() == 1 && isInactive(states.front());
    const bool deleteEnabled = !states.empty() &&
                               std::ranges::all_of(states, isInactive);
    const bool startEnabled = std::ranges::any_of(states, isInactive);
    const bool stopEnabled = std::ranges::any_of(
        states,
        [](State state) { return state == State::RUNNING; });
    const bool restartEnabled = std::ranges::any_of(
        states,
        [](State state) {
            return isInactive(state) || state == State::RUNNING;
        });
    const std::array<bool, ACTION_BUTTON_COUNT> enabled{
        true,
        editEnabled,
        deleteEnabled,
        startEnabled,
        stopEnabled,
        restartEnabled,
    };
    for (std::size_t index = 0; index < enabled.size(); ++index) {
        mActionToolBar.EnableButton(ACTION_BUTTON_IDS[index],
                                     enabled[index] ? TRUE : FALSE);
    }
    mClearButton.EnableWindow(mActiveCommandId.empty() ? FALSE : TRUE);
}

void MainWindow::refreshRows() {
    if (!mListView.IsWindow()) {
        return;
    }

    std::vector<std::wstring> selectedIds;
    for (int item = mListView.GetNextItem(-1, LVNI_SELECTED);
         item >= 0;
         item = mListView.GetNextItem(item, LVNI_SELECTED)) {
        if (item < static_cast<int>(mConfiguration.mCommands.size())) {
            selectedIds.push_back(mConfiguration.mCommands[item].mId);
        }
    }
    const int focusedIndex = mListView.focusedItem();
    const std::wstring focusedId =
        focusedIndex >= 0 &&
                focusedIndex < static_cast<int>(mConfiguration.mCommands.size())
            ? mConfiguration.mCommands[focusedIndex].mId
            : std::wstring{};

    mUpdatingList = true;
    mListView.DeleteAllItems();
    for (std::size_t index = 0; index < mConfiguration.mCommands.size(); ++index) {
        const CommandConfig& command = mConfiguration.mCommands[index];
        const RuntimeSnapshot runtime = mProcessManager.snapshot(command.mId);
        const std::array<std::wstring, 6> values{
            command.mName,
            stateText(runtime),
            numberOrEmpty(runtime.mPid),
            exitCodeOrEmpty(runtime.mExitCode),
            command.mAutoStart ? L"Yes" : L"No",
            command.mWorkingDirectory,
        };
        const int inserted = mListView.InsertItem(static_cast<int>(index),
                                                  values.front().c_str());
        if (inserted < 0) {
            continue;
        }
        for (std::size_t column = 1; column < values.size(); ++column) {
            mListView.SetItemText(inserted,
                                  static_cast<int>(column),
                                  values[column].c_str());
        }
    }

    for (std::size_t index = 0; index < mConfiguration.mCommands.size(); ++index) {
        const std::wstring& commandId = mConfiguration.mCommands[index].mId;
        if (std::ranges::find(selectedIds, commandId) != selectedIds.end()) {
            mListView.SetItemState(static_cast<int>(index),
                                   LVIS_SELECTED,
                                   LVIS_SELECTED);
        }
        if (commandId == focusedId) {
            mListView.SetItemState(static_cast<int>(index),
                                   LVIS_FOCUSED,
                                   LVIS_FOCUSED);
            mListView.setSelectionAnchor(static_cast<int>(index));
        }
    }
    mUpdatingList = false;
    syncSelection();
}

void MainWindow::refreshLogs() {
    if (!mLogEdit.IsWindow()) {
        return;
    }
    const bool wasAtBottom = logIsAtBottom();
    const int firstVisibleLine = mLogEdit.GetFirstVisibleLine();

    RuntimeSnapshot runtime{};
    if (!mActiveCommandId.empty()) {
        runtime = mProcessManager.snapshot(mActiveCommandId);
    }
    const std::vector<LogLine>* lines = &runtime.mCombinedLines;
    if (mLogView == LogView::STDOUT) {
        lines = &runtime.mStdoutLines;
    } else if (mLogView == LogView::STDERR) {
        lines = &runtime.mStderrLines;
    }

    std::wstring text;
    std::vector<std::pair<long, long>> stderrRanges;
    for (const LogLine& line : *lines) {
        const std::size_t start = text.size();
        text += formatLogLine(line);
        if (line.mStream == "stderr") {
            stderrRanges.emplace_back(static_cast<long>(start),
                                      static_cast<long>(text.size()));
        }
    }

    mLogEdit.SetWindowText(text.c_str());
    CHARFORMAT2W stderrFormat{};
    stderrFormat.cbSize = sizeof(CHARFORMAT2W);
    stderrFormat.dwMask = CFM_COLOR;
    stderrFormat.crTextColor = STDERR_COLOR;
    for (const auto [start, end] : stderrRanges) {
        mLogEdit.SetSel(start, end);
        mLogEdit.SetSelectionCharFormat(stderrFormat);
    }
    mLogEdit.HideSelection(TRUE, FALSE);

    if (mConfiguration.mPreferences.mAutoScroll && wasAtBottom) {
        mLogEdit.SetSel(-1, -1);
        scrollRichEditCaret(mLogEdit);
    } else {
        const int currentFirstVisibleLine = mLogEdit.GetFirstVisibleLine();
        mLogEdit.LineScroll(firstVisibleLine - currentFirstVisibleLine);
    }
}

void MainWindow::pollProcessEvents() {
    bool rowsDirty = false;
    bool logsDirty = false;
    for (const ProcessEvent& event : mProcessManager.drainEvents()) {
        if (std::holds_alternative<StateChanged>(event)) {
            rowsDirty = true;
            continue;
        }
        const auto& logEvent = std::get<LogAdded>(event);
        if (logEvent.mCommandId == mActiveCommandId) {
            logsDirty = true;
        }
    }
    if (rowsDirty) {
        refreshRows();
    }
    if (logsDirty) {
        refreshLogs();
    }
}

void MainWindow::savePreferences() {
    const auto result = mStore.save(mConfiguration);
    if (!result) {
        MessageBox(result.error().c_str(),
                   L"Save Failed",
                   MB_OK | MB_ICONERROR);
    }
}

void MainWindow::setSplitterFromClientY(int clientY) {
    const Win32xx::CRect client = GetClientRect();
    const int actionBarHeight = std::max(
        0,
        static_cast<int>(mActionReBar.SendMessage(RB_GETBARHEIGHT, 0, 0)));
    const int contentTop = actionBarHeight +
                           scaleForWindow(GetHwnd(), CONTENT_TOP_PADDING);
    const int contentBottom = client.bottom -
                              scaleForWindow(GetHwnd(), CONTENT_BOTTOM_PADDING);
    const int splitterHeight = scaleForWindow(GetHwnd(), SPLITTER_HEIGHT);
    const int availableHeight = std::max(
        0,
        contentBottom - contentTop - splitterHeight);
    if (availableHeight == 0) {
        return;
    }
    const int topHeight = std::clamp(clientY - contentTop,
                                     0,
                                     availableHeight);
    mSplitterPercent = std::clamp(
        MulDiv(topHeight, 100, availableHeight),
        10,
        90);
    layoutControls();
}

void MainWindow::startSelected() {
    for (const std::wstring& commandId : mSelectedCommandIds) {
        const CommandConfig* command = commandById(commandId);
        if (command != nullptr &&
            isInactive(mProcessManager.snapshot(commandId).mState)) {
            mProcessManager.start(*command);
        }
    }
    refreshRows();
}

void MainWindow::stopSelected() {
    for (const std::wstring& commandId : mSelectedCommandIds) {
        if (mProcessManager.snapshot(commandId).mState == State::RUNNING) {
            mProcessManager.stop(commandId);
        }
    }
    refreshRows();
}

void MainWindow::restartSelected() {
    for (const std::wstring& commandId : mSelectedCommandIds) {
        const CommandConfig* command = commandById(commandId);
        if (command == nullptr) {
            continue;
        }
        const State state = mProcessManager.snapshot(commandId).mState;
        if (isInactive(state) || state == State::RUNNING) {
            mProcessManager.restart(*command);
        }
    }
    refreshRows();
}

void MainWindow::clearLogs() {
    if (!mActiveCommandId.empty()) {
        mProcessManager.clearLogs(mActiveCommandId);
        refreshLogs();
    }
}

bool MainWindow::saveConfiguration() {
    const auto result = mStore.save(mConfiguration);
    if (result) {
        return true;
    }
    MessageBox(result.error().c_str(), L"Save Failed", MB_OK | MB_ICONERROR);
    return false;
}

void MainWindow::addCommand() {
    const auto command = CommandDialog::show(GetHwnd(), mInstance);
    if (!command) {
        return;
    }
    mConfiguration.mCommands.push_back(*command);
    if (!saveConfiguration()) {
        mConfiguration.mCommands.pop_back();
        return;
    }

    const std::wstring newCommandId = command->mId;
    refreshRows();
    const auto found = std::ranges::find_if(
        mConfiguration.mCommands,
        [&newCommandId](const CommandConfig& value) {
            return value.mId == newCommandId;
        });
    if (found != mConfiguration.mCommands.end()) {
        const int index = static_cast<int>(
            std::distance(mConfiguration.mCommands.begin(), found));
        mUpdatingList = true;
        mListView.SetItemState(-1,
                               0,
                               LVIS_SELECTED | LVIS_FOCUSED);
        mListView.SetItemState(index,
                               LVIS_SELECTED | LVIS_FOCUSED,
                               LVIS_SELECTED | LVIS_FOCUSED);
        mUpdatingList = false;
        mListView.setSelectionAnchor(index);
        syncSelection();
    }
}

void MainWindow::editSelectedCommand() {
    if (mSelectedCommandIds.size() != 1) {
        MessageBox(L"Please select one command.",
                   L"Edit Command",
                   MB_OK | MB_ICONINFORMATION);
        return;
    }
    const auto found = std::ranges::find_if(
        mConfiguration.mCommands,
        [this](const CommandConfig& command) {
            return command.mId == mSelectedCommandIds.front();
        });
    if (found == mConfiguration.mCommands.end()) {
        return;
    }
    if (!isEditable(*found)) {
        MessageBox(L"Stop this command before editing it.",
                   L"Cannot Edit",
                   MB_OK | MB_ICONWARNING);
        return;
    }

    const CommandConfig original = *found;
    const auto edited = CommandDialog::show(GetHwnd(), mInstance, &original);
    if (!edited) {
        return;
    }
    *found = *edited;
    if (!saveConfiguration()) {
        *found = original;
        return;
    }
    refreshRows();
}

void MainWindow::deleteSelectedCommands() {
    if (mSelectedCommandIds.empty()) {
        return;
    }
    const bool canDelete = std::ranges::all_of(
        mSelectedCommandIds,
        [this](const std::wstring& commandId) {
            return isInactive(mProcessManager.snapshot(commandId).mState);
        });
    if (!canDelete) {
        MessageBox(L"Stop the selected running commands before deleting them.",
                   L"Cannot Delete",
                   MB_OK | MB_ICONWARNING);
        return;
    }

    const std::wstring question =
        L"Are you sure you want to delete " +
        std::to_wstring(mSelectedCommandIds.size()) + L" command(s)?";
    if (MessageBox(question.c_str(),
                   L"Delete Commands",
                   MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
        return;
    }

    const ConfigData original = mConfiguration;
    const auto newEnd = std::ranges::remove_if(
        mConfiguration.mCommands,
        [this](const CommandConfig& command) {
            return std::ranges::find(mSelectedCommandIds, command.mId) !=
                   mSelectedCommandIds.end();
        });
    mConfiguration.mCommands.erase(newEnd.begin(),
                                   mConfiguration.mCommands.end());
    if (!saveConfiguration()) {
        mConfiguration = original;
        return;
    }
    mSelectedCommandIds.clear();
    mActiveCommandId.clear();
    refreshRows();
    refreshLogs();
}

void MainWindow::showStopping() {
    if (IsWindow()) {
        SetWindowText(L"Command Runner - Stopping all commands...");
    }
}

void MainWindow::invokeEditIfAvailable() {
    if (mSelectedCommandIds.size() != 1) {
        startSelected();
        return;
    }
    const CommandConfig* command = commandById(mSelectedCommandIds.front());
    if (command != nullptr && isEditable(*command)) {
        editSelectedCommand();
    } else {
        startSelected();
    }
}

void MainWindow::syncSelection() {
    mSelectedCommandIds.clear();
    for (int item = mListView.GetNextItem(-1, LVNI_SELECTED);
         item >= 0;
         item = mListView.GetNextItem(item, LVNI_SELECTED)) {
        if (item < static_cast<int>(mConfiguration.mCommands.size())) {
            mSelectedCommandIds.push_back(mConfiguration.mCommands[item].mId);
        }
    }
    const std::wstring newActive = mSelectedCommandIds.size() == 1
                                       ? mSelectedCommandIds.front()
                                       : std::wstring{};
    if (newActive != mActiveCommandId) {
        mActiveCommandId = newActive;
        refreshLogs();
    }
    updateActionAvailability();
}

bool MainWindow::isEditable(const CommandConfig& command) const {
    return isInactive(mProcessManager.snapshot(command.mId).mState);
}

const CommandConfig* MainWindow::commandById(
    std::wstring_view commandId) const {
    const auto found = std::ranges::find_if(
        mConfiguration.mCommands,
        [commandId](const CommandConfig& command) {
            return command.mId == commandId;
        });
    return found == mConfiguration.mCommands.end() ? nullptr : &*found;
}

bool MainWindow::isInactive(State state) {
    return state == State::STOPPED || state == State::EXITED ||
           state == State::FAILED;
}

bool MainWindow::logIsAtBottom() const {
    if (!mLogEdit.IsWindow()) {
        return true;
    }
    SCROLLINFO scrollInfo{};
    scrollInfo.cbSize = sizeof(SCROLLINFO);
    scrollInfo.fMask = SIF_ALL;
    if (mLogEdit.GetScrollInfo(SB_VERT, scrollInfo) == FALSE) {
        return true;
    }
    const int maximum = static_cast<int>(scrollInfo.nMax);
    const int position = static_cast<int>(scrollInfo.nPos);
    const int page = static_cast<int>(scrollInfo.nPage);
    return maximum <= 0 || position + page >= maximum - 1;
}

std::wstring MainWindow::formatLogLine(const LogLine& line) const {
    return formatTimestamp(line.mTimestamp) + L" [" +
           std::wstring(line.mStream.begin(), line.mStream.end()) + L"] " +
           line.mText + L"\r\n";
}

std::wstring MainWindow::formatTimestamp(double timestamp) {
    const auto seconds = static_cast<std::time_t>(timestamp);
    std::tm localTime{};
    if (localtime_s(&localTime, &seconds) != 0) {
        return L"00:00:00";
    }
    std::wostringstream output;
    output << std::setfill(L'0') << std::setw(2) << localTime.tm_hour << L":"
           << std::setw(2) << localTime.tm_min << L":" << std::setw(2)
           << localTime.tm_sec;
    return output.str();
}

void MainWindow::onListSelectionChanged() {
    if (!mUpdatingList) {
        syncSelection();
    }
}

void MainWindow::onListActivated() {
    invokeEditIfAvailable();
}

void MainWindow::onListDeleteRequested() {
    deleteSelectedCommands();
}

void MainWindow::onSplitterMoved(Win32xx::CPoint screenPoint) {
    if (!IsWindow()) {
        return;
    }
    if (ScreenToClient(screenPoint) != FALSE) {
        setSplitterFromClientY(screenPoint.y);
    }
}

BOOL MainWindow::OnCommand(WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    const int controlId = LOWORD(wParam);
    const int notificationCode = HIWORD(wParam);
    if (controlId == IDC_LOG_LABEL && notificationCode == STN_CLICKED) {
        mLogEdit.SetFocus();
        return TRUE;
    }
    if (notificationCode != BN_CLICKED) {
        return FALSE;
    }

    switch (controlId) {
    case IDC_ADD:
        addCommand();
        return TRUE;
    case IDC_EDIT:
        editSelectedCommand();
        return TRUE;
    case IDC_DELETE:
        deleteSelectedCommands();
        return TRUE;
    case IDC_START:
        startSelected();
        return TRUE;
    case IDC_STOP:
        stopSelected();
        return TRUE;
    case IDC_RESTART:
        restartSelected();
        return TRUE;
    case IDC_LOG_COMBINED:
        mLogView = LogView::COMBINED;
        updateLogOptions();
        refreshLogs();
        return TRUE;
    case IDC_LOG_STDOUT:
        mLogView = LogView::STDOUT;
        updateLogOptions();
        refreshLogs();
        return TRUE;
    case IDC_LOG_STDERR:
        mLogView = LogView::STDERR;
        updateLogOptions();
        refreshLogs();
        return TRUE;
    case IDC_CLEAR:
        clearLogs();
        return TRUE;
    case IDC_JUMP_LATEST:
        mLogEdit.SetSel(-1, -1);
        scrollRichEditCaret(mLogEdit);
        return TRUE;
    case IDC_WRAP_LINES:
        mConfiguration.mPreferences.mWrapLines =
            mWrapLinesCheck.GetCheck() == BST_CHECKED;
        updateLogOptions();
        refreshLogs();
        savePreferences();
        return TRUE;
    case IDC_AUTO_SCROLL:
        mConfiguration.mPreferences.mAutoScroll =
            mAutoScrollCheck.GetCheck() == BST_CHECKED;
        savePreferences();
        return TRUE;
    default:
        return FALSE;
    }
}

LRESULT MainWindow::OnNotify(WPARAM wParam, LPARAM lParam) {
    (void)wParam;
    const auto* header = reinterpret_cast<const NMHDR*>(lParam);
    if (header == nullptr || header->hwndFrom != mListView.GetHwnd()) {
        return CWnd::OnNotify(wParam, lParam);
    }
    if (header->code == LVN_ITEMCHANGED) {
        if (!mUpdatingList) {
            syncSelection();
        }
        return 0;
    }
    if (header->code == NM_DBLCLK) {
        const auto* activation = reinterpret_cast<const NMITEMACTIVATE*>(
            lParam);
        if (activation != nullptr &&
            activation->iItem >= 0 &&
            activation->iItem < mListView.GetItemCount()) {
            mUpdatingList = true;
            mListView.SetItemState(-1,
                                   0,
                                   LVIS_SELECTED | LVIS_FOCUSED);
            mListView.SetItemState(activation->iItem,
                                   LVIS_SELECTED | LVIS_FOCUSED,
                                   LVIS_SELECTED | LVIS_FOCUSED);
            mUpdatingList = false;
            mListView.setSelectionAnchor(activation->iItem);
            syncSelection();
            onListActivated();
        }
        return 0;
    }
    return CWnd::OnNotify(wParam, lParam);
}

LRESULT MainWindow::WndProc(UINT message,
                            WPARAM wParam,
                            LPARAM lParam) {
    switch (message) {
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0U) == SC_MINIMIZE) {
            mHost.onMainWindowMinimizeRequested();
            return 0;
        }
        break;
    case WM_CLOSE:
        mHost.onMainWindowCloseRequested();
        return 0;
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        if (limits != nullptr) {
            limits->ptMinTrackSize.x = scaleForWindow(
                GetHwnd(), MINIMUM_WINDOW_WIDTH);
            limits->ptMinTrackSize.y = scaleForWindow(
                GetHwnd(), MINIMUM_WINDOW_HEIGHT);
            return 0;
        }
        break;
    }
    case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        if (suggested != nullptr) {
            SetWindowPos(nullptr,
                         *suggested,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        updateControlFonts();
        updateLogFont();
        updateActionToolBarImages(HIWORD(wParam));
        updateActionToolBarMetrics();
        layoutControls();
        return 0;
    }
    case WM_SIZE:
        layoutControls();
        return 0;
    case WM_TIMER:
        if (static_cast<UINT_PTR>(wParam) == PROCESS_EVENT_TIMER) {
            pollProcessEvents();
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    default:
        break;
    }
    return WndProcDefault(message, wParam, lParam);
}

void MainWindow::OnDestroy() {
    KillTimer(PROCESS_EVENT_TIMER);
}

}  // namespace command_runner::ui
