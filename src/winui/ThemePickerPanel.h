#pragma once

#include "Base16Theme.h"

#include <functional>
#include <memory>
#include <string>

namespace Environ::winui
{

struct ThemeDialogContent
{
    winrt::Microsoft::UI::Xaml::Controls::Grid root{nullptr};
    std::shared_ptr<std::string> selected_theme;
};

/// Build compact 2-column Dark / Light Base16 Theme Picker content for a ContentDialog
/// so the live-previewed grid remains visible behind the dialog.
ThemeDialogContent create_theme_dialog_content(
    const Environ::core::Base16ThemeSet& themes,
    const std::string& initial_theme,
    std::function<void(const std::string& theme_name)> on_preview);

} // namespace Environ::winui
