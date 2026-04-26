#include "pch.h"
#include "HistoryPage.xaml.h"
#if __has_include("HistoryPage.g.cpp")
#include "HistoryPage.g.cpp"
#endif

#include "HistoryEntryViewModel.h"
#include "SnapshotStore.h"
#include "EnvStore.h"
#include "EnvWriter.h"

#include <pnq/unicode.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::Environ::implementation
{
    HistoryPage::HistoryPage()
    {
    }

    Windows::Foundation::Collections::IObservableVector<Windows::Foundation::IInspectable>
    HistoryPage::Snapshots() const
    {
        return m_snapshots;
    }

    void HistoryPage::OnPageLoaded(
        [[maybe_unused]] IInspectable const& sender,
        [[maybe_unused]] RoutedEventArgs const& args)
    {
        LoadSnapshots();
        CheckExternalChanges();
    }

    void HistoryPage::LoadSnapshots()
    {
        m_snapshots.Clear();

        auto snapshots{::Environ::core::snapshot_store().list_snapshots()};

        if (snapshots.empty()) {
            EmptyMessage().Visibility(Visibility::Visible);
            return;
        }
        EmptyMessage().Visibility(Visibility::Collapsed);

        for (auto& info : snapshots) {
            auto vm{make<Environ::implementation::HistoryEntryViewModel>()};
            vm.Id(info.id);
            vm.Timestamp(hstring{pnq::unicode::to_utf16(info.timestamp)});
            vm.Label(hstring{pnq::unicode::to_utf16(info.label)});

            // Scope badge
            if (info.scope_mask == 3) vm.ScopeBadge(L"Both");
            else if (info.scope_mask == 2) vm.ScopeBadge(L"Machine");
            else vm.ScopeBadge(L"User");

            // Load change descriptions
            auto changes{::Environ::core::snapshot_store().describe_snapshot_changes(info.id)};
            std::wstring change_text;
            for (auto& line : changes) {
                if (!change_text.empty()) change_text += L"\n";
                change_text += line;
            }
            vm.Changes(hstring{change_text});

            m_snapshots.Append(vm);
        }
    }

    void HistoryPage::OnContainerContentChanging(
        [[maybe_unused]] ListViewBase const& sender,
        ContainerContentChangingEventArgs const& args)
    {
        if (args.InRecycleQueue()) return;

        auto container{args.ItemContainer()};

        Controls::MenuFlyout flyout;
        flyout.Opening([this](IInspectable const& s, IInspectable const&)
        {
            auto flyout{s.as<Controls::MenuFlyout>()};
            flyout.Items().Clear();

            auto lvi{flyout.Target().as<ListViewItem>()};
            auto vm{lvi.Content().try_as<Environ::HistoryEntryViewModel>()};
            if (!vm) return;

            Controls::MenuFlyoutItem copyItem;
            copyItem.Text(L"Copy to Clipboard");
            FontIcon icon;
            icon.Glyph(L"\uE8C8"); // Copy
            copyItem.Icon(icon);
            copyItem.Click([vm](auto&&, auto&&) {
                std::wstring text;
                text += std::wstring{vm.Timestamp()};
                text += L"  [" + std::wstring{vm.ScopeBadge()} + L"]";
                text += L"  " + std::wstring{vm.Label()} + L"\n";
                auto changes{std::wstring{vm.Changes()}};
                if (!changes.empty()) {
                    text += changes;
                }

                Windows::ApplicationModel::DataTransfer::DataPackage data;
                data.SetText(hstring{text});
                Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(data);
            });
            flyout.Items().Append(copyItem);
        });

        container.ContextFlyout(flyout);
    }

    void HistoryPage::OnSnapshotSelected(
        [[maybe_unused]] IInspectable const& sender,
        [[maybe_unused]] SelectionChangedEventArgs const& args)
    {
        // Enable Restore button when a snapshot is selected
        bool has_selection{args.AddedItems().Size() > 0};
        RestoreButton().IsEnabled(has_selection);
    }

    void HistoryPage::CheckExternalChanges()
    {
        auto snapshots{::Environ::core::snapshot_store().list_snapshots()};
        if (snapshots.empty()) {
            ExternalChangesBar().IsOpen(false);
            return;
        }

        // Load the most recent snapshot
        auto snap_vars{::Environ::core::snapshot_store().load_snapshot(snapshots[0].id)};
        if (snap_vars.empty()) {
            ExternalChangesBar().IsOpen(false);
            return;
        }

        // Convert SnapshotVariable -> EnvVariable, split by scope
        std::vector<::Environ::core::EnvVariable> snap_user;
        std::vector<::Environ::core::EnvVariable> snap_machine;

        for (auto& sv : snap_vars) {
            ::Environ::core::EnvVariable var;
            var.name = sv.name;
            var.value = sv.value;
            var.is_expandable = sv.is_expandable;

            if (sv.value.find(L';') != std::wstring::npos) {
                var.kind = ::Environ::core::EnvVariableKind::PathList;
                std::wstring_view view{sv.value};
                size_t pos{0};
                while (pos < view.size()) {
                    auto semi{view.find(L';', pos)};
                    if (semi == std::wstring_view::npos) semi = view.size();
                    var.segments.emplace_back(view.substr(pos, semi - pos));
                    pos = semi + 1;
                }
            } else {
                var.kind = ::Environ::core::EnvVariableKind::Scalar;
            }

            if (sv.scope == ::Environ::core::Scope::User) {
                snap_user.push_back(std::move(var));
            } else {
                snap_machine.push_back(std::move(var));
            }
        }

        // Read live registry state
        auto current_user{::Environ::core::read_variables(::Environ::core::Scope::User)};
        auto current_machine{::Environ::core::read_variables(::Environ::core::Scope::Machine)};

        // Compute diffs: snapshot is "original", live is "current"
        auto user_changes{::Environ::core::compute_diff(snap_user, current_user)};
        auto machine_changes{::Environ::core::compute_diff(snap_machine, current_machine)};

        if (user_changes.empty() && machine_changes.empty()) {
            ExternalChangesBar().IsOpen(false);
            return;
        }

        // Format the change summary
        std::wstring summary;
        for (auto& c : user_changes) {
            if (!summary.empty()) summary += L"\n";
            summary += L"[User] " + c.describe();
        }
        for (auto& c : machine_changes) {
            if (!summary.empty()) summary += L"\n";
            summary += L"[Machine] " + c.describe();
        }

        ExternalChangesBar().Message(hstring{summary});
        ExternalChangesBar().IsOpen(true);
    }

    fire_and_forget HistoryPage::OnRestoreClick(
        [[maybe_unused]] IInspectable const& sender,
        [[maybe_unused]] RoutedEventArgs const& args)
    {
        auto strong_this{get_strong()};

        auto sel_item{SnapshotList().SelectedItem()};
        if (!sel_item) co_return;

        auto entry_vm{sel_item.as<Environ::HistoryEntryViewModel>()};
        int64_t snapshot_id{entry_vm.Id()};

        // Load snapshot variables
        auto snap_vars{::Environ::core::snapshot_store().load_snapshot(snapshot_id)};
        if (snap_vars.empty()) {
            RestoreResultBar().Title(L"Restore failed");
            RestoreResultBar().Message(L"Could not load snapshot data.");
            RestoreResultBar().Severity(InfoBarSeverity::Error);
            RestoreResultBar().IsOpen(true);
            co_return;
        }

        // Build target variable vectors from snapshot
        std::vector<::Environ::core::EnvVariable> target_user;
        std::vector<::Environ::core::EnvVariable> target_machine;

        for (auto& sv : snap_vars) {
            ::Environ::core::EnvVariable var;
            var.name = sv.name;
            var.value = sv.value;
            var.is_expandable = sv.is_expandable;

            // Detect PathList (value contains semicolons)
            if (sv.value.find(L';') != std::wstring::npos) {
                var.kind = ::Environ::core::EnvVariableKind::PathList;
                // Split into segments
                std::wstring_view view{sv.value};
                size_t pos{0};
                while (pos < view.size()) {
                    auto semi{view.find(L';', pos)};
                    if (semi == std::wstring_view::npos) semi = view.size();
                    var.segments.emplace_back(view.substr(pos, semi - pos));
                    pos = semi + 1;
                }
            } else {
                var.kind = ::Environ::core::EnvVariableKind::Scalar;
            }

            if (sv.scope == ::Environ::core::Scope::User) {
                target_user.push_back(std::move(var));
            } else {
                target_machine.push_back(std::move(var));
            }
        }

        // Read current state from registry
        auto current_user{::Environ::core::read_variables(::Environ::core::Scope::User)};
        auto current_machine{::Environ::core::read_variables(::Environ::core::Scope::Machine)};

        // Compute diffs
        auto user_changes{::Environ::core::compute_diff(current_user, target_user)};
        auto machine_changes{::Environ::core::compute_diff(current_machine, target_machine)};

        if (user_changes.empty() && machine_changes.empty()) {
            RestoreResultBar().Title(L"No changes needed");
            RestoreResultBar().Message(L"The current environment already matches this snapshot.");
            RestoreResultBar().Severity(InfoBarSeverity::Informational);
            RestoreResultBar().IsOpen(true);
            co_return;
        }

        // Build change summary for confirmation
        std::wstring summary;
        if (!user_changes.empty()) {
            summary += L"User:\n";
            for (auto& c : user_changes) {
                summary += L"  \u2022 " + c.describe() + L"\n";
            }
        }
        if (!machine_changes.empty()) {
            if (!summary.empty()) summary += L"\n";
            summary += L"Machine:\n";
            for (auto& c : machine_changes) {
                summary += L"  \u2022 " + c.describe() + L"\n";
            }
        }

        // Confirmation dialog
        ContentDialog dialog;
        dialog.XamlRoot(XamlRoot());
        dialog.Title(box_value(L"Restore Snapshot"));

        auto panel{StackPanel()};
        panel.Spacing(8);

        TextBlock intro;
        intro.Text(L"This will apply the following changes to restore the snapshot:");
        intro.TextWrapping(TextWrapping::Wrap);
        panel.Children().Append(intro);

        TextBlock details;
        details.Text(hstring{summary});
        details.TextWrapping(TextWrapping::Wrap);
        details.FontFamily(Microsoft::UI::Xaml::Media::FontFamily{L"Consolas"});
        panel.Children().Append(details);

        ScrollViewer sv;
        sv.Content(panel);
        sv.MaxHeight(400);
        dialog.Content(sv);

        dialog.PrimaryButtonText(L"Restore");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(ContentDialogButton::Close);

        auto result{co_await dialog.ShowAsync()};
        if (result != ContentDialogResult::Primary) {
            co_return;
        }

        // Snapshot current state before restore
        auto all_changes{user_changes};
        all_changes.insert(all_changes.end(), machine_changes.begin(), machine_changes.end());
        auto label{pnq::unicode::to_utf8(L"Pre-restore: " + ::Environ::core::summarize_changes(all_changes))};
        ::Environ::core::snapshot_store().create_snapshot(label, current_user, current_machine);

        // Apply changes
        bool is_admin{::Environ::core::is_elevated()};
        auto apply_result{::Environ::core::apply_document_changes(
            current_user, target_user, current_machine, target_machine, is_admin)};

        if (apply_result.succeeded()) {
            RestoreResultBar().Title(L"Snapshot restored");
            RestoreResultBar().Message(L"Environment variables have been restored successfully.");
            RestoreResultBar().Severity(InfoBarSeverity::Success);
            RestoreResultBar().IsOpen(true);

            // Reload snapshots to show the new pre-restore snapshot
            LoadSnapshots();
        } else {
            std::wstring errors;
            if (!apply_result.user.succeeded()) {
                errors += L"User: " + apply_result.user.error;
            }
            if (!apply_result.machine.succeeded()) {
                if (!errors.empty()) errors += L"\n";
                errors += L"Machine: " + apply_result.machine.error;
            }
            RestoreResultBar().Title(L"Restore failed");
            RestoreResultBar().Message(hstring{errors});
            RestoreResultBar().Severity(InfoBarSeverity::Error);
            RestoreResultBar().IsOpen(true);
        }
    }
}
