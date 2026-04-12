#include "pch.h"
#include "MainWindow.xaml.h"

#include <algorithm>
#include <format>

#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;

namespace winrt::EnvironNativeBaseline::implementation
{
    namespace
    {
        constexpr double kNameColumnWidth{220.0};
        constexpr double kScopeColumnWidth{90.0};

        struct VariableRef
        {
            Environ::core::Scope scope;
            std::size_t index;
        };

        struct VisibleRow
        {
            Environ::core::Scope scope;
            std::size_t variableIndex;
            std::size_t segmentIndex;
        };

        bool ContainsCaseInsensitive(std::wstring const& haystack, std::wstring const& needle)
        {
            auto const it{std::ranges::search(haystack, needle, [](wchar_t left, wchar_t right)
            {
                return ::towlower(left) == ::towlower(right);
            })};
            return !it.empty();
        }

        bool MatchesFilter(Environ::core::EnvVariable const& variable, std::wstring const& filter)
        {
            if (filter.empty())
            {
                return true;
            }

            if (ContainsCaseInsensitive(variable.name, filter) ||
                ContainsCaseInsensitive(variable.value, filter))
            {
                return true;
            }

            return std::ranges::any_of(variable.segments, [&filter](std::wstring const& segment)
            {
                return ContainsCaseInsensitive(segment, filter);
            });
        }

        bool NameMatchesFilter(Environ::core::EnvVariable const& variable, std::wstring const& filter)
        {
            return !filter.empty() && ContainsCaseInsensitive(variable.name, filter);
        }

        std::vector<std::wstring> VisibleSegments(
            Environ::core::EnvVariable const& variable,
            std::wstring const& filter)
        {
            std::vector<std::wstring> segments;

            auto const show_all_segments{
                filter.empty() || NameMatchesFilter(variable, filter)};

            for (auto const& segment : variable.segments)
            {
                if (!show_all_segments &&
                    !ContainsCaseInsensitive(segment, filter))
                {
                    continue;
                }

                segments.push_back(segment);
            }

            if (segments.empty())
            {
                segments.push_back(L"(empty)");
            }

            return segments;
        }

        std::vector<VisibleRow> BuildDisplayRows(
            VariableRef const& item,
            Environ::core::EnvVariable const& variable,
            std::wstring const& filter)
        {
            std::vector<VisibleRow> rows;

            if (variable.kind == Environ::core::EnvVariableKind::PathList)
            {
                auto const segments{VisibleSegments(variable, filter)};
                rows.reserve(segments.size());
                for (std::size_t i{0}; i < segments.size(); ++i)
                {
                    rows.push_back(VisibleRow{item.scope, item.index, i});
                }
                return rows;
            }

            rows.push_back(VisibleRow{item.scope, item.index, 0});
            return rows;
        }

        VariableRef MakeVariableRef(Environ::core::Scope const scope, std::size_t const index)
        {
            return VariableRef{scope, index};
        }

        Environ::core::EnvVariable const& ResolveVariable(
            VariableRef const& item,
            std::vector<Environ::core::EnvVariable> const& user_variables,
            std::vector<Environ::core::EnvVariable> const& machine_variables)
        {
            return item.scope == Environ::core::Scope::User
                ? user_variables[item.index]
                : machine_variables[item.index];
        }

        std::vector<VariableRef> BuildVariableRefs(
            std::vector<Environ::core::EnvVariable> const& user_variables,
            std::vector<Environ::core::EnvVariable> const& machine_variables)
        {
            std::vector<VariableRef> items;
            items.reserve(user_variables.size() + machine_variables.size());

            for (std::size_t i{0}; i < user_variables.size(); ++i)
            {
                items.push_back(MakeVariableRef(Environ::core::Scope::User, i));
            }

            for (std::size_t i{0}; i < machine_variables.size(); ++i)
            {
                items.push_back(MakeVariableRef(Environ::core::Scope::Machine, i));
            }

            std::ranges::sort(items, [&user_variables, &machine_variables](
                VariableRef const& left,
                VariableRef const& right)
            {
                auto const& left_variable{ResolveVariable(left, user_variables, machine_variables)};
                auto const& right_variable{ResolveVariable(right, user_variables, machine_variables)};
                auto const compare{_wcsicmp(left_variable.name.c_str(), right_variable.name.c_str())};
                if (compare != 0)
                {
                    return compare < 0;
                }

                return left.scope == Environ::core::Scope::User &&
                       right.scope == Environ::core::Scope::Machine;
            });

            return items;
        }

        TextBlock MakeText(
            std::wstring const& text,
            bool const prominent = false,
            double const opacity = 1.0)
        {
            auto block{TextBlock{}};
            block.Text(text);
            block.TextWrapping(TextWrapping::Wrap);
            block.Opacity(opacity);
            if (prominent)
            {
                block.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
            }
            return block;
        }

