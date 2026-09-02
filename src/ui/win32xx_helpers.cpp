#include "ui/win32xx_helpers.h"

#include <CommCtrl.h>
#include <Richedit.h>

namespace command_runner::ui {

DWORD lastWin32ErrorOr(DWORD fallback) noexcept {
    const DWORD error = GetLastError();
    return error == ERROR_SUCCESS ? fallback : error;
}

UINT windowDpi(HWND window) noexcept {
    const UINT dpi = window == nullptr ? GetDpiForSystem()
                                       : GetDpiForWindow(window);
    return dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi;
}

int scaleForDpi(UINT dpi, int value) noexcept {
    const UINT effectiveDpi = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi;
    return MulDiv(value, static_cast<int>(effectiveDpi), USER_DEFAULT_SCREEN_DPI);
}

int scaleForWindow(HWND window, int value) noexcept {
    return scaleForDpi(windowDpi(window), value);
}

void createFont(Win32xx::CFont& font,
                HWND referenceWindow,
                int pointSize,
                DWORD pitchAndFamily,
                LPCWSTR faceName) {
    const int height = -MulDiv(pointSize,
                                static_cast<int>(windowDpi(referenceWindow)),
                                72);
    font.CreateFont(height,
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
                    pitchAndFamily,
                    faceName);
}

void setListViewUnicodeFormat(const Win32xx::CWnd& control) noexcept {
    // Win32++ has no dedicated wrapper for LVM_SETUNICODEFORMAT.
    control.SendMessage(LVM_SETUNICODEFORMAT, TRUE, 0);
}

void scrollRichEditCaret(const Win32xx::CWnd& control) noexcept {
    // CRichEdit exposes selection and line scrolling, but not EM_SCROLLCARET.
    control.SendMessage(EM_SCROLLCARET, 0, 0);
}

}  // namespace command_runner::ui
