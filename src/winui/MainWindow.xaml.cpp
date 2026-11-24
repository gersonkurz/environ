#include "precomp.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "EnvExport.h"
#include "EnvWriter.h"
#include "FontPickerPanel.h"
#include "HistoryPanel.h"
#include "Resource.h"
#include "ShellHelpers.h"
#include "ThemePickerPanel.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Text.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Windows.UI.Core.h>

namespace winrt::EnvironApp::implementation
{

namespace
{

using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Input;
using namespace winrt::Microsoft::UI::Xaml::Media;

/// WinUI resource keys overridden by a Base16 theme (palette index 0x0..0xF + alpha).
struct ThemeKey
{
    const wchar_t* key;
    int base;
    uint8_t alpha{255};
};

constexpr ThemeKey kThemeKeys[] = {
    {L"ApplicationPageBackgroundThemeBrush", 0x0},
    {L"SolidBackgroundFillColorBaseBrush", 0x0},
    {L"LayerFillColorDefaultBrush", 0x1},
    {L"CardBackgroundFillColorDefaultBrush", 0x1},
    {L"MenuFlyoutPresenterBackground", 0x1},
    {L"ContentDialogBackground", 0x0},
    {L"ContentDialogTopOverlay", 0x1},
    {L"ControlFillColorDefaultBrush", 0x1},
    {L"ControlFillColorSecondaryBrush", 0x2},
    {L"SubtleFillColorSecondaryBrush", 0x2, 140},
    {L"SubtleFillColorTertiaryBrush", 0x2, 90},
    {L"ControlAltFillColorSecondaryBrush", 0x1, 120},
    {L"ButtonBackground", 0x1},
    {L"ButtonBackgroundPointerOver", 0x2},
    {L"ButtonBackgroundPressed", 0x2},
    {L"TextControlBackground", 0x1},
    {L"TextControlBackgroundPointerOver", 0x2},
    {L"TextControlBackgroundFocused", 0x0},
    {L"MenuFlyoutItemBackgroundPointerOver", 0x2},
    {L"MenuFlyoutSubItemBackgroundPointerOver", 0x2},
    {L"MenuBarItemBackgroundPointerOver", 0x2},
    {L"MenuBarItemBackgroundSelected", 0x2},
    {L"ListViewItemBackgroundPointerOver", 0x2, 120},
    {L"ListViewItemBackgroundSelected", 0x2, 220},
    {L"ListViewItemBackgroundSelectedPointerOver", 0x2, 245},
    {L"TextFillColorPrimaryBrush", 0x5},
    {L"TextFillColorSecondaryBrush", 0x4},
    {L"TextFillColorTertiaryBrush", 0x3},
    {L"TextFillColorDisabledBrush", 0x3},
    {L"ButtonForeground", 0x5},
    {L"ButtonForegroundPointerOver", 0x5},
    {L"ButtonForegroundPressed", 0x4},
    {L"TextControlForeground", 0x5},
    {L"TextControlForegroundPointerOver", 0x5},
    {L"TextControlForegroundFocused", 0x5},
    {L"TextControlPlaceholderForeground", 0x4},
    {L"MenuFlyoutItemForeground", 0x5},
    {L"MenuFlyoutItemForegroundPointerOver", 0x5},
    {L"DividerStrokeColorDefaultBrush", 0x2},
    {L"CardStrokeColorDefaultBrush", 0x2},
    {L"ControlStrokeColorDefaultBrush", 0x3, 180},
    {L"ControlStrongStrokeColorDefaultBrush", 0x3},
    {L"MenuFlyoutPresenterBorderBrush", 0x2},
    {L"ContentDialogBorderBrush", 0x3},
    {L"TextControlBorderBrush", 0x2},
    {L"ButtonBorderBrush", 0x2},
    {L"AccentFillColorDefaultBrush", 0xD},
    {L"AccentTextFillColorPrimaryBrush", 0xD},
    {L"ListViewItemSelectionIndicatorBrush", 0xD},
    {L"SystemFillColorCriticalBrush", 0x8},
};

winrt::Windows::UI::Color to_winrt_color(const Environ::core::Color4& c, uint8_t alpha_override = 255)
{
    const uint8_t a{(alpha_override == 255) ? c.a8() : alpha_override};
    return winrt::Windows::UI::Color{a, c.r8(), c.g8(), c.b8()};
}

SolidColorBrush make_brush(const Environ::core::Color4& c, uint8_t alpha_override = 255)
{
    return SolidColorBrush{to_winrt_color(c, alpha_override)};
}

bool is_key_down(winrt::Windows::System::VirtualKey vk)
{
    const auto state{winrt::Microsoft::UI::Input::InputKeyboardSource::GetKeyStateForCurrentThread(vk)};
    return (static_cast<uint32_t>(state) & static_cast<uint32_t>(winrt::Windows::UI::Core::CoreVirtualKeyStates::Down)) != 0;
}

const wchar_t* scope_glyph(Environ::core::RowGroup g)
{
    switch (g)
    {
    case Environ::core::RowGroup::User:      return L"\uE77B"; // Contact
    case Environ::core::RowGroup::Machine:   return L"\uEC76"; // Devices
    case Environ::core::RowGroup::Process:   return L"\uE72E"; // Lock
    case Environ::core::RowGroup::Inherited: return L"\uE756"; // CommandPrompt
    }
    return L"\uE77B";
}

} // namespace

MainWindow::MainWindow()
{
    InitializeComponent();

    m_elevated = Environ::core::is_elevated();
    m_settings.load();
    m_active_theme = m_settings.appearance.theme.get();
    m_active_font = m_settings.appearance.uiFont.get();
    m_zoom = std::clamp(m_settings.appearance.zoom.get(), 50, 200);

    const std::wstring exe_dir{Environ::winui::executable_directory()};
    m_themes.load_from_directory(exe_dir + L"\\themes");
    m_kb.load(pnq::unicode::to_utf8(exe_dir + L"\\knowledge.toml"));
    {
        wchar_t* appdata{nullptr};
        if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appdata) == S_OK && appdata)
        {
            std::filesystem::path user_kb{appdata};
            CoTaskMemFree(appdata);
            user_kb /= L"environ";
            user_kb /= L"knowledge.toml";
            if (std::filesystem::exists(user_kb))
                m_kb.load(user_kb.string());
        }
        else
        {
            CoTaskMemFree(appdata);
        }
    }
    m_snapshots.open();

    m_row_indices = winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>();
    EnvListView().ItemsSource(m_row_indices);

    init_window_chrome();
    init_inline_editor();
    init_wheel_handler();
    restore_window_placement();
    apply_theme(m_active_theme);
    apply_font(m_active_font);
    load_environment();
    update_sort_headers();
    update_status_bar();

    RootGrid().Loaded([this](auto&&, auto&&)
    {
        update_titlebar_regions();
    });

    Closed([this](auto&&, auto&&)
    {
        if (m_model.is_editing())
            cancel_inline_edit();
        save_window_placement();
    });
}

void MainWindow::init_window_chrome()
{
    auto window_native{this->try_as<::IWindowNative>()};
    if (window_native)
        window_native->get_WindowHandle(&m_hwnd);

    ExtendsContentIntoTitleBar(true);
    SetTitleBar(TitleBarRow());

    const std::wstring ver_title{L"environ " + pnq::unicode::to_utf16(ENVIRON_VERSION_STRING)};
    Title(m_elevated ? (ver_title + L" (Administrator)") : ver_title);
    TitleText().Text(ver_title);
    RoleBadgeText().Text(m_elevated ? L"Administrator" : L"Standard user");

    RunAsAdminButton().Visibility(m_elevated ? Visibility::Collapsed : Visibility::Visible);
    MenuElevateItem().Visibility(m_elevated ? Visibility::Collapsed : Visibility::Visible);
    MenuNewMachineVar().IsEnabled(m_elevated);
    DropDownNewMachineVar().IsEnabled(m_elevated);

    if (m_hwnd)
    {
        const HICON hicon{LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_ENVIRON))};
        if (hicon)
        {
            SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hicon));
            SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hicon));
        }
    }
}

