#pragma once

#include "MainWindow.g.h"

#include "..\..\src\core\EnvStore.h"

#include <optional>

namespace winrt::EnvironNativeBaseline::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();
        void OnFilterChanged(winrt::Windows::Foundation::IInspectable const& sender,
                             winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);

    private:
        struct RowVisual
        {
            Environ::core::Scope scope;
            std::size_t index;
            winrt::Microsoft::UI::Xaml::Controls::Border rowBorder;
        };

        void EnsureSelection();
        void LoadVariables();
        void RebuildRows();
        void SelectVariable(Environ::core::Scope scope, std::size_t index);

        std::vector<Environ::core::EnvVariable> m_userVariables;
        std::vector<Environ::core::EnvVariable> m_machineVariables;
        std::vector<RowVisual> m_rowVisuals;
        std::wstring m_filterText;
        bool m_isElevated{false};
        std::optional<RowVisual> m_selectedVariable;
    };
}

namespace winrt::EnvironNativeBaseline::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
