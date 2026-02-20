cmake_minimum_required(VERSION 3.20)
project(environ LANGUAGES CXX)

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
# Target
# ---------------------------------------------------------------------------
set(TARGET environ)

add_executable(${TARGET} WIN32
    src/ui/pch.cpp
    src/ui/main.cpp
    src/ui/App.cpp      src/ui/App.h
    src/ui/MainWindow.cpp src/ui/MainWindow.h
)

target_precompile_headers(${TARGET} PRIVATE src/ui/pch.h)
set_property(TARGET ${TARGET} PROPERTY CXX_STANDARD 20)

# /W4 warnings, suppress noisy external-header warnings
target_compile_options(${TARGET} PRIVATE
    /W4
    /external:anglebrackets /external:W0
)

# ---------------------------------------------------------------------------
# Include / link paths
# ---------------------------------------------------------------------------
target_include_directories(${TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui"
    "${WindowsAppSDK_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/Generated Files"
)

target_link_directories(${TARGET} PRIVATE
    "${WindowsAppSDK_DIR}/lib/win10-x64"
)

target_link_libraries(${TARGET}
    Microsoft.WindowsAppRuntime.Bootstrap
    Microsoft.WindowsAppRuntime
    WindowsApp
    ole32
    oleaut32
)

# ---------------------------------------------------------------------------
# Post-build: copy Bootstrap DLL next to the executable
# ---------------------------------------------------------------------------
add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${WindowsAppSDK_DIR}/runtimes/win-x64/native/Microsoft.WindowsAppRuntime.Bootstrap.dll"
        "$<TARGET_FILE_DIR:${TARGET}>"
)
