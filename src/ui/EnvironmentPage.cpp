#include "EnvironmentPage.h"

#include <format>

using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Media;

winrt::Microsoft::UI::Xaml::UIElement EnvironmentPage::Root() const {
    return m_root;
}

void EnvironmentPage::Refresh() {
    if (!m_root) {
        m_root = Grid{};
        m_root.Padding(ThicknessHelper::FromLengths(24, 24, 24, 24));
        m_root.RowSpacing(16);
    }
    m_root.Children().Clear();
    m_root.RowDefinitions().Clear();

    // Two rows: User (top half) and Machine (bottom half)
    auto userRow{RowDefinition{}};
    userRow.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    auto machineRow{RowDefinition{}};
    machineRow.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    m_root.RowDefinitions().Append(userRow);
    m_root.RowDefinitions().Append(machineRow);

    auto userPanel{StackPanel{}};
    BuildPanel(Environ::core::Scope::User, userPanel);
    Grid::SetRow(userPanel, 0);
    m_root.Children().Append(userPanel);

    auto machinePanel{StackPanel{}};
    BuildPanel(Environ::core::Scope::Machine, machinePanel);
    Grid::SetRow(machinePanel, 1);
    m_root.Children().Append(machinePanel);
}

void EnvironmentPage::BuildPanel(
    Environ::core::Scope scope,
    StackPanel const& parent) {

    auto vars{Environ::core::read_variables(scope)};

    // Header row: "User Variables  [count]"
    auto headerPanel{StackPanel{}};
    headerPanel.Orientation(Orientation::Horizontal);
    headerPanel.Spacing(8);
    headerPanel.Margin(ThicknessHelper::FromLengths(0, 0, 0, 8));

    auto headerText{TextBlock{}};
    headerText.Text(scope == Environ::core::Scope::User
                        ? L"User Variables"
                        : L"Machine Variables");
    headerText.FontSize(18);
    headerText.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());

    auto countBadge{TextBlock{}};
    countBadge.Text(std::to_wstring(vars.size()));
    countBadge.FontSize(12);
    countBadge.VerticalAlignment(VerticalAlignment::Center);
    countBadge.Padding(ThicknessHelper::FromLengths(8, 2, 8, 2));
    countBadge.Foreground(SolidColorBrush{winrt::Windows::UI::ColorHelper::FromArgb(255, 150, 150, 150)});

    headerPanel.Children().Append(headerText);
    headerPanel.Children().Append(countBadge);
    parent.Children().Append(headerPanel);

    // ScrollViewer with variable rows
    auto scrollViewer{ScrollViewer{}};
    scrollViewer.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    scrollViewer.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);

    auto rowsPanel{StackPanel{}};
    rowsPanel.Spacing(2);

    for (const auto& var : vars) {
        auto row{Grid{}};
        row.Padding(ThicknessHelper::FromLengths(4, 4, 4, 4));
        row.ColumnSpacing(16);

        // Two columns: name (200px fixed) and value (remaining)
        auto nameCol{ColumnDefinition{}};
        nameCol.Width(GridLengthHelper::FromPixels(200));
        auto valueCol{ColumnDefinition{}};
        valueCol.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        row.ColumnDefinitions().Append(nameCol);
        row.ColumnDefinitions().Append(valueCol);

        auto nameBlock{TextBlock{}};
        nameBlock.Text(var.name);
        nameBlock.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        nameBlock.TextTrimming(TextTrimming::CharacterEllipsis);
        Grid::SetColumn(nameBlock, 0);

        auto valueBlock{TextBlock{}};
        valueBlock.Text(var.value);
        valueBlock.TextTrimming(TextTrimming::CharacterEllipsis);
        valueBlock.Opacity(0.8);
        Grid::SetColumn(valueBlock, 1);

        row.Children().Append(nameBlock);
        row.Children().Append(valueBlock);
        rowsPanel.Children().Append(row);
    }

    scrollViewer.Content(rowsPanel);
    parent.Children().Append(scrollViewer);
}