void MainWindow::update_titlebar_regions()
{
    auto root{RootGrid().XamlRoot()};
    if (!root) return;

    const double scale{root.RasterizationScale()};
    const double inset{static_cast<double>(AppWindow().TitleBar().RightInset()) / scale};
    if (inset > 0.0 && CaptionInset().Width() != inset)
        CaptionInset().Width(inset);

    std::vector<winrt::Windows::Graphics::RectInt32> rects;
    const FrameworkElement controls[] = {
        MainMenu().as<FrameworkElement>(),
        SaveButton().as<FrameworkElement>(),
        RefreshButton().as<FrameworkElement>(),
        HistoryButton().as<FrameworkElement>(),
        NewVarButton().as<FrameworkElement>(),
        SearchBox().as<FrameworkElement>(),
        RunAsAdminButton().as<FrameworkElement>(),
    };

    for (auto const& el : controls)
    {
        if (el.Visibility() != Visibility::Visible || el.ActualWidth() <= 0.0)
            continue;
        const auto origin{el.TransformToVisual(nullptr).TransformPoint({0.0f, 0.0f})};
        rects.push_back(winrt::Windows::Graphics::RectInt32{
            static_cast<int32_t>(std::lround(origin.X * scale)),
            static_cast<int32_t>(std::lround(origin.Y * scale)),
            static_cast<int32_t>(std::lround(el.ActualWidth() * scale)),
            static_cast<int32_t>(std::lround(el.ActualHeight() * scale))});
    }

    winrt::Microsoft::UI::Input::InputNonClientPointerSource::GetForWindowId(AppWindow().Id())
        .SetRegionRects(winrt::Microsoft::UI::Input::NonClientRegionKind::Passthrough, rects);
}

void MainWindow::OnTitleBarLayoutChanged(winrt::Windows::Foundation::IInspectable const&,
                                         SizeChangedEventArgs const&)
{
    update_titlebar_regions();
}

void MainWindow::init_inline_editor()
{
    m_inline_editor = TextBox{};
    m_inline_editor.IsSpellCheckEnabled(false);
    m_inline_editor.VerticalAlignment(VerticalAlignment::Center);
    m_inline_editor.HorizontalAlignment(HorizontalAlignment::Stretch);
    m_inline_editor.Padding(Thickness{6.0, 2.0, 6.0, 2.0});
    m_inline_editor.MinHeight(24.0);
    m_inline_editor.BorderThickness(Thickness{1.0, 1.0, 1.0, 1.0});

    m_inline_editor.PreviewKeyDown([this](auto&&, KeyRoutedEventArgs const& args)
    {
        const auto key{args.Key()};
        if (key == winrt::Windows::System::VirtualKey::Enter)
        {
            args.Handled(true);
            auto host_item{m_inline_host_item};
            commit_inline_edit();
            if (host_item) host_item.Focus(FocusState::Programmatic);
        }
        else if (key == winrt::Windows::System::VirtualKey::Escape)
        {
            args.Handled(true);
            auto host_item{m_inline_host_item};
            cancel_inline_edit();
            if (host_item) host_item.Focus(FocusState::Programmatic);
        }
        else if (key == winrt::Windows::System::VirtualKey::Tab)
        {
            args.Handled(true);
            const bool shift{is_key_down(winrt::Windows::System::VirtualKey::Shift)};
            const bool was_name{m_model.is_editing_name()};
            commit_inline_edit();

            if (!shift && was_name)
            {
                begin_inline_edit(false);
            }
            else if (shift && !was_name && m_model.selection_has_name_cell())
            {
                begin_inline_edit(true);
            }
            else if (m_model.select_next_editable(shift ? -1 : +1))
            {
                const int fi{m_model.selected_visible_index()};
                select_and_scroll_visible(fi);
                DispatcherQueue().TryEnqueue([this, shift]()
                {
                    begin_inline_edit(!shift && m_model.selection_has_name_cell());
                });
            }
        }
    });

    m_inline_editor.LostFocus([this](auto&&, RoutedEventArgs const&)
    {
        if (!m_suppress_lost_focus && m_model.is_editing())
            commit_inline_edit();
    });
}

void MainWindow::init_wheel_handler()
{
    RootGrid().AddHandler(
        UIElement::PointerWheelChangedEvent(),
        winrt::box_value(PointerEventHandler{[this](auto&&, PointerRoutedEventArgs const& args)
        {
            const auto pt{args.GetCurrentPoint(RootGrid())};
            const int delta{pt.Properties().MouseWheelDelta()};
            if (delta == 0) return;

            if (is_key_down(winrt::Windows::System::VirtualKey::Control))
            {
                args.Handled(true);
                apply_zoom(m_zoom + (delta > 0 ? 10 : -10));
            }
            else if (is_key_down(winrt::Windows::System::VirtualKey::Shift)
                     && ValueHScrollBar().Visibility() == Visibility::Visible)
            {
                args.Handled(true);
                const double next{std::clamp(m_scroll_x - (delta > 0 ? 64.0 : -64.0),
                                             0.0, ValueHScrollBar().Maximum())};
                ValueHScrollBar().Value(next);
            }
        }}),
        /*handledEventsToo=*/true);
}

void MainWindow::restore_window_placement()
{
    if (!m_hwnd) return;
    auto app_window{AppWindow()};
    if (!app_window) return;

    const int32_t w{std::clamp(m_settings.window.width.get(), 640, 4096)};
    const int32_t h{std::clamp(m_settings.window.height.get(), 480, 2160)};
    const int32_t x{m_settings.window.x.get()};
    const int32_t y{m_settings.window.y.get()};

    app_window.Resize(winrt::Windows::Graphics::SizeInt32{w, h});

    if (x != -1 || y != -1)
    {
        RECT rc{x, y, x + w, y + h};
        if (MonitorFromRect(&rc, MONITOR_DEFAULTTONULL) != nullptr)
            app_window.Move(winrt::Windows::Graphics::PointInt32{x, y});
    }

    if (m_settings.window.maximized.get())
    {
        if (auto presenter{app_window.Presenter().try_as<winrt::Microsoft::UI::Windowing::OverlappedPresenter>()})
            presenter.Maximize();
    }
}

void MainWindow::save_window_placement()
{
    if (!m_hwnd) return;

    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (!GetWindowPlacement(m_hwnd, &wp)) return;

    const bool maximized{(wp.showCmd == SW_SHOWMAXIMIZED)};
    m_settings.window.maximized.set(maximized);

    RECT rc{};
    if (!maximized && GetWindowRect(m_hwnd, &rc))
    {
        m_settings.window.x.set(rc.left);
        m_settings.window.y.set(rc.top);
        m_settings.window.width.set(rc.right - rc.left);
        m_settings.window.height.set(rc.bottom - rc.top);
    }
    m_settings.save();
}

bool MainWindow::is_using_custom_theme() const
{
    return !m_active_theme.empty()
        && m_active_theme != "System"
        && m_active_theme != "Light"
        && m_active_theme != "Dark";
}

void MainWindow::apply_theme(const std::string& theme_name)
{
    m_active_theme = theme_name.empty() ? "System" : theme_name;
    auto res{Application::Current().Resources()};

    if (!is_using_custom_theme())
    {
        for (const auto& tk : kThemeKeys)
            res.Remove(winrt::box_value(tk.key));

        const ElementTheme req{
            m_active_theme == "Light" ? ElementTheme::Light
            : m_active_theme == "Dark" ? ElementTheme::Dark
            : ElementTheme::Default};
        RootGrid().RequestedTheme(req);
        RootGrid().ClearValue(Panel::BackgroundProperty());
        GridHeaderBorder().ClearValue(Border::BackgroundProperty());
        GridHeaderBorder().ClearValue(Border::BorderBrushProperty());
        NameHeaderButton().ClearValue(Control::ForegroundProperty());
        ValueHeaderButton().ClearValue(Control::ForegroundProperty());
        StatusFooter().ClearValue(Border::BackgroundProperty());
        StatusFooter().ClearValue(Border::BorderBrushProperty());
        RoleBadge().ClearValue(Border::BackgroundProperty());
        RoleBadgeText().ClearValue(TextBlock::ForegroundProperty());

        if (m_hwnd && req != ElementTheme::Default)
        {
            const BOOL dark_mode{(req == ElementTheme::Dark) ? TRUE : FALSE};
            DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark_mode, sizeof(dark_mode));
        }

        refresh_visible_containers();
        update_status_bar();
        return;
    }

    if (!m_themes.select_by_name(m_active_theme))
        return;

    const auto& s{m_themes.current()};
    for (const auto& tk : kThemeKeys)
    {
        res.Insert(winrt::box_value(tk.key),
                   make_brush(s.base16[static_cast<size_t>(tk.base)], tk.alpha));
    }

    // Toggle RequestedTheme so WinUI re-evaluates ThemeResource bindings on live controls.
    const ElementTheme target_theme{s.is_dark ? ElementTheme::Dark : ElementTheme::Light};
    RootGrid().RequestedTheme(s.is_dark ? ElementTheme::Light : ElementTheme::Dark);
    RootGrid().RequestedTheme(target_theme);

    const auto bg{make_brush(s.window_bg)};
    const auto card_bg{make_brush(s.card.fill)};
    const auto border{make_brush(s.card.border)};
    const auto header_bg{make_brush(s.header.fill)};
    const auto header_fg{make_brush(s.header.text)};

    RootGrid().Background(bg);
    GridHeaderBorder().Background(header_bg);
    GridHeaderBorder().BorderBrush(border);
    NameHeaderButton().Foreground(header_fg);
    ValueHeaderButton().Foreground(header_fg);
    StatusFooter().Background(card_bg);
    StatusFooter().BorderBrush(border);
    if (m_elevated)
    {
        RoleBadge().Background(make_brush(s.accent));
        RoleBadgeText().Foreground(make_brush(s.accent_text));
    }
    else
    {
        RoleBadge().Background(make_brush(s.base16[2], 140));
        RoleBadgeText().Foreground(make_brush(s.base16[5]));
    }

    if (m_hwnd)
    {
        const BOOL dark_mode{s.dark_title_bar ? TRUE : FALSE};
        DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark_mode, sizeof(dark_mode));
    }

    refresh_visible_containers();
    update_status_bar();
}

