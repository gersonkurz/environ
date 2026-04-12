#include "pch.h"
#include "MainWindow.xaml.h"

#include <algorithm>
#include <format>

#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;

namespace winrt::EnvironNativeBaseline::implementation
{
    namespace
    {
        constexpr double kNameColumnWidth{220.0};
        constexpr double kScopeColumnWidth{90.0};

        struct VariableRef
        {
            Environ::core::Scope scope;
            std::size_t index;
        };

        struct VisibleRow
        {
            Environ::core::Scope scope;
            std::size_t variableIndex;
            std::size_t segmentIndex;
        };

        bool ContainsCaseInsensitive(std::wstring const& haystack, std::wstring const& needle)
        {
            auto const it{std::ranges::search(haystack, needle, [](wchar_t left, wchar_t right)
            {
                return ::towlower(left) == ::towlower(right);
            })};
            return !it.empty();
        }

        bool NameMatchesFilter(Environ::core::EnvVariable const& variable, std::wstring const& filter)
        {
            return !filter.empty() && ContainsCaseInsensitive(variable.name, filter);
        }

        std::uint64_t MakeDraftKey(Environ::core::Scope const scope, std::size_t const index)
        {
            auto const scope_value{scope == Environ::core::Scope::User ? 0ULL : 1ULL};
            return (scope_value << 63) | static_cast<std::uint64_t>(index);
        }

        VariableRef MakeVariableRef(Environ::core::Scope const scope, std::size_t const index)
        {
            return VariableRef{scope, index};
        }

        Environ::core::EnvVariable const& ResolveVariable(
            VariableRef const& item,
            std::vector<Environ::core::EnvVariable> const& user_variables,
            std::vector<Environ::core::EnvVariable> const& machine_variables)
        {
            return item.scope == Environ::core::Scope::User
                ? user_variables[item.index]
                : machine_variables[item.index];
        }

        std::vector<VariableRef> BuildVariableRefs(
            std::vector<Environ::core::EnvVariable> const& user_variables,
            std::vector<Environ::core::EnvVariable> const& machine_variables)
        {
            std::vector<VariableRef> items;
            items.reserve(user_variables.size() + machine_variables.size());

            for (std::size_t i{0}; i < user_variables.size(); ++i)
            {
                items.push_back(MakeVariableRef(Environ::core::Scope::User, i));
            }

            for (std::size_t i{0}; i < machine_variables.size(); ++i)
            {
                items.push_back(MakeVariableRef(Environ::core::Scope::Machine, i));
            }

            std::ranges::sort(items, [&user_variables, &machine_variables](
                VariableRef const& left,
                VariableRef const& right)
            {
                auto const& left_variable{ResolveVariable(left, user_variables, machine_variables)};
                auto const& right_variable{ResolveVariable(right, user_variables, machine_variables)};
                auto const compare{_wcsicmp(left_variable.name.c_str(), right_variable.name.c_str())};
                if (compare != 0)
                {
                    return compare < 0;
                }

                return left.scope == Environ::core::Scope::User &&
                       right.scope == Environ::core::Scope::Machine;
            });

            return items;
        }

        TextBlock MakeText(
            std::wstring const& text,
            bool const prominent = false,
            double const opacity = 1.0)
        {
            auto block{TextBlock{}};
            block.Text(text);
            block.TextWrapping(TextWrapping::Wrap);
            block.Opacity(opacity);
            if (prominent)
            {
                block.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
            }
            return block;
        }

        Brush ThemeBrush(std::wstring const& key)
        {
            return Application::Current().Resources()
                .Lookup(winrt::box_value(winrt::hstring{key}))
                .as<Brush>();
        }

        winrt::Windows::UI::Color ThemeColor(std::wstring const& key)
        {
            return ThemeBrush(key).as<SolidColorBrush>().Color();
        }

        winrt::Windows::UI::Color BlendColor(
            winrt::Windows::UI::Color const& base_color,
            winrt::Windows::UI::Color const& overlay_color,
            double const overlay_weight)
        {
            auto const clamped_weight{std::clamp(overlay_weight, 0.0, 1.0)};
            auto const base_weight{1.0 - clamped_weight};

            return winrt::Windows::UI::ColorHelper::FromArgb(
                255,
                static_cast<std::uint8_t>(base_color.R * base_weight + overlay_color.R * clamped_weight),
                static_cast<std::uint8_t>(base_color.G * base_weight + overlay_color.G * clamped_weight),
                static_cast<std::uint8_t>(base_color.B * base_weight + overlay_color.B * clamped_weight));
        }

