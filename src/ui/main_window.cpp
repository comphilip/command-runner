#include "ui/main_window.h"

#include <utility>

namespace command_runner::ui {

MainWindow::MainWindow(HINSTANCE instance, ConfigData configuration)
    : mInstance(instance), mConfiguration(std::move(configuration)) {}

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
            return std::unexpected(error == ERROR_SUCCESS ? ERROR_FUNCTION_FAILED : error);
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
        return std::unexpected(error == ERROR_SUCCESS ? ERROR_FUNCTION_FAILED : error);
    }

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
            return std::unexpected(error == ERROR_SUCCESS ? ERROR_FUNCTION_FAILED : error);
        }
        if (result == 0) {
            return static_cast<int>(message.wParam);
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

LRESULT CALLBACK MainWindow::windowProc(HWND window, UINT message,
                                        WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
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
        if (suggested == nullptr) {
            return DefWindowProcW(mWindow, message, wParam, lParam);
        }
        SetWindowPos(mWindow, nullptr,
                     suggested->left,
                     suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const HDC deviceContext = BeginPaint(mWindow, &paint);
        if (deviceContext == nullptr) {
            return 0;
        }
        RECT client{};
        GetClientRect(mWindow, &client);
        FillRect(deviceContext, &client,
                 reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
        SetBkMode(deviceContext, TRANSPARENT);
        DrawTextW(deviceContext, L"Command Runner", -1, &client,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(mWindow, &paint);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(mWindow, message, wParam, lParam);
    }
}

int MainWindow::scaleForWindow(HWND window, int value) {
    const UINT dpi = window == nullptr ? DEFAULT_DPI : GetDpiForWindow(window);
    return MulDiv(value,
                  static_cast<int>(dpi == 0 ? DEFAULT_DPI : dpi),
                  DEFAULT_DPI);
}

}  // namespace command_runner::ui
