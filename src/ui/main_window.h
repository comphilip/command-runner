#pragma once

#include "core/config_store.h"
#include "core/process_manager.h"

#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include <windows.h>
#include <shellapi.h>
#include <wxx_listview.h>
#include <wxx_rebar.h>
#include <wxx_richedit.h>
#include <wxx_stdcontrols.h>
#include <wxx_toolbar.h>
#include <wxx_wincore.h>

namespace command_runner::ui {

class MainWindowHost {
public:
    virtual ~MainWindowHost() = default;

    virtual void onMainWindowCloseRequested() = 0;
    virtual void onMainWindowMinimizeRequested() = 0;
};

class CommandListViewListener {
public:
    virtual ~CommandListViewListener() = default;

    virtual void onListSelectionChanged() = 0;
    virtual void onListActivated() = 0;
    virtual void onListDeleteRequested() = 0;
};

class CommandListView final : public Win32xx::CListView {
public:
    void setListener(CommandListViewListener& listener) noexcept {
        mListener = &listener;
    }

    void setSelectionAnchor(int index) noexcept {
        mSelectionAnchorIndex = index;
    }

    [[nodiscard]] int focusedItem() const;

protected:
    LRESULT WndProc(UINT message, WPARAM wParam, LPARAM lParam) override;

private:
    void notifySelectionChanged();
    void selectRange(int targetIndex);
    void moveFocus(int direction);
    void moveToBoundary(bool last, bool select);
    void toggleFocusedSelection();

    CommandListViewListener* mListener{};
    int mSelectionAnchorIndex{-1};
};

class SplitterListener {
public:
    virtual ~SplitterListener() = default;

