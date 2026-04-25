#pragma once

#include "EnvironmentPage.g.h"
#include "EnvStore.h"
#include "EnvWriter.h"

namespace winrt::Environ::implementation
{
    struct EnvironmentPage : EnvironmentPageT<EnvironmentPage>
    {
        EnvironmentPage();

        Windows::Foundation::Collections::IObservableVector<Windows::Foundation::IInspectable> Variables() const;
        bool HasUnsavedChanges();
        Windows::Foundation::IAsyncOperation<bool> SaveChangesAsync();

        void OnPageLoaded(Windows::Foundation::IInspectable const& sender,
                          Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnFilterChanged(Windows::Foundation::IInspectable const& sender,
                             Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);
        void OnRestartAsAdminClick(Windows::Foundation::IInspectable const& sender,
                                   Microsoft::UI::Xaml::RoutedEventArgs const& args);
        fire_and_forget OnSaveClick(Windows::Foundation::IInspectable const& sender,
                                    Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnDiscardClick(Windows::Foundation::IInspectable const& sender,
                            Microsoft::UI::Xaml::RoutedEventArgs const& args);

        // CRUD handlers
        void OnNewUserVariable(Windows::Foundation::IInspectable const& sender,
                               Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnNewMachineVariable(Windows::Foundation::IInspectable const& sender,
                                  Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnContainerContentChanging(Microsoft::UI::Xaml::Controls::ListViewBase const& sender,
                                       Microsoft::UI::Xaml::Controls::ContainerContentChangingEventArgs const& args);
        void OnGridPointerWheelChanged(Windows::Foundation::IInspectable const& sender,
                                      Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);

    private:
        Windows::Foundation::Collections::IObservableVector<Windows::Foundation::IInspectable> m_variables{
            single_threaded_observable_vector<Windows::Foundation::IInspectable>()
        };

        std::vector<::Environ::core::EnvVariable> m_userVars;
        std::vector<::Environ::core::EnvVariable> m_machineVars;
        bool m_isAdmin{false};
        int32_t m_zoomPercent{100};

        void ApplyZoom();
        void LoadVariables();
        void RebuildList(std::wstring_view filter = L"");

        using VarPair = std::pair<std::vector<::Environ::core::EnvVariable>,
                                  std::vector<::Environ::core::EnvVariable>>;
        VarPair ReconstructCurrentVars();

        // CRUD helpers
        void InsertNewVariable(bool is_machine);
        void DeleteVariable(uint32_t index);
        void InsertPathSegment(uint32_t index, bool below);
        void RemovePathSegment(uint32_t index);
        void BrowseFolder(uint32_t index);

        // Find the "name row" index for a given item index (walks back to find non-empty Name)
        uint32_t FindNameRowIndex(uint32_t index);

        // Validate path segments for duplicates and missing folders
        void ValidatePathSegments(uint32_t startIndex, uint32_t count);

        // Build improved save dialog content
        Microsoft::UI::Xaml::UIElement BuildChangeSummaryContent(
            std::vector<::Environ::core::EnvChange> const& user_changes,
            std::vector<::Environ::core::EnvChange> const& machine_changes);
    };
}

namespace winrt::Environ::factory_implementation
{
    struct EnvironmentPage : EnvironmentPageT<EnvironmentPage, implementation::EnvironmentPage>
    {
    };
}
