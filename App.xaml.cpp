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
    std::filesystem::create_directories(dir);

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
#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e)
        {
            if (IsDebuggerPresent())
            {
                auto errorMessage = e.Message();
                __debugbreak();
            }
        });
#endif
    }

    void App::OnLaunched([[maybe_unused]] LaunchActivatedEventArgs const& e)
    {
        init_logging();
        ::Environ::core::app_settings().load();
        ::Environ::core::snapshot_store().open();

        window = make<MainWindow>();
        window.Activate();
    }
}
