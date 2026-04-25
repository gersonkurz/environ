#pragma once

#include "HistoryEntryViewModel.g.h"

namespace winrt::Environ::implementation
{
    struct HistoryEntryViewModel : HistoryEntryViewModelT<HistoryEntryViewModel>
    {
        HistoryEntryViewModel() = default;

        int64_t Id() const { return m_id; }
        void Id(int64_t value) { m_id = value; }

        hstring Timestamp() const { return m_timestamp; }
        void Timestamp(hstring const& value) { m_timestamp = value; }

        hstring Label() const { return m_label; }
        void Label(hstring const& value) { m_label = value; }

        hstring ScopeBadge() const { return m_scopeBadge; }
        void ScopeBadge(hstring const& value) { m_scopeBadge = value; }

        hstring Changes() const { return m_changes; }
        void Changes(hstring const& value) { m_changes = value; }

    private:
        int64_t m_id{0};
        hstring m_timestamp;
        hstring m_label;
        hstring m_scopeBadge;
        hstring m_changes;
    };
}

namespace winrt::Environ::factory_implementation
{
    struct HistoryEntryViewModel : HistoryEntryViewModelT<HistoryEntryViewModel, implementation::HistoryEntryViewModel>
    {
    };
}