void MainWindow::apply_font(const std::string& font_family)
{
    m_active_font = font_family.empty() ? "Segoe UI Variable Text" : font_family;
    const FontFamily ff{pnq::unicode::to_utf16(m_active_font)};
    TitleText().FontFamily(ff);
    SearchBox().FontFamily(ff);
    NameHeaderButton().FontFamily(ff);
    ValueHeaderButton().FontFamily(ff);
    StatusText().FontFamily(ff);
    StatusHintText().FontFamily(ff);
    if (m_inline_editor) m_inline_editor.FontFamily(ff);
    refresh_visible_containers();
}

void MainWindow::apply_zoom(int zoom_pct)
{
    m_zoom = std::clamp(zoom_pct, 50, 200);
    m_settings.appearance.zoom.set(m_zoom);
    m_settings.save();
    refresh_visible_containers();
    update_hscroll_range();
    update_status_bar();
}

double MainWindow::scaled_row_height() const
{
    return std::max(22.0, 28.0 * (static_cast<double>(m_zoom) / 100.0));
}

double MainWindow::scaled_font_size() const
{
    return std::max(9.0, 13.5 * (static_cast<double>(m_zoom) / 100.0));
}

void MainWindow::load_environment()
{
    if (m_model.is_editing())
        cancel_inline_edit();

    auto user_vars{Environ::core::read_variables(Environ::core::Scope::User, &m_kb)};
    auto machine_vars{Environ::core::read_variables(Environ::core::Scope::Machine, &m_kb)};
    auto process_vars{Environ::core::read_process_extras(user_vars, machine_vars, &m_kb)};

    Environ::core::learn_classifications(m_kb, user_vars);
    Environ::core::learn_classifications(m_kb, machine_vars);
    if (m_kb.has_learned())
    {
        wchar_t* appdata{nullptr};
        if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appdata) == S_OK && appdata)
        {
            std::filesystem::path p{appdata};
            CoTaskMemFree(appdata);
            m_kb.save_learned((p / L"environ" / L"knowledge.toml").string());
        }
        else
        {
            CoTaskMemFree(appdata);
        }
    }

    Environ::core::expand_and_validate(user_vars);
    Environ::core::expand_and_validate(machine_vars);
    Environ::core::expand_and_validate(process_vars);
    Environ::core::detect_duplicates(user_vars, machine_vars);
    Environ::core::detect_shadowed(user_vars, machine_vars, m_kb);

    m_model.set_data(user_vars, machine_vars, process_vars, m_elevated);
    sync_list_items();
    update_save_button();
    set_status_hint({});
    update_status_bar();
}

void MainWindow::sync_list_items()
{
    const uint32_t target{static_cast<uint32_t>(m_model.visible_row_count())};
    uint32_t current{m_row_indices.Size()};

    while (current < target)
    {
        m_row_indices.Append(winrt::box_value(static_cast<int32_t>(current)));
        ++current;
    }
    while (current > target)
    {
        m_row_indices.RemoveAtEnd();
        --current;
    }

    refresh_visible_containers();
    update_hscroll_range();
}

void MainWindow::refresh_visible_containers()
{
    const int count{m_model.visible_row_count()};
    for (int fi{0}; fi < count; ++fi)
    {
        if (auto container{EnvListView().ContainerFromIndex(fi).try_as<ListViewItem>()})
            bind_row_container(container, fi);
    }
}

winrt::environ_detail::RowVisuals& MainWindow::ensure_row_visuals(ListViewItem const& item)
{
    void* key{winrt::get_abi(item)};
    auto it{m_visuals.find(key)};
    if (it != m_visuals.end())
        return it->second;

    winrt::environ_detail::RowVisuals v{};
    v.root = item.ContentTemplateRoot().try_as<Grid>();
    if (!v.root)
    {
        v.root = Grid{};
        item.Content(v.root);
    }
    v.root.Children().Clear();
    v.root.ColumnDefinitions().Clear();

    v.name_col = ColumnDefinition{};
    v.name_col.Width(GridLengthHelper::FromPixels(m_name_col_width + 6.0));
    ColumnDefinition val_col{};
    val_col.Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
    v.root.ColumnDefinitions().Append(v.name_col);
    v.root.ColumnDefinitions().Append(val_col);

    v.accent_bar = Border{};
    v.accent_bar.Width(3.0);
    v.accent_bar.HorizontalAlignment(HorizontalAlignment::Left);
    v.accent_bar.Margin(Thickness{0.0, 4.0, 0.0, 4.0});
    v.accent_bar.CornerRadius(CornerRadius{1.5, 1.5, 1.5, 1.5});
    v.accent_bar.Visibility(Visibility::Collapsed);
    v.root.Children().Append(v.accent_bar);

    // Frozen NAME cell (2-column Grid so NameText trims cleanly with ellipsis)
    v.name_slot = Grid{};
    Grid::SetColumn(v.name_slot, 0);
    Grid name_row{};
    name_row.VerticalAlignment(VerticalAlignment::Center);
    name_row.ColumnSpacing(6.0);
    name_row.Margin(Thickness{10.0, 0.0, 8.0, 0.0});
    ColumnDefinition icon_col{};
    icon_col.Width(GridLengthHelper::Auto());
    ColumnDefinition text_col{};
    text_col.Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
    name_row.ColumnDefinitions().Append(icon_col);
    name_row.ColumnDefinitions().Append(text_col);

    v.scope_icon = FontIcon{};
    v.scope_icon.FontFamily(FontFamily{L"Segoe Fluent Icons"});
    v.scope_icon.VerticalAlignment(VerticalAlignment::Center);
    Grid::SetColumn(v.scope_icon, 0);

    v.name_text = TextBlock{};
    v.name_text.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
    v.name_text.TextTrimming(TextTrimming::CharacterEllipsis);
    v.name_text.TextWrapping(TextWrapping::NoWrap);
    v.name_text.VerticalAlignment(VerticalAlignment::Center);
    Grid::SetColumn(v.name_text, 1);

    name_row.Children().Append(v.scope_icon);
    name_row.Children().Append(v.name_text);
    v.name_slot.Children().Append(name_row);
    v.root.Children().Append(v.name_slot);

    // Clipped VALUE cell (shifted horizontally via TranslateTransform)
    v.value_clip_host = Grid{};
    Grid::SetColumn(v.value_clip_host, 1);
    v.value_clip_rect = RectangleGeometry{};
    v.value_clip_rect.Rect(winrt::Windows::Foundation::Rect{0.0f, 0.0f, 10000.0f, 1000.0f});
    v.value_clip_host.Clip(v.value_clip_rect);
    v.value_clip_host.SizeChanged([clip = v.value_clip_rect](auto&&, SizeChangedEventArgs const& e)
    {
        clip.Rect(winrt::Windows::Foundation::Rect{0.0f, 0.0f, e.NewSize().Width, e.NewSize().Height});
    });

    v.value_translate = TranslateTransform{};
    v.value_translate.X(-m_scroll_x);

    v.value_text = TextBlock{};
    v.value_text.VerticalAlignment(VerticalAlignment::Center);
    v.value_text.Margin(Thickness{8.0, 0.0, 36.0, 0.0});
    v.value_text.TextWrapping(TextWrapping::NoWrap);
    v.value_text.RenderTransform(v.value_translate);

    v.browse_icon = FontIcon{};
    v.browse_icon.FontFamily(FontFamily{L"Segoe Fluent Icons"});
    v.browse_icon.FontSize(13.0);

    v.browse_button = Button{};
    v.browse_button.Width(28.0);
    v.browse_button.Height(24.0);
    v.browse_button.Padding(Thickness{0.0, 0.0, 0.0, 0.0});
    v.browse_button.Margin(Thickness{0.0, 0.0, 4.0, 0.0});
    v.browse_button.HorizontalAlignment(HorizontalAlignment::Right);
    v.browse_button.VerticalAlignment(VerticalAlignment::Center);
    v.browse_button.Visibility(Visibility::Collapsed);
    v.browse_button.Content(v.browse_icon);
    v.browse_button.Click([this](auto&&, RoutedEventArgs const&)
    {
        action_browse_or_reveal();
    });

    v.value_clip_host.Children().Append(v.value_text);
    v.value_clip_host.Children().Append(v.browse_button);
    v.root.Children().Append(v.value_clip_host);

    auto [inserted, _] = m_visuals.emplace(key, std::move(v));
    return inserted->second;
}

