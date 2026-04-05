#include "EnvironmentPage.h"

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

TextBox MakeCell(std::wstring const& text) {
    auto cell{TextBox{}};
    cell.Text(text);
    cell.BorderThickness(ThicknessHelper::FromUniformLength(0));
    cell.Background(SolidColorBrush{winrt::Windows::UI::Colors::Transparent()});
    cell.Padding(ThicknessHelper::FromLengths(4, 2, 4, 2));
    cell.TextWrapping(TextWrapping::NoWrap);
    cell.AcceptsReturn(false);
    cell.VerticalAlignment(VerticalAlignment::Center);

    cell.LostFocus([](winrt::Windows::Foundation::IInspectable const& sender,
                      [[maybe_unused]] RoutedEventArgs const& e) {
        auto box{sender.as<TextBox>()};
        box.BorderThickness(ThicknessHelper::FromUniformLength(0));
        box.Background(SolidColorBrush{winrt::Windows::UI::Colors::Transparent()});
    });

    return cell;
}

} // namespace

EnvironmentPage::EnvironmentPage() {
    m_root = Grid{};
    m_root.Padding(ThicknessHelper::FromLengths(24, 24, 24, 24));
    m_scrollViewer = ScrollViewer{};
    Refresh();
}

winrt::Microsoft::UI::Xaml::UIElement EnvironmentPage::Root() const {
    return m_root;
}

