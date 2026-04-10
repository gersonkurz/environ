#include "pch.h"
#include "App.xaml.h"

#include <format>

#include <MddBootstrap.h>
using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace
{
    constexpr uint32_t kWindowsAppSdkReleaseMajorMinor{0x00010008};
    constexpr wchar_t const* kWindowsAppSdkVersionTag{L""};
    constexpr uint64_t kWindowsAppSdkRuntimeVersion{0x1F40032608CC0000u};

    struct BootstrapGuard
    {
        BootstrapGuard() = default;
        BootstrapGuard(BootstrapGuard const&) = delete;
        BootstrapGuard& operator=(BootstrapGuard const&) = delete;

        explicit BootstrapGuard(bool initialized) noexcept : m_initialized{initialized}
        {
        }

        ~BootstrapGuard() noexcept
        {
            if (m_initialized)
            {
                MddBootstrapShutdown();
            }
        }

    private:
        bool m_initialized{false};
    };

    void ShowStartupError(std::wstring const& message) noexcept
    {
        ::MessageBoxW(nullptr, message.c_str(), L"CppWinUIWheelProbe Startup Error", MB_OK | MB_ICONERROR);
    }
}

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    init_apartment(apartment_type::single_threaded);

    PACKAGE_VERSION runtime_version{};
    runtime_version.Version = kWindowsAppSdkRuntimeVersion;

    auto const bootstrap_result{
        MddBootstrapInitialize2(
            kWindowsAppSdkReleaseMajorMinor,
            kWindowsAppSdkVersionTag,
            runtime_version,
            MddBootstrapInitializeOptions_None)
    };
    check_hresult(bootstrap_result);
    BootstrapGuard const bootstrap_guard{true};

    try
    {
        Application::Start([](auto&&)
        {
            make<EnvironNativeBaseline::implementation::App>();
        });
    }
    catch (winrt::hresult_error const& error)
    {
        ShowStartupError(std::format(L"Startup failed: 0x{:08X}", static_cast<uint32_t>(error.code().value)));
        return 1;
    }
    catch (...)
    {
        ShowStartupError(L"Startup failed with an unknown exception.");
        return 1;
    }

    return 0;
}
