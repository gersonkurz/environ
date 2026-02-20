#include "pch.h"

#include <MddBootstrap.h>
#include <WindowsAppSDK-VersionInfo.h>

#include "App.h"

struct BootstrapGuard {
    BootstrapGuard() = default;
    ~BootstrapGuard() { MddBootstrapShutdown(); }
    BootstrapGuard(BootstrapGuard const&) = delete;
    BootstrapGuard& operator=(BootstrapGuard const&) = delete;
};

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    using namespace Microsoft::WindowsAppSDK;
    winrt::check_hresult(MddBootstrapInitialize2(
        Release::MajorMinor,
        Release::VersionTag,
        PACKAGE_VERSION{Runtime::Version::UInt64},
        MddBootstrapInitializeOptions_OnNoMatch_ShowUI));

    BootstrapGuard guard{};

    winrt::Microsoft::UI::Xaml::Application::Start(
        [](auto&&) { winrt::make<App>(); });

    return 0;
}
