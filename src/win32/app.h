#pragma once

namespace ui {

class App final
{
    PNQ_DECLARE_NON_COPYABLE(App)

public:
    App() = default;

    // Entry point. Returns process exit code.
    int Run(HINSTANCE hInst, int nCmdShow);

private:
    HINSTANCE m_hInst{nullptr};
    int       m_nCmdShow{SW_SHOWDEFAULT};

    bool InitLogging();
    bool InitGraphics();      // D2D + DWrite factories
    bool InitSettings();      // settings, theme, zoom
    bool InitFonts();
    void InitData();           // registry read + snapshots
    bool CreateMainWindow();
    int  MessageLoop();
};

} // namespace ui
