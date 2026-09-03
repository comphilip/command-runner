#pragma once

#include <expected>

#include <windows.h>
#include <shellapi.h>
#include <win32xx/wxx_wincore.h>
#include <win32xx/wxx_gdi.h>

namespace command_runner::ui {

[[nodiscard]] DWORD lastWin32ErrorOr(DWORD fallback) noexcept;
[[nodiscard]] UINT windowDpi(HWND window) noexcept;
[[nodiscard]] int scaleForDpi(UINT dpi, int value) noexcept;
[[nodiscard]] int scaleForWindow(HWND window, int value) noexcept;
[[nodiscard]] int measureFontHeight(HWND window, HFONT font) noexcept;
[[nodiscard]] int measureControlTextWidth(
    const Win32xx::CWnd& control, HFONT font) noexcept;
[[nodiscard]] SIZE preferredButtonSize(
    const Win32xx::CWnd& control) noexcept;

void createFont(Win32xx::CFont& font,
                HWND referenceWindow,
                int pointSize,
                DWORD pitchAndFamily,
                LPCWSTR faceName);

void setListViewUnicodeFormat(const Win32xx::CWnd& control) noexcept;
void scrollRichEditCaret(const Win32xx::CWnd& control) noexcept;

template <typename Control>
[[nodiscard]] std::expected<void, DWORD> createChild(
    Control& control,
    HWND parent,
    DWORD exStyle,
    LPCWSTR className,
    LPCWSTR windowName,
    DWORD style,
    int x,
    int y,
    int width,
    int height,
    UINT controlId) noexcept {
    try {
        const HMENU id = reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(controlId));
        if (control.CreateEx(exStyle,
                             className,
                             windowName,
                             style,
                             x,
                             y,
                             width,
                             height,
                             parent,
                             id) == nullptr) {
            return std::unexpected(lastWin32ErrorOr(ERROR_FUNCTION_FAILED));
        }
    } catch (const Win32xx::CException& exception) {
        const DWORD error = exception.GetError();
        return std::unexpected(error == ERROR_SUCCESS
                                   ? ERROR_FUNCTION_FAILED
                                   : error);
    }
    return {};
}

template <typename Control>
[[nodiscard]] bool hasWindow(const Control& control) noexcept {
    return control.IsWindow() != FALSE;
}

}  // namespace command_runner::ui
