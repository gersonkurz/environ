#pragma once

#include "MainWindow.g.h"

namespace winrt::Environ::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        void OnNavLoaded(Windows::Foundation::IInspectable const& sender,
                         Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnNavSelectionChanged(Microsoft::UI::Xaml::Controls::NavigationView const& sender,
                                   Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const& args);

    private:
        void ConfigureTitleBar();
        void UpdateCaptionButtonColors();
        void NavigateTo(hstring const& tag);
        void ApplyTheme();
        void RestoreWindowPlacement();
        void SaveWindowPlacement();
        fire_and_forget ShowUnsavedChangesDialog();

        Environ::EnvironmentPage m_envPage{nullptr};
        Environ::HistoryPage m_historyPage{nullptr};
        Environ::SettingsPage m_settingsPage{nullptr};
        Environ::AboutPage m_aboutPage{nullptr};
        bool m_closing{false};
    };
}

namespace winrt::Environ::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
