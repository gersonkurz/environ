cmake_minimum_required(VERSION 3.20)
project(environ LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# nuget_install — download a NuGet package at configure time
# ---------------------------------------------------------------------------
function(nuget_install package version dir out_dir)
    execute_process(
        COMMAND nuget install ${package} -Version ${version} -OutputDirectory "${dir}"
        RESULT_VARIABLE result
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "nuget_install: failed to install ${package} ${version}: ${error}")
    endif()
    set(${out_dir} "${dir}/${package}.${version}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# NuGet packages
# ---------------------------------------------------------------------------
nuget_install("Microsoft.WindowsAppSDK" "1.6.250228001"
    "${CMAKE_CURRENT_BINARY_DIR}/packages" WindowsAppSDK_DIR)
nuget_install("Microsoft.Web.WebView2" "1.0.2651.64"
    "${CMAKE_CURRENT_BINARY_DIR}/packages" WebView2_DIR)
nuget_install("Microsoft.Windows.CppWinRT" "2.0.240405.15"
    "${CMAKE_CURRENT_BINARY_DIR}/packages" CppWinRT_DIR)

# ---------------------------------------------------------------------------
# Generate C++/WinRT projection headers
# ---------------------------------------------------------------------------
execute_process(
    COMMAND "${CppWinRT_DIR}/bin/cppwinrt.exe"
        -optimize
        -input "${WindowsAppSDK_DIR}/lib/uap10.0"
        -input "${WindowsAppSDK_DIR}/lib/uap10.0.18362"
        -input "${WebView2_DIR}/lib"
        -input sdk
        -output "${CMAKE_CURRENT_BINARY_DIR}/Generated Files"
    RESULT_VARIABLE result
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "cppwinrt.exe failed: ${error}")
endif()

# ---------------------------------------------------------------------------
# Self-contained deployment: extract MSIX and generate WinRT manifest
# ---------------------------------------------------------------------------
set(MSIX_FILE "${WindowsAppSDK_DIR}/tools/MSIX/win10-x64/Microsoft.WindowsAppRuntime.1.6.msix")
set(MSIX_CONTENT "${CMAKE_CURRENT_BINARY_DIR}/MsixContent")
set(WINRT_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/WindowsAppSDK.manifest")

# Extract MSIX (it's a ZIP) at configure time
if(NOT EXISTS "${MSIX_CONTENT}/AppxManifest.xml")
    message(STATUS "Extracting Windows App SDK MSIX...")
    execute_process(
        COMMAND powershell -NoProfile -Command
            "Add-Type -Assembly System.IO.Compression.FileSystem; [IO.Compression.ZipFile]::ExtractToDirectory('${MSIX_FILE}', '${MSIX_CONTENT}')"
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Failed to extract Windows App SDK MSIX")
    endif()
endif()

# Generate WinRT activation manifest from AppxManifest.xml
message(STATUS "Generating WinRT activation manifest...")
execute_process(
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate-selfcontained-manifest.ps1"
        -AppxManifest "${MSIX_CONTENT}/AppxManifest.xml"
        -MsixContentDir "${MSIX_CONTENT}"
        -OutputManifest "${WINRT_MANIFEST}"
    RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Failed to generate WinRT activation manifest")
endif()

# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# SQLite3 (built as a separate C library to avoid C++ PCH conflicts)
# ---------------------------------------------------------------------------
add_library(sqlite3 STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/sqlite3/src/sqlite3/sqlite3.c"
)
target_include_directories(sqlite3 PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/sqlite3/src/sqlite3"
)
set_property(TARGET sqlite3 PROPERTY C_STANDARD 11)

set(TARGET environ)

add_executable(${TARGET} WIN32
    src/ui/pch.cpp
    src/ui/main.cpp
    src/ui/App.cpp                src/ui/App.h
    src/ui/MainWindow.cpp         src/ui/MainWindow.h
    src/ui/EnvironmentPage.cpp    src/ui/EnvironmentPage.h
    src/core/EnvStore.cpp         src/core/EnvStore.h
    src/core/EnvWriter.cpp        src/core/EnvWriter.h
    src/core/SnapshotStore.cpp    src/core/SnapshotStore.h
    "${WindowsAppSDK_DIR}/include/UndockedRegFreeWinRT-AutoInitializer.cpp"
)

target_precompile_headers(${TARGET} PRIVATE src/ui/pch.h)
set_property(TARGET ${TARGET} PROPERTY CXX_STANDARD 20)

target_compile_definitions(${TARGET} PRIVATE
    UNICODE _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    MICROSOFT_WINDOWSAPPSDK_SELFCONTAINED=1
)

# /W4 warnings, /utf-8 for spdlog/fmt, suppress noisy external-header warnings
target_compile_options(${TARGET} PRIVATE
    /W4 /utf-8
    /external:anglebrackets /external:W0
)

# ---------------------------------------------------------------------------
# Include / link paths
# ---------------------------------------------------------------------------
target_include_directories(${TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/spdlog/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/pnq/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/tomlplusplus/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/sqlite3/src/sqlite3"
    "${WindowsAppSDK_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/Generated Files"
)

target_link_directories(${TARGET} PRIVATE
    "${WindowsAppSDK_DIR}/lib/win10-x64"
)

target_link_libraries(${TARGET}
    sqlite3
    Microsoft.WindowsAppRuntime
    WindowsApp
    ole32
    oleaut32
    advapi32
)

# WinUI 3 self-contained layout passes need a larger stack than the 1 MB default
target_link_options(${TARGET} PRIVATE /STACK:8388608)

# Embed the WinRT activation manifest into the executable
target_sources(${TARGET} PRIVATE "${WINRT_MANIFEST}")

# ---------------------------------------------------------------------------
# Post-build: copy Windows App SDK runtime files next to the executable
# ---------------------------------------------------------------------------
add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DSRC="${MSIX_CONTENT}"
        -DDST="$<TARGET_FILE_DIR:${TARGET}>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/copy-runtime.cmake"
    COMMENT "Copying Windows App SDK runtime files"
)
