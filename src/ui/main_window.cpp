#include "ui/main_window.h"

#include <CommCtrl.h>
#include <Richedit.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <ctime>
#include <expected>
#include <iomanip>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace command_runner::ui {
namespace {

constexpr std::size_t ACTION_BUTTON_COUNT = 6;

constexpr std::array<int, ACTION_BUTTON_COUNT> ACTION_BUTTON_IDS{
    1001,
    1002,
    1003,
    1004,
    1005,
    1006,
};

constexpr std::array<const wchar_t*, ACTION_BUTTON_COUNT> ACTION_BUTTON_LABELS{
    L"&Add",
    L"&Edit",
    L"&Delete",
    L"&Start",
    L"S&top",
    L"&Restart",
};

constexpr std::array<int, ACTION_BUTTON_COUNT> ACTION_BUTTON_WIDTHS{
    62,
    62,
    74,
    66,
    66,
    80,
};

constexpr std::array<const wchar_t*, 6> COLUMN_LABELS{
    L"Name",
    L"Status",
    L"PID",
    L"Exit Code",
    L"Auto Start",
    L"Working Directory",
};

constexpr std::array<int, 5> FIXED_COLUMN_WIDTHS{
    180,
    100,
    80,
    70,
    80,
};

constexpr COLORREF STDERR_COLOR = RGB(198, 40, 40);

[[nodiscard]] std::wstring numberOrEmpty(
    const std::optional<std::uint32_t>& value) {
    return value ? std::to_wstring(*value) : std::wstring{};
}

[[nodiscard]] std::wstring exitCodeOrEmpty(
    const std::optional<std::int32_t>& value) {
    return value ? std::to_wstring(*value) : std::wstring{};
}

}  // namespace

MainWindow::MainWindow(HINSTANCE instance,
                       ConfigData configuration,
                       ConfigStore& store,
                       ProcessManager& processManager)
    : mInstance(instance),
      mConfiguration(std::move(configuration)),
      mStore(store),
      mProcessManager(processManager) {}

MainWindow::~MainWindow() {
    if (mWindow != nullptr && IsWindow(mWindow) != FALSE) {
        DestroyWindow(mWindow);
        mWindow = nullptr;
    }
    if (mLogFont != nullptr) {
        DeleteObject(mLogFont);
        mLogFont = nullptr;
    }
    if (mRichEditModule != nullptr) {
        FreeLibrary(mRichEditModule);
        mRichEditModule = nullptr;
    }
}

std::expected<void, DWORD> MainWindow::create(int showCommand) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.hInstance = mInstance;
    windowClass.lpfnWndProc = &MainWindow::windowProc;
    windowClass.lpszClassName = WINDOW_CLASS_NAME;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (RegisterClassExW(&windowClass) == 0) {
        const DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            const DWORD effectiveError =
                error == ERROR_SUCCESS ? ERROR_FUNCTION_FAILED : error;
            return std::unexpected(effectiveError);
        }
    }

    mWindow = CreateWindowExW(
        0,
        WINDOW_CLASS_NAME,
        L"Command Runner",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        scaleForWindow(nullptr, INITIAL_WINDOW_WIDTH),
        scaleForWindow(nullptr, INITIAL_WINDOW_HEIGHT),
        nullptr,
        nullptr,
        mInstance,
        this);
    if (mWindow == nullptr) {
        const DWORD error = GetLastError();
        return std::unexpected(error == ERROR_SUCCESS ? ERROR_FUNCTION_FAILED
                                                       : error);
    }

    const auto controls = createControls();
    if (!controls) {
        const DWORD error = controls.error();
        DestroyWindow(mWindow);
        mWindow = nullptr;
        return std::unexpected(error);
    }

    mCurrentDpi = GetDpiForWindow(mWindow);
    if (mCurrentDpi == 0) {
        mCurrentDpi = DEFAULT_DPI;
    }
    updateLogFont();
    layoutControls();
    refreshRows();
    refreshLogs();
    updateLogOptions();
    SetTimer(mWindow,
             PROCESS_EVENT_TIMER,
             PROCESS_EVENT_INTERVAL_MILLISECONDS,
             nullptr);

    ShowWindow(mWindow, showCommand);
    UpdateWindow(mWindow);
    return {};
}

