#pragma once

#include <expected>

#include <windows.h>
#include <shellapi.h>
#include <win32xx/wxx_wincore.h>

namespace command_runner::ui {

struct ToolbarImageLists final {
    Win32xx::CImageList mNormalImages;
    Win32xx::CImageList mDisabledImages;
};

[[nodiscard]] std::expected<ToolbarImageLists, DWORD>
createToolbarImageLists(HINSTANCE instance, UINT dpi);

}  // namespace command_runner::ui
