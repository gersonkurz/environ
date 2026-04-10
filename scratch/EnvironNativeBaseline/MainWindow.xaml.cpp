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

        Border MakeVariableRow(
            VariableRef const& item,
            std::vector<Environ::core::EnvVariable> const& user_variables,
            std::vector<Environ::core::EnvVariable> const& machine_variables,
            std::wstring const& filter,
            bool const is_elevated)
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
                if (!is_elevated && item.scope == Environ::core::Scope::Machine)
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
            row_border.Child(container);
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
        RebuildRows();
    }

    void MainWindow::RebuildRows()
    {
        auto const all_items{BuildVariableRefs(m_userVariables, m_machineVariables)};

        auto const list{ItemsList()};
        list.Items().Clear();
        list.Padding(ThicknessHelper::FromLengths(0, 4, 0, 0));

        std::size_t visible_count{0};
        for (auto const& item : all_items)
        {
            auto const& variable{ResolveVariable(item, m_userVariables, m_machineVariables)};
            if (!MatchesFilter(variable, m_filterText))
            {
                continue;
            }

            list.Items().Append(MakeVariableRow(
                item,
                m_userVariables,
                m_machineVariables,
                m_filterText,
                m_isElevated));
            ++visible_count;
        }

        SummaryText().Text(winrt::hstring{std::format(
            L"{} shown of {} variables: {} user, {} machine",
            visible_count,
            all_items.size(),
            m_userVariables.size(),
            m_machineVariables.size())});
    }

    void MainWindow::OnFilterChanged(
        IInspectable const& sender,
        Controls::TextChangedEventArgs const&)
    {
        m_filterText = sender.as<TextBox>().Text().c_str();
        RebuildRows();
    }
}
