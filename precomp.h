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
#include <d2d1.h>
#include <d2d1helper.h>

// C RunTime Header Files
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <algorithm>

#include <chrono>
#include <cwctype>
#include <format>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

