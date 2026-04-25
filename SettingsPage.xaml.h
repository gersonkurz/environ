#pragma once

#include "SettingsPage.g.h"

namespace winrt::Environ::implementation
{
    struct SettingsPage : SettingsPageT<SettingsPage>
    {
        SettingsPage() = default;

        void OnPageLoaded(Windows::Foundation::IInspectable const& sender,
                          Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnThemeChanged(Windows::Foundation::IInspectable const& sender,
                            Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);

    private:
        bool m_loading{false};
    };
}

namespace winrt::Environ::factory_implementation
{
    struct SettingsPage : SettingsPageT<SettingsPage, implementation::SettingsPage>
    {
    };
}
