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
nuget_install("Microsoft.WindowsAppSDK" "1.8.260317003"
    "${CMAKE_CURRENT_BINARY_DIR}/packages" WindowsAppSDK_DIR)
nuget_install("Microsoft.WindowsAppSDK.Foundation" "1.8.260222000"
    "${CMAKE_CURRENT_BINARY_DIR}/packages" WindowsAppSDK_Foundation_DIR)
nuget_install("Microsoft.WindowsAppSDK.Runtime" "1.8.260317003"
    "${CMAKE_CURRENT_BINARY_DIR}/packages" WindowsAppSDK_Runtime_DIR)
nuget_install("Microsoft.WindowsAppSDK.InteractiveExperiences" "1.8.260125001"
    "${CMAKE_CURRENT_BINARY_DIR}/packages" WindowsAppSDK_InteractiveExperiences_DIR)
nuget_install("Microsoft.WindowsAppSDK.WinUI" "1.8.260224000"
    "${CMAKE_CURRENT_BINARY_DIR}/packages" WindowsAppSDK_WinUI_DIR)
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
        -input "${WindowsAppSDK_Foundation_DIR}/metadata"
        -input "${WindowsAppSDK_InteractiveExperiences_DIR}/metadata/10.0.18362.0"
        -input "${WindowsAppSDK_WinUI_DIR}/metadata"
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
# Main app target
# ---------------------------------------------------------------------------
# The working app foundation is the MSBuild/XAML project under
# scratch/EnvironNativeBaseline. Keep CMake as the outer entry point, but
# delegate the real WinUI build to the project system that actually supports
# XAML compilation.

if(NOT CMAKE_VS_MSBUILD_COMMAND)
    message(FATAL_ERROR "This project requires a Visual Studio generator with MSBuild support.")
endif()

set(ENVIRON_APP_PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/scratch/EnvironNativeBaseline/EnvironNativeBaseline.vcxproj")
set(ENVIRON_APP_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/scratch/EnvironNativeBaseline/x64/$<CONFIG>/EnvironNativeBaseline")
set(ENVIRON_STAGE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
set(ENVIRON_STAGE_DIR "${ENVIRON_STAGE_ROOT}/environ")

add_custom_target(environ ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_BINARY_DIR}/packages"
        "${CMAKE_CURRENT_SOURCE_DIR}/scratch/EnvironNativeBaseline/packages"
    COMMAND "${CMAKE_VS_MSBUILD_COMMAND}" "${ENVIRON_APP_PROJECT}"
        /p:Configuration=$<CONFIG>
        /p:Platform=x64
    COMMAND "${CMAKE_COMMAND}" -E remove_directory "${ENVIRON_STAGE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${ENVIRON_STAGE_ROOT}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${ENVIRON_APP_OUTPUT_DIR}"
        "${ENVIRON_STAGE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E rm -f
        "${ENVIRON_STAGE_DIR}/EnvironNativeBaseline.exe"
        "${ENVIRON_STAGE_DIR}/EnvironNativeBaseline.pdb"
        "${ENVIRON_STAGE_DIR}/EnvironNativeBaseline.lib"
        "${ENVIRON_STAGE_DIR}/EnvironNativeBaseline.exp"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/knowledge.toml"
        "${ENVIRON_STAGE_DIR}/knowledge.toml"
    COMMENT "Building promoted WinUI app"
    VERBATIM
)
