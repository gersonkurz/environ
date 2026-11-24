#pragma once

#include "MainWindow.g.h"

#include "AppSettings.h"
#include "Base16Theme.h"
#include "EnvGridModel.h"
#include "KnowledgeBase.h"
#include "SnapshotStore.h"

namespace winrt::environ_detail
{
    struct RowVisuals
    {
        winrt::Microsoft::UI::Xaml::Controls::Grid root{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::Border accent_bar{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition name_col{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::Grid name_slot{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::FontIcon scope_icon{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock name_text{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::Grid value_clip_host{nullptr};
        winrt::Microsoft::UI::Xaml::Media::RectangleGeometry value_clip_rect{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock value_text{nullptr};
        winrt::Microsoft::UI::Xaml::Media::TranslateTransform value_translate{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::Button browse_button{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::FontIcon browse_icon{nullptr};
        int bound_visible_index{-1};
    };
}

namespace winrt::EnvironApp::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        // Title bar & MenuBar actions
        void OnTitleBarLayoutChanged(winrt::Windows::Foundation::IInspectable const&,
                                     winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const&);
        void OnRunAsAdminClick(winrt::Windows::Foundation::IInspectable const&,
                               winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSaveClick(winrt::Windows::Foundation::IInspectable const&,
                         winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnRefreshClick(winrt::Windows::Foundation::IInspectable const&,
                            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnExportClick(winrt::Windows::Foundation::IInspectable const&,
                           winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnImportClick(winrt::Windows::Foundation::IInspectable const&,
                           winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnExitClick(winrt::Windows::Foundation::IInspectable const&,
                         winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnNewUserVarClick(winrt::Windows::Foundation::IInspectable const&,
                               winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnNewMachineVarClick(winrt::Windows::Foundation::IInspectable const&,
                                  winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnDeleteVarClick(winrt::Windows::Foundation::IInspectable const&,
                              winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnInsertEntryClick(winrt::Windows::Foundation::IInspectable const&,
                                winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnRemoveEntryClick(winrt::Windows::Foundation::IInspectable const&,
                                winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnMoveEntryUpClick(winrt::Windows::Foundation::IInspectable const&,
                                winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnMoveEntryDownClick(winrt::Windows::Foundation::IInspectable const&,
                                  winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnCopyClick(winrt::Windows::Foundation::IInspectable const&,
                         winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnHistoryClick(winrt::Windows::Foundation::IInspectable const&,
                            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnThemesClick(winrt::Windows::Foundation::IInspectable const&,
                           winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnFontsClick(winrt::Windows::Foundation::IInspectable const&,
                          winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnZoomInClick(winrt::Windows::Foundation::IInspectable const&,
                           winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnZoomOutClick(winrt::Windows::Foundation::IInspectable const&,
                            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnZoomResetClick(winrt::Windows::Foundation::IInspectable const&,
                              winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnAboutClick(winrt::Windows::Foundation::IInspectable const&,
                          winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);

        // Search & header
        void OnSearchTextChanged(winrt::Windows::Foundation::IInspectable const&,
                                 winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
        void OnSearchPreviewKeyDown(winrt::Windows::Foundation::IInspectable const&,
                                    winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
        void OnNameHeaderClick(winrt::Windows::Foundation::IInspectable const&,
                               winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnValueHeaderClick(winrt::Windows::Foundation::IInspectable const&,
                                winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);

        // Resizable NAME column splitter
        void OnSplitterPointerPressed(winrt::Windows::Foundation::IInspectable const&,
                                      winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void OnSplitterPointerMoved(winrt::Windows::Foundation::IInspectable const&,
                                    winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void OnSplitterPointerReleased(winrt::Windows::Foundation::IInspectable const&,
                                       winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void OnSplitterPointerCaptureLost(winrt::Windows::Foundation::IInspectable const&,
                                          winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);

        // Virtualized ListView & H-Scroll
        void OnContainerContentChanging(winrt::Microsoft::UI::Xaml::Controls::ListViewBase const&,
                                        winrt::Microsoft::UI::Xaml::Controls::ContainerContentChangingEventArgs const&);
        void OnListSelectionChanged(winrt::Windows::Foundation::IInspectable const&,
                                    winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void OnListDoubleTapped(winrt::Windows::Foundation::IInspectable const&,
                                winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const&);
        void OnListRightTapped(winrt::Windows::Foundation::IInspectable const&,
                               winrt::Microsoft::UI::Xaml::Input::RightTappedRoutedEventArgs const&);
        void OnListSizeChanged(winrt::Windows::Foundation::IInspectable const&,
                               winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const&);
        void OnHScrollValueChanged(winrt::Windows::Foundation::IInspectable const&,
                                   winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&);

        // Tunneling keyboard routing
        void OnRootPreviewKeyDown(winrt::Windows::Foundation::IInspectable const&,
                                  winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);

    private:
        void init_window_chrome();
        void update_titlebar_regions();
        void init_inline_editor();
        void init_wheel_handler();
        void restore_window_placement();
        void save_window_placement();
        void apply_theme(const std::string& theme_name);
        void apply_font(const std::string& font_family);
        void apply_zoom(int zoom_pct);
        void load_environment();

        // Grid view synchronization
        void sync_list_items();
        void refresh_visible_containers();
        void bind_row_container(winrt::Microsoft::UI::Xaml::Controls::ListViewItem const& item, int visible_index);
        winrt::environ_detail::RowVisuals& ensure_row_visuals(winrt::Microsoft::UI::Xaml::Controls::ListViewItem const& item);
        void update_sort_headers();
        void update_hscroll_range();
        void update_all_hscroll_transforms();
        void update_name_column_width(double width);
        std::wstring build_row_tooltip(int actual_index) const;
        void set_status_hint(const std::wstring& hint);
        void update_status_bar();
        void update_save_button();
        void select_and_scroll_visible(int visible_index);

        // Single reusable inline TextBox state machine
        bool begin_inline_edit(bool edit_name);
        void commit_inline_edit();
        void cancel_inline_edit();
        void detach_inline_editor();

        // Grid actions
        void action_browse_or_reveal();
        void action_copy();
        void action_insert();
        void action_delete();
        void action_move_entry(int dir);
        void action_new_variable(Environ::core::Scope scope);
        void action_delete_variable();

        // Dialogs (live-preview Theme/Font dialogs + History + Save Review + Export/Import + About)
        winrt::fire_and_forget show_save_review_dialog();
        winrt::fire_and_forget show_history_dialog();
        winrt::fire_and_forget show_themes_dialog();
        winrt::fire_and_forget show_fonts_dialog();
        winrt::fire_and_forget action_export_toml();
        winrt::fire_and_forget action_import_toml();
        winrt::fire_and_forget show_about_dialog();
        winrt::fire_and_forget confirm_relaunch_as_admin();
        winrt::fire_and_forget show_message_dialog(std::wstring title, std::wstring message);

        double scaled_row_height() const;
        double scaled_font_size() const;
        bool is_using_custom_theme() const;

        HWND m_hwnd{nullptr};
        bool m_elevated{false};
        bool m_dialog_open{false};
        Environ::core::AppSettings m_settings;
        Environ::core::Base16ThemeSet m_themes;
        Environ::core::KnowledgeBase m_kb;
        Environ::core::SnapshotStore m_snapshots;
        Environ::core::EnvGridModel m_model;

        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_row_indices{nullptr};
        std::unordered_map<void*, winrt::environ_detail::RowVisuals> m_visuals;
        bool m_syncing_selection{false};

        double m_name_col_width{260.0};
        bool m_dragging_splitter{false};
        double m_drag_start_x{0.0};
        double m_drag_start_width{260.0};
        double m_scroll_x{0.0};
        double m_max_value_width{0.0};

        winrt::Microsoft::UI::Xaml::Controls::TextBox m_inline_editor{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::Panel m_inline_host_panel{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::ListViewItem m_inline_host_item{nullptr};
        bool m_suppress_lost_focus{false};

        std::string m_active_theme{"System"};
        std::string m_active_font{"Segoe UI Variable Text"};
        int m_zoom{100};
    };
}

namespace winrt::EnvironApp::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
