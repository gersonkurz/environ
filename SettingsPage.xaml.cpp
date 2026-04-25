#include "pch.h"
#include "SettingsPage.xaml.h"
#if __has_include("SettingsPage.g.cpp")
#include "SettingsPage.g.cpp"
#endif

#include "AppSettings.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::Environ::implementation
{
    void SettingsPage::OnPageLoaded(
        [[maybe_unused]] IInspectable const& sender,
        [[maybe_unused]] RoutedEventArgs const& args)
    {
        m_loading = true;

        auto theme{::Environ::core::app_settings().appearance.theme.get()};
        int index{0};
        if (theme == "Light") index = 1;
        else if (theme == "Dark") index = 2;
        ThemeCombo().SelectedIndex(index);

        m_loading = false;
    }

    void SettingsPage::OnThemeChanged(
        [[maybe_unused]] IInspectable const& sender,
        [[maybe_unused]] SelectionChangedEventArgs const& args)
    {
        if (m_loading) return;

        auto item{ThemeCombo().SelectedItem().try_as<ComboBoxItem>()};
        if (!item) return;

        auto tag{unbox_value<hstring>(item.Tag())};

        // Apply theme to the window root
        auto theme{ElementTheme::Default};
        if (tag == L"Light") theme = ElementTheme::Light;
        else if (tag == L"Dark") theme = ElementTheme::Dark;

        if (auto root{XamlRoot().Content().try_as<FrameworkElement>()}) {
            root.RequestedTheme(theme);
        }

        // Persist
        std::string theme_str{"System"};
        if (tag == L"Light") theme_str = "Light";
        else if (tag == L"Dark") theme_str = "Dark";

        ::Environ::core::app_settings().appearance.theme.set(theme_str);
        ::Environ::core::app_settings().save();
    }
}