        Brush ThemeBrush(std::wstring const& key)
        {
            return Application::Current().Resources()
                .Lookup(winrt::box_value(winrt::hstring{key}))
                .as<Brush>();
        }

        winrt::Windows::UI::Color ThemeColor(std::wstring const& key)
        {
            return ThemeBrush(key).as<SolidColorBrush>().Color();
        }

        winrt::Windows::UI::Color BlendColor(
            winrt::Windows::UI::Color const& base_color,
            winrt::Windows::UI::Color const& overlay_color,
            double const overlay_weight)
        {
            auto const clamped_weight{std::clamp(overlay_weight, 0.0, 1.0)};
            auto const base_weight{1.0 - clamped_weight};

            return winrt::Windows::UI::ColorHelper::FromArgb(
                255,
                static_cast<std::uint8_t>(base_color.R * base_weight + overlay_color.R * clamped_weight),
                static_cast<std::uint8_t>(base_color.G * base_weight + overlay_color.G * clamped_weight),
                static_cast<std::uint8_t>(base_color.B * base_weight + overlay_color.B * clamped_weight));
        }

        Brush SelectionBackgroundBrush()
        {
            auto const requested_theme{Application::Current().RequestedTheme()};
            auto const base_color{ThemeColor(L"ControlFillColorSecondaryBrush")};
            auto const accent_color{ThemeColor(L"AccentFillColorDefaultBrush")};
            auto const overlay_weight{
                requested_theme == ApplicationTheme::Dark ? 0.22 : 0.12};

            return SolidColorBrush{BlendColor(base_color, accent_color, overlay_weight)};
        }

        TextBlock MakeScopeText(Environ::core::Scope const scope)
        {
            auto text{TextBlock{}};
            text.Text(scope == Environ::core::Scope::User ? L"User" : L"Machine");
            text.FontSize(11);
            text.Opacity(0.78);
            text.VerticalAlignment(VerticalAlignment::Top);
            return text;
        }

        Grid MakeRowGrid()
        {
            auto row_grid{Grid{}};
            row_grid.ColumnSpacing(12);
            row_grid.VerticalAlignment(VerticalAlignment::Top);

            auto name_column{ColumnDefinition{}};
            name_column.Width(GridLengthHelper::FromPixels(kNameColumnWidth));
            auto scope_column{ColumnDefinition{}};
            scope_column.Width(GridLengthHelper::FromPixels(kScopeColumnWidth));
            auto value_column{ColumnDefinition{}};
            value_column.Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
            row_grid.ColumnDefinitions().Append(name_column);
            row_grid.ColumnDefinitions().Append(scope_column);
            row_grid.ColumnDefinitions().Append(value_column);

            return row_grid;
        }

        Border MakeRowSeparator()
        {
            auto separator{Border{}};
            separator.Height(1);
            separator.Margin(ThicknessHelper::FromLengths(0, 6, 0, 0));
            separator.Background(ThemeBrush(L"ControlStrokeColorSecondaryBrush"));
            return separator;
        }

        void ApplySelectionVisual(
            Border const& row_border,
            bool const is_selected,
            bool const is_elevated,
            Environ::core::Scope const scope)
        {
            row_border.ClearValue(Border::BorderThicknessProperty());
            row_border.ClearValue(Border::BorderBrushProperty());

            if (is_selected)
            {
                row_border.Background(SelectionBackgroundBrush());
            }
            else if (!is_elevated && scope == Environ::core::Scope::Machine)
            {
                row_border.Background(ThemeBrush(L"ControlFillColorSecondaryBrush"));
            }
            else
            {
                row_border.Background(SolidColorBrush{
                    winrt::Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)});
            }

            auto const container{row_border.Child().try_as<StackPanel>()};
            if (!container)
            {
                return;
            }

            for (auto const& child : container.Children())
            {
                auto const content_border{child.try_as<Border>()};
                if (!content_border || !content_border.Child())
                {
                    continue;
                }

                if (is_selected)
                {
                    content_border.ClearValue(Border::BackgroundProperty());
                }
                else if (!is_elevated && scope == Environ::core::Scope::Machine)
                {
                    content_border.ClearValue(Border::BackgroundProperty());
                }
                else
                {
                    content_border.ClearValue(Border::BackgroundProperty());
                }
            }
        }

