#include "precomp.h"
#include "HistoryPanel.h"

#include "EnvWriter.h"

#include <winrt/Microsoft.UI.Text.h>

namespace Environ::winui
{

using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Media;

struct HistoryDialogState
{
    Environ::core::SnapshotStore* snapshots{nullptr};
    std::vector<Environ::core::SnapshotInfo> items;
    std::vector<Environ::core::EnvVariable> cur_user;
    std::vector<Environ::core::EnvVariable> cur_machine;
    std::vector<Environ::core::EnvVariable> selected_snap_user;
    std::vector<Environ::core::EnvVariable> selected_snap_machine;
    int64_t selected_id{-1};
    std::function<void(std::vector<Environ::core::EnvVariable> const&,
                       std::vector<Environ::core::EnvVariable> const&)> on_restore;
    ListView list_view{nullptr};
    TextBlock diff_text{nullptr};
    Button restore_btn{nullptr};
    Button delete_btn{nullptr};
};

static void populate_snapshot_list(const std::shared_ptr<HistoryDialogState>& st)
{
    st->items = st->snapshots->list_snapshots();
    st->list_view.Items().Clear();
    st->selected_id = -1;
    st->restore_btn.IsEnabled(false);
    st->delete_btn.IsEnabled(false);

    for (const auto& info : st->items)
    {
        StackPanel row{};
        row.Padding(Thickness{2.0, 4.0, 2.0, 4.0});
        row.Spacing(2.0);

        TextBlock lbl{};
        lbl.Text(pnq::unicode::to_utf16(info.label.empty() ? "(snapshot)" : info.label));
        lbl.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
        lbl.TextTrimming(TextTrimming::CharacterEllipsis);

        const wchar_t* scope_str{(info.scope_mask == 3) ? L"User + Machine"
                                : (info.scope_mask == 2) ? L"Machine" : L"User"};
        TextBlock meta{};
        meta.Text(pnq::unicode::to_utf16(info.timestamp) + L"  \u2022  " + scope_str);
        meta.FontSize(11.0);
        meta.Opacity(0.75);

        row.Children().Append(lbl);
        row.Children().Append(meta);
        st->list_view.Items().Append(row);
    }

    if (!st->items.empty())
    {
        st->list_view.SelectedIndex(0);
    }
    else
    {
        st->diff_text.Text(L"No snapshots recorded yet.\n\nSnapshots are created automatically every time you save changes.");
    }
}

Grid create_history_dialog_content(
    Environ::core::SnapshotStore& snapshots,
    std::vector<Environ::core::EnvVariable> current_user,
    std::vector<Environ::core::EnvVariable> current_machine,
    std::string mono_font,
    std::function<void(std::vector<Environ::core::EnvVariable> const& snap_user,
                       std::vector<Environ::core::EnvVariable> const& snap_machine)> on_restore)
{
    auto st{std::make_shared<HistoryDialogState>()};
    st->snapshots = &snapshots;
    st->cur_user = std::move(current_user);
    st->cur_machine = std::move(current_machine);
    st->on_restore = std::move(on_restore);

    Grid root{};
    root.Width(760.0);
    root.Height(460.0);
    RowDefinition r0{}, r1{};
    r0.Height(GridLengthHelper::Auto());
    r1.Height(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
    root.RowDefinitions().Append(r0);
    root.RowDefinitions().Append(r1);
    root.RowSpacing(8.0);

    StackPanel actions{};
    actions.Orientation(Orientation::Horizontal);
    actions.Spacing(8.0);
    Grid::SetRow(actions, 0);

    Button restore_btn{};
    restore_btn.Content(winrt::box_value(L"Restore Selected Snapshot"));
    restore_btn.IsEnabled(false);

    Button delete_btn{};
    delete_btn.Content(winrt::box_value(L"Delete"));
    delete_btn.IsEnabled(false);

    actions.Children().Append(restore_btn);
    actions.Children().Append(delete_btn);
    root.Children().Append(actions);

    Grid body{};
    body.ColumnSpacing(10.0);
    ColumnDefinition left_col{}, right_col{};
    left_col.Width(GridLengthHelper::FromPixels(260.0));
    right_col.Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
    body.ColumnDefinitions().Append(left_col);
    body.ColumnDefinitions().Append(right_col);
    Grid::SetRow(body, 1);

    ListView lv{};
    lv.SelectionMode(ListViewSelectionMode::Single);
    Grid::SetColumn(lv, 0);

    ScrollViewer sv{};
    sv.HorizontalScrollBarVisibility(ScrollBarVisibility::Auto);
    sv.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    sv.Padding(Thickness{8.0, 6.0, 8.0, 6.0});
    Grid::SetColumn(sv, 1);

    TextBlock diff_tb{};
    diff_tb.FontFamily(FontFamily{pnq::unicode::to_utf16(mono_font.empty() ? "Consolas" : mono_font)});
    diff_tb.FontSize(12.0);
    diff_tb.IsTextSelectionEnabled(true);
    diff_tb.TextWrapping(TextWrapping::NoWrap);
    sv.Content(diff_tb);

    body.Children().Append(lv);
    body.Children().Append(sv);
    root.Children().Append(body);

    st->list_view = lv;
    st->diff_text = diff_tb;
    st->restore_btn = restore_btn;
    st->delete_btn = delete_btn;

    lv.SelectionChanged([st](auto&&, SelectionChangedEventArgs const&)
    {
        const int idx{st->list_view.SelectedIndex()};
        if (idx < 0 || idx >= static_cast<int>(st->items.size()))
        {
            st->selected_id = -1;
            st->restore_btn.IsEnabled(false);
            st->delete_btn.IsEnabled(false);
            return;
        }

        st->selected_id = st->items[static_cast<size_t>(idx)].id;
        auto raw_vars{st->snapshots->load_snapshot(st->selected_id)};
        st->selected_snap_user = Environ::core::reconstruct_variables(raw_vars, Environ::core::Scope::User);
        st->selected_snap_machine = Environ::core::reconstruct_variables(raw_vars, Environ::core::Scope::Machine);

        auto lines{Environ::core::build_diff_table(
            L"Current", L"Snapshot",
            st->cur_user, st->cur_machine,
            st->selected_snap_user, st->selected_snap_machine)};

        if (lines.empty())
        {
            st->diff_text.Text(L"No differences between this snapshot and the current environment.");
        }
        else
        {
            std::wstring joined;
            for (const auto& line : lines)
            {
                joined += line;
                joined += L"\n";
            }
            st->diff_text.Text(joined);
        }

        st->restore_btn.IsEnabled(true);
        st->delete_btn.IsEnabled(true);
    });

    restore_btn.Click([st](auto&&, RoutedEventArgs const&)
    {
        if (st->selected_id >= 0 && st->on_restore)
            st->on_restore(st->selected_snap_user, st->selected_snap_machine);
    });

    delete_btn.Click([st](auto&&, RoutedEventArgs const&)
    {
        if (st->selected_id >= 0)
        {
            st->snapshots->delete_snapshot(st->selected_id);
            populate_snapshot_list(st);
        }
    });

    populate_snapshot_list(st);
    return root;
}

} // namespace Environ::winui
