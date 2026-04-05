#include "EnvironmentPage.h"
#include "../core/EnvWriter.h"

#include <algorithm>

using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Media;

namespace {

constexpr double kNameColumnWidth{200.0};
constexpr double kScopeColumnWidth{80.0};
constexpr double kColumnSpacing{8.0};
constexpr double kRowMinHeight{32.0};

struct VariableRef {
    Environ::core::Scope scope;
    std::size_t index;
};

Brush ThemeBrush(winrt::hstring const& key) {
    return Application::Current().Resources()
        .Lookup(winrt::box_value(key))
        .as<Brush>();
}

void ApplyColumnDefinitions(Grid const& grid) {
    auto name_col{ColumnDefinition{}};
    name_col.Width(GridLengthHelper::FromPixels(kNameColumnWidth));
    auto scope_col{ColumnDefinition{}};
    scope_col.Width(GridLengthHelper::FromPixels(kScopeColumnWidth));
    auto value_col{ColumnDefinition{}};
    value_col.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    grid.ColumnDefinitions().Append(name_col);
    grid.ColumnDefinitions().Append(scope_col);
    grid.ColumnDefinitions().Append(value_col);
    grid.ColumnSpacing(kColumnSpacing);
}

std::wstring JoinSegments(std::vector<std::wstring> const& segments) {
    std::wstring result;
    for (std::size_t i{0}; i < segments.size(); ++i) {
        if (i > 0) {
            result.append(L";");
        }
        result.append(segments[i]);
    }
    return result;
}

std::vector<VariableRef> BuildVariableRefs(
    std::vector<Environ::core::EnvVariable> const& user_variables,
    std::vector<Environ::core::EnvVariable> const& machine_variables) {
    std::vector<VariableRef> refs;
    refs.reserve(user_variables.size() + machine_variables.size());

    for (std::size_t i{0}; i < user_variables.size(); ++i) {
        refs.push_back(VariableRef{Environ::core::Scope::User, i});
    }
    for (std::size_t i{0}; i < machine_variables.size(); ++i) {
        refs.push_back(VariableRef{Environ::core::Scope::Machine, i});
    }

    std::ranges::sort(refs, [&user_variables, &machine_variables](const VariableRef& a, const VariableRef& b) {
        const auto& va{a.scope == Environ::core::Scope::User ? user_variables[a.index] : machine_variables[a.index]};
        const auto& vb{b.scope == Environ::core::Scope::User ? user_variables[b.index] : machine_variables[b.index]};
        const auto cmp{_wcsicmp(va.name.c_str(), vb.name.c_str())};
        if (cmp != 0) return cmp < 0;
        return a.scope == Environ::core::Scope::User && b.scope == Environ::core::Scope::Machine;
    });

    return refs;
}

Border MakeScopeBadge(Environ::core::Scope scope) {
    auto badge{Border{}};
    badge.Padding(ThicknessHelper::FromLengths(6, 1, 6, 1));
    badge.CornerRadius(CornerRadiusHelper::FromUniformRadius(4));

    auto text{TextBlock{}};
    text.Text(scope == Environ::core::Scope::User ? L"User" : L"Machine");
    text.FontSize(11);

    if (scope == Environ::core::Scope::User) {
        auto accent{winrt::unbox_value<winrt::Windows::UI::Color>(
            Application::Current().Resources().Lookup(winrt::box_value(L"SystemAccentColor")))};
        accent.A = 30;
        badge.Background(SolidColorBrush{accent});
        text.Foreground(ThemeBrush(L"AccentTextFillColorPrimaryBrush"));
    } else {
        badge.Background(ThemeBrush(L"ControlFillColorSecondaryBrush"));
        text.Foreground(ThemeBrush(L"TextFillColorSecondaryBrush"));
    }

    badge.Child(text);
    return badge;
}

TextBox MakeCell(std::wstring const& text, bool read_only = false) {
    auto cell{TextBox{}};
    cell.Text(text);
    cell.BorderThickness(ThicknessHelper::FromUniformLength(0));
    cell.Background(SolidColorBrush{winrt::Windows::UI::Colors::Transparent()});
    cell.Padding(ThicknessHelper::FromLengths(4, 2, 4, 2));
    cell.TextWrapping(TextWrapping::NoWrap);
    cell.AcceptsReturn(false);
    cell.VerticalAlignment(VerticalAlignment::Center);
    cell.IsReadOnly(read_only);

    if (read_only) {
        cell.Opacity(0.5);
    }

    cell.LostFocus([](winrt::Windows::Foundation::IInspectable const& sender,
                      [[maybe_unused]] RoutedEventArgs const& e) {
        auto box{sender.as<TextBox>()};
        box.BorderThickness(ThicknessHelper::FromUniformLength(0));
        box.Background(SolidColorBrush{winrt::Windows::UI::Colors::Transparent()});
    });

    return cell;
}

Border MakeValueWrapper() {
    auto wrapper{Border{}};
    wrapper.BorderThickness(ThicknessHelper::FromLengths(0, 0, 0, 0));
    return wrapper;
}

void MarkDirty(Border const& wrapper, bool dirty) {
    if (dirty) {
        wrapper.BorderThickness(ThicknessHelper::FromLengths(2, 0, 0, 0));
        wrapper.BorderBrush(ThemeBrush(L"AccentFillColorDefaultBrush"));
    } else {
        wrapper.BorderThickness(ThicknessHelper::FromLengths(0, 0, 0, 0));
        wrapper.BorderBrush(nullptr);
    }
}

void ApplySegmentStyle(TextBox const& cell, bool valid, std::wstring const& tooltip) {
    if (!valid) {
        cell.Foreground(ThemeBrush(L"AccentTextFillColorPrimaryBrush"));
    }
    if (!tooltip.empty()) {
        ToolTipService::SetToolTip(cell, winrt::box_value(winrt::hstring{tooltip}));
    }
}

} // namespace

