#pragma once

#include "pch.h"
#include "../core/SnapshotStore.h"

class HistoryPage {
public:
    HistoryPage(Environ::core::SnapshotStore& store, std::function<void()> on_restore);

    winrt::Microsoft::UI::Xaml::UIElement Root() const;
    void Refresh();

private:
    winrt::Microsoft::UI::Xaml::Controls::Grid m_root{nullptr};
    Environ::core::SnapshotStore& m_snapshotStore;
    std::function<void()> m_onRestore;

    void Build();
};
