#include "precomp.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

#include <spdlog/sinks/rotating_file_sink.h>

namespace winrt::EnvironApp::implementation
{

App::App()
{
    UnhandledException([](winrt::Windows::Foundation::IInspectable const&,
                          winrt::Microsoft::UI::Xaml::UnhandledExceptionEventArgs const& e)
    {
        try
        {
            spdlog::error("Unhandled WinUI exception: 0x{:08X} ({})",
                static_cast<uint32_t>(e.Exception().value),
                winrt::to_string(e.Message()));
        }
        catch (...) {}
    });
}

void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&)
{
    try
    {
        m_window = make<MainWindow>();
        m_window.Activate();
    }
    catch (const winrt::hresult_error& e)
    {
        spdlog::error("OnLaunched hresult_error: 0x{:08X} ({})",
            static_cast<uint32_t>(e.code().value),
            winrt::to_string(e.message()));
        throw;
    }
}

} // namespace winrt::EnvironApp::implementation

namespace
{

void init_logging_non_throwing() noexcept
{
    try
    {
        wchar_t* appdata{nullptr};
        if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appdata) == S_OK && appdata)
        {
            std::filesystem::path dir{appdata};
            CoTaskMemFree(appdata);
            dir /= L"environ";
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            auto log_path{(dir / "environ.log").string()};
            auto logger{spdlog::rotating_logger_mt("environ", log_path, 2 * 1024 * 1024, 3)};
            spdlog::set_default_logger(logger);
            spdlog::set_level(spdlog::level::info);
            spdlog::flush_on(spdlog::level::warn);
            spdlog::info("environ {} starting (WinUI 3)", ENVIRON_VERSION_STRING);
        }
        else
        {
            CoTaskMemFree(appdata);
        }
    }
    catch (...)
    {
        // Logging must never throw during startup (prevents 0xC000027B stowed exception).
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    init_logging_non_throwing();

    try
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        winrt::Microsoft::UI::Xaml::Application::Start(
            [](auto&&)
            {
                winrt::make<winrt::EnvironApp::implementation::App>();
            });
    }
    catch (const winrt::hresult_error& e)
    {
        try
        {
            spdlog::error("Fatal WinRT error: 0x{:08X} ({})",
                static_cast<uint32_t>(e.code().value),
                winrt::to_string(e.message()));
        }
        catch (...) {}
        return static_cast<int>(e.code().value);
    }
    catch (const std::exception& e)
    {
        try
        {
            spdlog::error("Fatal std::exception: {}", e.what());
        }
        catch (...) {}
        return 1;
    }
    catch (...)
    {
        return 1;
    }

    return 0;
}