        Border MakeDisplayRow(
            VisibleRow const& display_row,
            std::vector<Environ::core::EnvVariable> const& user_variables,
            std::vector<Environ::core::EnvVariable> const& machine_variables,
            std::wstring const& filter,
            bool const is_elevated,
            bool const is_selected)
        {
            auto const variable_ref{VariableRef{display_row.scope, display_row.variableIndex}};
            auto const& variable{ResolveVariable(variable_ref, user_variables, machine_variables)};
            auto const is_first_row{display_row.segmentIndex == 0};

            std::wstring value_text;
            bool empty_value{false};
            if (variable.kind == Environ::core::EnvVariableKind::PathList)
            {
                auto const segments{VisibleSegments(variable, filter)};
                value_text = segments[display_row.segmentIndex];
                empty_value = value_text == L"(empty)";
            }
            else
            {
                value_text = variable.value.empty() ? L"(empty)" : variable.value;
                empty_value = variable.value.empty();
            }

            auto row_grid{MakeRowGrid()};

            if (is_first_row)
            {
                auto name_text{MakeText(variable.name, true)};
                name_text.TextTrimming(TextTrimming::CharacterEllipsis);
                name_text.VerticalAlignment(VerticalAlignment::Top);
                Grid::SetColumn(name_text, 0);
                row_grid.Children().Append(name_text);

                auto scope_text{MakeScopeText(display_row.scope)};
                Grid::SetColumn(scope_text, 1);
                row_grid.Children().Append(scope_text);
            }

            auto value_text_block{MakeText(value_text, false, empty_value ? 0.55 : 1.0)};
            value_text_block.VerticalAlignment(VerticalAlignment::Top);
            Grid::SetColumn(value_text_block, 2);
            row_grid.Children().Append(value_text_block);

            auto container{StackPanel{}};
            container.Spacing(0);

            auto content_border{Border{}};
            content_border.Padding(ThicknessHelper::FromLengths(0, 6, 0, 6));
            content_border.Child(row_grid);

            container.Children().Append(content_border);
            container.Children().Append(MakeRowSeparator());

            auto row_border{Border{}};
            row_border.Padding(ThicknessHelper::FromLengths(8, 0, 8, 0));
            row_border.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush{
                winrt::Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)});
            row_border.Child(container);
            ApplySelectionVisual(row_border, is_selected, is_elevated, display_row.scope);
            return row_border;
        }
    }

    MainWindow::MainWindow()
    {
        InitializeComponent();
        LoadVariables();
    }

    void MainWindow::LoadVariables()
    {
        m_userVariables = Environ::core::read_variables(Environ::core::Scope::User);
        m_machineVariables = Environ::core::read_variables(Environ::core::Scope::Machine);
        m_isElevated = Environ::core::is_elevated();
        EnsureSelection();
        RebuildRows();
    }

    void MainWindow::EnsureSelection()
    {
        auto const all_items{BuildVariableRefs(m_userVariables, m_machineVariables)};
        std::vector<VisibleRow> visible_rows;
        for (auto const& item : all_items)
        {
            auto const& variable{ResolveVariable(item, m_userVariables, m_machineVariables)};
            if (!MatchesFilter(variable, m_filterText))
            {
                continue;
            }

            auto rows{BuildDisplayRows(item, variable, m_filterText)};
            visible_rows.insert(visible_rows.end(), rows.begin(), rows.end());
        }

        auto selection_is_visible = [this, &visible_rows]()
        {
            if (!m_selectedRow.has_value())
            {
                return false;
            }

            return std::ranges::any_of(visible_rows, [this](VisibleRow const& row)
            {
                return row.scope == m_selectedRow->scope &&
                       row.variableIndex == m_selectedRow->variableIndex &&
                       row.segmentIndex == m_selectedRow->segmentIndex;
            });
        };

        if (selection_is_visible())
        {
            return;
        }

        if (!visible_rows.empty())
        {
            auto const& first_row{visible_rows.front()};
            m_selectedRow = DisplayRow{
                .scope = first_row.scope,
                .variableIndex = first_row.variableIndex,
                .segmentIndex = first_row.segmentIndex,
            };
            return;
        }

        m_selectedRow.reset();
    }

    void MainWindow::RebuildRows()
    {
        auto const all_items{BuildVariableRefs(m_userVariables, m_machineVariables)};
        EnsureSelection();

        auto const list{ItemsList()};
        auto const previous_selected_index{list.SelectedIndex()};
        list.Items().Clear();
        list.Padding(ThicknessHelper::FromLengths(0, 4, 0, 0));
        m_rowVisuals.clear();

        std::size_t visible_count{0};
        for (auto const& item : all_items)
        {
            auto const& variable{ResolveVariable(item, m_userVariables, m_machineVariables)};
            if (!MatchesFilter(variable, m_filterText))
            {
                continue;
            }

            for (auto const& display_row : BuildDisplayRows(item, variable, m_filterText))
            {
                auto const is_selected{
                    m_selectedRow.has_value() &&
                    m_selectedRow->scope == display_row.scope &&
                    m_selectedRow->variableIndex == display_row.variableIndex &&
                    m_selectedRow->segmentIndex == display_row.segmentIndex};

                auto row{MakeDisplayRow(
                    display_row,
                    m_userVariables,
                    m_machineVariables,
                    m_filterText,
                    m_isElevated,
                    is_selected)};
                auto const visible_index{static_cast<int>(m_rowVisuals.size())};
                row.PointerPressed([this, visible_index](IInspectable const& sender, Input::PointerRoutedEventArgs const& args)
                {
                    auto const source{sender.as<UIElement>()};
                    auto const point{args.GetCurrentPoint(source)};
                    if (!point.Properties().IsLeftButtonPressed())
                    {
                        return;
                    }

                    ItemsList().SelectedIndex(visible_index);
                });
                m_rowVisuals.push_back(RowVisual{
                    .displayRow = MainWindow::DisplayRow{
                        .scope = display_row.scope,
                        .variableIndex = display_row.variableIndex,
                        .segmentIndex = display_row.segmentIndex,
                    },
                    .rowBorder = row,
                });
                list.Items().Append(row);
            }
            ++visible_count;
        }

        if (m_selectedRow.has_value())
        {
            auto const it{std::ranges::find_if(m_rowVisuals, [this](RowVisual const& row)
            {
                return row.displayRow.scope == m_selectedRow->scope &&
                       row.displayRow.variableIndex == m_selectedRow->variableIndex &&
                       row.displayRow.segmentIndex == m_selectedRow->segmentIndex;
            })};
            if (it != m_rowVisuals.end())
            {
                auto const selected_index{static_cast<int>(std::distance(m_rowVisuals.begin(), it))};
                if (selected_index != previous_selected_index)
                {
                    list.SelectedIndex(selected_index);
                }
            }
            else
            {
                list.SelectedIndex(-1);
            }
        }
        else
        {
            list.SelectedIndex(-1);
        }

        SummaryText().Text(winrt::hstring{std::format(
            L"{} shown of {} variables: {} user, {} machine",
            visible_count,
            all_items.size(),
            m_userVariables.size(),
            m_machineVariables.size())});
    }

    void MainWindow::SelectDisplayRow(DisplayRow const& display_row)
    {
        if (m_selectedRow.has_value() &&
            m_selectedRow->scope == display_row.scope &&
            m_selectedRow->variableIndex == display_row.variableIndex &&
            m_selectedRow->segmentIndex == display_row.segmentIndex)
        {
            return;
        }

        auto update_row = [this](DisplayRow const& selected, bool const is_selected)
        {
            auto const it{std::ranges::find_if(m_rowVisuals, [&selected](RowVisual const& row)
            {
                return row.displayRow.scope == selected.scope &&
                       row.displayRow.variableIndex == selected.variableIndex &&
                       row.displayRow.segmentIndex == selected.segmentIndex;
            })};
            if (it == m_rowVisuals.end())
            {
                return;
            }

            ApplySelectionVisual(it->rowBorder, is_selected, m_isElevated, it->displayRow.scope);
        };

        if (m_selectedRow.has_value())
        {
            update_row(*m_selectedRow, false);
        }

        m_selectedRow = display_row;
        update_row(*m_selectedRow, true);
        BringSelectedRowIntoView();
    }

    void MainWindow::BringSelectedRowIntoView()
    {
        if (!m_selectedRow.has_value())
        {
            return;
        }

        auto const it{std::ranges::find_if(m_rowVisuals, [this](RowVisual const& row)
        {
            return row.displayRow.scope == m_selectedRow->scope &&
                   row.displayRow.variableIndex == m_selectedRow->variableIndex &&
                   row.displayRow.segmentIndex == m_selectedRow->segmentIndex;
        })};
        if (it == m_rowVisuals.end())
        {
            return;
        }

        it->rowBorder.StartBringIntoView();
    }

    void MainWindow::OnFilterChanged(
        IInspectable const& sender,
        Controls::TextChangedEventArgs const&)
    {
        m_filterText = sender.as<TextBox>().Text().c_str();
        RebuildRows();
    }

    void MainWindow::OnItemsListSelectionChanged(
        IInspectable const& sender,
        Controls::SelectionChangedEventArgs const&)
    {
        auto const list{sender.as<ListView>()};
        auto const selected_index{list.SelectedIndex()};
        if (selected_index < 0 || selected_index >= static_cast<int>(m_rowVisuals.size()))
        {
            return;
        }

        auto const& row{m_rowVisuals[static_cast<std::size_t>(selected_index)]};
        SelectDisplayRow(row.displayRow);
    }
}
