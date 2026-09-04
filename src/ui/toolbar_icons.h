#pragma once

#include <expected>

#include <windows.h>
#include <shellapi.h>
#include <win32xx/wxx_wincore.h>

namespace command_runner::ui {

[[nodiscard]] std::expected<Win32xx::CImageList, DWORD>
createToolbarImageList(HINSTANCE instance, UINT dpi);

}  // namespace command_runner::ui
