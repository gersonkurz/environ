#include "EnvironmentPage.h"
#include "../core/EnvExport.h"
#include "../core/EnvWriter.h"

#include <pnq/unicode.h>

#include <algorithm>
#include <fstream>

#include <commdlg.h>

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

bool ContainsCaseInsensitive(std::wstring const& haystack, std::wstring const& needle) {
    auto it{std::ranges::search(haystack, needle, [](wchar_t a, wchar_t b) {
        return ::towlower(a) == ::towlower(b);
    })};
    return !it.empty();
}

bool MatchesFilter(Environ::core::EnvVariable const& var, std::wstring const& filter) {
    if (filter.empty()) return true;
    if (ContainsCaseInsensitive(var.name, filter)) return true;
    if (ContainsCaseInsensitive(var.value, filter)) return true;
    return false;
}

bool SegmentMatchesFilter(std::wstring const& segment, std::wstring const& filter,
                          bool name_matches) {
    if (filter.empty() || name_matches) return true;
    return ContainsCaseInsensitive(segment, filter);
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
        // Subtle tinted background
        accent.A = 30;
        badge.Background(SolidColorBrush{accent});
        // Full accent for text
        accent.A = 255;
        text.Foreground(SolidColorBrush{accent});
    } else {
        // Neutral: just use reduced opacity on the whole badge
        badge.Opacity(0.7);
        badge.BorderThickness(ThicknessHelper::FromUniformLength(1));
        badge.BorderBrush(SolidColorBrush{winrt::Windows::UI::ColorHelper::FromArgb(40, 128, 128, 128)});
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

EnvironmentPage::EnvironmentPage(HWND owner_hwnd)
    : m_ownerHwnd{owner_hwnd}
{
    m_root = Grid{};
    m_root.Padding(ThicknessHelper::FromLengths(24, 24, 24, 24));
    m_elevated = Environ::core::is_elevated();
    m_snapshotStore.open();
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

    // --- Title row (title + filter + toolbar) ---
    auto title_grid{Grid{}};
    title_grid.Margin(ThicknessHelper::FromLengths(0, 0, 0, 12));
    auto title_left_col{ColumnDefinition{}};
    title_left_col.Width(GridLengthHelper::Auto());
    auto title_filter_col{ColumnDefinition{}};
    title_filter_col.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    auto title_right_col{ColumnDefinition{}};
    title_right_col.Width(GridLengthHelper::Auto());
    title_grid.ColumnDefinitions().Append(title_left_col);
    title_grid.ColumnDefinitions().Append(title_filter_col);
    title_grid.ColumnDefinitions().Append(title_right_col);

    auto title_panel{StackPanel{}};
    title_panel.Orientation(Orientation::Horizontal);
    title_panel.Spacing(8);

    auto title_text{TextBlock{}};
    title_text.Text(L"Environment Variables");
    title_text.FontSize(20);
    title_text.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());

    auto count_text{TextBlock{}};
    count_text.Text(std::to_wstring(refs.size()));
    count_text.FontSize(12);
    count_text.VerticalAlignment(VerticalAlignment::Center);
    count_text.Opacity(0.6);

    title_panel.Children().Append(title_text);
    title_panel.Children().Append(count_text);
    Grid::SetColumn(title_panel, 0);

    // Filter box
    auto filter_box{TextBox{}};
    filter_box.PlaceholderText(L"Filter...");
    filter_box.Text(m_filterText);
    filter_box.MaxWidth(300);
    filter_box.Margin(ThicknessHelper::FromLengths(16, 0, 16, 0));
    filter_box.VerticalAlignment(VerticalAlignment::Center);
    Grid::SetColumn(filter_box, 1);

    filter_box.TextChanged([this](winrt::Windows::Foundation::IInspectable const& sender,
                                  [[maybe_unused]] TextChangedEventArgs const& e) {
        m_filterText = sender.as<TextBox>().Text().c_str();
        RebuildRows();
    });

    title_grid.Children().Append(filter_box);

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

    auto history_btn{Button{}};
    history_btn.Content(winrt::box_value(L"History"));
    history_btn.Click([this]([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
                             [[maybe_unused]] RoutedEventArgs const& args) {
        OnHistory();
    });

    auto export_btn{Button{}};
    export_btn.Content(winrt::box_value(L"Export"));
    export_btn.Click([this]([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
                            [[maybe_unused]] RoutedEventArgs const& args) {
        OnExport();
    });

    toolbar.Children().Append(save_btn);
    toolbar.Children().Append(history_btn);
    toolbar.Children().Append(export_btn);
    Grid::SetColumn(toolbar, 2);

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
    name_header.Opacity(0.6);
    Grid::SetColumn(name_header, 0);

    auto scope_header{TextBlock{}};
    scope_header.Text(L"Scope");
    scope_header.FontSize(12);
    scope_header.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    scope_header.Opacity(0.6);
    Grid::SetColumn(scope_header, 1);

    auto value_header{TextBlock{}};
    value_header.Text(L"Value");
    value_header.FontSize(12);
    value_header.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    value_header.Opacity(0.6);
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

    m_rowsPanel = StackPanel{};
    m_rowsPanel.Spacing(0);
    RebuildRows();

    m_scrollViewer.Content(m_rowsPanel);
    Grid::SetRow(m_scrollViewer, 2);
    parent.Children().Append(m_scrollViewer);
}

void EnvironmentPage::RebuildRows() {
    m_rowsPanel.Children().Clear();
    m_selectedRowBorder = nullptr;

    auto refs{BuildVariableRefs(m_userVariables, m_machineVariables)};

    std::size_t visual_row{0};

    for (const auto& ref : refs) {
        auto& scoped_vars{ref.scope == Environ::core::Scope::User
            ? m_userVariables : m_machineVariables};
        auto& variable{scoped_vars[ref.index]};

        if (!MatchesFilter(variable, m_filterText)) continue;

        const bool name_matches{m_filterText.empty() ||
            ContainsCaseInsensitive(variable.name, m_filterText)};
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
        m_rowsPanel.Children().Append(row_border);
        ++visual_row;

        // --- Continuation rows for path-list segments 1..N ---
        if (variable.kind == Environ::core::EnvVariableKind::PathList) {
            for (std::size_t seg_i{1}; seg_i < variable.segments.size(); ++seg_i) {
                if (!SegmentMatchesFilter(variable.segments[seg_i], m_filterText, name_matches))
                    continue;
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
                m_rowsPanel.Children().Append(cont_border);
                ++visual_row;
            }

            // "Add segment" empty row at end of path-list (only if editable)
            if (!is_protected) {
                auto add_seg_border{Border{}};
                add_seg_border.Padding(ThicknessHelper::FromLengths(8, 0, 8, 0));
                add_seg_border.MinHeight(kRowMinHeight);
                add_seg_border.Opacity(0.5);

                auto add_seg_grid{Grid{}};
                ApplyColumnDefinitions(add_seg_grid);

                auto add_seg_indent{Border{}};
                add_seg_indent.Width(2);
                add_seg_indent.HorizontalAlignment(HorizontalAlignment::Left);
                add_seg_indent.Margin(ThicknessHelper::FromLengths(6, 0, 0, 0));
                add_seg_indent.Background(ThemeBrush(L"ControlStrokeColorDefaultBrush"));
                add_seg_indent.Opacity(0.4);
                Grid::SetColumn(add_seg_indent, 0);

                auto add_seg_cell{MakeCell(L"")};
                add_seg_cell.PlaceholderText(L"Add path entry...");
                WireScrollPassthrough(add_seg_cell);
                Grid::SetColumn(add_seg_cell, 2);

                add_seg_cell.KeyDown([this, scope = ref.scope, idx = ref.index](
                                         winrt::Windows::Foundation::IInspectable const& sender,
                                         winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args) {
                    if (args.Key() != winrt::Windows::System::VirtualKey::Enter) return;
                    auto box{sender.as<TextBox>()};
                    auto text{std::wstring{box.Text()}};
                    if (text.empty()) return;

                    box.Text(L""); // Prevent LostFocus from committing again
                    auto& vars{scope == Environ::core::Scope::User ? m_userVariables : m_machineVariables};
                    if (idx < vars.size()) {
                        vars[idx].segments.push_back(text);
                        vars[idx].value = JoinSegments(vars[idx].segments);
                        RebuildRows();
                    }
                    args.Handled(true);
                });

                add_seg_cell.LostFocus([this, scope = ref.scope, idx = ref.index](
                                           winrt::Windows::Foundation::IInspectable const& sender,
                                           [[maybe_unused]] RoutedEventArgs const& e) {
                    auto box{sender.as<TextBox>()};
                    auto text{std::wstring{box.Text()}};
                    box.BorderThickness(ThicknessHelper::FromUniformLength(0));
                    box.Background(SolidColorBrush{winrt::Windows::UI::Colors::Transparent()});
                    if (text.empty()) return;

                    auto& vars{scope == Environ::core::Scope::User ? m_userVariables : m_machineVariables};
                    if (idx < vars.size()) {
                        vars[idx].segments.push_back(text);
                        vars[idx].value = JoinSegments(vars[idx].segments);
                        RebuildRows();
                    }
                });

                add_seg_grid.Children().Append(add_seg_indent);
                add_seg_grid.Children().Append(add_seg_cell);
                add_seg_border.Child(add_seg_grid);
                m_rowsPanel.Children().Append(add_seg_border);
                ++visual_row;
            }
        }
    }

    // "Add variable" empty row at the bottom
    {
        auto add_border{Border{}};
        add_border.Padding(ThicknessHelper::FromLengths(8, 0, 8, 0));
        add_border.MinHeight(kRowMinHeight);
        add_border.Opacity(0.5);

        auto add_grid{Grid{}};
        ApplyColumnDefinitions(add_grid);

        auto add_name_cell{MakeCell(L"")};
        add_name_cell.PlaceholderText(L"New variable...");
        WireScrollPassthrough(add_name_cell);
        Grid::SetColumn(add_name_cell, 0);

        auto add_value_cell{MakeCell(L"")};
        add_value_cell.PlaceholderText(L"Value");
        WireScrollPassthrough(add_value_cell);
        Grid::SetColumn(add_value_cell, 2);

        // Commit when focus leaves either cell — but not when tabbing between them
        auto commit = [this, add_name_cell, add_value_cell]() {
            auto name{std::wstring{add_name_cell.Text()}};
            if (name.empty()) return;

            // Check if focus moved to the sibling cell — if so, don't commit yet
            auto focused{winrt::Microsoft::UI::Xaml::Input::FocusManager::GetFocusedElement(m_root.XamlRoot())};
            if (focused == add_name_cell || focused == add_value_cell) return;

            auto value{std::wstring{add_value_cell.Text()}};
            m_userVariables.push_back(Environ::core::EnvVariable{
                .name{name},
                .value{value},
                .kind{Environ::core::EnvVariableKind::Scalar},
                .is_expandable{false},
            });
            RebuildRows();
        };

        add_name_cell.LostFocus([commit, add_name_cell](
                                    [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
                                    [[maybe_unused]] RoutedEventArgs const& e) {
            add_name_cell.BorderThickness(ThicknessHelper::FromUniformLength(0));
            add_name_cell.Background(SolidColorBrush{winrt::Windows::UI::Colors::Transparent()});
            commit();
        });

        add_value_cell.LostFocus([commit, add_value_cell](
                                     [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
                                     [[maybe_unused]] RoutedEventArgs const& e) {
            add_value_cell.BorderThickness(ThicknessHelper::FromUniformLength(0));
            add_value_cell.Background(SolidColorBrush{winrt::Windows::UI::Colors::Transparent()});
            commit();
        });

        // Enter in value cell commits immediately
        add_value_cell.KeyDown([this, add_name_cell, add_value_cell](
                                   winrt::Windows::Foundation::IInspectable const&,
                                   winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args) {
            if (args.Key() != winrt::Windows::System::VirtualKey::Enter) return;
            auto name{std::wstring{add_name_cell.Text()}};
            if (name.empty()) return;

            auto value{std::wstring{add_value_cell.Text()}};
            add_name_cell.Text(L""); // Prevent LostFocus from committing again
            add_value_cell.Text(L"");
            m_userVariables.push_back(Environ::core::EnvVariable{
                .name{name},
                .value{value},
                .kind{Environ::core::EnvVariableKind::Scalar},
                .is_expandable{false},
            });
            RebuildRows();
            args.Handled(true);
        });

        // GotFocus styling for both cells
        auto focus_style = [](winrt::Windows::Foundation::IInspectable const& sender,
                              [[maybe_unused]] RoutedEventArgs const& e) {
            auto box{sender.as<TextBox>()};
            box.BorderThickness(ThicknessHelper::FromUniformLength(1));
            box.BorderBrush(ThemeBrush(L"ControlStrokeColorDefaultBrush"));
            box.Background(ThemeBrush(L"ControlFillColorDefaultBrush"));
        };
        add_name_cell.GotFocus(focus_style);
        add_value_cell.GotFocus(focus_style);

        auto add_scope{MakeScopeBadge(Environ::core::Scope::User)};
        add_scope.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(add_scope, 1);

        add_grid.Children().Append(add_name_cell);
        add_grid.Children().Append(add_scope);
        add_grid.Children().Append(add_value_cell);
        add_border.Child(add_grid);
        m_rowsPanel.Children().Append(add_border);
    }

}

void EnvironmentPage::OnExport() {
    wchar_t filename[MAX_PATH]{L"environ.toml"};

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_ownerHwnd;
    ofn.lpstrFilter = L"TOML Files (*.toml)\0*.toml\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"toml";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

    if (!GetSaveFileNameW(&ofn)) return;

    auto toml{Environ::core::export_toml(m_userVariables, m_machineVariables)};

    std::ofstream file{filename, std::ios::binary};
    if (file.is_open()) {
        file.write(toml.data(), static_cast<std::streamsize>(toml.size()));
        file.close();

        auto dlg{ContentDialog{}};
        dlg.Title(winrt::box_value(L"Export Complete"));
        dlg.Content(winrt::box_value(winrt::hstring{std::wstring{L"Saved to "} + filename}));
        dlg.CloseButtonText(L"OK");
        dlg.XamlRoot(m_root.XamlRoot());
        dlg.ShowAsync();
    } else {
        auto dlg{ContentDialog{}};
        dlg.Title(winrt::box_value(L"Export Failed"));
        dlg.Content(winrt::box_value(winrt::hstring{std::wstring{L"Could not write to "} + filename}));
        dlg.CloseButtonText(L"OK");
        dlg.XamlRoot(m_root.XamlRoot());
        dlg.ShowAsync();
    }
}

void EnvironmentPage::OnHistory() {
    auto snapshots{m_snapshotStore.list_snapshots()};

    auto dlg{ContentDialog{}};
    dlg.Title(winrt::box_value(L"Snapshot History"));
    dlg.CloseButtonText(L"Close");
    dlg.XamlRoot(m_root.XamlRoot());

    if (snapshots.empty()) {
        dlg.Content(winrt::box_value(L"No snapshots yet. Snapshots are created automatically when you save."));
        dlg.ShowAsync();
        return;
    }

    // Two-part layout: snapshot list (top) + change details (bottom)
    auto outer{Grid{}};
    outer.MinWidth(500);
    auto list_row{RowDefinition{}};
    list_row.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    auto detail_row{RowDefinition{}};
    detail_row.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    outer.RowDefinitions().Append(list_row);
    outer.RowDefinitions().Append(detail_row);

    // Change detail panel (bottom) — updated when a snapshot is clicked
    auto detail_panel{StackPanel{}};
    detail_panel.Spacing(2);

    auto detail_header{TextBlock{}};
    detail_header.Text(L"Select a snapshot to see changes");
    detail_header.FontSize(12);
    detail_header.Opacity(0.6);
    detail_header.FontStyle(winrt::Windows::UI::Text::FontStyle::Italic);
    detail_panel.Children().Append(detail_header);

    auto detail_scroll{ScrollViewer{}};
    detail_scroll.MaxHeight(150);
    detail_scroll.Content(detail_panel);
    detail_scroll.Margin(ThicknessHelper::FromLengths(0, 8, 0, 0));
    Grid::SetRow(detail_scroll, 1);
    outer.Children().Append(detail_scroll);

    // Snapshot list (top)
    auto list_panel{StackPanel{}};
    list_panel.Spacing(4);

    for (const auto& snap : snapshots) {
        auto row{Grid{}};
        auto ts_col{ColumnDefinition{}};
        ts_col.Width(GridLengthHelper::FromPixels(180));
        auto label_col{ColumnDefinition{}};
        label_col.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        auto btn_col{ColumnDefinition{}};
        btn_col.Width(GridLengthHelper::Auto());
        row.ColumnDefinitions().Append(ts_col);
        row.ColumnDefinitions().Append(label_col);
        row.ColumnDefinitions().Append(btn_col);
        row.ColumnSpacing(8);

        auto ts_text{TextBlock{}};
        ts_text.Text(winrt::to_hstring(snap.timestamp));
        ts_text.FontSize(12);
        ts_text.VerticalAlignment(VerticalAlignment::Center);
        ts_text.Opacity(0.6);
        Grid::SetColumn(ts_text, 0);

        auto label_text{TextBlock{}};
        label_text.Text(winrt::to_hstring(snap.label));
        label_text.FontSize(12);
        label_text.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(label_text, 1);

        // Clicking the row shows change details
        auto row_border{Border{}};
        row_border.Padding(ThicknessHelper::FromLengths(4, 2, 4, 2));
        row_border.CornerRadius(CornerRadiusHelper::FromUniformRadius(4));
        row_border.Tapped([this, detail_panel, snapshot_id = snap.id](
                              [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
                              [[maybe_unused]] winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& args) {
            detail_panel.Children().Clear();

            auto header{TextBlock{}};
            header.Text(L"Changes in this snapshot:");
            header.FontSize(12);
            header.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            header.Margin(ThicknessHelper::FromLengths(0, 0, 0, 4));
            detail_panel.Children().Append(header);

            auto changes{m_snapshotStore.describe_snapshot_changes(snapshot_id)};
            for (const auto& desc : changes) {
                auto line{TextBlock{}};
                line.Text(desc);
                line.FontSize(12);
                line.TextWrapping(TextWrapping::Wrap);
                detail_panel.Children().Append(line);
            }
        });

        auto restore_btn{Button{}};
        restore_btn.Content(winrt::box_value(L"Restore"));
        restore_btn.FontSize(11);
        restore_btn.Padding(ThicknessHelper::FromLengths(8, 2, 8, 2));
        Grid::SetColumn(restore_btn, 2);

        restore_btn.Click([this, dlg, snapshot_id = snap.id, timestamp = snap.timestamp](
                              [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
                              [[maybe_unused]] RoutedEventArgs const& args) {
            // Close history dialog before opening confirm dialog
            dlg.Hide();

            auto confirm{ContentDialog{}};
            confirm.Title(winrt::box_value(L"Restore Snapshot"));
            auto msg{TextBlock{}};
            msg.Text(std::format(L"Restore environment to snapshot from {}?\n\n"
                                 L"Current registry state will be saved as a snapshot first.",
                                 winrt::to_hstring(timestamp)));
            msg.TextWrapping(TextWrapping::Wrap);
            confirm.Content(msg);
            confirm.PrimaryButtonText(L"Restore");
            confirm.CloseButtonText(L"Cancel");
            confirm.XamlRoot(m_root.XamlRoot());

            confirm.PrimaryButtonClick([this, snapshot_id](
                                           [[maybe_unused]] ContentDialog const& s,
                                           [[maybe_unused]] ContentDialogButtonClickEventArgs const& a) {
                auto snap_vars{m_snapshotStore.load_snapshot(snapshot_id)};

                // Safety snapshot — only if current state differs from latest snapshot
                auto fresh_user{Environ::core::read_variables(Environ::core::Scope::User)};
                auto fresh_machine{Environ::core::read_variables(Environ::core::Scope::Machine)};
                if (!m_snapshotStore.matches_latest_snapshot(fresh_user, fresh_machine)) {
                    m_snapshotStore.create_snapshot("Auto (pre-restore)", fresh_user, fresh_machine);
                }

                // Split snapshot into user/machine
                std::vector<Environ::core::EnvVariable> snap_user;
                std::vector<Environ::core::EnvVariable> snap_machine;
                for (const auto& sv : snap_vars) {
                    Environ::core::EnvVariable var{
                        .name{sv.name},
                        .value{sv.value},
                        .is_expandable{sv.is_expandable},
                    };
                    if (sv.scope == Environ::core::Scope::User) {
                        snap_user.push_back(std::move(var));
                    } else {
                        snap_machine.push_back(std::move(var));
                    }
                }

                // Compute full-replacement diffs
                auto user_changes{Environ::core::compute_diff(fresh_user, snap_user)};
                std::wstring errors;
                if (!user_changes.empty()) {
                    errors += Environ::core::apply_changes(Environ::core::Scope::User, user_changes);
                }

                if (m_elevated) {
                    auto machine_changes{Environ::core::compute_diff(fresh_machine, snap_machine)};
                    if (!machine_changes.empty()) {
                        auto err{Environ::core::apply_changes(Environ::core::Scope::Machine, machine_changes)};
                        if (!err.empty()) {
                            if (!errors.empty()) errors += L"\n";
                            errors += err;
                        }
                    }
                }

                Environ::core::broadcast_environment_change();

                if (!errors.empty()) {
                    auto err_dlg{ContentDialog{}};
                    err_dlg.Title(winrt::box_value(L"Restore Errors"));
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

            confirm.ShowAsync();
        });

        row.Children().Append(ts_text);
        row.Children().Append(label_text);
        row.Children().Append(restore_btn);
        row_border.Child(row);
        list_panel.Children().Append(row_border);
    }

    auto list_scroll{ScrollViewer{}};
    list_scroll.MaxHeight(200);
    list_scroll.Content(list_panel);
    Grid::SetRow(list_scroll, 0);
    outer.Children().Append(list_scroll);

    dlg.Content(outer);
    dlg.ShowAsync();
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

    // 2. Dry-run review dialog with editable snapshot label
    auto all_changes{user_changes};
    all_changes.insert(all_changes.end(), machine_changes.begin(), machine_changes.end());
    auto suggested_label{Environ::core::summarize_changes(all_changes)};

    auto outer_panel{StackPanel{}};
    outer_panel.Spacing(8);
    outer_panel.MaxWidth(500);

    // Snapshot label field
    auto label_header{TextBlock{}};
    label_header.Text(L"Snapshot label:");
    label_header.FontSize(12);
    label_header.Opacity(0.6);
    outer_panel.Children().Append(label_header);

    auto label_box{TextBox{}};
    label_box.Text(suggested_label);
    label_box.PlaceholderText(L"Describe this change...");
    outer_panel.Children().Append(label_box);

    // Change list
    auto changes_header{TextBlock{}};
    changes_header.Text(L"Changes to apply:");
    changes_header.FontSize(12);
    changes_header.Opacity(0.6);
    changes_header.Margin(ThicknessHelper::FromLengths(0, 4, 0, 0));
    outer_panel.Children().Append(changes_header);

    auto review_panel{StackPanel{}};
    review_panel.Spacing(4);

    if (!user_changes.empty()) {
        auto scope_label{TextBlock{}};
        scope_label.Text(L"User scope:");
        scope_label.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
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
        scope_label.Margin(ThicknessHelper::FromLengths(0, 4, 0, 0));
        review_panel.Children().Append(scope_label);

        for (const auto& c : machine_changes) {
            auto line{TextBlock{}};
            line.Text(c.describe());
            line.TextWrapping(TextWrapping::Wrap);
            review_panel.Children().Append(line);
        }
    }

    auto scroll{ScrollViewer{}};
    scroll.MaxHeight(250);
    scroll.Content(review_panel);
    outer_panel.Children().Append(scroll);

    auto review_dlg{ContentDialog{}};
    review_dlg.Title(winrt::box_value(L"Review Changes"));
    review_dlg.Content(outer_panel);
    review_dlg.PrimaryButtonText(L"Apply");
    review_dlg.CloseButtonText(L"Cancel");
    review_dlg.XamlRoot(m_root.XamlRoot());

    review_dlg.PrimaryButtonClick([this, user_changes, machine_changes, label_box](
                                      [[maybe_unused]] ContentDialog const& sender,
                                      [[maybe_unused]] ContentDialogButtonClickEventArgs const& args) {
        auto label{pnq::unicode::to_utf8(std::wstring{label_box.Text()})};
        if (label.empty()) label = "Save";

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

        auto fresh_machine{Environ::core::read_variables(Environ::core::Scope::Machine)};
        if (m_elevated) {
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
            // External changes detected — snapshot the unexpected state before overwriting
            m_snapshotStore.create_snapshot("Auto (external changes detected)",
                fresh_user, fresh_machine);

            auto conflict_dlg{ContentDialog{}};
            conflict_dlg.Title(winrt::box_value(L"External Changes Detected"));
            auto conflict_text{TextBlock{}};
            conflict_text.Text(conflict_details + L"\nOverwrite with your changes?");
            conflict_text.TextWrapping(TextWrapping::Wrap);
            conflict_dlg.Content(conflict_text);
            conflict_dlg.PrimaryButtonText(L"Overwrite");
            conflict_dlg.CloseButtonText(L"Cancel");
            conflict_dlg.XamlRoot(m_root.XamlRoot());

            conflict_dlg.PrimaryButtonClick([this, user_changes, machine_changes, label](
                                                [[maybe_unused]] ContentDialog const& s,
                                                [[maybe_unused]] ContentDialogButtonClickEventArgs const& a) {
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

                // Snapshot the new state (what the user chose)
                auto new_user{Environ::core::read_variables(Environ::core::Scope::User)};
                auto new_machine{Environ::core::read_variables(Environ::core::Scope::Machine)};
                m_snapshotStore.create_snapshot(label, new_user, new_machine);

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

        // 4. No conflict — apply, then snapshot the result
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

        // Snapshot the new state with the user's label
        auto new_user{Environ::core::read_variables(Environ::core::Scope::User)};
        auto new_machine{Environ::core::read_variables(Environ::core::Scope::Machine)};
        m_snapshotStore.create_snapshot(label, new_user, new_machine);

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
