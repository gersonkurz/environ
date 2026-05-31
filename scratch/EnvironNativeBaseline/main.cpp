#include "pch.h"
#include "App.xaml.h"

#include <format>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace
{
    void ShowStartupError(std::wstring const& message) noexcept
    {
        ::MessageBoxW(nullptr, message.c_str(), L"Environ Startup Error", MB_OK | MB_ICONERROR);
    }
}

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    init_apartment(apartment_type::single_threaded);

    try
    {
        Application::Start([](auto&&)
        {
            try
            {
                make<EnvironNativeBaseline::implementation::App>();
            }
            catch (winrt::hresult_error const& error)
            {
                ShowStartupError(std::format(
                    L"Startup failed: 0x{:08X}\n{}",
                    static_cast<uint32_t>(error.code().value),
                    std::wstring_view{error.message()}));
            }
            catch (...)
            {
                ShowStartupError(L"Startup failed with an unknown exception.");
            }
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
