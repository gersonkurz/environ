#pragma once

#include "HistoryPage.g.h"

namespace winrt::Environ::implementation
{
    struct HistoryPage : HistoryPageT<HistoryPage>
    {
        HistoryPage();

        Windows::Foundation::Collections::IObservableVector<Windows::Foundation::IInspectable> Snapshots() const;

        void OnPageLoaded(Windows::Foundation::IInspectable const& sender,
                          Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnSnapshotSelected(Windows::Foundation::IInspectable const& sender,
                                Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        fire_and_forget OnRestoreClick(Windows::Foundation::IInspectable const& sender,
                                       Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        Windows::Foundation::Collections::IObservableVector<Windows::Foundation::IInspectable> m_snapshots{
            single_threaded_observable_vector<Windows::Foundation::IInspectable>()
        };

        void LoadSnapshots();
    };
}

namespace winrt::Environ::factory_implementation
{
    struct HistoryPage : HistoryPageT<HistoryPage, implementation::HistoryPage>
    {
    };
}