void EnvironmentPage::Refresh() {
    m_userVariables = Environ::core::read_variables(Environ::core::Scope::User);
    m_machineVariables = Environ::core::read_variables(Environ::core::Scope::Machine);
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
    // Clear previous selection
    if (m_selectedRowBorder) {
        m_selectedRowBorder.BorderThickness(ThicknessHelper::FromLengths(0, 0, 0, 0));
        m_selectedRowBorder.BorderBrush(nullptr);
    }

    // Apply new selection
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

    // Three rows: title, column headers, scrollable content
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

    // --- Title row ---
    auto title_panel{StackPanel{}};
    title_panel.Orientation(Orientation::Horizontal);
    title_panel.Spacing(8);
    title_panel.Margin(ThicknessHelper::FromLengths(0, 0, 0, 12));

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
    Grid::SetRow(title_panel, 0);
    parent.Children().Append(title_panel);

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

    // Header + separator wrapped in a StackPanel
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
    m_scrollViewer.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
    m_scrollViewer.Margin(ThicknessHelper::FromLengths(0, 4, 0, 0));

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

        // --- Main row ---
        auto row_border{Border{}};
        row_border.Padding(ThicknessHelper::FromLengths(8, 0, 8, 0));
        row_border.CornerRadius(CornerRadiusHelper::FromUniformRadius(2));
        row_border.MinHeight(kRowMinHeight);

        // Alternating row tint
        if (visual_row % 2 == 1) {
            row_border.Background(ThemeBrush(L"SubtleFillColorSecondaryBrush"));
        }

        // Selected row: left accent border
        if (is_selected) {
            row_border.BorderThickness(ThicknessHelper::FromLengths(2, 0, 0, 0));
            row_border.BorderBrush(ThemeBrush(L"AccentFillColorDefaultBrush"));
            m_selectedRowBorder = row_border;
        }

        // Hover effect
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
        auto name_cell{MakeCell(variable.name)};
        name_cell.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        WireScrollPassthrough(name_cell);
        Grid::SetColumn(name_cell, 0);

        name_cell.GotFocus([this, row_border, scope = ref.scope, idx = ref.index](
                               winrt::Windows::Foundation::IInspectable const& sender, RoutedEventArgs const&) {
            auto box{sender.as<TextBox>()};
            box.BorderThickness(ThicknessHelper::FromUniformLength(1));
            box.BorderBrush(ThemeBrush(L"ControlStrokeColorDefaultBrush"));
            box.Background(ThemeBrush(L"ControlFillColorDefaultBrush"));
            SelectRow(row_border, scope, idx);
        });

        name_cell.TextChanged([this, scope = ref.scope, idx = ref.index](
                                  winrt::Windows::Foundation::IInspectable const& sender,
                                  [[maybe_unused]] TextChangedEventArgs const& e) {
            auto& vars{scope == Environ::core::Scope::User ? m_userVariables : m_machineVariables};
            if (idx < vars.size()) {
                vars[idx].name = sender.as<TextBox>().Text().c_str();
            }
        });

        // Scope badge
        auto scope_badge{MakeScopeBadge(ref.scope)};
        scope_badge.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(scope_badge, 1);

        row_grid.Children().Append(name_cell);
        row_grid.Children().Append(scope_badge);

        // Value cell(s)
        if (variable.kind == Environ::core::EnvVariableKind::PathList && !variable.segments.empty()) {
            // First segment in the main row
            auto first_cell{MakeCell(variable.segments[0])};
            WireScrollPassthrough(first_cell);
            Grid::SetColumn(first_cell, 2);

            first_cell.GotFocus([this, row_border, scope = ref.scope, idx = ref.index](
                                    winrt::Windows::Foundation::IInspectable const& sender, RoutedEventArgs const&) {
                auto box{sender.as<TextBox>()};
                box.BorderThickness(ThicknessHelper::FromUniformLength(1));
                box.BorderBrush(ThemeBrush(L"ControlStrokeColorDefaultBrush"));
                box.Background(ThemeBrush(L"ControlFillColorDefaultBrush"));
                SelectRow(row_border, scope, idx);
            });

            first_cell.TextChanged([this, scope = ref.scope, idx = ref.index](
                                       winrt::Windows::Foundation::IInspectable const& sender,
                                       [[maybe_unused]] TextChangedEventArgs const& e) {
                auto& vars{scope == Environ::core::Scope::User ? m_userVariables : m_machineVariables};
                if (idx < vars.size() && !vars[idx].segments.empty()) {
                    vars[idx].segments[0] = sender.as<TextBox>().Text().c_str();
                    vars[idx].value = JoinSegments(vars[idx].segments);
                }
            });

            row_grid.Children().Append(first_cell);
        } else {
            // Scalar value
            auto value_cell{MakeCell(variable.value)};
            WireScrollPassthrough(value_cell);
            Grid::SetColumn(value_cell, 2);

            value_cell.GotFocus([this, row_border, scope = ref.scope, idx = ref.index](
                                    winrt::Windows::Foundation::IInspectable const& sender, RoutedEventArgs const&) {
                auto box{sender.as<TextBox>()};
                box.BorderThickness(ThicknessHelper::FromUniformLength(1));
                box.BorderBrush(ThemeBrush(L"ControlStrokeColorDefaultBrush"));
                box.Background(ThemeBrush(L"ControlFillColorDefaultBrush"));
                SelectRow(row_border, scope, idx);
            });

            value_cell.TextChanged([this, scope = ref.scope, idx = ref.index](
                                       winrt::Windows::Foundation::IInspectable const& sender,
                                       [[maybe_unused]] TextChangedEventArgs const& e) {
                auto& vars{scope == Environ::core::Scope::User ? m_userVariables : m_machineVariables};
                if (idx < vars.size()) {
                    vars[idx].value = sender.as<TextBox>().Text().c_str();
                }
            });

            row_grid.Children().Append(value_cell);
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

                // Same alternating tint as the parent row (visual grouping)
                if (visual_row % 2 == 1) {
                    cont_border.Background(ThemeBrush(L"SubtleFillColorSecondaryBrush"));
                }

                auto cont_grid{Grid{}};
                ApplyColumnDefinitions(cont_grid);

                // Visual grouping: thin left line in name column area
                auto indent_line{Border{}};
                indent_line.Width(2);
                indent_line.HorizontalAlignment(HorizontalAlignment::Left);
                indent_line.Margin(ThicknessHelper::FromLengths(6, 0, 0, 0));
                indent_line.Background(ThemeBrush(L"ControlStrokeColorDefaultBrush"));
                indent_line.Opacity(0.4);
                Grid::SetColumn(indent_line, 0);

                auto seg_cell{MakeCell(variable.segments[seg_i])};
                WireScrollPassthrough(seg_cell);
                Grid::SetColumn(seg_cell, 2);

                seg_cell.GotFocus([this, row_border, scope = ref.scope, idx = ref.index](
                                      winrt::Windows::Foundation::IInspectable const& sender, RoutedEventArgs const&) {
                    auto box{sender.as<TextBox>()};
                    box.BorderThickness(ThicknessHelper::FromUniformLength(1));
                    box.BorderBrush(ThemeBrush(L"ControlStrokeColorDefaultBrush"));
                    box.Background(ThemeBrush(L"ControlFillColorDefaultBrush"));
                    SelectRow(row_border, scope, idx);
                });

                seg_cell.TextChanged([this, scope = ref.scope, idx = ref.index, seg_i](
                                         winrt::Windows::Foundation::IInspectable const& sender,
                                         [[maybe_unused]] TextChangedEventArgs const& e) {
                    auto& vars{scope == Environ::core::Scope::User ? m_userVariables : m_machineVariables};
                    if (idx < vars.size() && seg_i < vars[idx].segments.size()) {
                        vars[idx].segments[seg_i] = sender.as<TextBox>().Text().c_str();
                        vars[idx].value = JoinSegments(vars[idx].segments);
                    }
                });

                cont_grid.Children().Append(indent_line);
                cont_grid.Children().Append(seg_cell);

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

void EnvironmentPage::WireScrollPassthrough(TextBox const& text_box) {
    text_box.PointerWheelChanged([this](winrt::Windows::Foundation::IInspectable const&,
                                        winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
        if (!m_scrollViewer) return;
        const auto point{args.GetCurrentPoint(m_scrollViewer)};
        const auto delta{static_cast<double>(point.Properties().MouseWheelDelta())};
        m_scrollViewer.ChangeView(nullptr, m_scrollViewer.VerticalOffset() - delta, nullptr, true);
        args.Handled(true);
    });
}