void MainWindow::bind_row_container(ListViewItem const& item, int visible_index)
{
    const int ai{m_model.visible_to_actual(visible_index)};
    if (ai < 0) return;

    auto& v{ensure_row_visuals(item)};
    v.bound_visible_index = visible_index;

    const auto& r{m_model.row(ai)};
    const double row_h{scaled_row_height()};
    const double font_sz{scaled_font_size()};
    const FontFamily ff{pnq::unicode::to_utf16(m_active_font)};

    v.root.Height(row_h);
    v.name_col.Width(GridLengthHelper::FromPixels(m_name_col_width + 6.0));
    v.value_translate.X(-m_scroll_x);

    const std::wstring& disp_name{m_model.display_name(ai)};
    const bool show_name{!disp_name.empty() || r.kind == Environ::core::GridRow::Kind::Variable};
    v.scope_icon.Visibility(show_name ? Visibility::Visible : Visibility::Collapsed);
    if (show_name)
    {
        v.scope_icon.Glyph(scope_glyph(r.group));
        v.scope_icon.FontSize(std::max(10.0, font_sz));
    }

    v.name_text.FontFamily(ff);
    v.name_text.FontSize(font_sz);
    v.name_text.Text(disp_name);

    v.value_text.FontFamily(ff);
    v.value_text.FontSize(font_sz);
    v.value_text.Text(r.col2);

    const bool value_dirty{m_model.row_value_dirty(ai)};
    const bool name_dirty{m_model.row_name_dirty(ai)};
    const bool focused{(ai == m_model.selected_row())};

    bool show_browse{false};
    if (focused)
    {
        const std::wstring var_name{m_model.selected_variable_name()};
        const auto role{m_kb.path_role(var_name)};
        if (r.seg_index >= 0 || role != Environ::core::KnowledgeBase::PathRole::None)
        {
            show_browse = true;
            if (r.read_only)
            {
                v.browse_icon.Glyph(L"\uE8A7");
                ToolTipService::SetToolTip(v.browse_button, winrt::box_value(L"Reveal in Explorer"));
            }
            else if (role == Environ::core::KnowledgeBase::PathRole::File)
            {
                v.browse_icon.Glyph(L"\uE8A5");
                ToolTipService::SetToolTip(v.browse_button, winrt::box_value(L"Browse for file..."));
            }
            else
            {
                v.browse_icon.Glyph(L"\uE8B7");
                ToolTipService::SetToolTip(v.browse_button, winrt::box_value(L"Browse for folder..."));
            }
        }
    }
    v.browse_button.Visibility(show_browse ? Visibility::Visible : Visibility::Collapsed);

    // Native Windows tooltip on the row: expanded path, invalid/duplicate/shadowed warnings, KB description & note.
    const std::wstring tip{build_row_tooltip(ai)};
    if (tip.empty())
        ToolTipService::SetToolTip(v.root, nullptr);
    else
        ToolTipService::SetToolTip(v.root, winrt::box_value(tip));

    if (is_using_custom_theme())
    {
        const auto& s{m_themes.current()};
        const auto ro_color{r.group == Environ::core::RowGroup::Inherited ? s.inherited_text : s.readonly_text};

        auto row_bg{s.row.fill};
        if (r.invalid) row_bg = s.row_invalid.fill;
        else if (value_dirty || name_dirty) row_bg = s.row_dirty.fill;
        else if (r.duplicate) row_bg = s.row_duplicate.fill;
        v.root.Background(make_brush(row_bg));

        auto name_fg{s.row.text};
        if (name_dirty) name_fg = s.row_dirty.text;
        else if (r.read_only) name_fg = ro_color;
        const auto name_brush{make_brush(name_fg)};
        v.scope_icon.Foreground(name_brush);
        v.name_text.Foreground(name_brush);
        v.name_text.Opacity(1.0);
        v.scope_icon.Opacity(1.0);

        auto val_fg{s.row.text};
        if (r.invalid) val_fg = s.row_invalid.text;
        else if (value_dirty) val_fg = s.row_dirty.text;
        else if (r.duplicate) val_fg = s.row_duplicate.text;
        else if (r.read_only) val_fg = ro_color;
        v.value_text.Foreground(make_brush(val_fg));
        v.value_text.Opacity(1.0);

        v.accent_bar.Background(make_brush((value_dirty || name_dirty) ? s.row_dirty.text : s.accent));
        v.accent_bar.Visibility((focused || value_dirty || name_dirty) ? Visibility::Visible : Visibility::Collapsed);
    }
    else
    {
        v.root.ClearValue(Panel::BackgroundProperty());
        if (r.invalid)
        {
            v.value_text.Foreground(SolidColorBrush{winrt::Windows::UI::Color{255, 224, 82, 82}});
            v.value_text.Opacity(1.0);
        }
        else if (value_dirty)
        {
            v.value_text.Foreground(SolidColorBrush{winrt::Windows::UI::Color{255, 218, 165, 32}});
            v.value_text.Opacity(1.0);
        }
        else if (r.duplicate)
        {
            v.value_text.Foreground(SolidColorBrush{winrt::Windows::UI::Color{255, 230, 140, 50}});
            v.value_text.Opacity(1.0);
        }
        else
        {
            v.value_text.ClearValue(TextBlock::ForegroundProperty());
            v.value_text.Opacity(r.read_only ? 0.6 : 1.0);
        }

        if (name_dirty)
            v.name_text.Foreground(SolidColorBrush{winrt::Windows::UI::Color{255, 218, 165, 32}});
        else
            v.name_text.ClearValue(TextBlock::ForegroundProperty());

        v.name_text.Opacity(r.read_only ? 0.6 : 1.0);
        v.scope_icon.Opacity(r.read_only ? 0.6 : 1.0);
        v.accent_bar.Background(SolidColorBrush{winrt::Windows::UI::Color{255, 218, 165, 32}});
        v.accent_bar.Visibility((value_dirty || name_dirty) ? Visibility::Visible : Visibility::Collapsed);
    }
}

void MainWindow::OnContainerContentChanging(ListViewBase const&, ContainerContentChangingEventArgs const& args)
{
    auto item{args.ItemContainer().try_as<ListViewItem>()};
    if (!item) return;

    if (args.InRecycleQueue())
    {
        if (m_inline_host_item && m_inline_host_item == item && m_model.is_editing())
            commit_inline_edit();
        return;
    }

    args.Handled(true);
    bind_row_container(item, args.ItemIndex());
}

void MainWindow::OnListSelectionChanged(winrt::Windows::Foundation::IInspectable const&,
                                        SelectionChangedEventArgs const&)
{
    if (m_syncing_selection) return;

    std::vector<int> vis_indices;
    for (auto const& range : EnvListView().SelectedRanges())
    {
        const int first{range.FirstIndex()};
        const int count{static_cast<int>(range.Length())};
        for (int i{0}; i < count; ++i)
            vis_indices.push_back(first + i);
    }

    const int focus_fi{EnvListView().SelectedIndex()};
    m_model.set_visible_selection(vis_indices, focus_fi);
    refresh_visible_containers();
    set_status_hint({});
}

void MainWindow::OnListDoubleTapped(winrt::Windows::Foundation::IInspectable const&,
                                    DoubleTappedRoutedEventArgs const& args)
{
    if (!m_model.has_selection()) return;

    const auto pt{args.GetPosition(EnvListView())};
    const bool clicked_name{pt.X < m_name_col_width && m_model.selection_has_name_cell()};
    begin_inline_edit(clicked_name);
}

