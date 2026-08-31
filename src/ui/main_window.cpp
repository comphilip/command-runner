#include "ui/main_window.h"

#include <utility>

namespace command_runner::ui {

MainWindow::MainWindow(HINSTANCE instance, ConfigData configuration)
    : instance_(instance), configuration_(std::move(configuration)) {}

bool MainWindow::create(int show_command) {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance_;
    window_class.lpfnWndProc = &MainWindow::window_proc;
    window_class.lpszClassName = kWindowClassName;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    window_class.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (RegisterClassExW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    window_ = CreateWindowExW(
        0,
        kWindowClassName,
        L"Command Runner",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        scale_for_window(nullptr, 900),
        scale_for_window(nullptr, 620),
        nullptr,
        nullptr,
        instance_,
        this);
    if (window_ == nullptr) {
        return false;
    }

    ShowWindow(window_, show_command);
    UpdateWindow(window_);
    return true;
}

int MainWindow::run_message_loop() const {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK MainWindow::window_proc(HWND window, UINT message,
                                         WPARAM wparam, LPARAM lparam) {
    MainWindow* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        self = static_cast<MainWindow*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self == nullptr ? DefWindowProcW(window, message, wparam, lparam)
                           : self->handle_message(message, wparam, lparam);
}

LRESULT MainWindow::handle_message(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
        limits->ptMinTrackSize.x = scale_for_window(window_, 640);
        limits->ptMinTrackSize.y = scale_for_window(window_, 420);
        return 0;
    }
    case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(window_, nullptr,
                     suggested->left,
                     suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const HDC device_context = BeginPaint(window_, &paint);
        RECT client{};
        GetClientRect(window_, &client);
        FillRect(device_context, &client,
                 reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
        SetBkMode(device_context, TRANSPARENT);
        DrawTextW(device_context, L"Command Runner", -1, &client,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(window_, &paint);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window_, message, wparam, lparam);
    }
}

int MainWindow::scale_for_window(HWND window, int value) {
    const UINT dpi = window == nullptr ? 96 : GetDpiForWindow(window);
    return MulDiv(value, static_cast<int>(dpi == 0 ? 96 : dpi), 96);
}

}  // namespace command_runner::ui
