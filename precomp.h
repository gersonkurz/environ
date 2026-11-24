#pragma once

// SQLite compile-time optimizations (must be before sqlite3.h)
#define SQLITE_THREADSAFE 0                 // No mutex overhead (single-threaded)
#define SQLITE_DEFAULT_MEMSTATUS 0          // Disable memory tracking
#define SQLITE_OMIT_DEPRECATED              // Remove deprecated APIs
#define SQLITE_OMIT_PROGRESS_CALLBACK       // Not used
#define SQLITE_OMIT_SHARED_CACHE            // Not used
#define SQLITE_DQS 0                        // Stricter SQL parsing
#define SQLITE_DEFAULT_WAL_SYNCHRONOUS 1    // NORMAL sync for WAL mode
#define SQLITE_USE_ALLOCA                   // Faster stack allocations
#define SQLITE_DEFAULT_CACHE_SIZE -65536    // 64MB page cache

#include "targetver.h"
#include "version.h"
#define NOMINMAX
#include <toml++/toml.hpp>

#include <sqlite3.h>
#include <pnq/pnq.h>
#include <pnq/sqlite/sqlite.h>
#include <pnq/regis3.h>
#include <pnq/unicode.h>

// Windows Header Files
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <dwrite.h>
#include <unknwn.h>

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

// C++/WinRT headers (only in WinUI 3 app build; src/core headless tests stay WinRT-free)
#ifdef ENVIRON_WINUI
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#include <microsoft.ui.xaml.window.h>
#endif

// C RunTime Header Files
#include <stdlib.h>
#ifdef environ
#undef environ
#endif
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
