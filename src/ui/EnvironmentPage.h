#pragma once

#include "pch.h"
#include "../core/EnvStore.h"

class EnvironmentPage {
public:
    EnvironmentPage();

    winrt::Microsoft::UI::Xaml::UIElement Root() const;
    void Refresh();

private:
    winrt::Microsoft::UI::Xaml::Controls::Grid m_root{nullptr};

    void BuildPanel(
        Environ::core::Scope scope,
        winrt::Microsoft::UI::Xaml::Controls::Grid const& parent);
};
