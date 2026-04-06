#pragma once

#include "pch.h"
#include "../core/AppSettings.h"

#include <functional>

class SettingsPage {
public:
    using ThemeChangedCallback = std::function<void()>;

    SettingsPage(Environ::core::AppSettings& settings, ThemeChangedCallback on_theme_changed);

    winrt::Microsoft::UI::Xaml::UIElement Root() const;

private:
    winrt::Microsoft::UI::Xaml::Controls::StackPanel m_root{nullptr};
    Environ::core::AppSettings& m_settings;
    ThemeChangedCallback m_onThemeChanged;

    void Build();
};
