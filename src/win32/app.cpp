#include "precomp.h"

// App — application lifecycle: logging, DPI, message loop, entry point.

#include "app.h"

// ---------------------------------------------------------------------------
// App class -- application lifecycle
// ---------------------------------------------------------------------------

bool ui::App::InitLogging()
{
    wchar_t* appdata{nullptr};
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appdata) != S_OK)
    {
        CoTaskMemFree(appdata);
        spdlog::error("SHGetKnownFolderPath failed; cannot set up logging");
        return false;
    }
    std::filesystem::path logPath{appdata};
    CoTaskMemFree(appdata);
    logPath /= L"environ";
    std::filesystem::create_directories(logPath);
    logPath /= L"environ.log";
    auto fileSink{std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true)};
    auto logger{std::make_shared<spdlog::logger>("environ", fileSink)};
    logger->set_level(spdlog::level::info);
    logger->flush_on(spdlog::level::info);
    spdlog::set_default_logger(logger);
    return true;
}

int ui::App::MessageLoop()
{
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

int ui::App::Run(HINSTANCE hInst, int nCmdShow)
{
    m_hInst = hInst;
    m_nCmdShow = nCmdShow;

    if (!InitLogging()) return 1;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    MainWindow wnd;
    if (!wnd.Create(hInst, nCmdShow))
        return 1;

    return MessageLoop();
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow)
{
    ui::App app;
    return app.Run(hInst, nCmdShow);
}
