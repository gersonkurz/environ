#pragma once

#include "pch.h"
#include "../core/EnvStore.h"

#include <optional>

class EnvironmentPage {
public:
    EnvironmentPage();

    winrt::Microsoft::UI::Xaml::UIElement Root() const;
    void Refresh();

private:
    struct SelectedVariable {
        Environ::core::Scope scope;
        std::wstring name;
    };

    winrt::Microsoft::UI::Xaml::Controls::Grid m_root{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ScrollViewer m_scrollViewer{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Border m_selectedRowBorder{nullptr};
    std::vector<Environ::core::EnvVariable> m_userVariables;
    std::vector<Environ::core::EnvVariable> m_machineVariables;
    std::vector<Environ::core::EnvVariable> m_originalUserVariables;
    std::vector<Environ::core::EnvVariable> m_originalMachineVariables;
    std::optional<SelectedVariable> m_selectedVariable;
    bool m_elevated{false};

    void EnsureSelection();
    void BuildList(winrt::Microsoft::UI::Xaml::Controls::Grid const& parent);
    void SelectRow(winrt::Microsoft::UI::Xaml::Controls::Border const& row_border,
                   Environ::core::Scope scope, std::size_t variable_index);
    void WireScrollPassthrough(winrt::Microsoft::UI::Xaml::Controls::TextBox const& text_box);
    void OnSave();
};