        Brush SelectionBackgroundBrush()
        {
            auto const requested_theme{Application::Current().RequestedTheme()};
            auto const base_color{ThemeColor(L"ControlFillColorSecondaryBrush")};
            auto const accent_color{ThemeColor(L"AccentFillColorDefaultBrush")};
            auto const overlay_weight{
                requested_theme == ApplicationTheme::Dark ? 0.22 : 0.12};

            return SolidColorBrush{BlendColor(base_color, accent_color, overlay_weight)};
        }

        Brush DirtyBackgroundBrush()
        {
            auto const requested_theme{Application::Current().RequestedTheme()};
            auto const base_color{ThemeColor(L"ControlFillColorSecondaryBrush")};
            auto const accent_color{ThemeColor(L"AccentFillColorDefaultBrush")};
            auto const overlay_weight{
                requested_theme == ApplicationTheme::Dark ? 0.14 : 0.08};

            return SolidColorBrush{BlendColor(base_color, accent_color, overlay_weight)};
        }

        std::wstring BuildApplyConfirmationText(
            std::vector<Environ::core::EnvChange> const& user_changes,
            std::vector<Environ::core::EnvChange> const& machine_changes,
            bool const is_elevated)
        {
            std::wstring text;

            auto append_scope = [&text](std::wstring_view title,
                                        std::vector<Environ::core::EnvChange> const& changes)
            {
                if (changes.empty())
                {
                    return;
                }

                if (!text.empty())
                {
                    text += L"\n\n";
                }

                text += std::wstring{title};
                text += L"\n";
                text += Environ::core::summarize_changes(changes);

                auto const preview_count{std::min<std::size_t>(changes.size(), 6)};
                for (std::size_t i{0}; i < preview_count; ++i)
                {
                    text += L"\n";
                    text += L"• ";
                    text += changes[i].describe();
                }

                if (changes.size() > preview_count)
                {
                    text += std::format(L"\n• … {} more change(s)", changes.size() - preview_count);
                }
            };

            append_scope(L"User variables", user_changes);
            append_scope(L"Machine variables", machine_changes);

            if (!machine_changes.empty() && !is_elevated)
            {
                text += L"\n\nMachine changes require administrator privileges and will fail in the current session.";
            }

            return text;
        }

        TextBlock MakeScopeText(Environ::core::Scope const scope)
        {
            auto text{TextBlock{}};
            text.Text(scope == Environ::core::Scope::User ? L"User" : L"Machine");
            text.Opacity(0.78);
            text.VerticalAlignment(VerticalAlignment::Top);
            return text;
        }

        Grid MakeRowGrid()
        {
            auto row_grid{Grid{}};
            row_grid.ColumnSpacing(12);
            row_grid.VerticalAlignment(VerticalAlignment::Top);

            auto name_column{ColumnDefinition{}};
            name_column.Width(GridLengthHelper::FromPixels(kNameColumnWidth));
            auto scope_column{ColumnDefinition{}};
            scope_column.Width(GridLengthHelper::FromPixels(kScopeColumnWidth));
            auto value_column{ColumnDefinition{}};
            value_column.Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
            row_grid.ColumnDefinitions().Append(name_column);
            row_grid.ColumnDefinitions().Append(scope_column);
            row_grid.ColumnDefinitions().Append(value_column);

            return row_grid;
        }

        Border MakeRowSeparator()
        {
            auto separator{Border{}};
            separator.Height(1);
            separator.Margin(ThicknessHelper::FromLengths(0, 6, 0, 0));
            separator.Background(ThemeBrush(L"ControlStrokeColorSecondaryBrush"));
            return separator;
        }

        void ApplySelectionVisual(
            Border const& row_border,
            bool const is_selected,
            bool const is_dirty,
            bool const is_elevated,
            Environ::core::Scope const scope)
        {
            row_border.ClearValue(Border::BorderThicknessProperty());
            row_border.ClearValue(Border::BorderBrushProperty());

            if (is_selected)
            {
                row_border.Background(SelectionBackgroundBrush());
            }
            else if (is_dirty)
            {
                row_border.Background(DirtyBackgroundBrush());
            }
            else if (!is_elevated && scope == Environ::core::Scope::Machine)
            {
                row_border.Background(ThemeBrush(L"ControlFillColorSecondaryBrush"));
            }
            else
            {
                row_border.Background(SolidColorBrush{
                    winrt::Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)});
            }

            auto const container{row_border.Child().try_as<StackPanel>()};
            if (!container)
            {
                return;
            }

