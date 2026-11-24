#pragma once

#include "EnvStore.h"
#include "SnapshotStore.h"

#include <functional>
#include <vector>

namespace Environ::winui
{

/// Build the History browser content for a ContentDialog (snapshot list + monospace diff + Delete / Restore).
winrt::Microsoft::UI::Xaml::Controls::Grid create_history_dialog_content(
    Environ::core::SnapshotStore& snapshots,
    std::vector<Environ::core::EnvVariable> current_user,
    std::vector<Environ::core::EnvVariable> current_machine,
    std::string mono_font,
    std::function<void(std::vector<Environ::core::EnvVariable> const& snap_user,
                       std::vector<Environ::core::EnvVariable> const& snap_machine)> on_restore);

} // namespace Environ::winui
