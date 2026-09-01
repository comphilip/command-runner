#pragma once

#include "core/config_store.h"
#include "core/process_manager.h"

#include <array>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include <windows.h>

namespace command_runner::ui {

class MainWindowHost {
public:
    virtual ~MainWindowHost() = default;

    virtual void onMainWindowCloseRequested() = 0;
    virtual void onMainWindowMinimizeRequested() = 0;
};

class MainWindow final {
public:
    MainWindow(HINSTANCE instance,
               ConfigData& configuration,
               ConfigStore& store,
               ProcessManager& processManager,
               MainWindowHost& host);
    ~MainWindow();

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    [[nodiscard]] std::expected<void, DWORD> create(int showCommand);
    [[nodiscard]] HWND window() const noexcept { return mWindow; }
    void dispose() noexcept;
    void showStopping();

private:
    enum class LogView {
        COMBINED,
        STDOUT,
        STDERR,
    };

    static constexpr int DEFAULT_DPI = 96;
    static constexpr int INITIAL_WINDOW_WIDTH = 900;
    static constexpr int INITIAL_WINDOW_HEIGHT = 620;
    static constexpr int MINIMUM_WINDOW_WIDTH = 640;
    static constexpr int MINIMUM_WINDOW_HEIGHT = 420;
    static constexpr int ACTION_BAR_HEIGHT = 40;
    static constexpr int ACTION_BAR_BUTTON_HEIGHT = 28;
    static constexpr int CONTENT_MARGIN = 8;
    static constexpr int CONTENT_TOP_PADDING = 4;
    static constexpr int CONTENT_BOTTOM_PADDING = 8;
    static constexpr int SPLITTER_HEIGHT = 6;
    static constexpr int INITIAL_SPLITTER_PERCENT = 40;
    static constexpr int MINIMUM_LIST_HEIGHT = 100;
    static constexpr int OPTIONS_BAR_HEIGHT = 30;
    static constexpr int LOG_LABEL_HEIGHT = 20;
    static constexpr int MINIMUM_WORKING_DIRECTORY_WIDTH = 180;
    static constexpr UINT PROCESS_EVENT_TIMER = 1;
    static constexpr UINT PROCESS_EVENT_INTERVAL_MILLISECONDS = 100;

    static constexpr int ACTION_BUTTON_COUNT = 6;
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
    inline static constexpr wchar_t SPLITTER_CLASS_NAME[] =
        L"CommandRunner.HorizontalSplitter";

    static LRESULT CALLBACK windowProc(HWND window,
                                       UINT message,
                                       WPARAM wParam,
                                       LPARAM lParam);
    static LRESULT CALLBACK listViewProc(HWND window,
                                         UINT message,
                                         WPARAM wParam,
                                         LPARAM lParam);
    static LRESULT CALLBACK splitterProc(HWND window,
                                         UINT message,
                                         WPARAM wParam,
                                         LPARAM lParam);

    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleListViewMessage(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT forwardListViewMessage(UINT message,
                                   WPARAM wParam,
                                   LPARAM lParam) const;

    [[nodiscard]] std::expected<void, DWORD> createControls();
    [[nodiscard]] std::expected<void, DWORD> registerSplitterClass() const;

    void layoutControls();
    void updateListColumns();
    void updateLogFont();
    void updateLogOptions();
    void updateActionAvailability();
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
    void selectRange(int anchorIndex, int targetIndex);
    void moveFocus(int direction);
    void moveToBoundary(bool last, bool select);
    void toggleFocusedSelection();
    [[nodiscard]] int focusedItem() const;
    [[nodiscard]] bool isEditable(const CommandConfig& command) const;
    [[nodiscard]] const CommandConfig* commandById(
        std::wstring_view commandId) const;
    [[nodiscard]] static bool isInactive(State state);

    [[nodiscard]] bool logIsAtBottom() const;
    [[nodiscard]] std::wstring formatLogLine(const LogLine& line) const;
    [[nodiscard]] static std::wstring formatTimestamp(double timestamp);
    [[nodiscard]] static DWORD lastErrorOr(DWORD fallback);
    [[nodiscard]] static int scaleForDpi(UINT dpi, int value);
    [[nodiscard]] static int scaleForWindow(HWND window, int value);

    HINSTANCE mInstance{};
    HWND mWindow{};
    HWND mActionBar{};
    HWND mOptionsBar{};
    HWND mListView{};
    HWND mSplitter{};
    HWND mLogLabel{};
    HWND mLogEdit{};
    std::array<HWND, ACTION_BUTTON_COUNT> mActionButtons{};
    HWND mCombinedRadio{};
    HWND mStdoutRadio{};
    HWND mStderrRadio{};
    HWND mClearButton{};
    HWND mJumpLatestButton{};
    HWND mWrapLinesCheck{};
    HWND mAutoScrollCheck{};
    WNDPROC mListViewPreviousProc{};
    HMODULE mRichEditModule{};
    HFONT mLogFont{};

    ConfigData& mConfiguration;
    ConfigStore& mStore;
    ProcessManager& mProcessManager;
    MainWindowHost& mHost;
    std::vector<std::wstring> mSelectedCommandIds;
    std::wstring mActiveCommandId;
    LogView mLogView{LogView::COMBINED};
    int mSelectionAnchorIndex{-1};
    int mSplitterPercent{INITIAL_SPLITTER_PERCENT};
    UINT mCurrentDpi{DEFAULT_DPI};
    bool mUpdatingList{};
    bool mSplitterDragging{};
    bool mDisposed{};
};

}  // namespace command_runner::ui
