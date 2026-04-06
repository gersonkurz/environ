#include "MainWindow.h"

#include <microsoft.ui.xaml.window.h>
#include <shellapi.h>

MainWindow::MainWindow() {
    m_settings.load();
    m_snapshotStore.open();
    InitializeComponent();
    RestoreWindowPlacement();

    Closed([this]([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
                  [[maybe_unused]] winrt::Microsoft::UI::Xaml::WindowEventArgs const& args) {
        SaveWindowPlacement();
    });
}

void MainWindow::ApplyTheme() {
    auto theme_str{m_settings.appearance.theme.get()};
    winrt::Microsoft::UI::Xaml::ElementTheme theme{winrt::Microsoft::UI::Xaml::ElementTheme::Default};
    if (theme_str == "Light") {
        theme = winrt::Microsoft::UI::Xaml::ElementTheme::Light;
    } else if (theme_str == "Dark") {
        theme = winrt::Microsoft::UI::Xaml::ElementTheme::Dark;
    }

    m_navView.RequestedTheme(theme);
}

void MainWindow::RestoreWindowPlacement() {
    auto app_window{AppWindow()};

    auto width{m_settings.window.width.get()};
    auto height{m_settings.window.height.get()};
    if (width > 0 && height > 0) {
        app_window.Resize({width, height});
    }

    auto x{m_settings.window.x.get()};
    auto y{m_settings.window.y.get()};
    if (x >= 0 && y >= 0) {
        app_window.Move({x, y});
    }

    if (m_settings.window.maximized.get()) {
        auto presenter{app_window.Presenter().as<winrt::Microsoft::UI::Windowing::OverlappedPresenter>()};
        presenter.Maximize();
    }
}

void MainWindow::SaveWindowPlacement() {
    auto app_window{AppWindow()};
    auto presenter{app_window.Presenter().as<winrt::Microsoft::UI::Windowing::OverlappedPresenter>()};

    bool maximized{presenter.State() == winrt::Microsoft::UI::Windowing::OverlappedPresenterState::Maximized};
    m_settings.window.maximized.set(maximized);

    if (!maximized) {
        auto pos{app_window.Position()};
        auto size{app_window.Size()};
        m_settings.window.x.set(pos.X);
        m_settings.window.y.set(pos.Y);
        m_settings.window.width.set(size.Width);
        m_settings.window.height.set(size.Height);
    }

    m_settings.save();
}

void MainWindow::InitializeComponent() {
    using namespace winrt::Microsoft::UI::Xaml;
    using namespace winrt::Microsoft::UI::Xaml::Controls;
    using namespace winrt::Microsoft::UI::Xaml::Media;

    Title(L"Environ");
    SystemBackdrop(MicaBackdrop{});
    ExtendsContentIntoTitleBar(true);

    // Custom title bar: just a draggable area with the app name
    auto title_bar{Grid{}};
    title_bar.Height(48);
    title_bar.Padding(ThicknessHelper::FromLengths(16, 0, 0, 0));

    auto title_icon{FontIcon{}};
    title_icon.Glyph(L"\uE774");
    title_icon.FontSize(14);
    title_icon.VerticalAlignment(VerticalAlignment::Center);
    title_icon.Margin(ThicknessHelper::FromLengths(0, 0, 8, 0));

    auto title_label{TextBlock{}};
    title_label.Text(L"Environ");
    title_label.FontSize(13);
    title_label.VerticalAlignment(VerticalAlignment::Center);

    auto title_content{StackPanel{}};
    title_content.Orientation(Orientation::Horizontal);
    title_content.VerticalAlignment(VerticalAlignment::Center);
    title_content.Children().Append(title_icon);
    title_content.Children().Append(title_label);
    title_bar.Children().Append(title_content);

    SetTitleBar(title_bar);

    // -- NavigationView --------------------------------------------------
    m_navView = NavigationView{};
    m_navView.IsSettingsVisible(false);
    m_navView.IsBackButtonVisible(NavigationViewBackButtonVisible::Collapsed);
    m_navView.IsTitleBarAutoPaddingEnabled(true);

    // Apply theme BEFORE creating pages so ThemeBrush lookups resolve correctly
    ApplyTheme();

    auto envItem{NavigationViewItem{}};
    envItem.Content(winrt::box_value(L"Environ"));
    auto globeIcon{FontIcon{}};
    globeIcon.Glyph(L"\uE774");
    envItem.Icon(globeIcon);
    envItem.Tag(winrt::box_value(L"env"));

    auto settingsItem{NavigationViewItem{}};
    settingsItem.Content(winrt::box_value(L"Settings"));
    auto settingIcon{FontIcon{}};
    settingIcon.Glyph(L"\uE713");
    settingsItem.Icon(settingIcon);
    settingsItem.Tag(winrt::box_value(L"settings"));

    auto aboutItem{NavigationViewItem{}};
    aboutItem.Content(winrt::box_value(L"About"));
    auto infoIcon{FontIcon{}};
    infoIcon.Glyph(L"\uE946");
    aboutItem.Icon(infoIcon);
    aboutItem.Tag(winrt::box_value(L"about"));

    auto historyItem{NavigationViewItem{}};
    historyItem.Content(winrt::box_value(L"History"));
    auto historyIcon{FontIcon{}};
    historyIcon.Glyph(L"\uE81C");
    historyItem.Icon(historyIcon);
    historyItem.Tag(winrt::box_value(L"history"));

    m_navView.MenuItems().Append(envItem);
    m_navView.MenuItems().Append(historyItem);

    if (!Environ::core::is_elevated()) {
        auto adminItem{NavigationViewItem{}};
        adminItem.Content(winrt::box_value(L"Restart as Admin"));
        auto shieldIcon{FontIcon{}};
        shieldIcon.Glyph(L"\uE7EF");
        adminItem.Icon(shieldIcon);
        adminItem.Tag(winrt::box_value(L"admin"));
        m_navView.FooterMenuItems().Append(adminItem);
    }

    m_navView.FooterMenuItems().Append(settingsItem);
    m_navView.FooterMenuItems().Append(aboutItem);

    // Get HWND for file dialogs
    HWND hwnd{nullptr};
    auto interop{this->try_as<IWindowNative>()};
    if (interop) {
        interop->get_WindowHandle(&hwnd);
    }

    m_envPage = std::make_unique<EnvironmentPage>(m_snapshotStore, hwnd);
    m_historyPage = std::make_unique<HistoryPage>(m_snapshotStore, [this]() {
        m_envPage->Refresh();
    });
    m_settingsPage = std::make_unique<SettingsPage>(m_settings, [this]() {
        ApplyTheme();
        m_envPage->Refresh();
    });

    m_navView.Content(m_envPage->Root());
    m_navView.SelectedItem(envItem);

    m_navView.SelectionChanged(
        [this](NavigationView const&,
               NavigationViewSelectionChangedEventArgs const& args) {
            auto item{args.SelectedItem()
                          .as<NavigationViewItem>()};
            auto tag{winrt::unbox_value<winrt::hstring>(item.Tag())};
            if (tag == L"env") {
                m_navView.Content(m_envPage->Root());
            } else if (tag == L"history") {
                m_historyPage->Refresh();
                m_navView.Content(m_historyPage->Root());
            } else if (tag == L"settings") {
                m_navView.Content(m_settingsPage->Root());
            } else if (tag == L"admin") {
                wchar_t exe_path[MAX_PATH]{};
                GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
                auto result{reinterpret_cast<INT_PTR>(
                    ShellExecuteW(nullptr, L"runas", exe_path, nullptr, nullptr, SW_SHOWNORMAL))};
                if (result > 32) {
                    Close();
                }
                // If result <= 32 (user cancelled UAC or error), stay open
            } else if (tag == L"about") {
                ShellExecuteW(nullptr, L"open", L"https://github.com/gersonkurz/environ", nullptr, nullptr, SW_SHOWNORMAL);
            }
        });

    // Stack title bar above navigation view
    auto root_grid{Grid{}};
    auto title_row{RowDefinition{}};
    title_row.Height(GridLengthHelper::Auto());
    auto content_row{RowDefinition{}};
    content_row.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    root_grid.RowDefinitions().Append(title_row);
    root_grid.RowDefinitions().Append(content_row);

    Grid::SetRow(title_bar, 0);
    Grid::SetRow(m_navView, 1);
    root_grid.Children().Append(title_bar);
    root_grid.Children().Append(m_navView);

    Content(root_grid);
}

winrt::Microsoft::UI::Xaml::Controls::TextBlock
MainWindow::MakePlaceholder(winrt::hstring const& text) {
    using namespace winrt::Microsoft::UI::Xaml;
    using namespace winrt::Microsoft::UI::Xaml::Controls;

    auto tb{TextBlock{}};
    tb.Text(text);
    tb.FontSize(24);
    tb.Margin(ThicknessHelper::FromLengths(24, 24, 24, 24));
    tb.VerticalAlignment(VerticalAlignment::Top);
    tb.HorizontalAlignment(HorizontalAlignment::Left);
    return tb;
}

winrt::Microsoft::UI::Xaml::Markup::IXamlType
MainWindow::GetXamlType(winrt::Windows::UI::Xaml::Interop::TypeName const& type) {
    return m_provider.GetXamlType(type);
}

winrt::Microsoft::UI::Xaml::Markup::IXamlType
MainWindow::GetXamlType(winrt::hstring const& fullname) {
    return m_provider.GetXamlType(fullname);
}

winrt::com_array<winrt::Microsoft::UI::Xaml::Markup::XmlnsDefinition>
MainWindow::GetXmlnsDefinitions() {
    return m_provider.GetXmlnsDefinitions();
}
