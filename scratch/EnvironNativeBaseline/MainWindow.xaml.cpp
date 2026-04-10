#include "pch.h"
#include "MainWindow.xaml.h"
#include "..\\..\\src\\core\\EnvStore.h"

#include <algorithm>
#include <format>

#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::EnvironNativeBaseline::implementation
{
    namespace
    {
        struct VariableRef
        {
            Environ::core::Scope scope;
            Environ::core::EnvVariable const* variable;
        };

        TextBlock MakeText(std::wstring const& text)
        {
            auto block{TextBlock{}};
            block.Text(text);
            block.TextWrapping(TextWrapping::Wrap);
            return block;
        }

        Border MakeVariableRow(VariableRef const& item)
        {
            auto panel{StackPanel{}};
            panel.Spacing(4);

            auto header{StackPanel{}};
            header.Orientation(Orientation::Horizontal);
            header.Spacing(8);

            auto scope{TextBlock{}};
            scope.Text(item.scope == Environ::core::Scope::User ? L"User" : L"Machine");
            scope.Opacity(0.7);
            scope.MinWidth(64);

            auto name{TextBlock{}};
            name.Text(item.variable->name);
            name.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());

            header.Children().Append(scope);
            header.Children().Append(name);
            panel.Children().Append(header);
            panel.Children().Append(MakeText(item.variable->value));

            auto border{Border{}};
            border.Padding(ThicknessHelper::FromLengths(12, 10, 12, 10));
            border.Margin(ThicknessHelper::FromLengths(0, 0, 0, 8));
            border.CornerRadius(CornerRadiusHelper::FromUniformRadius(6));
            border.Child(panel);
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
        auto user_variables{Environ::core::read_variables(Environ::core::Scope::User)};
        auto machine_variables{Environ::core::read_variables(Environ::core::Scope::Machine)};

        std::vector<VariableRef> items;
        items.reserve(user_variables.size() + machine_variables.size());

        for (auto const& variable : user_variables)
        {
            items.push_back(VariableRef{Environ::core::Scope::User, &variable});
        }

        for (auto const& variable : machine_variables)
        {
            items.push_back(VariableRef{Environ::core::Scope::Machine, &variable});
        }

        std::ranges::sort(items, [](VariableRef const& left, VariableRef const& right)
        {
            auto const compare{_wcsicmp(left.variable->name.c_str(), right.variable->name.c_str())};
            if (compare != 0)
            {
                return compare < 0;
            }

            return left.scope == Environ::core::Scope::User &&
                   right.scope == Environ::core::Scope::Machine;
        });

        SummaryText().Text(winrt::hstring{std::format(
            L"{} variables: {} user, {} machine",
            items.size(),
            user_variables.size(),
            machine_variables.size())});

        auto const list{ItemsList()};
        list.Items().Clear();
        for (auto const& item : items)
        {
            list.Items().Append(MakeVariableRow(item));
        }
    }
}