void MainWindow::OnListRightTapped(winrt::Windows::Foundation::IInspectable const&,
                                   RightTappedRoutedEventArgs const& args)
{
    if (auto fe{args.OriginalSource().try_as<DependencyObject>()})
    {
        DependencyObject cur{fe};
        while (cur)
        {
            if (auto lvi{cur.try_as<ListViewItem>()})
            {
                const int fi{EnvListView().IndexFromContainer(lvi)};
                if (fi >= 0)
                {
                    const int ai{m_model.visible_to_actual(fi)};
                    if (ai >= 0 && !m_model.is_row_selected(ai))
                    {
                        m_model.select_single(ai);
                        select_and_scroll_visible(fi);
                    }
                }
                break;
            }
            cur = VisualTreeHelper::GetParent(cur);
        }
    }

    MenuFlyout flyout{};
    auto add_item = [&](const wchar_t* text, bool enabled, auto&& handler) {
        MenuFlyoutItem mi{};
        mi.Text(text);
        mi.IsEnabled(enabled);
        mi.Click([handler = std::forward<decltype(handler)>(handler)](auto&&, RoutedEventArgs const&) { handler(); });
        flyout.Items().Append(mi);
    };

    const bool has_sel{m_model.has_selection()};
    const bool editable{m_model.selection_editable()};
    const bool is_path{m_model.selected_is_path_entry()};

    add_item(L"Copy (Ctrl+C)", has_sel, [this]() { action_copy(); });
    add_item(editable ? L"Browse / Reveal..." : L"Reveal in Explorer", has_sel, [this]() { action_browse_or_reveal(); });

    flyout.Items().Append(MenuFlyoutSeparator{});
    add_item(L"Insert entry (Ins)", is_path, [this]() { action_insert(); });
    add_item(L"Remove entry (Del)", is_path, [this]() { action_delete(); });
    add_item(L"Move up (Alt+Up)", is_path, [this]() { action_move_entry(-1); });
    add_item(L"Move down (Alt+Down)", is_path, [this]() { action_move_entry(+1); });

    flyout.Items().Append(MenuFlyoutSeparator{});
    add_item(L"New user variable", true, [this]() { action_new_variable(Environ::core::Scope::User); });
    add_item(L"New machine variable", m_elevated, [this]() { action_new_variable(Environ::core::Scope::Machine); });
    add_item(L"Delete variable", editable, [this]() { action_delete_variable(); });

    flyout.ShowAt(EnvListView(), args.GetPosition(EnvListView()));
}

void MainWindow::OnListSizeChanged(winrt::Windows::Foundation::IInspectable const&,
                                   SizeChangedEventArgs const&)
{
    update_hscroll_range();
}

void MainWindow::OnHScrollValueChanged(winrt::Windows::Foundation::IInspectable const&,
                                       Primitives::RangeBaseValueChangedEventArgs const& args)
{
    m_scroll_x = args.NewValue();
    update_all_hscroll_transforms();
}

void MainWindow::update_hscroll_range()
{
    const double char_w{scaled_font_size() * 0.56};
    size_t max_chars{0};
    const int count{m_model.visible_row_count()};
    for (int fi{0}; fi < count; ++fi)
    {
        const auto& r{m_model.visible_row(fi)};
        if (r.col2.size() > max_chars) max_chars = r.col2.size();
    }
    m_max_value_width = static_cast<double>(max_chars) * char_w + 64.0;

    const double list_w{EnvListView().ActualWidth()};
    const double viewport_w{std::max(100.0, list_w - m_name_col_width - 36.0)};
    const double max_scroll{std::max(0.0, m_max_value_width - viewport_w)};

    ValueHScrollBar().Margin(Thickness{m_name_col_width + 6.0, 0.0, 12.0, 0.0});
    ValueHScrollBar().ViewportSize(viewport_w);
    ValueHScrollBar().Maximum(max_scroll);

    if (max_scroll > 1.0)
    {
        ValueHScrollBar().Visibility(Visibility::Visible);
        if (m_scroll_x > max_scroll)
        {
            m_scroll_x = max_scroll;
            ValueHScrollBar().Value(m_scroll_x);
            update_all_hscroll_transforms();
        }
    }
    else
    {
        ValueHScrollBar().Visibility(Visibility::Collapsed);
        if (m_scroll_x != 0.0)
        {
            m_scroll_x = 0.0;
            ValueHScrollBar().Value(0.0);
            update_all_hscroll_transforms();
        }
    }
}

void MainWindow::update_all_hscroll_transforms()
{
    const int count{m_model.visible_row_count()};
    for (int fi{0}; fi < count; ++fi)
    {
        if (auto container{EnvListView().ContainerFromIndex(fi).try_as<ListViewItem>()})
        {
            auto it{m_visuals.find(winrt::get_abi(container))};
            if (it != m_visuals.end() && it->second.value_translate)
                it->second.value_translate.X(-m_scroll_x);
        }
    }
}

void MainWindow::update_name_column_width(double width)
{
    m_name_col_width = std::clamp(width, 120.0, 600.0);
    HeaderNameCol().Width(GridLengthHelper::FromPixels(m_name_col_width));
    const int count{m_model.visible_row_count()};
    for (int fi{0}; fi < count; ++fi)
    {
        if (auto container{EnvListView().ContainerFromIndex(fi).try_as<ListViewItem>()})
        {
            auto it{m_visuals.find(winrt::get_abi(container))};
            if (it != m_visuals.end() && it->second.name_col)
                it->second.name_col.Width(GridLengthHelper::FromPixels(m_name_col_width + 6.0));
        }
    }
    update_hscroll_range();
}

void MainWindow::OnSplitterPointerPressed(winrt::Windows::Foundation::IInspectable const&,
                                          PointerRoutedEventArgs const& args)
{
    m_dragging_splitter = true;
    m_drag_start_x = args.GetCurrentPoint(RootGrid()).Position().X;
    m_drag_start_width = m_name_col_width;
    NameSplitter().CapturePointer(args.Pointer());
    args.Handled(true);
}

void MainWindow::OnSplitterPointerMoved(winrt::Windows::Foundation::IInspectable const&,
                                        PointerRoutedEventArgs const& args)
{
    if (!m_dragging_splitter) return;
    const double dx{args.GetCurrentPoint(RootGrid()).Position().X - m_drag_start_x};
    update_name_column_width(m_drag_start_width + dx);
    args.Handled(true);
}

void MainWindow::OnSplitterPointerReleased(winrt::Windows::Foundation::IInspectable const&,
                                           PointerRoutedEventArgs const& args)
{
    if (!m_dragging_splitter) return;
    m_dragging_splitter = false;
    NameSplitter().ReleasePointerCapture(args.Pointer());
    args.Handled(true);
}

void MainWindow::OnSplitterPointerCaptureLost(winrt::Windows::Foundation::IInspectable const&,
                                              PointerRoutedEventArgs const&)
{
    m_dragging_splitter = false;
}

bool MainWindow::begin_inline_edit(bool edit_name)
{
    if (!m_model.has_selection()) return false;

    if (!m_model.selection_editable())
    {
        const std::wstring var_name{m_model.selected_variable_name()};
        std::wstring note{m_kb.note(var_name)};
        if (note.empty())
        {
            const auto& r{m_model.row(m_model.selected_row())};
            if (r.group == Environ::core::RowGroup::Machine)
                note = L"Machine variables are read-only while unelevated. Click 'Run as administrator' to edit.";
            else if (r.group == Environ::core::RowGroup::Inherited)
                note = L"Inherited from the launching process — read-only and not stored in the Windows registry.";
            else
                note = L"Computed by Windows at sign-in — read-only.";
        }
        set_status_hint(L"Read-only: " + note);
        return false;
    }

    if (m_model.is_editing())
        commit_inline_edit();

    auto initial{edit_name ? m_model.begin_edit_name() : m_model.begin_edit_value()};
    if (!initial) return false;

    const int fi{m_model.selected_visible_index()};
    if (fi < 0)
    {
        m_model.cancel_edit();
        return false;
    }

    EnvListView().ScrollIntoView(m_row_indices.GetAt(static_cast<uint32_t>(fi)));
    auto container{EnvListView().ContainerFromIndex(fi).try_as<ListViewItem>()};
    if (!container)
    {
        m_model.cancel_edit();
        return false;
    }

    auto& v{ensure_row_visuals(container)};
    detach_inline_editor();

    m_inline_host_item = container;
    m_inline_host_panel = edit_name ? v.name_slot : v.value_clip_host;

    m_inline_editor.FontSize(scaled_font_size());
    m_inline_editor.Text(*initial);
    m_inline_host_panel.Children().Append(m_inline_editor);
    m_inline_editor.Focus(FocusState::Programmatic);
    m_inline_editor.SelectAll();
    return true;
}

void MainWindow::commit_inline_edit()
{
    if (!m_model.is_editing()) return;

    m_suppress_lost_focus = true;
    const std::wstring text{m_inline_editor.Text().c_str()};
    detach_inline_editor();
    m_model.commit_edit(text);
    m_suppress_lost_focus = false;

    refresh_visible_containers();
    update_hscroll_range();
    update_save_button();
    set_status_hint({});
}

void MainWindow::cancel_inline_edit()
{
    if (!m_model.is_editing()) return;

    m_suppress_lost_focus = true;
    detach_inline_editor();
    m_model.cancel_edit();
    m_suppress_lost_focus = false;
}

void MainWindow::detach_inline_editor()
{
    if (m_inline_host_panel)
    {
        uint32_t idx{0};
        if (m_inline_host_panel.Children().IndexOf(m_inline_editor, idx))
            m_inline_host_panel.Children().RemoveAt(idx);
        m_inline_host_panel = nullptr;
    }
    m_inline_host_item = nullptr;
}

