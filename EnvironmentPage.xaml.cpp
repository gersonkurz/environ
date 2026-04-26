#include "pch.h"
#include "EnvironmentPage.xaml.h"
#if __has_include("EnvironmentPage.g.cpp")
#include "EnvironmentPage.g.cpp"
#endif

#include "EnvVariableViewModel.h"
#include "EnvStore.h"
#include "EnvWriter.h"
#include "SnapshotStore.h"
#include "core/AppSettings.h"
#include "core/VarDescriptions.h"

#include <pnq/unicode.h>

#include <cwctype>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace {

bool contains_ci(std::wstring const& haystack, std::wstring const& needle)
{
    auto it{std::ranges::search(haystack, needle, [](wchar_t a, wchar_t b) {
        return std::towlower(a) == std::towlower(b);
    })};
    return !it.empty();
}

// Segoe Fluent Icons glyphs
constexpr wchar_t const* GLYPH_USER    = L"\uE77B";  // Contact
constexpr wchar_t const* GLYPH_MACHINE = L"\uE7F8";  // PC / DeviceLaptopNoPic

} // namespace

namespace winrt::Environ::implementation
{
    EnvironmentPage::EnvironmentPage()
    {
        m_isAdmin = ::Environ::core::is_elevated();
        LoadVariables();
        RebuildList();
    }

    Windows::Foundation::Collections::IObservableVector<Windows::Foundation::IInspectable>
    EnvironmentPage::Variables() const
    {
        return m_variables;
    }

    void EnvironmentPage::OnPageLoaded(
        [[maybe_unused]] IInspectable const& sender,
        [[maybe_unused]] RoutedEventArgs const& args)
    {
        AdminInfoBar().IsOpen(!m_isAdmin);

        m_zoomPercent = std::clamp(::Environ::core::app_settings().appearance.zoom.get(), 50, 200);
        ApplyZoom();
    }

    void EnvironmentPage::OnFilterChanged(
        [[maybe_unused]] IInspectable const& sender,
        [[maybe_unused]] TextChangedEventArgs const& args)
    {
        RebuildList(std::wstring_view{FilterBox().Text()});
    }

    void EnvironmentPage::OnGridPointerWheelChanged(
        [[maybe_unused]] IInspectable const& sender,
        Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        auto point{args.GetCurrentPoint(nullptr)};
        if (point.Properties().IsHorizontalMouseWheel())
            return;

        using Mods = Windows::System::VirtualKeyModifiers;
        auto mods{static_cast<uint32_t>(args.KeyModifiers())};
        if (!(mods & static_cast<uint32_t>(Mods::Control)))
            return;

        auto delta{point.Properties().MouseWheelDelta()};
        m_zoomPercent = std::clamp(m_zoomPercent + ((delta > 0) ? 10 : -10), 50, 200);

        ApplyZoom();

        ::Environ::core::app_settings().appearance.zoom.set(m_zoomPercent);
        ::Environ::core::app_settings().save();

        args.Handled(true);
    }

    void EnvironmentPage::ApplyZoom()
    {
        Microsoft::UI::Xaml::Media::ScaleTransform transform;
        transform.ScaleX(m_zoomPercent / 100.0);
        transform.ScaleY(m_zoomPercent / 100.0);
        VariableList().RenderTransform(transform);
    }

    void EnvironmentPage::OnRestartAsAdminClick(
        [[maybe_unused]] IInspectable const& sender,
        [[maybe_unused]] RoutedEventArgs const& args)
    {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);

        SHELLEXECUTEINFOW sei{sizeof(sei)};
        sei.lpVerb = L"runas";
        sei.lpFile = path;
        sei.nShow = SW_SHOWNORMAL;

