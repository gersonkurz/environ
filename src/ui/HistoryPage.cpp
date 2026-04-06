#include "HistoryPage.h"
#include "../core/EnvWriter.h"

using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Media;

namespace {

Brush ThemeBrush(winrt::hstring const& key) {
    return Application::Current().Resources()
        .Lookup(winrt::box_value(key))
        .as<Brush>();
}

} // namespace

HistoryPage::HistoryPage(Environ::core::SnapshotStore& store, std::function<void()> on_restore)
    : m_snapshotStore{store}
    , m_onRestore{std::move(on_restore)}
{
    m_root = Grid{};
    m_root.Padding(ThicknessHelper::FromLengths(24, 24, 24, 24));
    Refresh();
}

winrt::Microsoft::UI::Xaml::UIElement HistoryPage::Root() const {
    return m_root;
}

void HistoryPage::Refresh() {
    m_root.Children().Clear();
    m_root.RowDefinitions().Clear();
    m_root.ColumnDefinitions().Clear();
    Build();
}

void HistoryPage::Build() {
    auto snapshots{m_snapshotStore.list_snapshots()};

    // Layout: title row, then two columns (snapshot list | change details)
    auto title_row{RowDefinition{}};
    title_row.Height(GridLengthHelper::Auto());
    auto content_row{RowDefinition{}};
    content_row.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    m_root.RowDefinitions().Append(title_row);
    m_root.RowDefinitions().Append(content_row);

    // Title
    auto title{TextBlock{}};
    title.Text(L"Snapshot History");
    title.FontSize(20);
    title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    title.Margin(ThicknessHelper::FromLengths(0, 0, 0, 16));
    Grid::SetRow(title, 0);
    m_root.Children().Append(title);

    if (snapshots.empty()) {
        auto empty_text{TextBlock{}};
        empty_text.Text(L"No snapshots yet. Snapshots are created automatically when you save.");
        empty_text.Opacity(0.6);
        empty_text.TextWrapping(TextWrapping::Wrap);
        Grid::SetRow(empty_text, 1);
        m_root.Children().Append(empty_text);
        return;
    }

    // Two-column content
    auto content_grid{Grid{}};
    auto list_col{ColumnDefinition{}};
    list_col.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    auto detail_col{ColumnDefinition{}};
    detail_col.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    content_grid.ColumnDefinitions().Append(list_col);
    content_grid.ColumnDefinitions().Append(detail_col);
    content_grid.ColumnSpacing(16);
    Grid::SetRow(content_grid, 1);
    m_root.Children().Append(content_grid);

    // --- Left: snapshot list ---
    auto list_panel{StackPanel{}};
    list_panel.Spacing(2);

    // Detail panel (right side) — updated when a snapshot is clicked
    auto detail_outer{StackPanel{}};
    detail_outer.Spacing(8);

    auto detail_title{TextBlock{}};
    detail_title.Text(L"Select a snapshot to see changes");
    detail_title.FontSize(14);
    detail_title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    detail_title.Opacity(0.6);
    detail_title.FontStyle(winrt::Windows::UI::Text::FontStyle::Italic);
    detail_outer.Children().Append(detail_title);

    auto detail_changes{StackPanel{}};
    detail_changes.Spacing(2);
    detail_outer.Children().Append(detail_changes);

    auto detail_restore_btn{Button{}};
    detail_restore_btn.Content(winrt::box_value(L"Restore this snapshot"));
    detail_restore_btn.Visibility(Visibility::Collapsed);
    detail_outer.Children().Append(detail_restore_btn);

    auto detail_scroll{ScrollViewer{}};
    detail_scroll.Content(detail_outer);
    Grid::SetColumn(detail_scroll, 1);
    content_grid.Children().Append(detail_scroll);

    // Track the currently selected list row for highlight
    auto selected_border{std::make_shared<Border>(nullptr)};

    for (std::size_t i{0}; i < snapshots.size(); ++i) {
        const auto& snap{snapshots[i]};

        auto row_border{Border{}};
        row_border.Padding(ThicknessHelper::FromLengths(8, 6, 8, 6));
        row_border.CornerRadius(CornerRadiusHelper::FromUniformRadius(4));

        auto row_panel{StackPanel{}};
        row_panel.Spacing(2);

        auto label_text{TextBlock{}};
        label_text.Text(winrt::to_hstring(snap.label));
        label_text.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        label_text.TextTrimming(TextTrimming::CharacterEllipsis);

        auto ts_text{TextBlock{}};
        ts_text.Text(winrt::to_hstring(snap.timestamp));
        ts_text.FontSize(11);
        ts_text.Opacity(0.6);

        row_panel.Children().Append(label_text);
        row_panel.Children().Append(ts_text);
        row_border.Child(row_panel);

        row_border.Tapped([this, selected_border, row_border,
                           detail_title, detail_changes, detail_restore_btn,
                           snapshot_id = snap.id, label = snap.label](
                              [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
                              [[maybe_unused]] winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& args) {
            // Deselect previous
            if (*selected_border) {
                selected_border->Background(nullptr);
                selected_border->BorderThickness(ThicknessHelper::FromUniformLength(0));
            }

            // Select this row
            *selected_border = row_border;
            row_border.BorderThickness(ThicknessHelper::FromLengths(2, 0, 0, 0));
            row_border.BorderBrush(ThemeBrush(L"AccentFillColorDefaultBrush"));

            // Update detail panel
            detail_title.Text(winrt::to_hstring(label));
            detail_title.Opacity(1.0);
            detail_title.FontStyle(winrt::Windows::UI::Text::FontStyle::Normal);

            detail_changes.Children().Clear();
            auto changes{m_snapshotStore.describe_snapshot_changes(snapshot_id)};
            for (const auto& desc : changes) {
                auto line{TextBlock{}};
                line.Text(desc);
                line.FontSize(12);
                line.TextWrapping(TextWrapping::Wrap);
                detail_changes.Children().Append(line);
            }

            detail_restore_btn.Visibility(Visibility::Visible);
        });

        // Hover
        row_border.PointerEntered([selected_border](winrt::Windows::Foundation::IInspectable const& sender,
                                                     [[maybe_unused]] winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
            auto border{sender.as<Border>()};
            if (*selected_border != border) {
                border.Background(ThemeBrush(L"SubtleFillColorTertiaryBrush"));
            }
        });
        row_border.PointerExited([selected_border](winrt::Windows::Foundation::IInspectable const& sender,
                                                    [[maybe_unused]] winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
            auto border{sender.as<Border>()};
            if (*selected_border != border) {
                border.Background(nullptr);
            }
        });

        list_panel.Children().Append(row_border);
    }

    // Wire restore button — captures the snapshot list to find selected snapshot
    detail_restore_btn.Click([this, snapshots, selected_border, list_panel](
                                 [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
                                 [[maybe_unused]] RoutedEventArgs const& args) {
        if (!*selected_border) return;

        // Find which snapshot is selected by matching the border in the list
        uint32_t selected_idx{0};
        if (!list_panel.Children().IndexOf(*selected_border, selected_idx)) return;
        if (selected_idx >= snapshots.size()) return;

        auto snapshot_id{snapshots[selected_idx].id};

        auto confirm{ContentDialog{}};
        confirm.Title(winrt::box_value(L"Restore Snapshot"));
        auto msg{TextBlock{}};
        msg.Text(std::format(L"Restore environment to \"{}\"?",
                             winrt::to_hstring(snapshots[selected_idx].label)));
        msg.TextWrapping(TextWrapping::Wrap);
        confirm.Content(msg);
        confirm.PrimaryButtonText(L"Restore");
        confirm.CloseButtonText(L"Cancel");
        confirm.XamlRoot(m_root.XamlRoot());

        confirm.PrimaryButtonClick([this, snapshot_id](
                                       [[maybe_unused]] ContentDialog const& s,
                                       [[maybe_unused]] ContentDialogButtonClickEventArgs const& a) {
            auto snap_vars{m_snapshotStore.load_snapshot(snapshot_id)};

            auto fresh_user{Environ::core::read_variables(Environ::core::Scope::User)};
            auto fresh_machine{Environ::core::read_variables(Environ::core::Scope::Machine)};
            if (!m_snapshotStore.matches_latest_snapshot(fresh_user, fresh_machine)) {
                m_snapshotStore.create_snapshot("Auto (pre-restore)", fresh_user, fresh_machine);
            }

            std::vector<Environ::core::EnvVariable> snap_user;
            std::vector<Environ::core::EnvVariable> snap_machine;
            for (const auto& sv : snap_vars) {
                Environ::core::EnvVariable var{
                    .name{sv.name},
                    .value{sv.value},
                    .is_expandable{sv.is_expandable},
                };
                if (sv.scope == Environ::core::Scope::User) {
                    snap_user.push_back(std::move(var));
                } else {
                    snap_machine.push_back(std::move(var));
                }
            }

            auto user_changes{Environ::core::compute_diff(fresh_user, snap_user)};
            std::wstring errors;
            if (!user_changes.empty()) {
                errors += Environ::core::apply_changes(Environ::core::Scope::User, user_changes);
            }

            // Machine restore only if elevated
            if (Environ::core::is_elevated()) {
                auto machine_changes{Environ::core::compute_diff(fresh_machine, snap_machine)};
                if (!machine_changes.empty()) {
                    auto err{Environ::core::apply_changes(Environ::core::Scope::Machine, machine_changes)};
                    if (!err.empty()) {
                        if (!errors.empty()) errors += L"\n";
                        errors += err;
                    }
                }
            }

            Environ::core::broadcast_environment_change();

            if (!errors.empty()) {
                auto err_dlg{ContentDialog{}};
                err_dlg.Title(winrt::box_value(L"Restore Errors"));
                auto err_text{TextBlock{}};
                err_text.Text(errors);
                err_text.TextWrapping(TextWrapping::Wrap);
                err_dlg.Content(err_text);
                err_dlg.CloseButtonText(L"OK");
                err_dlg.XamlRoot(m_root.XamlRoot());
                err_dlg.ShowAsync();
            }

            if (m_onRestore) m_onRestore();
            Refresh();
        });

        confirm.ShowAsync();
    });

    auto list_scroll{ScrollViewer{}};
    list_scroll.Content(list_panel);
    Grid::SetColumn(list_scroll, 0);
    content_grid.Children().Append(list_scroll);
}
