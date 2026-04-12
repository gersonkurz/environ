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
            if (is_selected)
            {
                row_border.BorderThickness(ThicknessHelper::FromLengths(2, 0, 0, 0));
                row_border.BorderBrush(ThemeBrush(L"AccentFillColorDefaultBrush"));
            }
            else
            {
                row_border.ClearValue(Border::BorderThicknessProperty());
                row_border.ClearValue(Border::BorderBrushProperty());
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
                    content_border.Background(ThemeBrush(L"ControlFillColorTertiaryBrush"));
                }
                else if (!is_elevated && scope == Environ::core::Scope::Machine)
                {
                    content_border.Background(ThemeBrush(L"ControlFillColorSecondaryBrush"));
                }
                else
                {
                    content_border.ClearValue(Border::BackgroundProperty());
                }
            }
        }

        Border MakeVariableRow(
            VariableRef const& item,
            std::vector<Environ::core::EnvVariable> const& user_variables,
            std::vector<Environ::core::EnvVariable> const& machine_variables,
            std::wstring const& filter,
            bool const is_elevated,
            bool const is_selected)
        {
            auto const& variable{ResolveVariable(item, user_variables, machine_variables)};

            auto container{StackPanel{}};
            container.Spacing(0);

            auto append_row = [&](std::wstring const& value_text, bool const first_row, bool const empty_value)
            {
                auto row_grid{MakeRowGrid()};

                if (first_row)
                {
                    auto name_text{MakeText(variable.name, true)};
                    name_text.TextTrimming(TextTrimming::CharacterEllipsis);
                    name_text.VerticalAlignment(VerticalAlignment::Top);
                    Grid::SetColumn(name_text, 0);
                    row_grid.Children().Append(name_text);

                    auto scope_text{MakeScopeText(item.scope)};
                    Grid::SetColumn(scope_text, 1);
                    row_grid.Children().Append(scope_text);
                }

                auto value_text_block{MakeText(value_text, false, empty_value ? 0.55 : 1.0)};
                value_text_block.VerticalAlignment(VerticalAlignment::Top);
                Grid::SetColumn(value_text_block, 2);
                row_grid.Children().Append(value_text_block);

                auto content_border{Border{}};
                content_border.Padding(ThicknessHelper::FromLengths(0, 6, 0, 6));
                if (is_selected)
                {
                    content_border.Background(ThemeBrush(L"ControlFillColorTertiaryBrush"));
                }
                else if (!is_elevated && item.scope == Environ::core::Scope::Machine)
                {
                    content_border.Background(ThemeBrush(L"ControlFillColorSecondaryBrush"));
                }
                content_border.Child(row_grid);

                container.Children().Append(content_border);
                container.Children().Append(MakeRowSeparator());
            };

            if (variable.kind == Environ::core::EnvVariableKind::PathList)
            {
                auto const segments{VisibleSegments(variable, filter)};
                for (std::size_t i{0}; i < segments.size(); ++i)
                {
                    append_row(segments[i], i == 0, segments[i] == L"(empty)");
                }
            }
            else
            {
                auto const scalar_value{variable.value.empty() ? L"(empty)" : variable.value};
                append_row(scalar_value, true, variable.value.empty());
            }

            auto row_border{Border{}};
            row_border.Padding(ThicknessHelper::FromLengths(8, 0, 8, 0));
            row_border.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush{
                winrt::Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)});
            row_border.Child(container);
            ApplySelectionVisual(row_border, is_selected, is_elevated, item.scope);
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

        auto selection_is_visible = [this, &all_items]()
        {
            if (!m_selectedVariable.has_value())
            {
                return false;
            }

            return std::ranges::any_of(all_items, [this](VariableRef const& item)
            {
                return item.scope == m_selectedVariable->scope &&
                       item.index == m_selectedVariable->index &&
                       MatchesFilter(ResolveVariable(item, m_userVariables, m_machineVariables), m_filterText);
            });
        };

        if (selection_is_visible())
        {
            return;
        }

        for (auto const& item : all_items)
        {
            if (!MatchesFilter(ResolveVariable(item, m_userVariables, m_machineVariables), m_filterText))
            {
                continue;
            }

            m_selectedVariable = RowVisual{item.scope, item.index, nullptr};
            return;
        }

        m_selectedVariable.reset();
    }

    void MainWindow::RebuildRows()
    {
        auto const all_items{BuildVariableRefs(m_userVariables, m_machineVariables)};
        EnsureSelection();

        auto const list{ItemsList()};
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

            auto const is_selected{
                m_selectedVariable.has_value() &&
                m_selectedVariable->scope == item.scope &&
                m_selectedVariable->index == item.index};

            auto row{MakeVariableRow(
                item,
                m_userVariables,
                m_machineVariables,
                m_filterText,
                m_isElevated,
                is_selected)};
            row.PointerPressed([this, item](IInspectable const& sender, Input::PointerRoutedEventArgs const& args)
            {
                auto const source{sender.as<UIElement>()};
                auto const point{args.GetCurrentPoint(source)};
                if (!point.Properties().IsLeftButtonPressed())
                {
                    return;
                }

                SelectVariable(item.scope, item.index);
            });
            m_rowVisuals.push_back(RowVisual{item.scope, item.index, row});
            list.Items().Append(row);
            ++visible_count;
        }

        SummaryText().Text(winrt::hstring{std::format(
            L"{} shown of {} variables: {} user, {} machine",
            visible_count,
            all_items.size(),
            m_userVariables.size(),
            m_machineVariables.size())});
    }

    void MainWindow::SelectVariable(Environ::core::Scope const scope, std::size_t const index)
    {
        if (m_selectedVariable.has_value() &&
            m_selectedVariable->scope == scope &&
            m_selectedVariable->index == index)
        {
            return;
        }

        auto update_row = [this](RowVisual const& selected, bool const is_selected)
        {
            auto const it{std::ranges::find_if(m_rowVisuals, [&selected](RowVisual const& row)
            {
                return row.scope == selected.scope && row.index == selected.index;
            })};
            if (it == m_rowVisuals.end())
            {
                return;
            }

            ApplySelectionVisual(it->rowBorder, is_selected, m_isElevated, it->scope);
        };

        if (m_selectedVariable.has_value())
        {
            update_row(*m_selectedVariable, false);
        }

        m_selectedVariable = RowVisual{scope, index, nullptr};
        update_row(*m_selectedVariable, true);
    }

    void MainWindow::OnFilterChanged(
        IInspectable const& sender,
        Controls::TextChangedEventArgs const&)
    {
        m_filterText = sender.as<TextBox>().Text().c_str();
        RebuildRows();
    }
}