        if (ShellExecuteExW(&sei)) {
            Application::Current().Exit();
        }
    }

    // ─── New Variable ──────────────────────────────────────────────

    void EnvironmentPage::OnNewUserVariable(
        [[maybe_unused]] IInspectable const& sender,
        [[maybe_unused]] RoutedEventArgs const& args)
    {
        InsertNewVariable(false);
    }

    void EnvironmentPage::OnNewMachineVariable(
        [[maybe_unused]] IInspectable const& sender,
        [[maybe_unused]] RoutedEventArgs const& args)
    {
        InsertNewVariable(true);
    }

    void EnvironmentPage::InsertNewVariable(bool is_machine)
    {
        if (is_machine && !m_isAdmin) {
            SaveResultBar().Title(L"Cannot create machine variable");
            SaveResultBar().Message(L"Restart as administrator to edit machine variables.");
            SaveResultBar().Severity(InfoBarSeverity::Warning);
            SaveResultBar().IsOpen(true);
            return;
        }

        auto vm{make<Environ::implementation::EnvVariableViewModel>()};
        vm.Name(L"");
        vm.OriginalName(L"");
        vm.Value(L"");
        vm.OriginalValue(L"");
        vm.Scope(is_machine ? L"Machine" : L"User");
        vm.ScopeGlyph(is_machine ? hstring{GLYPH_MACHINE} : hstring{GLYPH_USER});
        vm.IsReadOnly(false);

        // Insert at current selection or top
        int32_t sel{VariableList().SelectedIndex()};
        uint32_t insert_idx{sel >= 0 ? static_cast<uint32_t>(sel) : 0u};

        m_variables.InsertAt(insert_idx, vm);
        VariableList().SelectedIndex(static_cast<int32_t>(insert_idx));
    }

    // ─── Context Menu ──────────────────────────────────────────────

    void EnvironmentPage::OnContainerContentChanging(
        [[maybe_unused]] ListViewBase const& sender,
        ContainerContentChangingEventArgs const& args)
    {
        if (args.InRecycleQueue()) return;

        auto container{args.ItemContainer()};

        // Build a placeholder MenuFlyout with an Opening handler that populates it
        // just-in-time. Setting ContextFlyout on the ListViewItem overrides any
        // child TextBox context menu.
        Controls::MenuFlyout flyout;
        flyout.Opening([this](IInspectable const& s, IInspectable const&)
        {
            auto flyout{s.as<Controls::MenuFlyout>()};
            flyout.Items().Clear();

            // The flyout's target is the ListViewItem
            auto lvi{flyout.Target().as<ListViewItem>()};
            auto vm{lvi.Content().try_as<Environ::EnvVariableViewModel>()};
            if (!vm) return;

            // Find index
            uint32_t idx{0};
            bool found{false};
            for (uint32_t i{0}; i < m_variables.Size(); ++i) {
                if (m_variables.GetAt(i) == vm) {
                    idx = i;
                    found = true;
                    break;
                }
            }
            if (!found) return;

            VariableList().SelectedIndex(static_cast<int32_t>(idx));

            bool is_segment{vm.IsPathSegment()};
            bool is_readonly{vm.IsReadOnly()};

            if (is_readonly) {
                MenuFlyoutItem restartItem;
                restartItem.Text(L"Restart as Admin to edit");
                restartItem.Icon(FontIcon{});
                restartItem.Icon().as<FontIcon>().Glyph(L"\uE7EF"); // Shield
                restartItem.Click([this](auto&&, auto&&) {
                    OnRestartAsAdminClick(nullptr, nullptr);
                });
                flyout.Items().Append(restartItem);
            } else if (!is_segment) {
                MenuFlyoutItem newItem;
                newItem.Text(L"New Variable Here...");
                newItem.Icon(FontIcon{});
                newItem.Icon().as<FontIcon>().Glyph(L"\uE710");
                newItem.Click([this, idx](auto&&, auto&&) {
                    auto target_vm{m_variables.GetAt(idx).as<Environ::EnvVariableViewModel>()};
                    bool target_machine{target_vm.Scope() == L"Machine"};

                    auto new_vm{make<Environ::implementation::EnvVariableViewModel>()};
                    new_vm.Name(L"");
                    new_vm.OriginalName(L"");
                    new_vm.Value(L"");
                    new_vm.OriginalValue(L"");
                    new_vm.Scope(target_machine ? L"Machine" : L"User");
                    new_vm.ScopeGlyph(target_machine ? hstring{GLYPH_MACHINE} : hstring{GLYPH_USER});
                    new_vm.IsReadOnly(false);
                    m_variables.InsertAt(idx, new_vm);
                    VariableList().SelectedIndex(static_cast<int32_t>(idx));
                });
                flyout.Items().Append(newItem);

                MenuFlyoutItem deleteItem;
                deleteItem.Text(L"Delete Variable");
                deleteItem.Icon(FontIcon{});
                deleteItem.Icon().as<FontIcon>().Glyph(L"\uE74D");
                deleteItem.Click([this, idx](auto&&, auto&&) { DeleteVariable(idx); });
                flyout.Items().Append(deleteItem);

                flyout.Items().Append(MenuFlyoutSeparator{});

                {
                    MenuFlyoutItem addSegItem;
                    addSegItem.Text(L"Add Path Segment");
                    addSegItem.Icon(FontIcon{});
                    addSegItem.Icon().as<FontIcon>().Glyph(L"\uE710");
                    addSegItem.Click([this, idx](auto&&, auto&&) {
                        uint32_t last{idx};
                        for (uint32_t j{idx + 1}; j < m_variables.Size(); ++j) {
                            auto j_vm{m_variables.GetAt(j).as<Environ::EnvVariableViewModel>()};
                            if (!j_vm.IsPathSegment()) break;
                            last = j;
                        }
                        InsertPathSegment(last, true);
                    });
                    flyout.Items().Append(addSegItem);
                }

                flyout.Items().Append(MenuFlyoutSeparator{});

                MenuFlyoutItem browseItem;
                browseItem.Text(L"Browse Folder...");
                browseItem.Icon(FontIcon{});
                browseItem.Icon().as<FontIcon>().Glyph(L"\uE838");
                browseItem.Click([this, idx](auto&&, auto&&) { BrowseFolder(idx); });
                flyout.Items().Append(browseItem);
            } else {
                MenuFlyoutItem insertAbove;
                insertAbove.Text(L"Insert Path Above");
                insertAbove.Icon(FontIcon{});
                insertAbove.Icon().as<FontIcon>().Glyph(L"\uE710");
                insertAbove.Click([this, idx](auto&&, auto&&) { InsertPathSegment(idx, false); });
                flyout.Items().Append(insertAbove);

                MenuFlyoutItem insertBelow;
                insertBelow.Text(L"Insert Path Below");
                insertBelow.Icon(FontIcon{});
                insertBelow.Icon().as<FontIcon>().Glyph(L"\uE710");
                insertBelow.Click([this, idx](auto&&, auto&&) { InsertPathSegment(idx, true); });
                flyout.Items().Append(insertBelow);

                flyout.Items().Append(MenuFlyoutSeparator{});

                MenuFlyoutItem browseItem;
                browseItem.Text(L"Browse Folder...");
                browseItem.Icon(FontIcon{});
                browseItem.Icon().as<FontIcon>().Glyph(L"\uE838");
                browseItem.Click([this, idx](auto&&, auto&&) { BrowseFolder(idx); });
                flyout.Items().Append(browseItem);

                flyout.Items().Append(MenuFlyoutSeparator{});

                MenuFlyoutItem removeItem;
                removeItem.Text(L"Remove Path");
                removeItem.Icon(FontIcon{});
                removeItem.Icon().as<FontIcon>().Glyph(L"\uE74D");
                removeItem.Click([this, idx](auto&&, auto&&) { RemovePathSegment(idx); });
                flyout.Items().Append(removeItem);
            }
        });

        container.ContextFlyout(flyout);

        // Defer validation visuals to Phase 1 (template root is null in Phase 0)
        args.RegisterUpdateCallback([](ListViewBase const&, ContainerContentChangingEventArgs const& e) {
            if (e.InRecycleQueue()) return;
            auto ctr{e.ItemContainer()};
            auto vm{e.Item().try_as<Environ::EnvVariableViewModel>()};
            if (!vm) return;

            auto severity{vm.ValidationSeverity()};

            // Tooltip on the container
            if (severity > 0) {
                ToolTipService::SetToolTip(ctr, box_value(vm.ValidationTooltip()));
            } else {
                ToolTipService::SetToolTip(ctr, nullptr);
            }

            // Color the Value TextBox text (child index 4 in the DataTemplate Grid)
            if (auto grid{ctr.ContentTemplateRoot().try_as<Controls::Grid>()}) {
                if (grid.Children().Size() > 4) {
                    if (auto valueBox{grid.Children().GetAt(4).try_as<Controls::TextBox>()}) {
                        if (severity == 2) {
                            auto brush{Application::Current().Resources()
                                .Lookup(box_value(L"SystemFillColorCriticalBrush"))
                                .as<Microsoft::UI::Xaml::Media::Brush>()};
                            valueBox.Foreground(brush);
                        } else if (severity == 1) {
                            auto brush{Application::Current().Resources()
                                .Lookup(box_value(L"SystemFillColorCautionBrush"))
                                .as<Microsoft::UI::Xaml::Media::Brush>()};
                            valueBox.Foreground(brush);
                        } else {
                            valueBox.ClearValue(Controls::Control::ForegroundProperty());
                        }
                    }
                }
            }
        });
    }

    // ─── CRUD Helpers ──────────────────────────────────────────────

    uint32_t EnvironmentPage::FindNameRowIndex(uint32_t index)
    {
        // Walk backwards to find the row with a non-empty Name
        for (uint32_t i{index}; ; ) {
            auto vm{m_variables.GetAt(i).as<Environ::EnvVariableViewModel>()};
            if (!vm.IsPathSegment()) return i;
            if (i == 0) break;
            --i;
        }
        return index; // shouldn't happen
    }

    void EnvironmentPage::DeleteVariable(uint32_t index)
    {
        uint32_t name_idx{FindNameRowIndex(index)};

        // Count continuation rows
        uint32_t end{name_idx + 1};
        while (end < m_variables.Size()) {
            auto vm{m_variables.GetAt(end).as<Environ::EnvVariableViewModel>()};
            if (!vm.IsPathSegment()) break;
            ++end;
        }

        // Remove from end to start to avoid index shifting
        for (uint32_t i{end}; i > name_idx; ) {
            --i;
            m_variables.RemoveAt(i);
        }
    }

    void EnvironmentPage::InsertPathSegment(uint32_t index, bool below)
    {
        uint32_t name_idx{FindNameRowIndex(index)};
        auto name_vm{m_variables.GetAt(name_idx).as<Environ::EnvVariableViewModel>()};

        auto vm{make<Environ::implementation::EnvVariableViewModel>()};
        vm.Name(L"");
        vm.OriginalName(L"");
        vm.Value(L"");
        vm.OriginalValue(L"");
        vm.Scope(hstring{name_vm.Scope()});
        vm.ScopeGlyph(L"");
        vm.IsReadOnly(false);

        uint32_t insert_at{below ? index + 1 : index};
        m_variables.InsertAt(insert_at, vm);
        VariableList().SelectedIndex(static_cast<int32_t>(insert_at));
    }

    void EnvironmentPage::RemovePathSegment(uint32_t index)
    {
        auto vm{m_variables.GetAt(index).as<Environ::EnvVariableViewModel>()};

        if (vm.IsPathSegment()) {
            // This is a continuation row — just remove it
            m_variables.RemoveAt(index);
        } else {
            // This is the name row of a PathList variable
            // If there's a next continuation row, promote it (move name/scope/glyph to it)
            if (index + 1 < m_variables.Size()) {
                auto next{m_variables.GetAt(index + 1).as<Environ::EnvVariableViewModel>()};
                if (next.IsPathSegment()) {
                    next.Name(vm.Name());
                    next.OriginalName(vm.OriginalName());
                    next.Scope(vm.Scope());
                    next.ScopeGlyph(vm.ScopeGlyph());
                    m_variables.RemoveAt(index);
                    return;
                }
            }
            // Only row for this variable — remove it entirely
            m_variables.RemoveAt(index);
        }
    }

    void EnvironmentPage::BrowseFolder(uint32_t index)
    {
        if (index >= m_variables.Size()) return;
        auto vm{m_variables.GetAt(index).as<Environ::EnvVariableViewModel>()};

        // Resolve the current value, expanding %ENV_VAR% references
        std::wstring current_path{vm.Value()};
        if (!current_path.empty()) {
            wchar_t expanded[MAX_PATH]{};
            if (ExpandEnvironmentStringsW(current_path.c_str(), expanded, MAX_PATH) > 0) {
                current_path = expanded;
            }
        }

        // Use Win32 IFileOpenDialog for folder picking — supports SetFolder
        winrt::com_ptr<IFileOpenDialog> dialog;
        auto hr{CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(dialog.put()))};
        if (FAILED(hr)) return;

        FILEOPENDIALOGOPTIONS options{};
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);

        // Set starting folder to the current value if it's a valid directory
        if (!current_path.empty()) {
            DWORD attrs{GetFileAttributesW(current_path.c_str())};
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                winrt::com_ptr<IShellItem> start_folder;
                if (SUCCEEDED(SHCreateItemFromParsingName(
                        current_path.c_str(), nullptr, IID_PPV_ARGS(start_folder.put())))) {
                    dialog->SetFolder(start_folder.get());
                }
            }
        }

        HWND hwnd{GetActiveWindow()};
        hr = dialog->Show(hwnd);
        if (FAILED(hr)) return; // user cancelled or error

        winrt::com_ptr<IShellItem> result;
        hr = dialog->GetResult(result.put());
        if (FAILED(hr)) return;

        PWSTR path{nullptr};
        hr = result->GetDisplayName(SIGDN_FILESYSPATH, &path);
        if (SUCCEEDED(hr) && path) {
            vm.Value(hstring{path});
            CoTaskMemFree(path);
        }
    }

    // ─── Save ──────────────────────────────────────────────────────

    Microsoft::UI::Xaml::UIElement EnvironmentPage::BuildChangeSummaryContent(
        std::vector<::Environ::core::EnvChange> const& user_changes,
        std::vector<::Environ::core::EnvChange> const& machine_changes)
    {
        auto panel{StackPanel()};
        panel.Spacing(8);
        panel.MinWidth(480);

        auto addScopeSection = [&](std::wstring_view header, std::wstring_view glyph,
                                   std::vector<::Environ::core::EnvChange> const& changes) {
            if (changes.empty()) return;

            // Scope header row
            auto hdrRow{StackPanel()};
            hdrRow.Orientation(Orientation::Horizontal);
            hdrRow.Spacing(8);
            hdrRow.Margin({0, 4, 0, 0});

            FontIcon scopeIcon;
            scopeIcon.Glyph(hstring{glyph});
            scopeIcon.FontSize(16);
            scopeIcon.VerticalAlignment(VerticalAlignment::Center);
            scopeIcon.Opacity(0.8);
            hdrRow.Children().Append(scopeIcon);

            TextBlock hdr;
            hdr.Text(hstring{header});
            hdr.Style(Application::Current()
                .Resources()
                .Lookup(box_value(L"BodyStrongTextBlockStyle"))
                .as<Microsoft::UI::Xaml::Style>());
            hdr.VerticalAlignment(VerticalAlignment::Center);
            hdrRow.Children().Append(hdr);

            panel.Children().Append(hdrRow);

            // Separator under header
            Microsoft::UI::Xaml::Shapes::Rectangle sep;
            sep.Height(1);
            sep.Fill(Application::Current().Resources()
                .Lookup(box_value(L"DividerStrokeColorDefaultBrush"))
                .as<Microsoft::UI::Xaml::Media::Brush>());
            sep.Margin({0, 0, 0, 4});
            panel.Children().Append(sep);

            for (size_t i{0}; i < changes.size(); ++i) {
                auto& c{changes[i]};

                auto card{StackPanel()};
                card.Padding({8, 6, 8, 6});

                // Top row: [Icon | Name | Badge]
                auto topRow{Grid()};

                // Three columns: Auto (icon), * (name), Auto (badge)
                Controls::ColumnDefinition col0;
                col0.Width(GridLengthHelper::FromPixels(24));
                Controls::ColumnDefinition col1;
                col1.Width({1, GridUnitType::Star});
                Controls::ColumnDefinition col2;
                col2.Width(GridLengthHelper::Auto());
                topRow.ColumnDefinitions().Append(col0);
                topRow.ColumnDefinitions().Append(col1);
                topRow.ColumnDefinitions().Append(col2);

                // Icon
                FontIcon icon;
                icon.FontSize(14);
                icon.VerticalAlignment(VerticalAlignment::Center);

                std::wstring_view kindText;
                Windows::UI::Color iconColor;

                switch (c.kind) {
                case ::Environ::core::EnvChange::Kind::Add:
                    icon.Glyph(L"\uE710");
                    kindText = L"ADD";
                    iconColor = Windows::UI::Color{0xFF, 0x0F, 0x7B, 0x0F}; // green
                    break;
                case ::Environ::core::EnvChange::Kind::Modify:
                    icon.Glyph(L"\uE70F");
                    kindText = L"MODIFY";
                    iconColor = Windows::UI::Color{0xFF, 0x00, 0x63, 0xB1}; // accent blue
                    break;
                case ::Environ::core::EnvChange::Kind::Delete:
                    icon.Glyph(L"\uE74D");
                    kindText = L"DELETE";
                    iconColor = Windows::UI::Color{0xFF, 0xC4, 0x2B, 0x1C}; // red
                    break;
                case ::Environ::core::EnvChange::Kind::Rename:
                    icon.Glyph(L"\uE8AB");
                    kindText = L"RENAME";
                    iconColor = Windows::UI::Color{0xFF, 0x00, 0x63, 0xB1}; // accent blue
                    break;
                }

                Microsoft::UI::Xaml::Media::SolidColorBrush iconBrush{iconColor};
                icon.Foreground(iconBrush);
                Grid::SetColumn(icon, 0);
                topRow.Children().Append(icon);

                // Name
                TextBlock nameBlock;
                if (c.kind == ::Environ::core::EnvChange::Kind::Rename) {
                    nameBlock.Text(hstring{c.old_name + L" \u2192 " + c.name});
                } else {
                    nameBlock.Text(hstring{c.name});
                }
                nameBlock.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
                nameBlock.VerticalAlignment(VerticalAlignment::Center);
                nameBlock.Margin({4, 0, 8, 0});
                Grid::SetColumn(nameBlock, 1);
                topRow.Children().Append(nameBlock);

                // Kind badge
                TextBlock badgeText;
                badgeText.Text(hstring{kindText});
                badgeText.FontSize(11);
                badgeText.Foreground(iconBrush);
                badgeText.VerticalAlignment(VerticalAlignment::Center);

                Border badge;
                badge.BorderBrush(iconBrush);
                badge.BorderThickness({1, 1, 1, 1});
                badge.CornerRadius({4, 4, 4, 4});
                badge.Padding({6, 1, 6, 1});
                badge.VerticalAlignment(VerticalAlignment::Center);
                badge.Child(badgeText);
                Grid::SetColumn(badge, 2);
                topRow.Children().Append(badge);

                card.Children().Append(topRow);

                // Value preview for Add/Modify
                if (c.kind == ::Environ::core::EnvChange::Kind::Add) {
                    auto preview{c.value};
                    if (preview.size() > 80) {
                        preview = preview.substr(0, 77) + L"...";
                    }

                    TextBlock valueBlock;
                    valueBlock.Text(hstring{preview});
                    valueBlock.FontSize(12);
                    valueBlock.Opacity(0.6);
                    valueBlock.TextTrimming(TextTrimming::CharacterEllipsis);
                    valueBlock.Margin({28, 0, 0, 0});
                    card.Children().Append(valueBlock);
                }
                else if (c.kind == ::Environ::core::EnvChange::Kind::Modify) {
                    if (!c.segment_changes.empty()) {
                        // Show individual path segment adds/removes
                        for (auto& sc : c.segment_changes) {
                            bool is_add{sc.kind == ::Environ::core::PathSegmentChange::Kind::Add};

                            auto segRow{StackPanel()};
                            segRow.Orientation(Orientation::Horizontal);
                            segRow.Spacing(6);
                            segRow.Margin({28, 1, 0, 1});

                            FontIcon segIcon;
                            segIcon.FontSize(11);
                            segIcon.Glyph(is_add ? L"\uE710" : L"\uE738");
                            auto segColor{is_add
                                ? Windows::UI::Color{0xFF, 0x0F, 0x7B, 0x0F}
                                : Windows::UI::Color{0xFF, 0xC4, 0x2B, 0x1C}};
                            segIcon.Foreground(Microsoft::UI::Xaml::Media::SolidColorBrush{segColor});
                            segIcon.VerticalAlignment(VerticalAlignment::Center);
                            segRow.Children().Append(segIcon);

                            TextBlock segText;
                            segText.Text(hstring{sc.segment});
                            segText.FontSize(12);
                            segText.Opacity(0.8);
                            segText.TextTrimming(TextTrimming::CharacterEllipsis);
                            segText.VerticalAlignment(VerticalAlignment::Center);
                            segRow.Children().Append(segText);

                            card.Children().Append(segRow);
                        }
                    } else {
                        // Non-path variable or pure reorder
                        TextBlock valueBlock;
                        valueBlock.Text(L"(value changed)");
                        valueBlock.FontSize(12);
                        valueBlock.Opacity(0.6);
                        valueBlock.Margin({28, 0, 0, 0});
                        card.Children().Append(valueBlock);
                    }
                }

                panel.Children().Append(card);

                // Separator between change rows (but not after the last one)
                if (i + 1 < changes.size()) {
                    Microsoft::UI::Xaml::Shapes::Rectangle rowSep;
                    rowSep.Height(1);
                    rowSep.Fill(Application::Current().Resources()
                        .Lookup(box_value(L"DividerStrokeColorDefaultBrush"))
                        .as<Microsoft::UI::Xaml::Media::Brush>());
                    rowSep.Margin({28, 0, 0, 0});
                    panel.Children().Append(rowSep);
                }
            }
        };

        addScopeSection(L"User Variables", GLYPH_USER, user_changes);
        addScopeSection(L"Machine Variables", GLYPH_MACHINE, machine_changes);

        ScrollViewer sv;
        sv.Content(panel);
        sv.MaxHeight(400);
        return sv;
    }

    bool EnvironmentPage::HasUnsavedChanges()
    {
        auto [current_user, current_machine]{ReconstructCurrentVars()};
        auto user_changes{::Environ::core::compute_diff(m_userVars, current_user)};
        auto machine_changes{::Environ::core::compute_diff(m_machineVars, current_machine)};
        return !user_changes.empty() || !machine_changes.empty();
    }

    Windows::Foundation::IAsyncOperation<bool> EnvironmentPage::SaveChangesAsync()
    {
        auto strong_this{get_strong()};

        auto [current_user, current_machine]{ReconstructCurrentVars()};

        auto user_changes{::Environ::core::compute_diff(m_userVars, current_user)};
        auto machine_changes{::Environ::core::compute_diff(m_machineVars, current_machine)};

        if (user_changes.empty() && machine_changes.empty()) {
            co_return false;
        }

        // Confirm dialog with improved layout
        ContentDialog dialog;
        dialog.XamlRoot(XamlRoot());
        dialog.Title(box_value(L"Apply Changes"));
        dialog.Content(BuildChangeSummaryContent(user_changes, machine_changes));
        dialog.PrimaryButtonText(L"Apply");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(ContentDialogButton::Primary);

        // Widen the dialog beyond the default 548px
        dialog.Resources().Insert(
            box_value(L"ContentDialogMaxWidth"),
            box_value(600.0));

        auto result{co_await dialog.ShowAsync()};
        if (result != ContentDialogResult::Primary) {
            co_return false;
        }

        // Snapshot pre-save state
        auto all_changes{user_changes};
        all_changes.insert(all_changes.end(), machine_changes.begin(), machine_changes.end());
        auto label{pnq::unicode::to_utf8(::Environ::core::summarize_changes(all_changes))};
        ::Environ::core::snapshot_store().create_snapshot(label, m_userVars, m_machineVars);

        // Apply changes
        auto apply_result{::Environ::core::apply_document_changes(
            m_userVars, current_user, m_machineVars, current_machine, m_isAdmin)};

        if (apply_result.succeeded()) {
            SaveResultBar().Title(L"Changes saved");
            SaveResultBar().Message(L"Environment variables updated successfully.");
            SaveResultBar().Severity(InfoBarSeverity::Success);
            SaveResultBar().IsOpen(true);

            // Reload from registry
            LoadVariables();
            auto filter{std::wstring{FilterBox().Text()}};
            RebuildList(filter);
            co_return true;
        } else {
            std::wstring errors;
            if (!apply_result.user.succeeded()) {
                errors += L"User: " + apply_result.user.error;
            }
            if (!apply_result.machine.succeeded()) {
                if (!errors.empty()) errors += L"\n";
                errors += L"Machine: " + apply_result.machine.error;
            }
            SaveResultBar().Title(L"Error saving changes");
            SaveResultBar().Message(hstring{errors});
            SaveResultBar().Severity(InfoBarSeverity::Error);
            SaveResultBar().IsOpen(true);
            co_return false;
        }
    }

    fire_and_forget EnvironmentPage::OnSaveClick(
        [[maybe_unused]] IInspectable const& sender,
        [[maybe_unused]] RoutedEventArgs const& args)
    {
        auto strong_this{get_strong()};

        if (!HasUnsavedChanges()) {
            SaveResultBar().Title(L"No changes");
            SaveResultBar().Message(L"There are no unsaved changes.");
            SaveResultBar().Severity(InfoBarSeverity::Informational);
            SaveResultBar().IsOpen(true);
            co_return;
        }

        co_await SaveChangesAsync();
    }

    void EnvironmentPage::OnDiscardClick(
        [[maybe_unused]] IInspectable const& sender,
        [[maybe_unused]] RoutedEventArgs const& args)
    {
        LoadVariables();
        auto filter{std::wstring{FilterBox().Text()}};
        RebuildList(filter);

        SaveResultBar().IsOpen(false);
    }

    void EnvironmentPage::LoadVariables()
    {
        m_userVars = ::Environ::core::read_variables(::Environ::core::Scope::User);
        m_machineVars = ::Environ::core::read_variables(::Environ::core::Scope::Machine);
    }

    EnvironmentPage::VarPair EnvironmentPage::ReconstructCurrentVars()
    {
        std::vector<::Environ::core::EnvVariable> user_vars;
        std::vector<::Environ::core::EnvVariable> machine_vars;
        ::Environ::core::EnvVariable* current{nullptr};

        for (uint32_t i{0}; i < m_variables.Size(); ++i) {
            auto vm{m_variables.GetAt(i).as<Environ::EnvVariableViewModel>()};
            auto name{std::wstring{vm.Name()}};
            auto value{std::wstring{vm.Value()}};

            if (!name.empty()) {
                // New variable
                bool is_machine{vm.Scope() == L"Machine"};
                auto& target{is_machine ? machine_vars : user_vars};
                auto& originals{is_machine ? m_machineVars : m_userVars};

                // Look up is_expandable from originals
                bool expandable{false};
                auto orig_name{std::wstring{vm.OriginalName()}};
                for (auto& orig : originals) {
                    if (_wcsicmp(orig.name.c_str(), orig_name.c_str()) == 0) {
                        expandable = orig.is_expandable;
                        break;
                    }
                }

                target.push_back(::Environ::core::EnvVariable{
                    .name{std::move(name)},
                    .value{std::move(value)},
                    .kind{::Environ::core::EnvVariableKind::Scalar},
                    .is_expandable{expandable},
                });

                // Track rename
                if (vm.Name() != vm.OriginalName() && !vm.OriginalName().empty()) {
                    target.back().original_name = std::wstring{vm.OriginalName()};
                }

                current = &target.back();
            } else if (current) {
                // PathList continuation
                if (current->kind == ::Environ::core::EnvVariableKind::Scalar) {
                    // First continuation: convert to PathList, first row value is first segment
                    current->kind = ::Environ::core::EnvVariableKind::PathList;
                    current->segments.push_back(current->value);
                }
                current->segments.push_back(std::move(value));
            }
        }

        // Rejoin PathList segments into value
        for (auto* vars : {&user_vars, &machine_vars}) {
            for (auto& var : *vars) {
                if (var.kind == ::Environ::core::EnvVariableKind::PathList) {
                    var.value.clear();
                    for (size_t j{0}; j < var.segments.size(); ++j) {
                        if (j > 0) var.value += L';';
                        var.value += var.segments[j];
                    }
                }
            }
        }

        return {std::move(user_vars), std::move(machine_vars)};
    }

    void EnvironmentPage::ValidatePathSegments(uint32_t startIndex, uint32_t count)
    {
        // Expand all segment values once
        struct SegInfo {
            std::wstring expanded;
            uint32_t index;
        };
        std::vector<SegInfo> segments;
        segments.reserve(count);

        for (uint32_t i{startIndex}; i < startIndex + count; ++i) {
            auto vm{m_variables.GetAt(i).as<Environ::EnvVariableViewModel>()};
            std::wstring raw{vm.Value()};

            // Expand environment variables
            std::wstring expanded;
            if (!raw.empty()) {
                wchar_t buf[MAX_PATH]{};
                DWORD len{ExpandEnvironmentStringsW(raw.c_str(), buf, MAX_PATH)};
                if (len > 0 && len <= MAX_PATH) {
                    expanded = buf;
                } else {
                    expanded = raw;
                }
            }
            segments.push_back({std::move(expanded), i});
        }

        // Validate each segment
        for (size_t s{0}; s < segments.size(); ++s) {
            auto vm{m_variables.GetAt(segments[s].index).as<Environ::EnvVariableViewModel>()};
            auto& path{segments[s].expanded};

            if (path.empty()) {
                vm.ValidationSeverity(0);
                vm.ValidationTooltip(L"");
                continue;
            }

            // Check existence
            DWORD attrs{GetFileAttributesW(path.c_str())};
            if (attrs == INVALID_FILE_ATTRIBUTES) {
                vm.ValidationSeverity(2); // error
                vm.ValidationTooltip(L"Folder does not exist");
                continue;
            }

            // Check for case-insensitive duplicate (earlier entry wins)
            bool is_duplicate{false};
            for (size_t d{0}; d < s; ++d) {
                if (_wcsicmp(segments[d].expanded.c_str(), path.c_str()) == 0) {
                    is_duplicate = true;
                    break;
                }
            }
            if (is_duplicate) {
                vm.ValidationSeverity(1); // warning
                vm.ValidationTooltip(L"Duplicate path entry");
            } else {
                vm.ValidationSeverity(0);
                vm.ValidationTooltip(L"");
            }
        }
    }

    void EnvironmentPage::RebuildList(std::wstring_view filter)
    {
        m_variables.Clear();

        auto needle{std::wstring{filter}};

        // Build a combined sorted view across both scopes
        struct VarRef {
            ::Environ::core::EnvVariable* var;
            ::Environ::core::Scope scope;
        };
        std::vector<VarRef> all;
        all.reserve(m_userVars.size() + m_machineVars.size());
        for (auto& v : m_userVars) all.push_back({&v, ::Environ::core::Scope::User});
        for (auto& v : m_machineVars) all.push_back({&v, ::Environ::core::Scope::Machine});
        std::ranges::stable_sort(all, [](const VarRef& a, const VarRef& b) {
            return _wcsicmp(a.var->name.c_str(), b.var->name.c_str()) < 0;
        });

        for (auto& ref : all) {
            auto& var{*ref.var};
            bool is_machine{ref.scope == ::Environ::core::Scope::Machine};
            bool read_only{is_machine && !m_isAdmin};
            auto glyph{is_machine ? GLYPH_MACHINE : GLYPH_USER};
            auto scope_label{is_machine ? L"Machine" : L"User"};

            if (!needle.empty() && !contains_ci(var.name, needle) &&
                !contains_ci(var.value, needle)) {
                continue;
            }

            // Look up description for this variable
            auto desc{::Environ::core::var_descriptions().find(var.name)};

            if (var.kind == ::Environ::core::EnvVariableKind::PathList) {
                bool name_matches{needle.empty() || contains_ci(var.name, needle)};
                bool first{true};
                uint32_t seg_start{m_variables.Size()};
                for (auto& seg : var.segments) {
                    if (!name_matches && !contains_ci(seg, needle)) {
                        continue;
                    }
                    auto vm{make<Environ::implementation::EnvVariableViewModel>()};
                    if (first) {
                        vm.OriginalName(hstring{var.name});
                        vm.Name(hstring{var.name});
                        vm.Scope(hstring{scope_label});
                        vm.ScopeGlyph(hstring{glyph});
                        if (desc) vm.Description(hstring{*desc});
                        first = false;
                    }
                    vm.IsReadOnly(read_only);
                    vm.OriginalValue(hstring{seg});
                    vm.Value(hstring{seg});
                    m_variables.Append(vm);
                }
                uint32_t seg_count{m_variables.Size() - seg_start};
                if (seg_count > 0) {
                    ValidatePathSegments(seg_start, seg_count);
                }
            } else {
                auto vm{make<Environ::implementation::EnvVariableViewModel>()};
                vm.OriginalName(hstring{var.name});
                vm.Name(hstring{var.name});
                vm.Scope(hstring{scope_label});
                vm.ScopeGlyph(hstring{glyph});
                vm.IsReadOnly(read_only);
                vm.OriginalValue(hstring{var.value});
                vm.Value(hstring{var.value});
                if (desc) vm.Description(hstring{*desc});
                m_variables.Append(vm);
            }
        }
    }
}