void MainWindow::select_and_scroll_visible(int visible_index)
{
    if (visible_index < 0 || visible_index >= static_cast<int>(m_row_indices.Size())) return;
    m_syncing_selection = true;
    EnvListView().SelectedIndex(visible_index);
    m_syncing_selection = false;
    EnvListView().ScrollIntoView(m_row_indices.GetAt(static_cast<uint32_t>(visible_index)));
    refresh_visible_containers();
    set_status_hint({});
}

void MainWindow::OnRootPreviewKeyDown(winrt::Windows::Foundation::IInspectable const&,
                                      KeyRoutedEventArgs const& args)
{
    if (m_dialog_open) return;

    const auto key{args.Key()};
    const bool ctrl{is_key_down(winrt::Windows::System::VirtualKey::Control)};
    const bool alt{is_key_down(winrt::Windows::System::VirtualKey::Menu)};

    if (ctrl && key == winrt::Windows::System::VirtualKey::F)
    {
        args.Handled(true);
        SearchBox().Focus(FocusState::Programmatic);
        SearchBox().SelectAll();
        return;
    }
    if (ctrl && key == winrt::Windows::System::VirtualKey::S)
    {
        args.Handled(true);
        if (m_model.has_changes()) show_save_review_dialog();
        return;
    }
    if (ctrl && key == winrt::Windows::System::VirtualKey::H)
    {
        args.Handled(true);
        show_history_dialog();
        return;
    }
    if (key == winrt::Windows::System::VirtualKey::F5)
    {
        args.Handled(true);
        load_environment();
        return;
    }
    if (ctrl && (key == winrt::Windows::System::VirtualKey::Add || static_cast<int>(key) == 187))
    {
        args.Handled(true);
        apply_zoom(m_zoom + 10);
        return;
    }
    if (ctrl && (key == winrt::Windows::System::VirtualKey::Subtract || static_cast<int>(key) == 189))
    {
        args.Handled(true);
        apply_zoom(m_zoom - 10);
        return;
    }
    if (ctrl && key == winrt::Windows::System::VirtualKey::Number0)
    {
        args.Handled(true);
        apply_zoom(100);
        return;
    }

    if (m_model.is_editing())
        return;

    auto focused{FocusManager::GetFocusedElement(RootGrid().XamlRoot())};
    if (focused && focused.try_as<TextBox>())
        return;

    if (ctrl && key == winrt::Windows::System::VirtualKey::C)
    {
        args.Handled(true);
        action_copy();
    }
    else if (key == winrt::Windows::System::VirtualKey::F2)
    {
        args.Handled(true);
        begin_inline_edit(false);
    }
    else if (key == winrt::Windows::System::VirtualKey::Insert)
    {
        args.Handled(true);
        action_insert();
    }
    else if (key == winrt::Windows::System::VirtualKey::Delete)
    {
        args.Handled(true);
        action_delete();
    }
    else if (alt && key == winrt::Windows::System::VirtualKey::Up)
    {
        args.Handled(true);
        action_move_entry(-1);
    }
    else if (alt && key == winrt::Windows::System::VirtualKey::Down)
    {
        args.Handled(true);
        action_move_entry(+1);
    }
}

void MainWindow::OnSearchTextChanged(winrt::Windows::Foundation::IInspectable const&,
                                     TextChangedEventArgs const&)
{
    if (m_model.is_editing())
        commit_inline_edit();

    m_model.set_filter(SearchBox().Text().c_str());
    sync_list_items();
    const int fi{m_model.selected_visible_index()};
    m_syncing_selection = true;
    EnvListView().SelectedIndex(fi);
    m_syncing_selection = false;
    set_status_hint({});
}

void MainWindow::OnSearchPreviewKeyDown(winrt::Windows::Foundation::IInspectable const&,
                                        KeyRoutedEventArgs const& args)
{
    if (args.Key() == winrt::Windows::System::VirtualKey::Escape)
    {
        args.Handled(true);
        SearchBox().Text(L"");
        EnvListView().Focus(FocusState::Programmatic);
    }
}

