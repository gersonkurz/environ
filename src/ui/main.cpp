#include "pch.h"
#include "App.h"

#include <MddBootstrap.h>
#include <WindowsAppSDK-VersionInfo.h>

#include <format>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    struct BootstrapGuard {
        ~BootstrapGuard() {
            MddBootstrapShutdown();
        }
    };

    PACKAGE_VERSION min_version{};
    min_version.Version = WINDOWSAPPSDK_RUNTIME_VERSION_UINT64;

    try {
        winrt::check_hresult(MddBootstrapInitialize2(
            WINDOWSAPPSDK_RELEASE_MAJORMINOR,
            WINDOWSAPPSDK_RELEASE_VERSION_TAG_W,
            min_version,
            MddBootstrapInitializeOptions_None));
        BootstrapGuard bootstrap_shutdown{};

        winrt::Microsoft::UI::Xaml::Application::Start(
            [](auto&&) { winrt::make<App>(); });
    } catch (winrt::hresult_error const& ex) {
        auto message{std::format(L"Bootstrap failed: 0x{:08X}", static_cast<std::uint32_t>(ex.code().value))};
        MessageBoxW(nullptr, message.c_str(), L"Environ Startup Error", MB_OK | MB_ICONERROR);
        return ex.code().value;
    }

    return 0;
}