EnvironmentPage::EnvironmentPage() {
    m_root = Grid{};
    m_root.Padding(ThicknessHelper::FromLengths(24, 24, 24, 24));
    m_elevated = Environ::core::is_elevated();
    Refresh();
}

winrt::Microsoft::UI::Xaml::UIElement EnvironmentPage::Root() const {
    return m_root;
}

void EnvironmentPage::Refresh() {
    m_userVariables = Environ::core::read_variables(Environ::core::Scope::User);
    m_machineVariables = Environ::core::read_variables(Environ::core::Scope::Machine);
    Environ::core::expand_and_validate(m_userVariables);
    Environ::core::expand_and_validate(m_machineVariables);

    m_originalUserVariables = m_userVariables;
    m_originalMachineVariables = m_machineVariables;
    m_selectedRowBorder = nullptr;
    EnsureSelection();

    m_root.Children().Clear();
    m_root.RowDefinitions().Clear();

    auto content_row{RowDefinition{}};
    content_row.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    m_root.RowDefinitions().Append(content_row);

    auto list_panel{Grid{}};
    BuildList(list_panel);
    Grid::SetRow(list_panel, 0);
    m_root.Children().Append(list_panel);
}

void EnvironmentPage::EnsureSelection() {
    auto selection_is_valid = [this]() {
        if (!m_selectedVariable.has_value()) {
            return false;
        }
        const auto& variables{m_selectedVariable->scope == Environ::core::Scope::User
            ? m_userVariables : m_machineVariables};
        return std::ranges::any_of(variables, [this](const Environ::core::EnvVariable& v) {
            return v.name == m_selectedVariable->name;
        });
    };

    if (selection_is_valid()) return;

    if (!m_userVariables.empty()) {
        m_selectedVariable = SelectedVariable{Environ::core::Scope::User, m_userVariables.front().name};
    } else if (!m_machineVariables.empty()) {
        m_selectedVariable = SelectedVariable{Environ::core::Scope::Machine, m_machineVariables.front().name};
    } else {
        m_selectedVariable.reset();
    }
}

void EnvironmentPage::SelectRow(Border const& row_border,
                                Environ::core::Scope scope, std::size_t variable_index) {
    if (m_selectedRowBorder) {
        m_selectedRowBorder.BorderThickness(ThicknessHelper::FromLengths(0, 0, 0, 0));
        m_selectedRowBorder.BorderBrush(nullptr);
    }

    m_selectedRowBorder = row_border;
    row_border.BorderThickness(ThicknessHelper::FromLengths(2, 0, 0, 0));
    row_border.BorderBrush(ThemeBrush(L"AccentFillColorDefaultBrush"));

    const auto& variables{scope == Environ::core::Scope::User
        ? m_userVariables : m_machineVariables};
    if (variable_index < variables.size()) {
        m_selectedVariable = SelectedVariable{scope, variables[variable_index].name};
    }
}