std::expected<int, DWORD> MainWindow::runMessageLoop() const {
    MSG message{};
    while (true) {
        const int result = GetMessageW(&message, nullptr, 0, 0);
        if (result == -1) {
            const DWORD error = GetLastError();
            return std::unexpected(error == ERROR_SUCCESS ? ERROR_FUNCTION_FAILED
                                                           : error);
        }
        if (result == 0) {
            return static_cast<int>(message.wParam);
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

LRESULT CALLBACK MainWindow::windowProc(HWND window,
                                        UINT message,
                                        WPARAM wParam,
                                        LPARAM lParam) {
    auto* self = reinterpret_cast<MainWindow*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        if (createStruct == nullptr || createStruct->lpCreateParams == nullptr) {
            return FALSE;
        }
        self = static_cast<MainWindow*>(createStruct->lpCreateParams);
        self->mWindow = window;
        SetLastError(ERROR_SUCCESS);
        if (SetWindowLongPtrW(window,
                              GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(self)) == 0 &&
            GetLastError() != ERROR_SUCCESS) {
            return FALSE;
        }
    }
    return self == nullptr ? DefWindowProcW(window, message, wParam, lParam)
                           : self->handleMessage(message, wParam, lParam);
}

LRESULT CALLBACK MainWindow::listViewProc(HWND window,
                                          UINT message,
                                          WPARAM wParam,
                                          LPARAM lParam) {
    auto* self = reinterpret_cast<MainWindow*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (self == nullptr) {
        return DefWindowProcW(window, message, wParam, lParam);
    }
    return self->handleListViewMessage(message, wParam, lParam);
}

LRESULT CALLBACK MainWindow::splitterProc(HWND window,
                                          UINT message,
                                          WPARAM wParam,
                                          LPARAM lParam) {
    auto* self = reinterpret_cast<MainWindow*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        if (createStruct == nullptr || createStruct->lpCreateParams == nullptr) {
            return FALSE;
        }
        self = static_cast<MainWindow*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(window,
                          GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
    }
    if (self == nullptr) {
        return DefWindowProcW(window, message, wParam, lParam);
    }

    switch (message) {
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
        return TRUE;
    case WM_LBUTTONDOWN:
        self->mSplitterDragging = true;
        SetCapture(window);
        return 0;
    case WM_MOUSEMOVE:
        if (self->mSplitterDragging) {
            POINT point{
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam),
            };
            ClientToScreen(window, &point);
            ScreenToClient(self->mWindow, &point);
            self->setSplitterFromClientY(point.y);
        }
        return 0;
    case WM_LBUTTONUP:
        self->mSplitterDragging = false;
        if (GetCapture() == window) {
            ReleaseCapture();
        }
        return 0;
    case WM_CAPTURECHANGED:
        self->mSplitterDragging = false;
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const HDC deviceContext = BeginPaint(window, &paint);
        if (deviceContext == nullptr) {
            return 0;
        }
        RECT client{};
        GetClientRect(window, &client);
        FillRect(deviceContext,
                 &client,
                 reinterpret_cast<HBRUSH>(COLOR_3DFACE + 1));
        DrawEdge(deviceContext, &client, EDGE_RAISED, BF_RECT);
        EndPaint(window, &paint);
        return 0;
    }
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

LRESULT MainWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        if (limits == nullptr) {
            return DefWindowProcW(mWindow, message, wParam, lParam);
        }
        limits->ptMinTrackSize.x = scaleForWindow(mWindow, MINIMUM_WINDOW_WIDTH);
        limits->ptMinTrackSize.y = scaleForWindow(mWindow, MINIMUM_WINDOW_HEIGHT);
        return 0;
    }
    case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        if (suggested != nullptr) {
            SetWindowPos(mWindow,
                         nullptr,
                         suggested->left,
                         suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        mCurrentDpi = HIWORD(wParam);
        if (mCurrentDpi == 0) {
            mCurrentDpi = DEFAULT_DPI;
        }
        updateLogFont();
        layoutControls();
        return 0;
    }
    case WM_SIZE:
        layoutControls();
        return 0;
    case WM_TIMER:
        if (wParam == PROCESS_EVENT_TIMER) {
            pollProcessEvents();
            return 0;
        }
        break;
    case WM_COMMAND: {
        const int controlId = LOWORD(wParam);
        const int notificationCode = HIWORD(wParam);
        if (controlId == IDC_LOG_LABEL && notificationCode == STN_CLICKED) {
            SetFocus(mLogEdit);
            return 0;
        }
        if (notificationCode != BN_CLICKED) {
            break;
        }
        switch (controlId) {
        case IDC_ADD:
            showPhaseFourMessage(L"Add");
            return 0;
        case IDC_EDIT:
            invokeEditIfAvailable();
            return 0;
        case IDC_DELETE:
            showPhaseFourMessage(L"Delete");
            return 0;
        case IDC_START:
            startSelected();
            return 0;
        case IDC_STOP:
            stopSelected();
            return 0;
        case IDC_RESTART:
            restartSelected();
            return 0;
        case IDC_LOG_COMBINED:
            mLogView = LogView::COMBINED;
            updateLogOptions();
            refreshLogs();
            return 0;
        case IDC_LOG_STDOUT:
            mLogView = LogView::STDOUT;
            updateLogOptions();
            refreshLogs();
            return 0;
        case IDC_LOG_STDERR:
            mLogView = LogView::STDERR;
            updateLogOptions();
            refreshLogs();
            return 0;
        case IDC_CLEAR:
            clearLogs();
            return 0;
        case IDC_JUMP_LATEST:
            SendMessageW(mLogEdit,
                         EM_SETSEL,
                         static_cast<WPARAM>(-1),
                         static_cast<LPARAM>(-1));
            SendMessageW(mLogEdit, EM_SCROLLCARET, 0, 0);
            return 0;
        case IDC_WRAP_LINES:
            mConfiguration.mPreferences.mWrapLines =
                SendMessageW(mWrapLinesCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
            updateLogOptions();
            refreshLogs();
            savePreferences();
            return 0;
        case IDC_AUTO_SCROLL:
            mConfiguration.mPreferences.mAutoScroll =
                SendMessageW(mAutoScrollCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
            savePreferences();
            return 0;
        default:
            break;
        }
        break;
    }
    case WM_NOTIFY:
        return handleListViewMessage(message, wParam, lParam);
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        KillTimer(mWindow, PROCESS_EVENT_TIMER);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(mWindow, message, wParam, lParam);
}

LRESULT MainWindow::handleListViewMessage(UINT message,
                                          WPARAM wParam,
                                          LPARAM lParam) {
    if (message == WM_NOTIFY) {
        const auto* header = reinterpret_cast<const NMHDR*>(lParam);
        if (header == nullptr || header->hwndFrom != mListView) {
            return DefWindowProcW(mWindow, message, wParam, lParam);
        }
        if (header->code == LVN_ITEMCHANGED) {
            if (!mUpdatingList) {
                syncSelection();
            }
            return 0;
        }
        if (header->code == NM_DBLCLK) {
            const auto* activation =
                reinterpret_cast<const NMITEMACTIVATE*>(lParam);
            if (activation != nullptr && activation->iItem >= 0 &&
                activation->iItem < ListView_GetItemCount(mListView)) {
                mUpdatingList = true;
                ListView_SetItemState(mListView,
                                      -1,
                                      0,
                                      LVIS_SELECTED | LVIS_FOCUSED);
                ListView_SetItemState(mListView,
                                      activation->iItem,
                                      LVIS_SELECTED | LVIS_FOCUSED,
                                      LVIS_SELECTED | LVIS_FOCUSED);
                mUpdatingList = false;
                mSelectionAnchorIndex = activation->iItem;
                syncSelection();
                if (mSelectedCommandIds.size() == 1) {
                    const CommandConfig* command =
                        commandById(mSelectedCommandIds.front());
                    if (command != nullptr && isEditable(*command)) {
                        showPhaseFourMessage(L"Edit");
                    }
                }
            }
            return 0;
        }
        return DefWindowProcW(mWindow, message, wParam, lParam);
    }

    if (message == WM_LBUTTONDOWN) {
        LVHITTESTINFO hitTest{};
        hitTest.pt.x = GET_X_LPARAM(lParam);
        hitTest.pt.y = GET_Y_LPARAM(lParam);
        const int item = ListView_HitTest(mListView, &hitTest);
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
                const int count = ListView_GetItemCount(mListView);
                if (count > 0) {
                    mUpdatingList = true;
                    ListView_SetItemState(mListView,
                                          -1,
                                          LVIS_SELECTED,
                                          LVIS_SELECTED);
                    ListView_SetItemState(mListView,
                                          0,
                                          LVIS_FOCUSED,
                                          LVIS_FOCUSED);
                    mUpdatingList = false;
                    mSelectionAnchorIndex = 0;
                    syncSelection();
                }
                return 0;
            }
            break;
        case VK_UP:
            if (shiftPressed) {
                selectRange(mSelectionAnchorIndex, focusedItem() - 1);
                return 0;
            }
            if (controlPressed) {
                moveFocus(-1);
                return 0;
            }
            break;
        case VK_DOWN:
            if (shiftPressed) {
                selectRange(mSelectionAnchorIndex, focusedItem() + 1);
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
            invokeEditIfAvailable();
            return 0;
        case VK_DELETE:
            if (!mSelectedCommandIds.empty()) {
                showPhaseFourMessage(L"Delete");
            }
            return 0;
        case VK_SPACE:
            toggleFocusedSelection();
            return 0;
        default:
            break;
        }
    }

    return forwardListViewMessage(message, wParam, lParam);
}

LRESULT MainWindow::forwardListViewMessage(UINT message,
                                           WPARAM wParam,
                                           LPARAM lParam) const {
    if (mListViewPreviousProc == nullptr) {
        return DefWindowProcW(mListView, message, wParam, lParam);
    }
    return CallWindowProcW(mListViewPreviousProc,
                           mListView,
                           message,
                           wParam,
                           lParam);
}

std::expected<void, DWORD> MainWindow::createControls() {
    const auto splitterClass = registerSplitterClass();
    if (!splitterClass) {
        return splitterClass;
    }

    mRichEditModule = LoadLibraryW(L"Msftedit.dll");
    if (mRichEditModule == nullptr) {
        const DWORD error = GetLastError();
        return std::unexpected(error == ERROR_SUCCESS ? ERROR_MOD_NOT_FOUND
                                                       : error);
    }

    const DWORD childStyle = WS_CHILD | WS_VISIBLE;
    mActionBar = CreateWindowExW(0,
                                 L"STATIC",
                                 nullptr,
                                 childStyle,
                                 0,
                                 0,
                                 0,
                                 0,
                                 mWindow,
                                 nullptr,
                                 mInstance,
                                 nullptr);
    mOptionsBar = CreateWindowExW(0,
                                  L"STATIC",
                                  nullptr,
                                  childStyle,
                                  0,
                                  0,
                                  0,
                                  0,
                                  mWindow,
                                  nullptr,
                                  mInstance,
                                  nullptr);
    mSplitter = CreateWindowExW(0,
                                SPLITTER_CLASS_NAME,
                                nullptr,
                                childStyle | WS_TABSTOP,
                                0,
                                0,
                                0,
                                0,
                                mWindow,
                                nullptr,
                                mInstance,
                                this);
    mListView = CreateWindowExW(WS_EX_CLIENTEDGE,
                                WC_LISTVIEWW,
                                nullptr,
                                childStyle | WS_TABSTOP | LVS_REPORT |
                                    LVS_SHOWSELALWAYS,
                                0,
                                0,
                                0,
                                0,
                                mWindow,
                                nullptr,
                                mInstance,
                                nullptr);
    mLogLabel = CreateWindowExW(0,
                                L"STATIC",
                                L"Lo&g output",
                                childStyle | SS_LEFT | SS_NOTIFY,
                                0,
                                0,
                                0,
                                0,
                                mWindow,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(1108)),
                                mInstance,
                                nullptr);
    mLogEdit = CreateWindowExW(WS_EX_CLIENTEDGE,
                               MSFTEDIT_CLASS,
                               nullptr,
                               childStyle | WS_TABSTOP | ES_MULTILINE |
                                   ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL |
                                   WS_VSCROLL | WS_HSCROLL,
                               0,
                               0,
                               0,
                               0,
                               mWindow,
                               nullptr,
                               mInstance,
                               nullptr);

    const std::array<bool, 6> requiredWindows{
        mActionBar != nullptr,
        mOptionsBar != nullptr,
        mSplitter != nullptr,
        mListView != nullptr,
        mLogLabel != nullptr,
        mLogEdit != nullptr,
    };
    if (std::ranges::any_of(requiredWindows,
                            [](bool value) { return !value; })) {
        const DWORD error = GetLastError();
        return std::unexpected(error == ERROR_SUCCESS ? ERROR_FUNCTION_FAILED
                                                       : error);
    }

    const DWORD listStyle = ListView_GetExtendedListViewStyle(mListView);
    ListView_SetExtendedListViewStyle(
        mListView,
        listStyle | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES |
            LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    ListView_SetUnicodeFormat(mListView, TRUE);
    for (std::size_t index = 0; index < COLUMN_LABELS.size(); ++index) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        column.pszText = const_cast<wchar_t*>(COLUMN_LABELS[index]);
        column.cx = scaleForWindow(mWindow,
                                    index < FIXED_COLUMN_WIDTHS.size()
                                        ? FIXED_COLUMN_WIDTHS[index]
                                        : MINIMUM_WORKING_DIRECTORY_WIDTH);
        column.iSubItem = static_cast<int>(index);
        ListView_InsertColumn(mListView, static_cast<int>(index), &column);
    }

    const auto setDefaultFont = [](HWND control) {
        SendMessageW(control,
                     WM_SETFONT,
                     reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
                     TRUE);
    };
    setDefaultFont(mListView);
    setDefaultFont(mLogLabel);

    for (std::size_t index = 0; index < ACTION_BUTTON_LABELS.size(); ++index) {
        mActionButtons[index] = CreateWindowExW(
            0,
            L"BUTTON",
            ACTION_BUTTON_LABELS[index],
            childStyle | WS_TABSTOP | BS_PUSHBUTTON,
            0,
            0,
            0,
            0,
            mWindow,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(ACTION_BUTTON_IDS[index])),
            mInstance,
            nullptr);
        if (mActionButtons[index] == nullptr) {
            const DWORD error = GetLastError();
            return std::unexpected(error == ERROR_SUCCESS ? ERROR_FUNCTION_FAILED
                                                           : error);
        }
        setDefaultFont(mActionButtons[index]);
    }

    mCombinedRadio = CreateWindowExW(0,
                                     L"BUTTON",
                                     L"Co&mbined",
                                     childStyle | WS_TABSTOP | BS_AUTORADIOBUTTON |
                                         WS_GROUP,
                                     0,
                                     0,
                                     0,
                                     0,
                                     mWindow,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(1101)),
                                     mInstance,
                                     nullptr);
    mStdoutRadio = CreateWindowExW(0,
                                   L"BUTTON",
                                   L"stdo&ut",
                                   childStyle | WS_TABSTOP | BS_AUTORADIOBUTTON,
                                   0,
                                   0,
                                   0,
                                   0,
                                   mWindow,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(1102)),
                                   mInstance,
                                   nullptr);
    mStderrRadio = CreateWindowExW(
        0,
        L"BUTTON",
        L"Standard &error",
        childStyle | WS_TABSTOP | BS_AUTORADIOBUTTON,
        0,
        0,
        0,
        0,
        mWindow,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(1103)),
        mInstance,
        nullptr);
    mClearButton = CreateWindowExW(0,
                                   L"BUTTON",
                                   L"&Clear",
                                   childStyle | WS_TABSTOP | BS_PUSHBUTTON,
                                   0,
                                   0,
                                   0,
                                   0,
                                   mWindow,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(1104)),
                                   mInstance,
                                   nullptr);
    mJumpLatestButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"&Jump to Latest",
        childStyle | WS_TABSTOP | BS_PUSHBUTTON,
        0,
        0,
        0,
        0,
        mWindow,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(1105)),
        mInstance,
        nullptr);
    mWrapLinesCheck = CreateWindowExW(0,
                                     L"BUTTON",
                                     L"&Word wrap",
                                     childStyle | WS_TABSTOP | BS_AUTOCHECKBOX,
                                     0,
                                     0,
                                     0,
                                     0,
                                     mWindow,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(1106)),
                                     mInstance,
                                     nullptr);
    mAutoScrollCheck = CreateWindowExW(
        0,
        L"BUTTON",
        L"Auto-scro&ll",
        childStyle | WS_TABSTOP | BS_AUTOCHECKBOX,
        0,
        0,
        0,
        0,
        mWindow,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(1107)),
        mInstance,
        nullptr);

    const std::array<HWND, 7> optionControls{
        mCombinedRadio,
        mStdoutRadio,
        mStderrRadio,
        mClearButton,
        mJumpLatestButton,
        mWrapLinesCheck,
        mAutoScrollCheck,
    };
    if (std::ranges::any_of(optionControls,
                            [](HWND control) { return control == nullptr; })) {
        const DWORD error = GetLastError();
        return std::unexpected(error == ERROR_SUCCESS ? ERROR_FUNCTION_FAILED
                                                       : error);
    }
    for (const HWND control : optionControls) {
        setDefaultFont(control);
    }

    SendMessageW(mLogEdit, EM_EXLIMITTEXT, 0, 1'048'576);
    SendMessageW(mLogEdit, EM_HIDESELECTION, TRUE, 0);
    SendMessageW(mLogEdit, EM_SETBKGNDCOLOR, 0, RGB(255, 255, 255));

    SetLastError(ERROR_SUCCESS);
    mListViewPreviousProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        mListView,
        GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(&MainWindow::listViewProc)));
    if (mListViewPreviousProc == nullptr && GetLastError() != ERROR_SUCCESS) {
        const DWORD error = GetLastError();
        return std::unexpected(error == ERROR_SUCCESS ? ERROR_FUNCTION_FAILED
                                                       : error);
    }
    SetWindowLongPtrW(mListView,
                      GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(this));
    return {};
}

