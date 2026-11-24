#include "precomp.h"
#include "ThemePickerPanel.h"

#include <winrt/Microsoft.UI.Text.h>

namespace Environ::winui
{

using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Media;

namespace
{

SolidColorBrush brush_from(const Environ::core::Color4& c)
{
    return SolidColorBrush{winrt::Windows::UI::Color{c.a8(), c.r8(), c.g8(), c.b8()}};
}

UIElement make_theme_item(const Environ::core::Base16ThemeInfo& info)
{
    StackPanel panel{};
    panel.Padding(Thickness{2.0, 4.0, 2.0, 4.0});
    panel.Spacing(3.0);

    TextBlock name{};
    name.Text(pnq::unicode::to_utf16(info.name));
    name.FontSize(12.5);
    name.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());

    StackPanel swatch{};
    swatch.Orientation(Orientation::Horizontal);
    swatch.Spacing(2.0);
    for (int i{0}; i < 16; ++i)
    {
        Border chip{};
        chip.Width(12.0);
        chip.Height(8.0);
        chip.CornerRadius(CornerRadius{2.0, 2.0, 2.0, 2.0});
        chip.Background(brush_from(info.base16[static_cast<size_t>(i)]));
        swatch.Children().Append(chip);
    }

    panel.Children().Append(name);
    panel.Children().Append(swatch);
    return panel;
}

} // namespace

ThemeDialogContent create_theme_dialog_content(
    const Environ::core::Base16ThemeSet& themes,
    const std::string& initial_theme,
    std::function<void(const std::string& theme_name)> on_preview)
{
    struct State
    {
        std::shared_ptr<std::string> selected_theme;
        std::vector<std::string> dark_names;
        std::vector<std::string> light_names;
        bool syncing{false};
        ListView dark_list{nullptr};
        ListView light_list{nullptr};
        std::function<void(const std::string&)> on_preview;
    };

    auto st{std::make_shared<State>()};
    st->selected_theme = std::make_shared<std::string>(initial_theme.empty() ? "System" : initial_theme);
    st->on_preview = std::move(on_preview);

    Grid root{};
    root.Width(540.0);
    root.Height(420.0);
    RowDefinition r0{}, r1{};
    r0.Height(GridLengthHelper::Auto());
    r1.Height(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
    root.RowDefinitions().Append(r0);
    root.RowDefinitions().Append(r1);
    root.RowSpacing(8.0);

    // Quick built-in buttons (System / Light / Dark)
    StackPanel quick{};
    quick.Orientation(Orientation::Horizontal);
    quick.Spacing(6.0);
    Grid::SetRow(quick, 0);

    auto add_builtin = [&](const wchar_t* label, const char* value) {
        Button b{};
        b.Content(winrt::box_value(label));
        b.Click([st, val = std::string{value}](auto&&, RoutedEventArgs const&)
        {
            st->syncing = true;
            st->dark_list.SelectedIndex(-1);
            st->light_list.SelectedIndex(-1);
            st->syncing = false;
            *st->selected_theme = val;
            if (st->on_preview) st->on_preview(val);
        });
        quick.Children().Append(b);
    };
    add_builtin(L"Use Windows setting", "System");
    add_builtin(L"Light", "Light");
    add_builtin(L"Dark", "Dark");
    root.Children().Append(quick);

    // 2-column Dark / Light Base16 lists
    Grid body{};
    body.ColumnSpacing(12.0);
    ColumnDefinition c0{}, c1{};
    c0.Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
    c1.Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
    body.ColumnDefinitions().Append(c0);
    body.ColumnDefinitions().Append(c1);
    RowDefinition br0{}, br1{};
    br0.Height(GridLengthHelper::Auto());
    br1.Height(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
    body.RowDefinitions().Append(br0);
    body.RowDefinitions().Append(br1);
    Grid::SetRow(body, 1);

    TextBlock dark_hdr{};
    dark_hdr.Text(L"DARK THEMES");
    dark_hdr.FontSize(11.5);
    dark_hdr.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
    dark_hdr.Opacity(0.75);
    dark_hdr.Margin(Thickness{4.0, 0.0, 0.0, 4.0});
    Grid::SetRow(dark_hdr, 0);
    Grid::SetColumn(dark_hdr, 0);

    TextBlock light_hdr{};
    light_hdr.Text(L"LIGHT THEMES");
    light_hdr.FontSize(11.5);
    light_hdr.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
    light_hdr.Opacity(0.75);
    light_hdr.Margin(Thickness{4.0, 0.0, 0.0, 4.0});
    Grid::SetRow(light_hdr, 0);
    Grid::SetColumn(light_hdr, 1);

    ListView dark_lv{};
    dark_lv.SelectionMode(ListViewSelectionMode::Single);
    Grid::SetRow(dark_lv, 1);
    Grid::SetColumn(dark_lv, 0);

    ListView light_lv{};
    light_lv.SelectionMode(ListViewSelectionMode::Single);
    Grid::SetRow(light_lv, 1);
    Grid::SetColumn(light_lv, 1);

    st->dark_list = dark_lv;
    st->light_list = light_lv;

    int initial_dark{-1};
    int initial_light{-1};
    for (const auto& info : themes.themes())
    {
        if (info.dark)
        {
            if (info.name == *st->selected_theme) initial_dark = static_cast<int>(st->dark_names.size());
            st->dark_names.push_back(info.name);
            dark_lv.Items().Append(make_theme_item(info));
        }
        else
        {
            if (info.name == *st->selected_theme) initial_light = static_cast<int>(st->light_names.size());
            st->light_names.push_back(info.name);
            light_lv.Items().Append(make_theme_item(info));
        }
    }

    body.Children().Append(dark_hdr);
    body.Children().Append(light_hdr);
    body.Children().Append(dark_lv);
    body.Children().Append(light_lv);
    root.Children().Append(body);

    dark_lv.SelectionChanged([st](auto&&, SelectionChangedEventArgs const&)
    {
        if (st->syncing) return;
        const int idx{st->dark_list.SelectedIndex()};
        if (idx < 0 || idx >= static_cast<int>(st->dark_names.size())) return;
        st->syncing = true;
        st->light_list.SelectedIndex(-1);
        st->syncing = false;
        *st->selected_theme = st->dark_names[static_cast<size_t>(idx)];
        if (st->on_preview) st->on_preview(*st->selected_theme);
    });

    light_lv.SelectionChanged([st](auto&&, SelectionChangedEventArgs const&)
    {
        if (st->syncing) return;
        const int idx{st->light_list.SelectedIndex()};
        if (idx < 0 || idx >= static_cast<int>(st->light_names.size())) return;
        st->syncing = true;
        st->dark_list.SelectedIndex(-1);
        st->syncing = false;
        *st->selected_theme = st->light_names[static_cast<size_t>(idx)];
        if (st->on_preview) st->on_preview(*st->selected_theme);
    });

    st->syncing = true;
    if (initial_dark >= 0) dark_lv.SelectedIndex(initial_dark);
    if (initial_light >= 0) light_lv.SelectedIndex(initial_light);
    st->syncing = false;

    return ThemeDialogContent{root, st->selected_theme};
}

} // namespace Environ::winui