    virtual void onSplitterMoved(Win32xx::CPoint screenPoint) = 0;
};

class Splitter final : public Win32xx::CWnd {
public:
    void setListener(SplitterListener& listener) noexcept {
        mListener = &listener;
    }

protected:
    LRESULT WndProc(UINT message, WPARAM wParam, LPARAM lParam) override;
    void PreRegisterClass(WNDCLASS& windowClass) override;
    void PreCreate(CREATESTRUCT& createStruct) override;

private:
    SplitterListener* mListener{};
    bool mDragging{};
};

class MainWindow final : public Win32xx::CWnd,
                         public CommandListViewListener,
                         public SplitterListener {
public:
    MainWindow(HINSTANCE instance,
               ConfigData& configuration,
               ConfigStore& store,
               ProcessManager& processManager,
               MainWindowHost& host);
    ~MainWindow() override;

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    [[nodiscard]] std::expected<void, DWORD> create(int showCommand);
    [[nodiscard]] HWND window() const noexcept { return GetHwnd(); }
    void dispose() noexcept;
    void showStopping();

private:
    enum class LogView {
        COMBINED,
        STDOUT,
        STDERR,
    };

    static constexpr int INITIAL_WINDOW_WIDTH = 900;
    static constexpr int INITIAL_WINDOW_HEIGHT = 620;
    static constexpr int MINIMUM_WINDOW_WIDTH = 640;
    static constexpr int MINIMUM_WINDOW_HEIGHT = 420;
    static constexpr int CONTENT_MARGIN = 8;
    static constexpr int CONTENT_TOP_PADDING = 4;
    static constexpr int CONTENT_BOTTOM_PADDING = 8;
    static constexpr int SPLITTER_HEIGHT = 6;
    static constexpr int INITIAL_SPLITTER_PERCENT = 40;
    static constexpr int MINIMUM_LIST_HEIGHT = 100;
    static constexpr int MINIMUM_WORKING_DIRECTORY_WIDTH = 180;
    static constexpr UINT PROCESS_EVENT_TIMER = 1;
    static constexpr UINT PROCESS_EVENT_INTERVAL_MILLISECONDS = 100;

    static constexpr UINT IDC_ACTION_TOOLBAR = 1201;
    static constexpr int IDC_ADD = 1001;
    static constexpr int IDC_EDIT = 1002;
    static constexpr int IDC_DELETE = 1003;
    static constexpr int IDC_START = 1004;
    static constexpr int IDC_STOP = 1005;
    static constexpr int IDC_RESTART = 1006;
    static constexpr int IDC_LOG_COMBINED = 1101;
    static constexpr int IDC_LOG_STDOUT = 1102;
    static constexpr int IDC_LOG_STDERR = 1103;
    static constexpr int IDC_CLEAR = 1104;
    static constexpr int IDC_JUMP_LATEST = 1105;
    static constexpr int IDC_WRAP_LINES = 1106;
    static constexpr int IDC_AUTO_SCROLL = 1107;
    static constexpr int IDC_LOG_LABEL = 1108;

    inline static constexpr wchar_t WINDOW_CLASS_NAME[] =
        L"CommandRunner.MainWindow";

    [[nodiscard]] std::expected<void, DWORD> createControls();
    void destroyControls() noexcept;
    void layoutControls();
    void updateActionToolBarMetrics();
    void updateListColumns();
    void updateLogFont();
    void updateLogOptions();
    void updateActionAvailability();
    void updateControlFonts();
    void refreshRows();
    void refreshLogs();
    void pollProcessEvents();
    void savePreferences();
    void setSplitterFromClientY(int clientY);

    void startSelected();
    void stopSelected();
    void restartSelected();
    void clearLogs();
    void addCommand();
    void editSelectedCommand();
    void deleteSelectedCommands();
    void invokeEditIfAvailable();
    [[nodiscard]] bool saveConfiguration();

    void syncSelection();
    [[nodiscard]] bool isEditable(const CommandConfig& command) const;
    [[nodiscard]] const CommandConfig* commandById(
        std::wstring_view commandId) const;
    [[nodiscard]] static bool isInactive(State state);

    [[nodiscard]] bool logIsAtBottom() const;
    [[nodiscard]] std::wstring formatLogLine(const LogLine& line) const;
    [[nodiscard]] static std::wstring formatTimestamp(double timestamp);

    void onListSelectionChanged() override;
    void onListActivated() override;
    void onListDeleteRequested() override;
    void onSplitterMoved(Win32xx::CPoint screenPoint) override;

protected:
    LRESULT WndProc(UINT message, WPARAM wParam, LPARAM lParam) override;
    BOOL OnCommand(WPARAM wParam, LPARAM lParam) override;
    LRESULT OnNotify(WPARAM wParam, LPARAM lParam) override;
    void OnDestroy() override;
    void PreRegisterClass(WNDCLASS& windowClass) override;
    void PreCreate(CREATESTRUCT& createStruct) override;

    HINSTANCE mInstance{};
    Win32xx::CReBar mActionReBar;
    Win32xx::CToolBar mActionToolBar;
    Win32xx::CStatic mOptionsBar;
    CommandListView mListView;
    Splitter mSplitter;
    Win32xx::CStatic mLogLabel;
    Win32xx::CRichEdit mLogEdit;
    Win32xx::CButton mCombinedRadio;
    Win32xx::CButton mStdoutRadio;
    Win32xx::CButton mStderrRadio;
    Win32xx::CButton mClearButton;
    Win32xx::CButton mJumpLatestButton;
    Win32xx::CButton mWrapLinesCheck;
    Win32xx::CButton mAutoScrollCheck;
    Win32xx::CFont mUiFont;
    Win32xx::CFont mLogFont;

    ConfigData& mConfiguration;
    ConfigStore& mStore;
    ProcessManager& mProcessManager;
    MainWindowHost& mHost;
    std::vector<std::wstring> mSelectedCommandIds;
    std::wstring mActiveCommandId;
    LogView mLogView{LogView::COMBINED};
    int mSplitterPercent{INITIAL_SPLITTER_PERCENT};
    bool mUpdatingList{};
    bool mDisposed{};
};

}  // namespace command_runner::ui