std::expected<void, DWORD> MainWindow::registerSplitterClass() const {
    WNDCLASSEXW splitterClass{};
    splitterClass.cbSize = sizeof(WNDCLASSEXW);
    splitterClass.hInstance = mInstance;
    splitterClass.lpfnWndProc = &MainWindow::splitterProc;
    splitterClass.lpszClassName = SPLITTER_CLASS_NAME;
    splitterClass.hCursor = LoadCursorW(nullptr, IDC_SIZENS);
    splitterClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_3DFACE + 1);
    if (RegisterClassExW(&splitterClass) == 0) {
        const DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            return std::unexpected(error == ERROR_SUCCESS ? ERROR_FUNCTION_FAILED
                                                           : error);
        }
    }
    return {};
}

void MainWindow::layoutControls() {
    if (mWindow == nullptr || mListView == nullptr) {
        return;
    }
    RECT client{};
    GetClientRect(mWindow, &client);
    const int width = std::max(0L, client.right - client.left);
    const int height = std::max(0L, client.bottom - client.top);
    const int margin = scaleForWindow(mWindow, CONTENT_MARGIN);
    const int actionBarHeight = scaleForWindow(mWindow, ACTION_BAR_HEIGHT);
    const int contentTop = actionBarHeight +
                           scaleForWindow(mWindow, CONTENT_TOP_PADDING);
    const int contentBottom = std::max(
        contentTop,
        height - scaleForWindow(mWindow, CONTENT_BOTTOM_PADDING));
    const int splitterHeight = scaleForWindow(mWindow, SPLITTER_HEIGHT);
    const int availableHeight =
        std::max(0, contentBottom - contentTop - splitterHeight);

    int topHeight = MulDiv(availableHeight, mSplitterPercent, 100);
    const int minimumListHeight = scaleForWindow(mWindow, MINIMUM_LIST_HEIGHT);
    const int minimumBottomHeight = scaleForWindow(
        mWindow, OPTIONS_BAR_HEIGHT + LOG_LABEL_HEIGHT + 60);
    const int maximumTopHeight =
        std::max(0, availableHeight - minimumBottomHeight);
    const int minimumTopHeight = std::min(minimumListHeight, maximumTopHeight);
    topHeight = std::clamp(topHeight, minimumTopHeight, maximumTopHeight);
    const int splitterY = contentTop + topHeight;
    const int bottomY = splitterY + splitterHeight;
    const int contentWidth = std::max(0, width - 2 * margin);

    SetWindowPos(mActionBar,
                 nullptr,
                 0,
                 0,
                 width,
                 actionBarHeight,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(mOptionsBar,
                 nullptr,
                 margin,
                 bottomY,
                 contentWidth,
                 scaleForWindow(mWindow, OPTIONS_BAR_HEIGHT),
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(mListView,
                 nullptr,
                 margin,
                 contentTop,
                 contentWidth,
                 std::max(0, topHeight),
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(mSplitter,
                 nullptr,
                 margin,
                 splitterY,
                 contentWidth,
                 splitterHeight,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    const int actionY = scaleForWindow(mWindow, 6);
    int actionX = margin;
    const int actionGap = scaleForWindow(mWindow, 4);
    for (std::size_t index = 0; index < mActionButtons.size(); ++index) {
        const int buttonWidth =
            scaleForWindow(mWindow, ACTION_BUTTON_WIDTHS[index]);
        SetWindowPos(mActionButtons[index],
                     nullptr,
                     actionX,
                     actionY,
                     buttonWidth,
                     scaleForWindow(mWindow, ACTION_BAR_BUTTON_HEIGHT),
                     SWP_NOZORDER | SWP_NOACTIVATE);
        actionX += buttonWidth + actionGap;
    }

    const int optionsY = bottomY + scaleForWindow(mWindow, 1);
    int optionX = margin + scaleForWindow(mWindow, 2);
    const int optionGap = scaleForWindow(mWindow, 2);
    const std::array<std::pair<HWND, int>, 3> radios{
        std::pair{mCombinedRadio, 78},
        std::pair{mStdoutRadio, 72},
        std::pair{mStderrRadio, 110},
    };
    for (const auto& [control, logicalWidth] : radios) {
        const int controlWidth = scaleForWindow(mWindow, logicalWidth);
        SetWindowPos(control,
                     nullptr,
                     optionX,
                     optionsY,
                     controlWidth,
                     scaleForWindow(mWindow, 26),
                     SWP_NOZORDER | SWP_NOACTIVATE);
        optionX += controlWidth + optionGap;
    }

    const int rightGap = scaleForWindow(mWindow, 4);
    int rightX = width - margin;
    const std::array<std::pair<HWND, int>, 4> rightControls{
        std::pair{mAutoScrollCheck, 88},
        std::pair{mWrapLinesCheck, 82},
        std::pair{mJumpLatestButton, 108},
        std::pair{mClearButton, 60},
    };
    for (const auto& [control, logicalWidth] : rightControls) {
        const int controlWidth = scaleForWindow(mWindow, logicalWidth);
        rightX -= controlWidth;
        SetWindowPos(control,
                     nullptr,
                     rightX,
                     optionsY,
                     controlWidth,
                     scaleForWindow(mWindow, 26),
                     SWP_NOZORDER | SWP_NOACTIVATE);
        rightX -= rightGap;
    }

    const int labelY = bottomY + scaleForWindow(mWindow, OPTIONS_BAR_HEIGHT);
    SetWindowPos(mLogLabel,
                 nullptr,
                 margin,
                 labelY,
                 contentWidth,
                 scaleForWindow(mWindow, LOG_LABEL_HEIGHT),
                 SWP_NOZORDER | SWP_NOACTIVATE);
    const int logY = labelY + scaleForWindow(mWindow, LOG_LABEL_HEIGHT);
    SetWindowPos(mLogEdit,
                 nullptr,
                 margin,
                 logY,
                 contentWidth,
                 std::max(0, height - logY - margin),
                 SWP_NOZORDER | SWP_NOACTIVATE);

    updateListColumns();
}

void MainWindow::updateListColumns() {
    if (mListView == nullptr) {
        return;
    }
    RECT client{};
    GetClientRect(mListView, &client);
    const int availableWidth = std::max(0L, client.right - client.left);
    int fixedWidth = 0;
    for (std::size_t index = 0; index < FIXED_COLUMN_WIDTHS.size(); ++index) {
        const int width = scaleForWindow(mWindow, FIXED_COLUMN_WIDTHS[index]);
        ListView_SetColumnWidth(mListView, static_cast<int>(index), width);
        fixedWidth += width;
    }
    const int workingDirectoryWidth = std::max(
        scaleForWindow(mWindow, MINIMUM_WORKING_DIRECTORY_WIDTH),
        availableWidth - fixedWidth);
    ListView_SetColumnWidth(mListView,
                            static_cast<int>(FIXED_COLUMN_WIDTHS.size()),
                            workingDirectoryWidth);
}

void MainWindow::updateLogFont() {
    if (mLogEdit == nullptr) {
        return;
    }
    const int fontHeight = -MulDiv(10, static_cast<int>(mCurrentDpi), 72);
    const HFONT newFont = CreateFontW(fontHeight,
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
                                      FIXED_PITCH | FF_MODERN,
                                      L"Consolas");
    if (newFont == nullptr) {
        return;
    }
    const HFONT oldFont = mLogFont;
    mLogFont = newFont;
    SendMessageW(mLogEdit,
                 WM_SETFONT,
                 reinterpret_cast<WPARAM>(mLogFont),
                 TRUE);
    if (oldFont != nullptr) {
        DeleteObject(oldFont);
    }
}

void MainWindow::updateLogOptions() {
    if (mCombinedRadio == nullptr) {
        return;
    }
    SendMessageW(mCombinedRadio,
                 BM_SETCHECK,
                 mLogView == LogView::COMBINED ? BST_CHECKED : BST_UNCHECKED,
                 0);
    SendMessageW(mStdoutRadio,
                 BM_SETCHECK,
                 mLogView == LogView::STDOUT ? BST_CHECKED : BST_UNCHECKED,
                 0);
    SendMessageW(mStderrRadio,
                 BM_SETCHECK,
                 mLogView == LogView::STDERR ? BST_CHECKED : BST_UNCHECKED,
                 0);
    SendMessageW(mWrapLinesCheck,
                 BM_SETCHECK,
                 mConfiguration.mPreferences.mWrapLines ? BST_CHECKED
                                                         : BST_UNCHECKED,
                 0);
    SendMessageW(mAutoScrollCheck,
                 BM_SETCHECK,
                 mConfiguration.mPreferences.mAutoScroll ? BST_CHECKED
                                                          : BST_UNCHECKED,
                 0);
    SendMessageW(mLogEdit,
                 EM_SETTARGETDEVICE,
                 0,
                 mConfiguration.mPreferences.mWrapLines ? 0 : 1);
}

void MainWindow::updateActionAvailability() {
    if (mActionButtons[0] == nullptr) {
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
        states, [](State state) { return state == State::RUNNING; });
    const bool restartEnabled = std::ranges::any_of(
        states, [](State state) {
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
        EnableWindow(mActionButtons[index], enabled[index] ? TRUE : FALSE);
    }
    EnableWindow(mClearButton,
                 mActiveCommandId.empty() ? FALSE : TRUE);
}

void MainWindow::refreshRows() {
    if (mListView == nullptr) {
        return;
    }
    std::vector<std::wstring> selectedIds;
    for (int item = ListView_GetNextItem(mListView, -1, LVNI_SELECTED);
         item >= 0;
         item = ListView_GetNextItem(mListView, item, LVNI_SELECTED)) {
        if (item < static_cast<int>(mConfiguration.mCommands.size())) {
            selectedIds.push_back(mConfiguration.mCommands[item].mId);
        }
    }
    const int focusedIndex = focusedItem();
    const std::wstring focusedId =
        focusedIndex >= 0 &&
                focusedIndex < static_cast<int>(mConfiguration.mCommands.size())
            ? mConfiguration.mCommands[focusedIndex].mId
            : std::wstring{};

    mUpdatingList = true;
    ListView_DeleteAllItems(mListView);
    for (std::size_t index = 0; index < mConfiguration.mCommands.size(); ++index) {
        const CommandConfig& command = mConfiguration.mCommands[index];
        const RuntimeSnapshot runtime = mProcessManager.snapshot(command.mId);
        std::array<std::wstring, 6> values{
            command.mName,
            stateToString(runtime.mState),
            numberOrEmpty(runtime.mPid),
            exitCodeOrEmpty(runtime.mExitCode),
            command.mAutoStart ? L"Yes" : L"No",
            command.mWorkingDirectory,
        };
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(index);
        item.pszText = values[0].data();
        const int inserted = ListView_InsertItem(mListView, &item);
        if (inserted < 0) {
            continue;
        }
        for (std::size_t column = 1; column < values.size(); ++column) {
            ListView_SetItemText(mListView,
                                 inserted,
                                 static_cast<int>(column),
                                 values[column].data());
        }
    }

    for (std::size_t index = 0; index < mConfiguration.mCommands.size(); ++index) {
        const std::wstring& commandId = mConfiguration.mCommands[index].mId;
        if (std::ranges::find(selectedIds, commandId) != selectedIds.end()) {
            ListView_SetItemState(mListView,
                                  static_cast<int>(index),
                                  LVIS_SELECTED,
                                  LVIS_SELECTED);
        }
        if (commandId == focusedId) {
            ListView_SetItemState(mListView,
                                  static_cast<int>(index),
                                  LVIS_FOCUSED,
                                  LVIS_FOCUSED);
            mSelectionAnchorIndex = static_cast<int>(index);
        }
    }
    mUpdatingList = false;
    syncSelection();
}

void MainWindow::refreshLogs() {
    if (mLogEdit == nullptr) {
        return;
    }
    const bool wasAtBottom = logIsAtBottom();
    const int firstVisibleLine = static_cast<int>(SendMessageW(
        mLogEdit, EM_GETFIRSTVISIBLELINE, 0, 0));

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
    std::vector<std::pair<std::size_t, std::size_t>> stderrRanges;
    for (const LogLine& line : *lines) {
        const std::size_t start = text.size();
        text += formatLogLine(line);
        if (line.mStream == "stderr") {
            stderrRanges.emplace_back(start, text.size());
        }
    }

    SetWindowTextW(mLogEdit, text.c_str());
    CHARFORMAT2W stderrFormat{};
    stderrFormat.cbSize = sizeof(CHARFORMAT2W);
    stderrFormat.dwMask = CFM_COLOR;
    stderrFormat.crTextColor = STDERR_COLOR;
    for (const auto& [start, end] : stderrRanges) {
        SendMessageW(mLogEdit,
                     EM_SETSEL,
                     static_cast<WPARAM>(start),
                     static_cast<LPARAM>(end));
        SendMessageW(mLogEdit,
                     EM_SETCHARFORMAT,
                     SCF_SELECTION,
                     reinterpret_cast<LPARAM>(&stderrFormat));
    }
    SendMessageW(mLogEdit, EM_HIDESELECTION, TRUE, 0);

    if (mConfiguration.mPreferences.mAutoScroll && wasAtBottom) {
        SendMessageW(mLogEdit,
                     EM_SETSEL,
                     static_cast<WPARAM>(-1),
                     static_cast<LPARAM>(-1));
        SendMessageW(mLogEdit, EM_SCROLLCARET, 0, 0);
    } else {
        const int currentFirstVisibleLine = static_cast<int>(SendMessageW(
            mLogEdit, EM_GETFIRSTVISIBLELINE, 0, 0));
        SendMessageW(mLogEdit,
                     EM_LINESCROLL,
                     0,
                     firstVisibleLine - currentFirstVisibleLine);
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
        MessageBoxW(mWindow,
                    result.error().c_str(),
                    L"Save Failed",
                    MB_OK | MB_ICONERROR);
    }
}

void MainWindow::setSplitterFromClientY(int clientY) {
    RECT client{};
    GetClientRect(mWindow, &client);
    const int actionBarHeight = scaleForWindow(mWindow, ACTION_BAR_HEIGHT);
    const int contentTop = actionBarHeight +
                           scaleForWindow(mWindow, CONTENT_TOP_PADDING);
    const int contentBottom = client.bottom -
                              scaleForWindow(mWindow, CONTENT_BOTTOM_PADDING);
    const int splitterHeight = scaleForWindow(mWindow, SPLITTER_HEIGHT);
    const int availableHeight =
        std::max(0, contentBottom - contentTop - splitterHeight);
    if (availableHeight == 0) {
        return;
    }
    const int topHeight = std::clamp(clientY - contentTop,
                                     0,
                                     availableHeight);
    mSplitterPercent = std::clamp(MulDiv(topHeight, 100, availableHeight),
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

void MainWindow::showPhaseFourMessage(std::wstring_view action) {
    const std::wstring message = std::wstring(action) +
                                 L" command dialogs will be implemented in "
                                 L"the next migration phase.";
    MessageBoxW(mWindow,
                message.c_str(),
                L"Command Runner",
                MB_OK | MB_ICONINFORMATION);
}

void MainWindow::invokeEditIfAvailable() {
    if (mSelectedCommandIds.size() != 1) {
        startSelected();
        return;
    }
    const CommandConfig* command = commandById(mSelectedCommandIds.front());
    if (command != nullptr && isEditable(*command)) {
        showPhaseFourMessage(L"Edit");
    } else {
        startSelected();
    }
}

void MainWindow::syncSelection() {
    mSelectedCommandIds.clear();
    for (int item = ListView_GetNextItem(mListView, -1, LVNI_SELECTED);
         item >= 0;
         item = ListView_GetNextItem(mListView, item, LVNI_SELECTED)) {
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

void MainWindow::selectRange(int anchorIndex, int targetIndex) {
    const int count = ListView_GetItemCount(mListView);
    if (count == 0) {
        return;
    }
    const int currentIndex = focusedItem();
    if (anchorIndex < 0 || anchorIndex >= count) {
        anchorIndex = currentIndex >= 0 ? currentIndex : 0;
    }
    if (targetIndex < 0 || targetIndex >= count) {
        targetIndex = std::clamp(targetIndex, 0, count - 1);
    }
    mUpdatingList = true;
    ListView_SetItemState(mListView, -1, 0, LVIS_SELECTED);
    const int first = std::min(anchorIndex, targetIndex);
    const int last = std::max(anchorIndex, targetIndex);
    for (int index = first; index <= last; ++index) {
        ListView_SetItemState(mListView,
                              index,
                              LVIS_SELECTED,
                              LVIS_SELECTED);
    }
    ListView_SetItemState(mListView,
                          targetIndex,
                          LVIS_FOCUSED,
                          LVIS_FOCUSED);
    mUpdatingList = false;
    ListView_EnsureVisible(mListView, targetIndex, FALSE);
    syncSelection();
}

void MainWindow::moveFocus(int direction) {
    const int count = ListView_GetItemCount(mListView);
    if (count == 0) {
        return;
    }
    const int current = focusedItem();
    const int start = current >= 0 ? current : 0;
    const int target = std::clamp(start + direction, 0, count - 1);
    ListView_SetItemState(mListView, target, LVIS_FOCUSED, LVIS_FOCUSED);
    ListView_EnsureVisible(mListView, target, FALSE);
}

void MainWindow::moveToBoundary(bool last, bool select) {
    const int count = ListView_GetItemCount(mListView);
    if (count == 0) {
        return;
    }
    const int target = last ? count - 1 : 0;
    mUpdatingList = true;
    if (select) {
        ListView_SetItemState(mListView, -1, 0, LVIS_SELECTED);
        ListView_SetItemState(mListView,
                              target,
                              LVIS_SELECTED,
                              LVIS_SELECTED);
    }
    ListView_SetItemState(mListView,
                          target,
                          LVIS_FOCUSED,
                          LVIS_FOCUSED);
    mUpdatingList = false;
    mSelectionAnchorIndex = target;
    ListView_EnsureVisible(mListView, target, FALSE);
    syncSelection();
}

void MainWindow::toggleFocusedSelection() {
    const int item = focusedItem();
    if (item < 0) {
        return;
    }
    const UINT state = ListView_GetItemState(mListView, item, LVIS_SELECTED);
    const UINT newState =
        (state == LVIS_SELECTED ? 0U : LVIS_SELECTED) | LVIS_FOCUSED;
    ListView_SetItemState(mListView,
                          item,
                          newState,
                          LVIS_SELECTED | LVIS_FOCUSED);
    mSelectionAnchorIndex = item;
    ListView_EnsureVisible(mListView, item, FALSE);
    syncSelection();
}

int MainWindow::focusedItem() const {
    if (mListView == nullptr) {
        return -1;
    }
    return ListView_GetNextItem(mListView, -1, LVNI_FOCUSED);
}

bool MainWindow::isEditable(const CommandConfig& command) const {
    return isInactive(mProcessManager.snapshot(command.mId).mState);
}

const CommandConfig* MainWindow::commandById(std::wstring_view commandId) const {
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
    if (mLogEdit == nullptr) {
        return true;
    }
    SCROLLINFO scrollInfo{};
    scrollInfo.cbSize = sizeof(SCROLLINFO);
    scrollInfo.fMask = SIF_ALL;
    if (GetScrollInfo(mLogEdit, SB_VERT, &scrollInfo) == FALSE) {
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

int MainWindow::scaleForDpi(UINT dpi, int value) {
    const UINT effectiveDpi = dpi == 0 ? DEFAULT_DPI : dpi;
    return MulDiv(value, static_cast<int>(effectiveDpi), DEFAULT_DPI);
}

int MainWindow::scaleForWindow(HWND window, int value) {
    if (window == nullptr) {
        return scaleForDpi(GetDpiForSystem(), value);
    }
    return scaleForDpi(GetDpiForWindow(window), value);
}

}  // namespace command_runner::ui
