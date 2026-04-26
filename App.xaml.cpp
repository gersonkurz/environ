#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

#include "AppSettings.h"
#include "SnapshotStore.h"

#include <spdlog/sinks/basic_file_sink.h>

#include <filesystem>

#include <windows.h>
#include <shlobj.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace {

void init_logging()
{
    wchar_t* appdata{nullptr};
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appdata) != S_OK) {
        CoTaskMemFree(appdata);
        return;
    }
    std::filesystem::path dir{appdata};
    CoTaskMemFree(appdata);
    dir /= L"environ";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return;

    auto log_path{(dir / "environ.log").string()};
    auto logger{spdlog::basic_logger_mt("environ", log_path, true)};
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);
    spdlog::info("Environ starting up");
}

} // namespace

namespace winrt::Environ::implementation
{
    App::App()
    {
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e)
        {
            auto msg{winrt::to_string(e.Message())};
            spdlog::critical("Unhandled exception: {}", msg);
            spdlog::default_logger()->flush();
#if defined _DEBUG
            if (IsDebuggerPresent())
            {
                __debugbreak();
            }
#endif
        });
    }

    void App::OnLaunched([[maybe_unused]] LaunchActivatedEventArgs const& e)
    {
        init_logging();
        ::Environ::core::app_settings().load();
        ::Environ::core::snapshot_store().open();

        window = make<MainWindow>();
        window.Closed([this](auto&&, auto&&) {
            window = nullptr;
        });
        window.Activate();
    }
}