void MainWindow::OnNameHeaderClick(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
{
    if (m_model.is_editing()) commit_inline_edit();
    m_model.toggle_sort(Environ::core::SortColumn::Name);
    update_sort_headers();
    sync_list_items();
    select_and_scroll_visible(m_model.selected_visible_index());
}

void MainWindow::OnValueHeaderClick(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
{
    if (m_model.is_editing()) commit_inline_edit();
    m_model.toggle_sort(Environ::core::SortColumn::Value);
    update_sort_headers();
    sync_list_items();
    select_and_scroll_visible(m_model.selected_visible_index());
}

void MainWindow::update_sort_headers()
{
    const wchar_t* arrow{m_model.sort_ascending() ? L"  \u25B2" : L"  \u25BC"};
    const bool by_name{m_model.sort_column() == Environ::core::SortColumn::Name};
    NameHeaderButton().Content(winrt::box_value(by_name ? (std::wstring{L"NAME"} + arrow) : L"NAME"));
    ValueHeaderButton().Content(winrt::box_value(!by_name ? (std::wstring{L"VALUE"} + arrow) : L"VALUE"));
}

void MainWindow::action_copy()
{
    const std::wstring text{m_model.copy_text()};
    if (!text.empty())
        Environ::winui::copy_text_to_clipboard(m_hwnd, text);
}

void MainWindow::action_browse_or_reveal()
{
    if (!m_model.has_selection()) return;

    const auto& r{m_model.row(m_model.selected_row())};
    const std::wstring var_name{m_model.selected_variable_name()};
    const auto role{m_kb.path_role(var_name)};

    if (r.read_only)
    {
        auto detail{m_model.get_selection_detail()};
        const std::wstring target{detail && !detail->expanded_path.empty() ? detail->expanded_path : r.col2};
        Environ::winui::reveal_in_explorer(target);
        return;
    }

    const bool pick_folder{role != Environ::core::KnowledgeBase::PathRole::File};
    const std::wstring current{m_model.selected_value_text()};
    if (auto picked{Environ::winui::browse_for_path(m_hwnd, pick_folder, current)})
    {
        const std::wstring final_val{Environ::core::preserve_env_form(current, *picked)};
        if (m_model.set_selected_value(final_val))
        {
            refresh_visible_containers();
            update_hscroll_range();
            update_save_button();
            set_status_hint({});
        }
    }
}

void MainWindow::action_insert()
{
    if (m_model.selected_is_path_entry())
    {
        if (m_model.add_entry())
        {
            sync_list_items();
            select_and_scroll_visible(m_model.selected_visible_index());
            update_save_button();
            DispatcherQueue().TryEnqueue([this]() { begin_inline_edit(false); });
        }
    }
    else
    {
        action_new_variable(m_model.new_variable_scope());
    }
}

void MainWindow::action_delete()
{
    if (m_model.selected_is_path_entry())
    {
        if (m_model.remove_entry())
        {
            sync_list_items();
            select_and_scroll_visible(m_model.selected_visible_index());
            update_save_button();
        }
    }
    else
    {
        action_delete_variable();
    }
}

void MainWindow::action_move_entry(int dir)
{
    if (m_model.move_entry(dir))
    {
        sync_list_items();
        select_and_scroll_visible(m_model.selected_visible_index());
        update_save_button();
    }
}

void MainWindow::action_new_variable(Environ::core::Scope scope)
{
    if (scope == Environ::core::Scope::Machine && !m_elevated) return;
    if (m_model.add_variable(scope))
    {
        sync_list_items();
        select_and_scroll_visible(m_model.selected_visible_index());
        update_save_button();
        update_status_bar();
        DispatcherQueue().TryEnqueue([this]() { begin_inline_edit(true); });
    }
}

void MainWindow::action_delete_variable()
{
    if (m_model.remove_variable())
    {
        sync_list_items();
        select_and_scroll_visible(m_model.selected_visible_index());
        update_save_button();
        update_status_bar();
    }
}

std::wstring MainWindow::build_row_tooltip(int actual_index) const
{
    if (actual_index < 0 || actual_index >= m_model.total_row_count())
        return {};

    const auto& r{m_model.row(actual_index)};
    const std::wstring var_name{m_model.row_variable_name(actual_index)};
    const auto detail{m_model.get_row_detail(actual_index)};

    std::vector<std::wstring> lines;
    if (detail)
    {
        if (!detail->valid)
            lines.push_back(L"\u26A0 Path not found: " + detail->expanded_path);
        else if (!detail->duplicate_desc.empty())
            lines.push_back(L"\u26A0 " + detail->duplicate_desc + L" — " + detail->expanded_path);
        else if (!detail->expanded_path.empty() && detail->expanded_path != detail->display_path)
            lines.push_back(L"Expands to: " + detail->expanded_path);
    }

    if (m_model.row_shadowed(actual_index))
        lines.push_back(L"\u26A0 Not in effect — overridden by Windows: " + m_model.row_effective_value(actual_index));

    const std::wstring desc{m_kb.describe(var_name)};
    if (!desc.empty())
        lines.push_back(desc);

    const std::wstring note{m_kb.note(var_name)};
    if (!note.empty())
        lines.push_back(note);
    else if (r.group == Environ::core::RowGroup::Inherited)
        lines.push_back(L"Inherited from the launching process (not stored in the registry).");

    std::wstring out;
    for (size_t i{0}; i < lines.size(); ++i)
    {
        if (i > 0) out += L"\n";
        out += lines[i];
    }
    return out;
}

void MainWindow::set_status_hint(const std::wstring& hint)
{
    StatusHintText().Text(hint);
}

void MainWindow::update_save_button()
{
    const bool dirty{m_model.has_changes()};
    SaveButton().IsEnabled(dirty);
    MenuSaveItem().IsEnabled(dirty);
    SaveButtonText().Text(dirty ? L"Save \u25CF" : L"Save");
}

void MainWindow::update_status_bar()
{
    const auto c{m_model.group_counts()};
    const std::wstring text{std::format(
        L"{}  \u2022  {} user, {} machine, {} session, {} inherited  \u2022  {}%",
        pnq::unicode::to_utf16(m_active_theme),
        c.user, c.machine, c.session, c.inherited, m_zoom)};
    StatusText().Text(text);
}

// --- MenuBar & Toolbar Click Handlers ----------------------------------------

void MainWindow::OnSaveClick(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { show_save_review_dialog(); }
void MainWindow::OnRefreshClick(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { load_environment(); }
void MainWindow::OnExportClick(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { action_export_toml(); }
void MainWindow::OnImportClick(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { action_import_toml(); }
void MainWindow::OnExitClick(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { Close(); }
void MainWindow::OnNewUserVarClick(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { action_new_variable(Environ::core::Scope::User); }
void MainWindow::OnNewMachineVarClick(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { action_new_variable(Environ::core::Scope::Machine); }
void MainWindow::OnDeleteVarClick(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { action_delete_variable(); }
void MainWindow::OnInsertEntryClick(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { action_insert(); }
void MainWindow::OnRemoveEntryClick(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { action_delete(); }
void MainWindow::OnMoveEntryUpClick(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { action_move_entry(-1); }
void MainWindow::OnMoveEntryDownClick(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { action_move_entry(+1); }
void MainWindow::OnCopyClick(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { action_copy(); }
void MainWindow::OnHistoryClick(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { show_history_dialog(); }
void MainWindow::OnThemesClick(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { show_themes_dialog(); }
void MainWindow::OnFontsClick(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { show_fonts_dialog(); }
void MainWindow::OnZoomInClick(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { apply_zoom(m_zoom + 10); }
void MainWindow::OnZoomOutClick(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { apply_zoom(m_zoom - 10); }
void MainWindow::OnZoomResetClick(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { apply_zoom(100); }
void MainWindow::OnAboutClick(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { show_about_dialog(); }

void MainWindow::OnRunAsAdminClick(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
{
    if (m_model.has_changes())
    {
        confirm_relaunch_as_admin();
        return;
    }
    if (Environ::winui::relaunch_as_admin(m_hwnd))
        Close();
}

// --- Dialogs -----------------------------------------------------------------

winrt::fire_and_forget MainWindow::show_message_dialog(std::wstring title, std::wstring message)
{
    auto lifetime{get_strong()};
    if (m_dialog_open) co_return;
    m_dialog_open = true;

    ContentDialog dlg{};
    dlg.XamlRoot(RootGrid().XamlRoot());
    dlg.Title(winrt::box_value(title));
    dlg.Content(winrt::box_value(message));
    dlg.CloseButtonText(L"OK");
    co_await dlg.ShowAsync();
    m_dialog_open = false;
}

winrt::fire_and_forget MainWindow::confirm_relaunch_as_admin()
{
    auto lifetime{get_strong()};
    if (m_dialog_open) co_return;
    m_dialog_open = true;

    ContentDialog dlg{};
    dlg.XamlRoot(RootGrid().XamlRoot());
    dlg.Title(winrt::box_value(L"Unsaved Changes"));
    dlg.Content(winrt::box_value(L"You have unsaved environment changes. Relaunching as Administrator will discard them. Continue?"));
    dlg.PrimaryButtonText(L"Relaunch");
    dlg.CloseButtonText(L"Cancel");
    dlg.DefaultButton(ContentDialogButton::Close);

    const auto res{co_await dlg.ShowAsync()};
    m_dialog_open = false;
    if (res == ContentDialogResult::Primary)
    {
        if (Environ::winui::relaunch_as_admin(m_hwnd))
            Close();
    }
}

winrt::fire_and_forget MainWindow::show_save_review_dialog()
{
    auto lifetime{get_strong()};
    if (m_dialog_open) co_return;
    if (m_model.is_editing()) commit_inline_edit();

    const auto cur_user{m_model.current_vars(Environ::core::Scope::User)};
    const auto cur_machine{m_model.current_vars(Environ::core::Scope::Machine)};

    if (const auto err{Environ::core::validate_variables(cur_user)}; !err.empty())
    {
        show_message_dialog(L"Validation Error (User)", err);
        co_return;
    }
    if (m_elevated)
    {
        if (const auto err{Environ::core::validate_variables(cur_machine)}; !err.empty())
        {
            show_message_dialog(L"Validation Error (Machine)", err);
            co_return;
        }
    }

    const auto& orig_user{m_model.original_vars(Environ::core::Scope::User)};
    const auto& orig_machine{m_model.original_vars(Environ::core::Scope::Machine)};
    const auto user_diff{Environ::core::compute_diff(orig_user, cur_user)};
    const auto machine_diff{m_elevated ? Environ::core::compute_diff(orig_machine, cur_machine)
                                       : std::vector<Environ::core::EnvChange>{}};

    if (user_diff.empty() && machine_diff.empty())
    {
        show_message_dialog(L"No Changes", L"There are no changes to save.");
        co_return;
    }

    StackPanel list{};
    list.Spacing(6.0);

    auto append_changes = [&](const wchar_t* scope_label, const std::vector<Environ::core::EnvChange>& changes) {
        for (const auto& ch : changes)
        {
            TextBlock tb{};
            tb.Text(std::wstring{scope_label} + L" " + ch.describe());
            tb.FontFamily(FontFamily{L"Consolas"});
            tb.FontSize(12.5);
            tb.TextWrapping(TextWrapping::Wrap);
            switch (ch.kind)
            {
            case Environ::core::EnvChange::Kind::Add:
                tb.Foreground(SolidColorBrush{winrt::Windows::UI::Color{255, 80, 185, 100}});
                break;
            case Environ::core::EnvChange::Kind::Delete:
                tb.Foreground(SolidColorBrush{winrt::Windows::UI::Color{255, 224, 82, 82}});
                break;
            case Environ::core::EnvChange::Kind::Modify:
                tb.Foreground(SolidColorBrush{winrt::Windows::UI::Color{255, 218, 165, 32}});
                break;
            case Environ::core::EnvChange::Kind::Rename:
                tb.Foreground(SolidColorBrush{winrt::Windows::UI::Color{255, 100, 160, 240}});
                break;
            }
            list.Children().Append(tb);
        }
    };

    append_changes(L"[User]", user_diff);
    append_changes(L"[Machine]", machine_diff);

    ScrollViewer sv{};
    sv.MaxHeight(420.0);
    sv.Content(list);

    m_dialog_open = true;
    ContentDialog dlg{};
    dlg.XamlRoot(RootGrid().XamlRoot());
    dlg.Title(winrt::box_value(L"Review Changes"));
    dlg.Content(sv);
    dlg.PrimaryButtonText(L"Apply Changes");
    dlg.CloseButtonText(L"Cancel");
    dlg.DefaultButton(ContentDialogButton::Primary);

    const auto res{co_await dlg.ShowAsync()};
    m_dialog_open = false;
    if (res != ContentDialogResult::Primary)
        co_return;

    const auto live_user{Environ::core::read_variables(Environ::core::Scope::User, &m_kb)};
    const auto live_machine{m_elevated ? Environ::core::read_variables(Environ::core::Scope::Machine, &m_kb)
                                       : orig_machine};
    const auto ext_user{Environ::core::compute_diff(orig_user, live_user)};
    const auto ext_machine{m_elevated ? Environ::core::compute_diff(orig_machine, live_machine)
                                      : std::vector<Environ::core::EnvChange>{}};

    if (!ext_user.empty() || !ext_machine.empty())
    {
        m_dialog_open = true;
        ContentDialog warn{};
        warn.XamlRoot(RootGrid().XamlRoot());
        warn.Title(winrt::box_value(L"Registry Changed Externally"));
        warn.Content(winrt::box_value(
            L"Another process modified the Windows environment since environ loaded. "
            L"Applying now may overwrite those external changes. Continue anyway?"));
        warn.PrimaryButtonText(L"Overwrite");
        warn.CloseButtonText(L"Cancel");
        warn.DefaultButton(ContentDialogButton::Close);
        const auto wres{co_await warn.ShowAsync()};
        m_dialog_open = false;
        if (wres != ContentDialogResult::Primary)
            co_return;
    }

    std::vector<Environ::core::EnvChange> all_changes;
    all_changes.insert(all_changes.end(), user_diff.begin(), user_diff.end());
    all_changes.insert(all_changes.end(), machine_diff.begin(), machine_diff.end());
    const std::string summary{pnq::unicode::to_utf8(Environ::core::summarize_changes(all_changes))};
    m_snapshots.create_snapshot(summary, live_user, live_machine);

    const auto apply_res{Environ::core::apply_document_changes(
        orig_user, cur_user, orig_machine, cur_machine, m_elevated)};

    load_environment();

    if (!apply_res.succeeded())
    {
        std::wstring msg;
        if (!apply_res.user.error.empty()) msg += L"User: " + apply_res.user.error + L"\n";
        if (!apply_res.machine.error.empty()) msg += L"Machine: " + apply_res.machine.error + L"\n";
        show_message_dialog(L"Save Error", msg);
    }
}

winrt::fire_and_forget MainWindow::show_history_dialog()
{
    auto lifetime{get_strong()};
    if (m_dialog_open) co_return;
    if (m_model.is_editing()) commit_inline_edit();

    const auto cur_user{m_model.original_vars(Environ::core::Scope::User)};
    const auto cur_machine{m_model.original_vars(Environ::core::Scope::Machine)};

    ContentDialog dlg{};
    dlg.XamlRoot(RootGrid().XamlRoot());
    dlg.Title(winrt::box_value(L"Snapshot History"));
    dlg.CloseButtonText(L"Close");
    dlg.Resources().Insert(winrt::box_value(L"ContentDialogMaxWidth"), winrt::box_value(900.0));

    auto content{Environ::winui::create_history_dialog_content(
        m_snapshots, cur_user, cur_machine, m_settings.appearance.monoFont.get(),
        [this, dlg](const auto& snap_user, const auto& snap_machine)
        {
            const auto live_user{Environ::core::read_variables(Environ::core::Scope::User, &m_kb)};
            const auto live_machine{Environ::core::read_variables(Environ::core::Scope::Machine, &m_kb)};
            m_model.set_data_for_restore(live_user, live_machine, snap_user, snap_machine, m_elevated);
            sync_list_items();
            update_save_button();
            update_status_bar();
            dlg.Hide();
        })};

    dlg.Content(content);
    m_dialog_open = true;
    co_await dlg.ShowAsync();
    m_dialog_open = false;
}

winrt::fire_and_forget MainWindow::show_themes_dialog()
{
    auto lifetime{get_strong()};
    if (m_dialog_open) co_return;
    if (m_model.is_editing()) commit_inline_edit();

    const std::string saved_theme{m_active_theme};
    auto picker{Environ::winui::create_theme_dialog_content(
        m_themes, saved_theme,
        [this](const std::string& name)
        {
            apply_theme(name);
        })};

    ContentDialog dlg{};
    dlg.XamlRoot(RootGrid().XamlRoot());
    dlg.Title(winrt::box_value(L"Choose Theme"));
    dlg.Content(picker.root);
    dlg.PrimaryButtonText(L"Apply");
    dlg.CloseButtonText(L"Cancel");
    dlg.DefaultButton(ContentDialogButton::Primary);
    dlg.Resources().Insert(winrt::box_value(L"ContentDialogMaxWidth"), winrt::box_value(640.0));

    m_dialog_open = true;
    const auto res{co_await dlg.ShowAsync()};
    m_dialog_open = false;

    if (res == ContentDialogResult::Primary && picker.selected_theme)
    {
        apply_theme(*picker.selected_theme);
        m_settings.appearance.theme.set(*picker.selected_theme);
        m_settings.save();
    }
    else
    {
        apply_theme(saved_theme);
    }
}

winrt::fire_and_forget MainWindow::show_fonts_dialog()
{
    auto lifetime{get_strong()};
    if (m_dialog_open) co_return;
    if (m_model.is_editing()) commit_inline_edit();

    const std::string saved_font{m_active_font};
    auto picker{Environ::winui::create_font_dialog_content(
        saved_font,
        [this](const std::string& fam)
        {
            apply_font(fam);
        })};

    ContentDialog dlg{};
    dlg.XamlRoot(RootGrid().XamlRoot());
    dlg.Title(winrt::box_value(L"Choose UI Font"));
    dlg.Content(picker.root);
    dlg.PrimaryButtonText(L"Apply");
    dlg.CloseButtonText(L"Cancel");
    dlg.DefaultButton(ContentDialogButton::Primary);

    m_dialog_open = true;
    const auto res{co_await dlg.ShowAsync()};
    m_dialog_open = false;

    if (res == ContentDialogResult::Primary && picker.selected_font)
    {
        apply_font(*picker.selected_font);
        m_settings.appearance.uiFont.set(*picker.selected_font);
        m_settings.save();
    }
    else
    {
        apply_font(saved_font);
    }
}

winrt::fire_and_forget MainWindow::action_export_toml()
{
    auto lifetime{get_strong()};
    if (m_model.is_editing()) commit_inline_edit();

    auto path{Environ::winui::browse_save_toml(m_hwnd)};
    if (!path) co_return;

    const auto cur_user{m_model.current_vars(Environ::core::Scope::User)};
    const auto cur_machine{m_model.current_vars(Environ::core::Scope::Machine)};
    const std::string toml_text{Environ::core::export_toml(cur_user, cur_machine)};

    std::ofstream out(*path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        show_message_dialog(L"Export Failed", L"Could not write to " + *path);
        co_return;
    }
    out.write(toml_text.data(), static_cast<std::streamsize>(toml_text.size()));
    out.close();

    show_message_dialog(L"Export Complete", L"Environment exported to:\n" + *path);
}

winrt::fire_and_forget MainWindow::action_import_toml()
{
    auto lifetime{get_strong()};
    if (m_model.is_editing()) commit_inline_edit();

    auto path{Environ::winui::browse_open_toml(m_hwnd)};
    if (!path) co_return;

    std::ifstream in(*path, std::ios::binary);
    if (!in.is_open())
    {
        show_message_dialog(L"Import Failed", L"Could not open " + *path);
        co_return;
    }
    std::ostringstream oss;
    oss << in.rdbuf();

    auto parsed{Environ::core::import_toml(oss.str())};
    if (!parsed)
    {
        show_message_dialog(L"Import Failed", L"Could not parse TOML file:\n" + *path);
        co_return;
    }

    const auto& orig_user{m_model.original_vars(Environ::core::Scope::User)};
    const auto& orig_machine{m_model.original_vars(Environ::core::Scope::Machine)};
    auto merged_user{Environ::core::merge_variables(m_model.current_vars(Environ::core::Scope::User), parsed->user)};
    auto merged_machine{m_elevated
        ? Environ::core::merge_variables(m_model.current_vars(Environ::core::Scope::Machine), parsed->machine)
        : orig_machine};

    Environ::core::expand_and_validate(merged_user);
    Environ::core::expand_and_validate(merged_machine);
    Environ::core::detect_duplicates(merged_user, merged_machine);

    m_model.set_data_for_restore(orig_user, orig_machine, merged_user, merged_machine, m_elevated);
    sync_list_items();
    update_save_button();
    update_status_bar();

    show_message_dialog(L"Import Staged",
        L"Imported variables have been merged as staged edits.\nReview and click Save (Ctrl+S) to apply them.");
}

winrt::fire_and_forget MainWindow::show_about_dialog()
{
    auto lifetime{get_strong()};
#if defined(_M_ARM64)
    const wchar_t* arch{L"ARM64"};
#else
    const wchar_t* arch{L"x64"};
#endif
    const std::wstring body{std::format(
        L"environ {} ({})\n"
        L"Modern Windows 11 environment-variable editor (WinUI 3 / C++/WinRT)\n\n"
        L"Co-authored with Antigravity (Gemini 4 Neon)\n"
        L"Mode: {}\n"
        L"https://github.com/gersonkurz/environ",
        pnq::unicode::to_utf16(ENVIRON_VERSION_STRING),
        arch,
        m_elevated ? L"Administrator (User + Machine)" : L"Standard User (Machine read-only)")};

    show_message_dialog(L"About environ", body);
    co_return;
}

} // namespace winrt::EnvironApp::implementation
