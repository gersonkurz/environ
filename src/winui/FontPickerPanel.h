#pragma once

#include <functional>
#include <memory>
#include <string>

namespace Environ::winui
{

struct FontDialogContent
{
    winrt::Microsoft::UI::Xaml::Controls::Grid root{nullptr};
    std::shared_ptr<std::string> selected_font;
};

/// Build compact live-preview Font Picker content for a ContentDialog
/// (DirectWrite system fonts + A-Z type-to-jump + Default button).
FontDialogContent create_font_dialog_content(
    const std::string& initial_font,
    std::function<void(const std::string& font_family)> on_preview);

} // namespace Environ::winui
