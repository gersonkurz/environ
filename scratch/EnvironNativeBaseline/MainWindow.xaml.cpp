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

        Border MakeScopeBadge(Environ::core::Scope const scope)
        {
            auto badge{Border{}};
            badge.Padding(ThicknessHelper::FromLengths(6, 2, 6, 2));
            badge.CornerRadius(CornerRadiusHelper::FromUniformRadius(4));
            badge.VerticalAlignment(VerticalAlignment::Top);

            auto text{TextBlock{}};
            text.Text(scope == Environ::core::Scope::User ? L"User" : L"Machine");
            text.FontSize(11);

            if (scope == Environ::core::Scope::User)
            {
                badge.Background(ThemeBrush(L"AccentFillColorSecondaryBrush"));
                text.Foreground(ThemeBrush(L"AccentTextFillColorPrimaryBrush"));
            }
            else
            {
                badge.Background(ThemeBrush(L"ControlFillColorSecondaryBrush"));
                text.Foreground(ThemeBrush(L"TextFillColorSecondaryBrush"));
            }

            badge.Child(text);
            return badge;
        }

        UIElement MakeValueElement(
            Environ::core::EnvVariable const& variable,
            std::wstring const& filter)
        {
            if (variable.kind == Environ::core::EnvVariableKind::PathList)
            {
                auto values{StackPanel{}};
                values.Spacing(2);

                auto const show_all_segments{
                    filter.empty() ||
                    NameMatchesFilter(variable, filter)};
                bool any_segment_visible{false};

                for (auto const& segment : variable.segments)
                {
                    if (!show_all_segments &&
                        !ContainsCaseInsensitive(segment, filter))
                    {
                        continue;
                    }

                    auto segment_text{MakeText(segment)};
                    segment_text.Margin(ThicknessHelper::FromLengths(0, 0, 0, 0));
                    values.Children().Append(segment_text);
                    any_segment_visible = true;
                }

                if (variable.segments.empty() || !any_segment_visible)
                {
                    auto empty_text{MakeText(L"(empty)", false, 0.55)};
                    values.Children().Append(empty_text);
                }

                return values;
            }

            return MakeText(variable.value.empty() ? L"(empty)" : variable.value,
                            false,
                            variable.value.empty() ? 0.55 : 1.0);
        }

        Border MakeVariableRow(
            VariableRef const& item,
            std::vector<Environ::core::EnvVariable> const& user_variables,
            std::vector<Environ::core::EnvVariable> const& machine_variables,
            std::wstring const& filter)
        {
            auto const& variable{ResolveVariable(item, user_variables, machine_variables)};

            auto row_grid{Grid{}};
            row_grid.ColumnSpacing(12);

            auto scope_column{ColumnDefinition{}};
            scope_column.Width(GridLengthHelper::Auto());
            auto value_column{ColumnDefinition{}};
            value_column.Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
            row_grid.ColumnDefinitions().Append(scope_column);
            row_grid.ColumnDefinitions().Append(value_column);

            auto left_panel{StackPanel{}};
            left_panel.Spacing(6);
            left_panel.Children().Append(MakeScopeBadge(item.scope));

            auto name_text{MakeText(variable.name, true)};
            left_panel.Children().Append(name_text);
            Grid::SetColumn(left_panel, 0);

            auto value_panel{StackPanel{}};
            value_panel.Spacing(6);
            value_panel.Children().Append(MakeValueElement(variable, filter));
            Grid::SetColumn(value_panel, 1);

            row_grid.Children().Append(left_panel);
            row_grid.Children().Append(value_panel);

            auto border{Border{}};
            border.Padding(ThicknessHelper::FromLengths(12, 10, 12, 10));
            border.Margin(ThicknessHelper::FromLengths(0, 0, 0, 8));
            border.CornerRadius(CornerRadiusHelper::FromUniformRadius(6));
            border.Child(row_grid);
            return border;
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
        RebuildRows();
    }

    void MainWindow::RebuildRows()
    {
        auto const all_items{BuildVariableRefs(m_userVariables, m_machineVariables)};

        auto const list{ItemsList()};
        list.Items().Clear();

        std::size_t visible_count{0};
        for (auto const& item : all_items)
        {
            auto const& variable{ResolveVariable(item, m_userVariables, m_machineVariables)};
            if (!MatchesFilter(variable, m_filterText))
            {
                continue;
            }

            list.Items().Append(MakeVariableRow(item, m_userVariables, m_machineVariables, m_filterText));
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