void EnvironmentPage::BuildList(Grid const& parent) {
    parent.RowDefinitions().Clear();
    parent.Children().Clear();

    auto title_row{RowDefinition{}};
    title_row.Height(GridLengthHelper::Auto());
    auto header_row{RowDefinition{}};
    header_row.Height(GridLengthHelper::Auto());
    auto content_row{RowDefinition{}};
    content_row.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    parent.RowDefinitions().Append(title_row);
    parent.RowDefinitions().Append(header_row);
    parent.RowDefinitions().Append(content_row);

    auto refs{BuildVariableRefs(m_userVariables, m_machineVariables)};

    // --- Title row (title + toolbar) ---
    auto title_grid{Grid{}};
    title_grid.Margin(ThicknessHelper::FromLengths(0, 0, 0, 12));
    auto title_left_col{ColumnDefinition{}};
    title_left_col.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    auto title_right_col{ColumnDefinition{}};
    title_right_col.Width(GridLengthHelper::Auto());
    title_grid.ColumnDefinitions().Append(title_left_col);
    title_grid.ColumnDefinitions().Append(title_right_col);

    auto title_panel{StackPanel{}};
    title_panel.Orientation(Orientation::Horizontal);
    title_panel.Spacing(8);

    auto title_text{TextBlock{}};
    title_text.Text(L"Environment Variables");
    title_text.FontSize(20);
    title_text.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    title_text.Foreground(ThemeBrush(L"TextFillColorPrimaryBrush"));

    auto count_text{TextBlock{}};
    count_text.Text(std::to_wstring(refs.size()));
    count_text.FontSize(12);
    count_text.VerticalAlignment(VerticalAlignment::Center);
    count_text.Foreground(ThemeBrush(L"TextFillColorSecondaryBrush"));

    title_panel.Children().Append(title_text);
    title_panel.Children().Append(count_text);
    Grid::SetColumn(title_panel, 0);

    // Toolbar buttons
    auto toolbar{StackPanel{}};
    toolbar.Orientation(Orientation::Horizontal);
    toolbar.Spacing(4);

    auto save_icon{FontIcon{}};
    save_icon.Glyph(L"\uE74E");
    save_icon.FontSize(14);

    auto save_btn{Button{}};
    save_btn.Content(winrt::box_value(L"Save"));
    save_btn.Click([this]([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
                          [[maybe_unused]] RoutedEventArgs const& args) {
        OnSave();
    });

    toolbar.Children().Append(save_btn);
    Grid::SetColumn(toolbar, 1);

    title_grid.Children().Append(title_panel);
    title_grid.Children().Append(toolbar);
    Grid::SetRow(title_grid, 0);
    parent.Children().Append(title_grid);

    // --- Column header row ---
    auto header_grid{Grid{}};
    ApplyColumnDefinitions(header_grid);
    header_grid.Padding(ThicknessHelper::FromLengths(8, 4, 8, 4));

    auto name_header{TextBlock{}};
    name_header.Text(L"Name");
    name_header.FontSize(12);
    name_header.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    name_header.Foreground(ThemeBrush(L"TextFillColorSecondaryBrush"));
    Grid::SetColumn(name_header, 0);

    auto scope_header{TextBlock{}};
    scope_header.Text(L"Scope");
    scope_header.FontSize(12);
    scope_header.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    scope_header.Foreground(ThemeBrush(L"TextFillColorSecondaryBrush"));
    Grid::SetColumn(scope_header, 1);

    auto value_header{TextBlock{}};
    value_header.Text(L"Value");
    value_header.FontSize(12);
    value_header.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    value_header.Foreground(ThemeBrush(L"TextFillColorSecondaryBrush"));
    Grid::SetColumn(value_header, 2);

    header_grid.Children().Append(name_header);
    header_grid.Children().Append(scope_header);
    header_grid.Children().Append(value_header);

    auto header_wrapper{StackPanel{}};
    header_wrapper.Children().Append(header_grid);

    auto separator{Border{}};
    separator.Height(1);
    separator.Background(ThemeBrush(L"ControlStrokeColorDefaultBrush"));
    separator.Margin(ThicknessHelper::FromLengths(0, 4, 0, 0));
    header_wrapper.Children().Append(separator);

    Grid::SetRow(header_wrapper, 1);
    parent.Children().Append(header_wrapper);

    // --- Scrollable content ---
    m_scrollViewer = ScrollViewer{};
    m_scrollViewer.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    m_scrollViewer.HorizontalScrollBarVisibility(ScrollBarVisibility::Auto);
    m_scrollViewer.Margin(ThicknessHelper::FromLengths(0, 4, 0, 0));
    m_scrollViewer.ZoomMode(ZoomMode::Enabled);
    m_scrollViewer.MinZoomFactor(0.5f);
    m_scrollViewer.MaxZoomFactor(3.0f);

    m_scrollViewer.PointerWheelChanged([this](winrt::Windows::Foundation::IInspectable const&,
                                              winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
        const bool ctrl{(static_cast<uint32_t>(args.KeyModifiers()) &
                        static_cast<uint32_t>(winrt::Windows::System::VirtualKeyModifiers::Control)) != 0};
        if (!ctrl) return;

        const auto point{args.GetCurrentPoint(m_scrollViewer)};
        const auto delta{point.Properties().MouseWheelDelta()};
        const auto step{static_cast<float>(delta) / 120.0f * 0.1f};
        const auto new_zoom{std::clamp(m_scrollViewer.ZoomFactor() + step, 0.5f, 3.0f)};
        m_scrollViewer.ChangeView(nullptr, nullptr, new_zoom);
        args.Handled(true);
    });

    auto rows_panel{StackPanel{}};
    rows_panel.Spacing(0);

    std::size_t visual_row{0};

    for (const auto& ref : refs) {
        auto& scoped_vars{ref.scope == Environ::core::Scope::User
            ? m_userVariables : m_machineVariables};
        auto& variable{scoped_vars[ref.index]};
        const bool is_selected{m_selectedVariable.has_value()
            && m_selectedVariable->scope == ref.scope
            && m_selectedVariable->name == variable.name};
        const bool is_protected{ref.scope == Environ::core::Scope::Machine && !m_elevated};

        // --- Main row ---
        auto row_border{Border{}};
        row_border.Padding(ThicknessHelper::FromLengths(8, 0, 8, 0));
        row_border.CornerRadius(CornerRadiusHelper::FromUniformRadius(2));
        row_border.MinHeight(kRowMinHeight);

        if (visual_row % 2 == 1) {
            row_border.Background(ThemeBrush(L"SubtleFillColorSecondaryBrush"));
        }

        if (is_selected) {
            row_border.BorderThickness(ThicknessHelper::FromLengths(2, 0, 0, 0));
            row_border.BorderBrush(ThemeBrush(L"AccentFillColorDefaultBrush"));
            m_selectedRowBorder = row_border;
        }

        row_border.PointerEntered([is_selected](winrt::Windows::Foundation::IInspectable const& sender,
                                                [[maybe_unused]] winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
            if (!is_selected) {
                sender.as<Border>().Background(ThemeBrush(L"SubtleFillColorTertiaryBrush"));
            }
        });
        row_border.PointerExited([is_selected, alt_row = (visual_row % 2 == 1)](
                                     winrt::Windows::Foundation::IInspectable const& sender,
                                     [[maybe_unused]] winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
            if (!is_selected) {
                if (alt_row) {
                    sender.as<Border>().Background(ThemeBrush(L"SubtleFillColorSecondaryBrush"));
                } else {
                    sender.as<Border>().Background(SolidColorBrush{winrt::Windows::UI::Colors::Transparent()});
                }
            }
        });

        auto row_grid{Grid{}};
        ApplyColumnDefinitions(row_grid);

        // Name cell
        auto name_wrapper{MakeValueWrapper()};
        auto name_cell{MakeCell(variable.name, is_protected)};
        name_cell.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        WireScrollPassthrough(name_cell);
        name_wrapper.Child(name_cell);
        Grid::SetColumn(name_wrapper, 0);

        name_cell.GotFocus([this, row_border, scope = ref.scope, idx = ref.index](
                               winrt::Windows::Foundation::IInspectable const& sender, RoutedEventArgs const&) {
            auto box{sender.as<TextBox>()};
            box.BorderThickness(ThicknessHelper::FromUniformLength(1));
            box.BorderBrush(ThemeBrush(L"ControlStrokeColorDefaultBrush"));
            box.Background(ThemeBrush(L"ControlFillColorDefaultBrush"));
            SelectRow(row_border, scope, idx);
        });

        name_cell.TextChanged([this, scope = ref.scope, idx = ref.index, name_wrapper](
                                  winrt::Windows::Foundation::IInspectable const& sender,
                                  [[maybe_unused]] TextChangedEventArgs const& e) {
            auto& vars{scope == Environ::core::Scope::User ? m_userVariables : m_machineVariables};
            const auto& originals{scope == Environ::core::Scope::User
                ? m_originalUserVariables : m_originalMachineVariables};
            if (idx < vars.size()) {
                vars[idx].name = sender.as<TextBox>().Text().c_str();
                bool dirty{idx >= originals.size() || vars[idx].name != originals[idx].name};
                MarkDirty(name_wrapper, dirty);
            }
        });

        // Scope badge
        auto scope_badge{MakeScopeBadge(ref.scope)};
        scope_badge.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(scope_badge, 1);

        row_grid.Children().Append(name_wrapper);
        row_grid.Children().Append(scope_badge);

        // Value cell(s)
        if (variable.kind == Environ::core::EnvVariableKind::PathList && !variable.segments.empty()) {
            // First segment
            auto first_wrapper{MakeValueWrapper()};
            auto first_cell{MakeCell(variable.segments[0], is_protected)};
            WireScrollPassthrough(first_cell);
            first_wrapper.Child(first_cell);
            Grid::SetColumn(first_wrapper, 2);

            // Tooltip and invalid-path styling for segment 0
            if (!variable.expanded_segments.empty()) {
                const auto& expanded{variable.expanded_segments[0]};
                auto tooltip{variable.segments[0] != expanded ? expanded : std::wstring{}};
                bool valid{!variable.segment_valid.empty() && variable.segment_valid[0]};
                ApplySegmentStyle(first_cell, valid, tooltip);
            }

            first_cell.GotFocus([this, row_border, scope = ref.scope, idx = ref.index](
                                    winrt::Windows::Foundation::IInspectable const& sender, RoutedEventArgs const&) {
                auto box{sender.as<TextBox>()};
                box.BorderThickness(ThicknessHelper::FromUniformLength(1));
                box.BorderBrush(ThemeBrush(L"ControlStrokeColorDefaultBrush"));
                box.Background(ThemeBrush(L"ControlFillColorDefaultBrush"));
                SelectRow(row_border, scope, idx);
            });

            first_cell.TextChanged([this, scope = ref.scope, idx = ref.index, first_wrapper](
                                       winrt::Windows::Foundation::IInspectable const& sender,
                                       [[maybe_unused]] TextChangedEventArgs const& e) {
                auto& vars{scope == Environ::core::Scope::User ? m_userVariables : m_machineVariables};
                const auto& originals{scope == Environ::core::Scope::User
                    ? m_originalUserVariables : m_originalMachineVariables};
                if (idx < vars.size() && !vars[idx].segments.empty()) {
                    vars[idx].segments[0] = sender.as<TextBox>().Text().c_str();
                    vars[idx].value = JoinSegments(vars[idx].segments);
                    bool dirty{idx >= originals.size()
                        || originals[idx].segments.empty()
                        || vars[idx].segments[0] != originals[idx].segments[0]};
                    MarkDirty(first_wrapper, dirty);
                }
            });

            row_grid.Children().Append(first_wrapper);
        } else {
            // Scalar value
            auto value_wrapper{MakeValueWrapper()};
            auto value_cell{MakeCell(variable.value, is_protected)};
            WireScrollPassthrough(value_cell);
            value_wrapper.Child(value_cell);
            Grid::SetColumn(value_wrapper, 2);

            // Tooltip for expanded value
            if (variable.is_expandable && variable.expanded_value != variable.value) {
                ToolTipService::SetToolTip(value_cell, winrt::box_value(winrt::hstring{variable.expanded_value}));
            }

            value_cell.GotFocus([this, row_border, scope = ref.scope, idx = ref.index](
                                    winrt::Windows::Foundation::IInspectable const& sender, RoutedEventArgs const&) {
                auto box{sender.as<TextBox>()};
                box.BorderThickness(ThicknessHelper::FromUniformLength(1));
                box.BorderBrush(ThemeBrush(L"ControlStrokeColorDefaultBrush"));
                box.Background(ThemeBrush(L"ControlFillColorDefaultBrush"));
                SelectRow(row_border, scope, idx);
            });

            value_cell.TextChanged([this, scope = ref.scope, idx = ref.index, value_wrapper](
                                       winrt::Windows::Foundation::IInspectable const& sender,
                                       [[maybe_unused]] TextChangedEventArgs const& e) {
                auto& vars{scope == Environ::core::Scope::User ? m_userVariables : m_machineVariables};
                const auto& originals{scope == Environ::core::Scope::User
                    ? m_originalUserVariables : m_originalMachineVariables};
                if (idx < vars.size()) {
                    vars[idx].value = sender.as<TextBox>().Text().c_str();
                    bool dirty{idx >= originals.size() || vars[idx].value != originals[idx].value};
                    MarkDirty(value_wrapper, dirty);
                }
            });

            row_grid.Children().Append(value_wrapper);
        }

        // Row tap selects
        row_border.Tapped([this, row_border, scope = ref.scope, idx = ref.index](
                              [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
                              [[maybe_unused]] winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& args) {
            SelectRow(row_border, scope, idx);
        });

        row_border.Child(row_grid);
        rows_panel.Children().Append(row_border);
        ++visual_row;

        // --- Continuation rows for path-list segments 1..N ---
        if (variable.kind == Environ::core::EnvVariableKind::PathList) {
            for (std::size_t seg_i{1}; seg_i < variable.segments.size(); ++seg_i) {
                auto cont_border{Border{}};
                cont_border.Padding(ThicknessHelper::FromLengths(8, 0, 8, 0));
                cont_border.MinHeight(kRowMinHeight);

                if (visual_row % 2 == 1) {
                    cont_border.Background(ThemeBrush(L"SubtleFillColorSecondaryBrush"));
                }

                auto cont_grid{Grid{}};
                ApplyColumnDefinitions(cont_grid);

                auto indent_line{Border{}};
                indent_line.Width(2);
                indent_line.HorizontalAlignment(HorizontalAlignment::Left);
                indent_line.Margin(ThicknessHelper::FromLengths(6, 0, 0, 0));
                indent_line.Background(ThemeBrush(L"ControlStrokeColorDefaultBrush"));
                indent_line.Opacity(0.4);
                Grid::SetColumn(indent_line, 0);

                auto seg_wrapper{MakeValueWrapper()};
                auto seg_cell{MakeCell(variable.segments[seg_i], is_protected)};
                WireScrollPassthrough(seg_cell);
                seg_wrapper.Child(seg_cell);
                Grid::SetColumn(seg_wrapper, 2);

                // Tooltip and invalid-path styling
                if (seg_i < variable.expanded_segments.size()) {
                    const auto& expanded{variable.expanded_segments[seg_i]};
                    auto tooltip{variable.segments[seg_i] != expanded ? expanded : std::wstring{}};
                    bool valid{seg_i < variable.segment_valid.size() && variable.segment_valid[seg_i]};
                    ApplySegmentStyle(seg_cell, valid, tooltip);
                }

                seg_cell.GotFocus([this, row_border, scope = ref.scope, idx = ref.index](
                                      winrt::Windows::Foundation::IInspectable const& sender, RoutedEventArgs const&) {
                    auto box{sender.as<TextBox>()};
                    box.BorderThickness(ThicknessHelper::FromUniformLength(1));
                    box.BorderBrush(ThemeBrush(L"ControlStrokeColorDefaultBrush"));
                    box.Background(ThemeBrush(L"ControlFillColorDefaultBrush"));
                    SelectRow(row_border, scope, idx);
                });

                seg_cell.TextChanged([this, scope = ref.scope, idx = ref.index, seg_i, seg_wrapper](
                                         winrt::Windows::Foundation::IInspectable const& sender,
                                         [[maybe_unused]] TextChangedEventArgs const& e) {
                    auto& vars{scope == Environ::core::Scope::User ? m_userVariables : m_machineVariables};
                    const auto& originals{scope == Environ::core::Scope::User
                        ? m_originalUserVariables : m_originalMachineVariables};
                    if (idx < vars.size() && seg_i < vars[idx].segments.size()) {
                        vars[idx].segments[seg_i] = sender.as<TextBox>().Text().c_str();
                        vars[idx].value = JoinSegments(vars[idx].segments);
                        bool dirty{idx >= originals.size()
                            || seg_i >= originals[idx].segments.size()
                            || vars[idx].segments[seg_i] != originals[idx].segments[seg_i]};
                        MarkDirty(seg_wrapper, dirty);
                    }
                });

                cont_grid.Children().Append(indent_line);
                cont_grid.Children().Append(seg_wrapper);

                cont_border.Tapped([this, row_border, scope = ref.scope, idx = ref.index](
                                       [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
                                       [[maybe_unused]] winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& args) {
                    SelectRow(row_border, scope, idx);
                });

                cont_border.Child(cont_grid);
                rows_panel.Children().Append(cont_border);
                ++visual_row;
            }
        }
    }

    m_scrollViewer.Content(rows_panel);
    Grid::SetRow(m_scrollViewer, 2);
    parent.Children().Append(m_scrollViewer);
}

void EnvironmentPage::OnSave() {
    // 1. Compute diffs
    auto user_changes{Environ::core::compute_diff(m_originalUserVariables, m_userVariables)};
    std::vector<Environ::core::EnvChange> machine_changes;
    if (m_elevated) {
        machine_changes = Environ::core::compute_diff(m_originalMachineVariables, m_machineVariables);
    }

    if (user_changes.empty() && machine_changes.empty()) {
        auto dlg{ContentDialog{}};
        dlg.Title(winrt::box_value(L"No Changes"));
        dlg.Content(winrt::box_value(L"There are no changes to save."));
        dlg.CloseButtonText(L"OK");
        dlg.XamlRoot(m_root.XamlRoot());
        dlg.ShowAsync();
        return;
    }

    // 2. Dry-run review dialog
    auto review_panel{StackPanel{}};
    review_panel.Spacing(4);
    review_panel.MaxWidth(500);

    if (!user_changes.empty()) {
        auto scope_label{TextBlock{}};
        scope_label.Text(L"User scope:");
        scope_label.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        scope_label.Margin(ThicknessHelper::FromLengths(0, 4, 0, 2));
        review_panel.Children().Append(scope_label);

        for (const auto& c : user_changes) {
            auto line{TextBlock{}};
            line.Text(c.describe());
            line.TextWrapping(TextWrapping::Wrap);
            review_panel.Children().Append(line);
        }
    }

    if (!machine_changes.empty()) {
        auto scope_label{TextBlock{}};
        scope_label.Text(L"Machine scope:");
        scope_label.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        scope_label.Margin(ThicknessHelper::FromLengths(0, 8, 0, 2));
        review_panel.Children().Append(scope_label);

        for (const auto& c : machine_changes) {
            auto line{TextBlock{}};
            line.Text(c.describe());
            line.TextWrapping(TextWrapping::Wrap);
            review_panel.Children().Append(line);
        }
    }

    auto scroll{ScrollViewer{}};
    scroll.MaxHeight(300);
    scroll.Content(review_panel);

    auto review_dlg{ContentDialog{}};
    review_dlg.Title(winrt::box_value(L"Review Changes"));
    review_dlg.Content(scroll);
    review_dlg.PrimaryButtonText(L"Apply");
    review_dlg.CloseButtonText(L"Cancel");
    review_dlg.XamlRoot(m_root.XamlRoot());

    review_dlg.PrimaryButtonClick([this, user_changes, machine_changes](
                                      [[maybe_unused]] ContentDialog const& sender,
                                      [[maybe_unused]] ContentDialogButtonClickEventArgs const& args) {
        // 3. Conflict detection: re-read registry and compare with our baseline
        auto fresh_user{Environ::core::read_variables(Environ::core::Scope::User)};
        bool conflict{false};
        std::wstring conflict_details;

        if (fresh_user.size() != m_originalUserVariables.size()) {
            conflict = true;
            conflict_details += L"Number of User variables changed externally.\n";
        } else {
            for (std::size_t i{0}; i < fresh_user.size(); ++i) {
                if (fresh_user[i].name != m_originalUserVariables[i].name ||
                    fresh_user[i].value != m_originalUserVariables[i].value) {
                    conflict = true;
                    conflict_details += std::format(L"'{}' was changed externally.\n",
                                                    fresh_user[i].name);
                }
            }
        }

        if (m_elevated) {
            auto fresh_machine{Environ::core::read_variables(Environ::core::Scope::Machine)};
            if (fresh_machine.size() != m_originalMachineVariables.size()) {
                conflict = true;
                conflict_details += L"Number of Machine variables changed externally.\n";
            } else {
                for (std::size_t i{0}; i < fresh_machine.size(); ++i) {
                    if (fresh_machine[i].name != m_originalMachineVariables[i].name ||
                        fresh_machine[i].value != m_originalMachineVariables[i].value) {
                        conflict = true;
                        conflict_details += std::format(L"'{}' (Machine) was changed externally.\n",
                                                        fresh_machine[i].name);
                    }
                }
            }
        }

        if (conflict) {
            auto conflict_dlg{ContentDialog{}};
            conflict_dlg.Title(winrt::box_value(L"External Changes Detected"));
            auto conflict_text{TextBlock{}};
            conflict_text.Text(conflict_details + L"\nOverwrite with your changes?");
            conflict_text.TextWrapping(TextWrapping::Wrap);
            conflict_dlg.Content(conflict_text);
            conflict_dlg.PrimaryButtonText(L"Overwrite");
            conflict_dlg.CloseButtonText(L"Cancel");
            conflict_dlg.XamlRoot(m_root.XamlRoot());

            conflict_dlg.PrimaryButtonClick([this, user_changes, machine_changes](
                                                [[maybe_unused]] ContentDialog const& s,
                                                [[maybe_unused]] ContentDialogButtonClickEventArgs const& a) {
                // Apply despite conflict
                std::wstring errors;
                if (!user_changes.empty()) {
                    errors += Environ::core::apply_changes(Environ::core::Scope::User, user_changes);
                }
                if (!machine_changes.empty()) {
                    auto err{Environ::core::apply_changes(Environ::core::Scope::Machine, machine_changes)};
                    if (!err.empty()) {
                        if (!errors.empty()) errors += L"\n";
                        errors += err;
                    }
                }
                Environ::core::broadcast_environment_change();

                if (!errors.empty()) {
                    auto err_dlg{ContentDialog{}};
                    err_dlg.Title(winrt::box_value(L"Save Errors"));
                    auto err_text{TextBlock{}};
                    err_text.Text(errors);
                    err_text.TextWrapping(TextWrapping::Wrap);
                    err_dlg.Content(err_text);
                    err_dlg.CloseButtonText(L"OK");
                    err_dlg.XamlRoot(m_root.XamlRoot());
                    err_dlg.ShowAsync();
                }
                Refresh();
            });

            conflict_dlg.ShowAsync();
            return;
        }

        // 4. No conflict — apply directly
        std::wstring errors;
        if (!user_changes.empty()) {
            errors += Environ::core::apply_changes(Environ::core::Scope::User, user_changes);
        }
        if (!machine_changes.empty()) {
            auto err{Environ::core::apply_changes(Environ::core::Scope::Machine, machine_changes)};
            if (!err.empty()) {
                if (!errors.empty()) errors += L"\n";
                errors += err;
            }
        }
        Environ::core::broadcast_environment_change();

        if (!errors.empty()) {
            auto err_dlg{ContentDialog{}};
            err_dlg.Title(winrt::box_value(L"Save Errors"));
            auto err_text{TextBlock{}};
            err_text.Text(errors);
            err_text.TextWrapping(TextWrapping::Wrap);
            err_dlg.Content(err_text);
            err_dlg.CloseButtonText(L"OK");
            err_dlg.XamlRoot(m_root.XamlRoot());
            err_dlg.ShowAsync();
        }
        Refresh();
    });

    review_dlg.ShowAsync();
}

void EnvironmentPage::WireScrollPassthrough(TextBox const& text_box) {
    text_box.PointerWheelChanged([this](winrt::Windows::Foundation::IInspectable const&,
                                        winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
        if (!m_scrollViewer) return;

        const auto point{args.GetCurrentPoint(m_scrollViewer)};
        const auto delta{point.Properties().MouseWheelDelta()};
        const bool ctrl{(static_cast<uint32_t>(args.KeyModifiers()) &
                        static_cast<uint32_t>(winrt::Windows::System::VirtualKeyModifiers::Control)) != 0};

        if (ctrl) {
            const auto step{static_cast<float>(delta) / 120.0f * 0.1f};
            const auto new_zoom{std::clamp(m_scrollViewer.ZoomFactor() + step, 0.5f, 3.0f)};
            m_scrollViewer.ChangeView(nullptr, nullptr, new_zoom);
        } else {
            m_scrollViewer.ChangeView(nullptr, m_scrollViewer.VerticalOffset() - static_cast<double>(delta), nullptr, true);
        }
        args.Handled(true);
    });
}