            for (auto const& child : container.Children())
            {
                auto const content_border{child.try_as<Border>()};
                if (!content_border || !content_border.Child())
                {
                    continue;
                }

                if (is_selected)
                {
                    content_border.ClearValue(Border::BackgroundProperty());
                }
                else if (!is_elevated && scope == Environ::core::Scope::Machine)
                {
                    content_border.ClearValue(Border::BackgroundProperty());
                }
                else
                {
                    content_border.ClearValue(Border::BackgroundProperty());
                }
            }
        }

        MainWindow::RowVisual MakeDisplayRow(
            VisibleRow const& display_row,
            std::vector<Environ::core::EnvVariable> const& user_variables,
            std::vector<Environ::core::EnvVariable> const& machine_variables,
            bool const is_elevated,
            bool const is_selected,
            bool const is_first_row,
            std::wstring const& value_text,
            bool const empty_value)
        {
            auto const variable_ref{VariableRef{display_row.scope, display_row.variableIndex}};
            auto const& variable{ResolveVariable(variable_ref, user_variables, machine_variables)};

            auto row_grid{MakeRowGrid()};

            if (is_first_row)
            {
                auto name_text{MakeText(variable.name, true)};
                name_text.TextTrimming(TextTrimming::CharacterEllipsis);
                name_text.VerticalAlignment(VerticalAlignment::Top);
                Grid::SetColumn(name_text, 0);
                row_grid.Children().Append(name_text);

                auto scope_text{MakeScopeText(display_row.scope)};
                Grid::SetColumn(scope_text, 1);
                row_grid.Children().Append(scope_text);
            }

            auto value_text_block{MakeText(value_text, false, empty_value ? 0.55 : 1.0)};
            value_text_block.VerticalAlignment(VerticalAlignment::Top);
            Grid::SetColumn(value_text_block, 2);
            row_grid.Children().Append(value_text_block);

            auto container{StackPanel{}};
            container.Spacing(0);

            auto content_border{Border{}};
            content_border.Padding(ThicknessHelper::FromLengths(0, 6, 0, 6));
            content_border.Child(row_grid);

            container.Children().Append(content_border);
            container.Children().Append(MakeRowSeparator());

            auto row_border{Border{}};
            row_border.Padding(ThicknessHelper::FromLengths(8, 0, 8, 0));
            row_border.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush{
                winrt::Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)});
            row_border.Child(container);
            ApplySelectionVisual(row_border, is_selected, false, is_elevated, display_row.scope);
            return MainWindow::RowVisual{
                .displayRow = MainWindow::DisplayRow{
                    .scope = display_row.scope,
                    .variableIndex = display_row.variableIndex,
                    .segmentIndex = display_row.segmentIndex,
                },
                .rowBorder = row_border,
                .rowGrid = row_grid,
            };
        }
    }

    MainWindow::MainWindow()
    {
        InitializeComponent();
        LoadVariables();
    }

    void MainWindow::LoadVariables()
    {
        m_userVariables = Environ::core::read_variables(Environ::core::Scope::User);
        m_machineVariables = Environ::core::read_variables(Environ::core::Scope::Machine);
        m_isElevated = Environ::core::is_elevated();
        EnsureSelection();
        RebuildRows();
    }

    void MainWindow::EnsureSelection()
    {
        auto const all_items{BuildVariableRefs(m_userVariables, m_machineVariables)};
        std::vector<DisplayRow> visible_rows;
        for (auto const& item : all_items)
        {
            auto rows{BuildDisplayRows(item.scope, item.index)};
            visible_rows.insert(visible_rows.end(), rows.begin(), rows.end());
        }

        auto selection_is_visible = [this, &visible_rows]()
        {
            if (!m_selectedRow.has_value())
            {
                return false;
            }

            return std::ranges::any_of(visible_rows, [this](DisplayRow const& row)
            {
                return row.scope == m_selectedRow->scope &&
                       row.variableIndex == m_selectedRow->variableIndex &&
                       row.segmentIndex == m_selectedRow->segmentIndex;
            });
        };

        if (selection_is_visible())
        {
            return;
        }

        if (!visible_rows.empty())
        {
            auto const& first_row{visible_rows.front()};
            m_selectedRow = DisplayRow{
                .scope = first_row.scope,
                .variableIndex = first_row.variableIndex,
                .segmentIndex = first_row.segmentIndex,
            };
            return;
        }

        m_selectedRow.reset();
    }

    void MainWindow::RebuildRows()
    {
        auto const all_items{BuildVariableRefs(m_userVariables, m_machineVariables)};
        EnsureSelection();

        auto const list{ItemsList()};
        auto const previous_selected_index{list.SelectedIndex()};
        list.Items().Clear();
        list.Padding(ThicknessHelper::FromLengths(0, 4, 0, 0));
        m_rowVisuals.clear();

        std::size_t visible_count{0};
        for (auto const& item : all_items)
        {
            auto rows{BuildDisplayRows(item.scope, item.index)};
            if (rows.empty())
            {
                continue;
            }

            for (std::size_t row_index{0}; row_index < rows.size(); ++row_index)
            {
                auto const& display_row{rows[row_index]};
                auto const is_selected{
                    m_selectedRow.has_value() &&
                    m_selectedRow->scope == display_row.scope &&
                    m_selectedRow->variableIndex == display_row.variableIndex &&
                    m_selectedRow->segmentIndex == display_row.segmentIndex};

                auto row_visual{MakeDisplayRow(
                    VisibleRow{
                        .scope = display_row.scope,
                        .variableIndex = display_row.variableIndex,
                        .segmentIndex = display_row.segmentIndex,
                    },
                    m_userVariables,
                    m_machineVariables,
                    m_isElevated,
                    is_selected,
                    row_index == 0,
                    IsScalarRow(display_row) ? CurrentScalarValue(display_row) : CurrentPathSegmentValue(display_row),
                    IsScalarRow(display_row)
                        ? CurrentScalarValue(display_row).empty()
                        : CurrentPathSegmentValue(display_row).empty())};
                ApplySelectionVisual(
                    row_visual.rowBorder,
                    is_selected,
                    IsRowDirty(display_row),
                    m_isElevated,
                    display_row.scope);
                auto const visible_index{static_cast<int>(m_rowVisuals.size())};
                row_visual.rowBorder.PointerPressed([this, visible_index](IInspectable const& sender, Input::PointerRoutedEventArgs const& args)
                {
                    auto const source{sender.as<UIElement>()};
                    auto const point{args.GetCurrentPoint(source)};                    
                    if (!point.Properties().IsLeftButtonPressed())
                    {
                        return;
                    }

                    ItemsList().SelectedIndex(visible_index);
                });
                UpdateRowEditor(row_visual, is_selected);
                m_rowVisuals.push_back(row_visual);
                list.Items().Append(row_visual.rowBorder);
            }
            ++visible_count;
        }

        if (m_selectedRow.has_value())
        {
            auto const it{std::ranges::find_if(m_rowVisuals, [this](RowVisual const& row)
            {
                return row.displayRow.scope == m_selectedRow->scope &&
                       row.displayRow.variableIndex == m_selectedRow->variableIndex &&
                       row.displayRow.segmentIndex == m_selectedRow->segmentIndex;
            })};
            if (it != m_rowVisuals.end())
            {
                auto const selected_index{static_cast<int>(std::distance(m_rowVisuals.begin(), it))};
                if (selected_index != previous_selected_index)
                {
                    list.SelectedIndex(selected_index);
                }
            }
            else
            {
                list.SelectedIndex(-1);
            }
        }
        else
        {
            list.SelectedIndex(-1);
        }

        SummaryText().Text(winrt::hstring{std::format(
            L"{} shown of {} variables: {} user, {} machine",
            visible_count,
            all_items.size(),
            m_userVariables.size(),
            m_machineVariables.size())});
        RefreshDirtyState();
    }

    void MainWindow::SelectDisplayRow(DisplayRow const& display_row)
    {
        if (m_selectedRow.has_value() &&
            m_selectedRow->scope == display_row.scope &&
            m_selectedRow->variableIndex == display_row.variableIndex &&
            m_selectedRow->segmentIndex == display_row.segmentIndex)
        {
            return;
        }

        auto update_row = [this](DisplayRow const& selected, bool const is_selected)
        {
            auto const it{std::ranges::find_if(m_rowVisuals, [&selected](RowVisual const& row)
            {
                return row.displayRow.scope == selected.scope &&
                       row.displayRow.variableIndex == selected.variableIndex &&
                       row.displayRow.segmentIndex == selected.segmentIndex;
            })};
            if (it == m_rowVisuals.end())
            {
                return;
            }

            ApplySelectionVisual(
                it->rowBorder,
                is_selected,
                IsRowDirty(it->displayRow),
                m_isElevated,
                it->displayRow.scope);
            UpdateRowEditor(*it, is_selected);
        };

        if (m_selectedRow.has_value())
        {
            update_row(*m_selectedRow, false);
        }

        m_selectedRow = display_row;
        update_row(*m_selectedRow, true);
        BringSelectedRowIntoView();
    }

    void MainWindow::BringSelectedRowIntoView()
    {
        if (!m_selectedRow.has_value())
        {
            return;
        }

        auto const it{std::ranges::find_if(m_rowVisuals, [this](RowVisual const& row)
        {
            return row.displayRow.scope == m_selectedRow->scope &&
                   row.displayRow.variableIndex == m_selectedRow->variableIndex &&
                   row.displayRow.segmentIndex == m_selectedRow->segmentIndex;
        })};
        if (it == m_rowVisuals.end())
        {
            return;
        }

        it->rowBorder.StartBringIntoView();
    }

    void MainWindow::MoveSelectionBy(int const delta)
    {
        auto const list{ItemsList()};
        auto const current_index{list.SelectedIndex()};
        if (current_index < 0 || m_rowVisuals.empty())
        {
            return;
        }

        auto const next_index{std::clamp(
            current_index + delta,
            0,
            static_cast<int>(m_rowVisuals.size()) - 1)};
        if (next_index == current_index)
        {
            return;
        }

        list.SelectedIndex(next_index);
    }

    void MainWindow::RefreshDirtyState()
    {
        auto const has_dirty_state{HasDirtyState()};
        ApplyButton().IsEnabled(has_dirty_state);
        DiscardButton().IsEnabled(has_dirty_state);

        if (!m_statusText.empty())
        {
            DirtyStateText().Text(winrt::hstring{m_statusText});
            DirtyStateText().Foreground(ThemeBrush(
                m_statusIsError ? L"SystemFillColorCriticalBrush" : L"AccentTextFillColorPrimaryBrush"));
            return;
        }

        DirtyStateText().Text(has_dirty_state ? winrt::hstring{L"Unsaved changes"} : winrt::hstring{});
        DirtyStateText().Foreground(ThemeBrush(L"AccentTextFillColorPrimaryBrush"));
    }

    void MainWindow::RefreshVariableVisuals(
        Environ::core::Scope const scope,
        std::size_t const variable_index)
    {
        for (auto const& row_visual : m_rowVisuals)
        {
            if (row_visual.displayRow.scope != scope ||
                row_visual.displayRow.variableIndex != variable_index)
            {
                continue;
            }

            auto const is_selected{
                m_selectedRow.has_value() &&
                m_selectedRow->scope == row_visual.displayRow.scope &&
                m_selectedRow->variableIndex == row_visual.displayRow.variableIndex &&
                m_selectedRow->segmentIndex == row_visual.displayRow.segmentIndex};
            ApplySelectionVisual(
                row_visual.rowBorder,
                is_selected,
                IsRowDirty(row_visual.displayRow),
                m_isElevated,
                row_visual.displayRow.scope);
        }

        RefreshDirtyState();
    }

    void MainWindow::SetStatus(std::wstring text, bool const is_error)
    {
        m_statusText = std::move(text);
        m_statusIsError = is_error;
        RefreshDirtyState();
    }

    void MainWindow::OnFilterChanged(
        IInspectable const& sender,
        Controls::TextChangedEventArgs const&)
    {
        m_filterText = sender.as<TextBox>().Text().c_str();
        RebuildRows();
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::OnApplyButtonClick(
        IInspectable const&,
        RoutedEventArgs const&)
    {
        auto const current_user{BuildCurrentVariables(Environ::core::Scope::User)};
        auto const current_machine{BuildCurrentVariables(Environ::core::Scope::Machine)};
        auto const user_changes{Environ::core::compute_diff(m_userVariables, current_user)};
        auto const machine_changes{Environ::core::compute_diff(m_machineVariables, current_machine)};

        if (user_changes.empty() && machine_changes.empty())
        {
            SetStatus({}, false);
            co_return;
        }

        Controls::ContentDialog dialog;
        dialog.XamlRoot(this->Content().as<FrameworkElement>().XamlRoot());
        dialog.Title(box_value(L"Apply changes?"));
        dialog.PrimaryButtonText(L"Apply");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(Controls::ContentDialogButton::Primary);

        Controls::ScrollViewer scroll_viewer;
        scroll_viewer.MaxHeight(420);

        TextBlock content_text;
        content_text.Text(winrt::hstring{BuildApplyConfirmationText(
            user_changes,
            machine_changes,
            m_isElevated)});
        content_text.TextWrapping(TextWrapping::Wrap);
        scroll_viewer.Content(content_text);
        dialog.Content(scroll_viewer);

        auto const dialog_result{co_await dialog.ShowAsync()};
        if (dialog_result != Controls::ContentDialogResult::Primary)
        {
            co_return;
        }

        auto const result{Environ::core::apply_document_changes(
            m_userVariables,
            current_user,
            m_machineVariables,
            current_machine,
            m_isElevated)};

        if (result.user.succeeded() && result.user.has_changes())
        {
            m_userVariables = current_user;
            ClearScopeDrafts(Environ::core::Scope::User);
        }

        if (result.machine.succeeded() && result.machine.has_changes())
        {
            m_machineVariables = current_machine;
            ClearScopeDrafts(Environ::core::Scope::Machine);
        }

        if (!result.machine.succeeded())
        {
            SetStatus(result.machine.error, true);
        }
        else if (!result.user.succeeded())
        {
            SetStatus(result.user.error, true);
        }
        else if (result.has_changes())
        {
            SetStatus(L"Changes applied.", false);
        }
        else
        {
            SetStatus({}, false);
        }

        RebuildRows();
        co_return;
    }

    void MainWindow::OnDiscardButtonClick(
        IInspectable const&,
        RoutedEventArgs const&)
    {
        if (!HasDirtyState())
        {
            return;
        }

        m_scalarDrafts.clear();
        m_pathDrafts.clear();
        SetStatus({}, false);
        RebuildRows();
    }

    void MainWindow::OnItemsListSelectionChanged(
        IInspectable const& sender,
        Controls::SelectionChangedEventArgs const&)
    {
        auto const list{sender.as<ListView>()};
        auto const selected_index{list.SelectedIndex()};
        if (selected_index < 0 || selected_index >= static_cast<int>(m_rowVisuals.size()))
        {
            return;
        }

        auto const& row{m_rowVisuals[static_cast<std::size_t>(selected_index)]};
        SelectDisplayRow(row.displayRow);
    }

    bool MainWindow::HasDirtyState() const
    {
        return !m_scalarDrafts.empty() || !m_pathDrafts.empty();
    }

    bool MainWindow::IsScalarRow(DisplayRow const& display_row) const
    {
        auto const variable_ref{VariableRef{display_row.scope, display_row.variableIndex}};
        auto const& variable{ResolveVariable(variable_ref, m_userVariables, m_machineVariables)};
        return variable.kind != Environ::core::EnvVariableKind::PathList;
    }

    bool MainWindow::IsVariableDirty(
        Environ::core::Scope const scope,
        std::size_t const variable_index) const
    {
        auto const key{MakeDraftKey(scope, variable_index)};
        return m_scalarDrafts.contains(key) || m_pathDrafts.contains(key);
    }

    bool MainWindow::IsRowDirty(DisplayRow const& display_row) const
    {
        return IsVariableDirty(display_row.scope, display_row.variableIndex);
    }

    std::vector<std::wstring> MainWindow::CurrentPathSegments(DisplayRow const& display_row) const
    {
        auto const key{MakeDraftKey(display_row.scope, display_row.variableIndex)};
        if (auto const it{m_pathDrafts.find(key)}; it != m_pathDrafts.end())
        {
            return it->second;
        }

        auto const variable_ref{VariableRef{display_row.scope, display_row.variableIndex}};
        auto const& variable{ResolveVariable(variable_ref, m_userVariables, m_machineVariables)};
        return variable.segments;
    }

    std::wstring MainWindow::CurrentScalarValue(DisplayRow const& display_row) const
    {
        auto const key{MakeDraftKey(display_row.scope, display_row.variableIndex)};
        if (auto const it{m_scalarDrafts.find(key)}; it != m_scalarDrafts.end())
        {
            return it->second;
        }

        auto const variable_ref{VariableRef{display_row.scope, display_row.variableIndex}};
        auto const& variable{ResolveVariable(variable_ref, m_userVariables, m_machineVariables)};
        return variable.value;
    }

    std::wstring MainWindow::CurrentPathSegmentValue(DisplayRow const& display_row) const
    {
        auto const segments{CurrentPathSegments(display_row)};
        if (segments.empty())
        {
            return {};
        }

        if (display_row.segmentIndex >= segments.size())
        {
            return {};
        }

        return segments[display_row.segmentIndex];
    }

    std::vector<Environ::core::EnvVariable> MainWindow::BuildCurrentVariables(
        Environ::core::Scope const scope) const
    {
        auto variables{
            scope == Environ::core::Scope::User ? m_userVariables : m_machineVariables};

        for (std::size_t i{0}; i < variables.size(); ++i)
        {
            auto const key{MakeDraftKey(scope, i)};

            if (auto const scalar_it{m_scalarDrafts.find(key)}; scalar_it != m_scalarDrafts.end())
            {
                variables[i].value = scalar_it->second;
            }

            if (auto const path_it{m_pathDrafts.find(key)}; path_it != m_pathDrafts.end())
            {
                variables[i].segments = path_it->second;
                std::wstring joined_value;
                for (std::size_t segment_index{0}; segment_index < path_it->second.size(); ++segment_index)
                {
                    if (segment_index > 0)
                    {
                        joined_value += L";";
                    }

                    joined_value += path_it->second[segment_index];
                }
                variables[i].value = std::move(joined_value);
            }
        }

        return variables;
    }

    bool MainWindow::MatchesFilter(DisplayRow const& display_row) const
    {
        auto const variable_ref{VariableRef{display_row.scope, display_row.variableIndex}};
        auto const& variable{ResolveVariable(variable_ref, m_userVariables, m_machineVariables)};
        if (m_filterText.empty())
        {
            return true;
        }

        if (NameMatchesFilter(variable, m_filterText))
        {
            return true;
        }

        if (IsScalarRow(display_row))
        {
            return ContainsCaseInsensitive(CurrentScalarValue(display_row), m_filterText);
        }

        return ContainsCaseInsensitive(CurrentPathSegmentValue(display_row), m_filterText);
    }

    std::vector<MainWindow::DisplayRow> MainWindow::BuildDisplayRows(
        Environ::core::Scope const scope,
        std::size_t const variable_index) const
    {
        std::vector<DisplayRow> rows;
        auto const display_row{DisplayRow{
            .scope = scope,
            .variableIndex = variable_index,
            .segmentIndex = 0,
        }};
        auto const variable_ref{VariableRef{scope, variable_index}};
        auto const& variable{ResolveVariable(variable_ref, m_userVariables, m_machineVariables)};

        if (variable.kind != Environ::core::EnvVariableKind::PathList)
        {
            if (MatchesFilter(display_row))
            {
                rows.push_back(display_row);
            }
            return rows;
        }

        auto const show_all_segments{m_filterText.empty() || NameMatchesFilter(variable, m_filterText)};
        auto const segments{CurrentPathSegments(display_row)};
        if (segments.empty())
        {
            if (m_filterText.empty())
            {
                rows.push_back(display_row);
            }
            return rows;
        }

        rows.reserve(segments.size());
        for (std::size_t i{0}; i < segments.size(); ++i)
        {
            auto const segment_row{DisplayRow{
                .scope = scope,
                .variableIndex = variable_index,
                .segmentIndex = i,
            }};
            if (!show_all_segments && !MatchesFilter(segment_row))
            {
                continue;
            }

            rows.push_back(segment_row);
        }

        return rows;
    }

    void MainWindow::ClearScopeDrafts(Environ::core::Scope const scope)
    {
        auto const scope_is_user{scope == Environ::core::Scope::User};

        for (auto it{m_scalarDrafts.begin()}; it != m_scalarDrafts.end();)
        {
            auto const key_is_user{(it->first >> 63) == 0};
            if (key_is_user == scope_is_user)
            {
                it = m_scalarDrafts.erase(it);
            }
            else
            {
                ++it;
            }
        }

        for (auto it{m_pathDrafts.begin()}; it != m_pathDrafts.end();)
        {
            auto const key_is_user{(it->first >> 63) == 0};
            if (key_is_user == scope_is_user)
            {
                it = m_pathDrafts.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void MainWindow::StoreScalarDraft(DisplayRow const& display_row, std::wstring const& value)
    {
        auto const variable_ref{VariableRef{display_row.scope, display_row.variableIndex}};
        auto const& variable{ResolveVariable(variable_ref, m_userVariables, m_machineVariables)};
        auto const key{MakeDraftKey(display_row.scope, display_row.variableIndex)};
        m_statusText.clear();
        m_statusIsError = false;

        if (value == variable.value)
        {
            m_scalarDrafts.erase(key);
            return;
        }

        m_scalarDrafts[key] = value;
    }

    void MainWindow::RestoreScalarDraft(DisplayRow const& display_row)
    {
        m_scalarDrafts.erase(MakeDraftKey(display_row.scope, display_row.variableIndex));
    }

    void MainWindow::StorePathSegmentDraft(DisplayRow const& display_row, std::wstring const& value)
    {
        auto const variable_ref{VariableRef{display_row.scope, display_row.variableIndex}};
        auto const& variable{ResolveVariable(variable_ref, m_userVariables, m_machineVariables)};
        auto segments{CurrentPathSegments(display_row)};
        m_statusText.clear();
        m_statusIsError = false;

        if (display_row.segmentIndex >= segments.size())
        {
            return;
        }

        segments[display_row.segmentIndex] = value;

        auto const key{MakeDraftKey(display_row.scope, display_row.variableIndex)};
        if (segments == variable.segments)
        {
            m_pathDrafts.erase(key);
            return;
        }

        m_pathDrafts[key] = std::move(segments);
    }

    void MainWindow::RestorePathDraft(DisplayRow const& display_row)
    {
        m_pathDrafts.erase(MakeDraftKey(display_row.scope, display_row.variableIndex));
    }

    void MainWindow::UpdateRowEditor(RowVisual const& row_visual, bool const is_selected)
    {
        auto const display_row{row_visual.displayRow};
        auto const container{row_visual.rowBorder.Child().as<StackPanel>()};
        auto const content_border{container.Children().GetAt(0).as<Border>()};

        auto value_index{-1};
        auto children{row_visual.rowGrid.Children()};
        for (uint32_t i{0}; i < children.Size(); ++i)
        {
            if (Grid::GetColumn(children.GetAt(i).as<FrameworkElement>()) == 2)
            {
                value_index = static_cast<int>(i);
                break;
            }
        }

        if (value_index < 0)
        {
            return;
        }

        auto const is_scalar{IsScalarRow(display_row)};
        auto const current_value{
            is_scalar ? CurrentScalarValue(display_row) : CurrentPathSegmentValue(display_row)};
        if (is_selected)
        {
            content_border.Padding(ThicknessHelper::FromLengths(0, 6, 0, 6));

            auto editor{TextBox{}};
            editor.AcceptsReturn(false);
            editor.Background(SolidColorBrush{winrt::Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)});
            editor.BorderThickness(ThicknessHelper::FromUniformLength(0));
            editor.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadiusHelper::FromUniformRadius(0));
            editor.Margin(ThicknessHelper::FromLengths(0, -6, 0, -6));
            editor.MinHeight(0);
            editor.Padding(ThicknessHelper::FromLengths(0, 6, 0, 6));
            editor.Text(winrt::hstring{current_value});
            editor.TextWrapping(TextWrapping::NoWrap);
            editor.HorizontalAlignment(HorizontalAlignment::Stretch);
            editor.VerticalAlignment(VerticalAlignment::Top);
            editor.TextChanged([this, display_row, is_scalar](IInspectable const& sender, Controls::TextChangedEventArgs const&)
            {
                auto const value{std::wstring{sender.as<TextBox>().Text().c_str()}};
                if (is_scalar)
                {
                    StoreScalarDraft(display_row, value);
                    RefreshVariableVisuals(display_row.scope, display_row.variableIndex);
                    return;
                }

                StorePathSegmentDraft(display_row, value);
                RefreshVariableVisuals(display_row.scope, display_row.variableIndex);
            });
            editor.PreviewKeyDown([this, display_row](IInspectable const&, Input::KeyRoutedEventArgs const& args)
            {
                if (args.Key() == winrt::Windows::System::VirtualKey::Up)
                {
                    MoveSelectionBy(-1);
                    args.Handled(true);
                    return;
                }

                if (args.Key() == winrt::Windows::System::VirtualKey::Down)
                {
                    MoveSelectionBy(1);
                    args.Handled(true);
                    return;
                }
            });
            editor.KeyDown([this, display_row, is_scalar](IInspectable const& sender, Input::KeyRoutedEventArgs const& args)
            {

                if (args.Key() != winrt::Windows::System::VirtualKey::Escape)
                {
                    return;
                }

                std::wstring restored_value;
                if (is_scalar)
                {
                    auto const variable_ref{VariableRef{display_row.scope, display_row.variableIndex}};
                    auto const& variable{ResolveVariable(variable_ref, m_userVariables, m_machineVariables)};
                    RestoreScalarDraft(display_row);
                    restored_value = variable.value;
                }
                else
                {
                    RestorePathDraft(display_row);
                    restored_value = CurrentPathSegmentValue(display_row);
                }

                auto const editor{sender.as<TextBox>()};
                editor.Text(winrt::hstring{restored_value});
                editor.SelectAll();
                RefreshVariableVisuals(display_row.scope, display_row.variableIndex);
                args.Handled(true);
            });
            Grid::SetColumn(editor, 2);
            children.SetAt(value_index, editor);
            editor.Focus(FocusState::Programmatic);
            editor.SelectAll();
            return;
        }

        content_border.Padding(ThicknessHelper::FromLengths(0, 6, 0, 6));

        auto text_block{MakeText(current_value.empty() ? L"(empty)" : current_value, false, current_value.empty() ? 0.55 : 1.0)};
        text_block.VerticalAlignment(VerticalAlignment::Top);
        Grid::SetColumn(text_block, 2);
        children.SetAt(value_index, text_block);
    }
}
