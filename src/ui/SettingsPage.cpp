#include "SettingsPage.h"

using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

SettingsPage::SettingsPage(Environ::core::AppSettings& settings, ThemeChangedCallback on_theme_changed)
    : m_settings{settings}
    , m_onThemeChanged{std::move(on_theme_changed)}
{
    m_root = StackPanel{};
    m_root.Padding(ThicknessHelper::FromLengths(24, 24, 24, 24));
    m_root.Spacing(24);
    Build();
}

winrt::Microsoft::UI::Xaml::UIElement SettingsPage::Root() const {
    return m_root;
}

void SettingsPage::Build() {
    // Title
    auto title{TextBlock{}};
    title.Text(L"Settings");
    title.FontSize(20);
    title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    title;
    m_root.Children().Append(title);

    // Appearance section
    auto section_header{TextBlock{}};
    section_header.Text(L"Appearance");
    section_header.FontSize(16);
    section_header.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    section_header;
    m_root.Children().Append(section_header);

    // Theme selector
    auto theme_panel{StackPanel{}};
    theme_panel.Spacing(8);

    auto theme_label{TextBlock{}};
    theme_label.Text(L"Theme");
    theme_label.FontSize(14);
    theme_label.Opacity(0.6);
    theme_panel.Children().Append(theme_label);

    auto theme_combo{ComboBox{}};
    theme_combo.Items().Append(winrt::box_value(L"System"));
    theme_combo.Items().Append(winrt::box_value(L"Light"));
    theme_combo.Items().Append(winrt::box_value(L"Dark"));
    theme_combo.MinWidth(200);

    // Select current value
    auto current_theme{winrt::to_hstring(m_settings.appearance.theme.get())};
    for (int32_t i{0}; i < static_cast<int32_t>(theme_combo.Items().Size()); ++i) {
        auto item{winrt::unbox_value<winrt::hstring>(theme_combo.Items().GetAt(i))};
        if (item == current_theme) {
            theme_combo.SelectedIndex(i);
            break;
        }
    }

    theme_combo.SelectionChanged([this](winrt::Windows::Foundation::IInspectable const& sender,
                                        [[maybe_unused]] SelectionChangedEventArgs const& args) {
        auto combo{sender.as<ComboBox>()};
        if (combo.SelectedItem() == nullptr) return;

        auto selected{winrt::unbox_value<winrt::hstring>(combo.SelectedItem())};
        std::string theme_str{winrt::to_string(selected)};
        m_settings.appearance.theme.set(theme_str);
        m_settings.save();

        if (m_onThemeChanged) {
            m_onThemeChanged();
        }
    });

    theme_panel.Children().Append(theme_combo);
    m_root.Children().Append(theme_panel);
}
