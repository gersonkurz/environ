#pragma once

#include "MainWindow.g.h"

#include "..\..\src\core\EnvStore.h"

#include <optional>
#include <unordered_map>

namespace winrt::EnvironNativeBaseline::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();
        void OnFilterChanged(winrt::Windows::Foundation::IInspectable const& sender,
                             winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);
        void OnItemsListSelectionChanged(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);

        struct DisplayRow
        {
            Environ::core::Scope scope;
            std::size_t variableIndex;
            std::size_t segmentIndex;
        };

        struct RowVisual
        {
            DisplayRow displayRow;
            winrt::Microsoft::UI::Xaml::Controls::Border rowBorder;
            winrt::Microsoft::UI::Xaml::Controls::Grid rowGrid;
        };

    private:
        void EnsureSelection();
        void LoadVariables();
        void RebuildRows();
        void SelectDisplayRow(DisplayRow const& display_row);
        void BringSelectedRowIntoView();
        void UpdateRowEditor(RowVisual const& row_visual, bool is_selected);
        [[nodiscard]] bool IsScalarRow(DisplayRow const& display_row) const;
        [[nodiscard]] std::wstring CurrentScalarValue(DisplayRow const& display_row) const;
        void StoreScalarDraft(DisplayRow const& display_row, std::wstring const& value);
        void RestoreScalarDraft(DisplayRow const& display_row);

        std::vector<Environ::core::EnvVariable> m_userVariables;
        std::vector<Environ::core::EnvVariable> m_machineVariables;
        std::vector<RowVisual> m_rowVisuals;
        std::wstring m_filterText;
        bool m_isElevated{false};
        std::optional<DisplayRow> m_selectedRow;
        std::unordered_map<std::uint64_t, std::wstring> m_scalarDrafts;
    };
}

namespace winrt::EnvironNativeBaseline::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
