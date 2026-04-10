#pragma once

#include "MainWindow.g.h"

#include "..\..\src\core\EnvStore.h"

namespace winrt::EnvironNativeBaseline::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();
        void OnFilterChanged(winrt::Windows::Foundation::IInspectable const& sender,
                             winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);

    private:
        void LoadVariables();
        void RebuildRows();

        std::vector<Environ::core::EnvVariable> m_userVariables;
        std::vector<Environ::core::EnvVariable> m_machineVariables;
        std::wstring m_filterText;
        bool m_isElevated{false};
    };
}

namespace winrt::EnvironNativeBaseline::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
