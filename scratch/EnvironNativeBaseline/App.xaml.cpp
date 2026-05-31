#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

#include <format>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::EnvironNativeBaseline::implementation
{
    namespace
    {
        void ShowStartupError(std::wstring const& message) noexcept
        {
            ::MessageBoxW(nullptr, message.c_str(), L"Environ Startup Error", MB_OK | MB_ICONERROR);
        }
    }

    App::App()
    {
#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const&)
        {
            if (IsDebuggerPresent())
            {
                __debugbreak();
            }
        });
#endif
    }

    void App::OnLaunched([[maybe_unused]] LaunchActivatedEventArgs const& e)
    {
        try
        {
            m_window = make<MainWindow>();
            m_window.Activate();
        }
        catch (winrt::hresult_error const& error)
        {
            ShowStartupError(std::format(
                L"Startup failed: 0x{:08X}\n{}",
                static_cast<uint32_t>(error.code().value),
                std::wstring_view{error.message()}));
            Exit();
        }
        catch (...)
        {
            ShowStartupError(L"Startup failed with an unknown exception.");
            Exit();
        }
    }
}
