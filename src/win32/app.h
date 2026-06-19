#pragma once

#include "mainwindow.h"

namespace ui {

class App final
{
    PNQ_DECLARE_NON_COPYABLE(App)

public:
    App() = default;

    // Entry point. Returns process exit code.
    int Run(HINSTANCE hInst, int nCmdShow);

private:
    bool InitLogging();
    int  MessageLoop();

    HINSTANCE m_hInst{nullptr};
    int       m_nCmdShow{SW_SHOWDEFAULT};
};

} // namespace ui
