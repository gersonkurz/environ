#include "MainWindow.h"

MainWindow::MainWindow() {
    m_settings.load();
    InitializeComponent();
    RestoreWindowPlacement();

    Closed([this]([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
                  [[maybe_unused]] winrt::Microsoft::UI::Xaml::WindowEventArgs const& args) {
        SaveWindowPlacement();
    });
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

    // -- NavigationView --------------------------------------------------
    m_navView = NavigationView{};
    m_navView.IsSettingsVisible(false);
    m_navView.IsBackButtonVisible(NavigationViewBackButtonVisible::Collapsed);

    auto envItem{NavigationViewItem{}};
    envItem.Content(winrt::box_value(L"Environment"));
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

    m_navView.MenuItems().Append(envItem);
    m_navView.FooterMenuItems().Append(settingsItem);
    m_navView.FooterMenuItems().Append(aboutItem);

    m_envPage = std::make_unique<EnvironmentPage>();
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
            } else if (tag == L"settings") {
                m_navView.Content(MakePlaceholder(L"Settings — coming soon"));
            } else if (tag == L"about") {
                m_navView.Content(MakePlaceholder(L"About — coming soon"));
            }
        });

    Content(m_navView);
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
