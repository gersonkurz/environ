#pragma once

#include "MainWindow.g.h"

#include "..\..\src\core\EnvStore.h"
#include "..\..\src\core\SnapshotStore.h"
#include "..\..\src\core\EnvWriter.h"

#include <optional>
#include <unordered_map>

namespace winrt::EnvironNativeBaseline::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        enum class ActivePage
        {
            Environment,
            History,
        };

        MainWindow();
        winrt::Windows::Foundation::IAsyncAction OnApplyButtonClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnEnvironmentNavButtonClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnHistoryButtonClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnHistoryListSelectionChanged(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        winrt::Windows::Foundation::IAsyncAction OnRestoreSnapshotButtonClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnFilterChanged(winrt::Windows::Foundation::IInspectable const& sender,
                             winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);
        void OnDiscardButtonClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
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
        void RefreshHistoryPage();
        void UpdateHistoryDetails();
        void ShowPage(ActivePage page);
        void SelectDisplayRow(DisplayRow const& display_row);
        void BringSelectedRowIntoView();
        void MoveSelectionBy(int delta);
        winrt::Windows::Foundation::IAsyncAction RestoreSnapshotAsync(
            int64_t snapshot_id,
            winrt::hstring const& snapshot_label);
        void RefreshDirtyState();
        void RefreshVariableVisuals(Environ::core::Scope scope, std::size_t variable_index);
        void SetStatus(std::wstring text, bool is_error);
        void UpdateRowEditor(RowVisual const& row_visual, bool is_selected);
        [[nodiscard]] bool HasDirtyState() const;
        [[nodiscard]] bool IsScalarRow(DisplayRow const& display_row) const;
        [[nodiscard]] bool IsVariableDirty(Environ::core::Scope scope, std::size_t variable_index) const;
        [[nodiscard]] bool IsRowDirty(DisplayRow const& display_row) const;
        [[nodiscard]] std::vector<std::wstring> CurrentPathSegments(DisplayRow const& display_row) const;
        [[nodiscard]] std::wstring CurrentScalarValue(DisplayRow const& display_row) const;
        [[nodiscard]] std::wstring CurrentPathSegmentValue(DisplayRow const& display_row) const;
        [[nodiscard]] std::vector<Environ::core::EnvVariable> BuildCurrentVariables(Environ::core::Scope scope) const;
        [[nodiscard]] bool MatchesFilter(DisplayRow const& display_row) const;
        [[nodiscard]] std::vector<DisplayRow> BuildDisplayRows(
            Environ::core::Scope scope,
            std::size_t variable_index) const;
        void ClearScopeDrafts(Environ::core::Scope scope);
        void StoreScalarDraft(DisplayRow const& display_row, std::wstring const& value);
        void RestoreScalarDraft(DisplayRow const& display_row);
        void StorePathSegmentDraft(DisplayRow const& display_row, std::wstring const& value);
        void RestorePathDraft(DisplayRow const& display_row);

        std::vector<Environ::core::EnvVariable> m_userVariables;
        std::vector<Environ::core::EnvVariable> m_machineVariables;
        std::vector<RowVisual> m_rowVisuals;
        std::wstring m_filterText;
        bool m_isElevated{false};
        std::optional<DisplayRow> m_selectedRow;
        ActivePage m_activePage{ActivePage::Environment};
        std::vector<Environ::core::SnapshotInfo> m_historySnapshots;
        std::unordered_map<std::uint64_t, std::wstring> m_scalarDrafts;
        std::unordered_map<std::uint64_t, std::vector<std::wstring>> m_pathDrafts;
        std::wstring m_statusText;
        bool m_statusIsError{false};
        bool m_snapshotStoreAvailable{false};
        Environ::core::SnapshotStore m_snapshotStore;
    };
}

namespace winrt::EnvironNativeBaseline::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
