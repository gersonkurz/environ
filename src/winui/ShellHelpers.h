#pragma once

#include <windows.h>
#include <optional>
#include <string>

namespace Environ::winui
{

/// Browse for a folder or file using RAII IFileOpenDialog.
std::optional<std::wstring> browse_for_path(HWND owner, bool pick_folder, const std::wstring& initial_path);

/// Open a file dialog for importing a TOML file.
std::optional<std::wstring> browse_open_toml(HWND owner);

/// Save file dialog for exporting a TOML file.
std::optional<std::wstring> browse_save_toml(HWND owner);

/// Open a folder in Explorer or select a file in Explorer.
bool reveal_in_explorer(const std::wstring& expanded_path);

/// Relaunch the current executable with UAC elevation ("runas"). Returns true if launched.
bool relaunch_as_admin(HWND owner);

/// Copy UTF-16 text to the Windows clipboard.
bool copy_text_to_clipboard(HWND owner, const std::wstring& text);

/// Return the directory containing the running executable (no trailing backslash).
std::wstring executable_directory();

} // namespace Environ::winui
