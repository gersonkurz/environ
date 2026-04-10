#pragma once

#include "MainWindow.g.h"

namespace winrt::EnvironNativeBaseline::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

    private:
        void LoadVariables();
    };
}

namespace winrt::EnvironNativeBaseline::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
