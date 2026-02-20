#include "MainWindow.h"

MainWindow::MainWindow() {
    InitializeComponent();
}

void MainWindow::InitializeComponent() {
    using namespace winrt::Microsoft::UI::Xaml;
    using namespace winrt::Microsoft::UI::Xaml::Controls;
    using namespace winrt::Microsoft::UI::Xaml::Media;

    Title(L"Environ");
    SystemBackdrop(MicaBackdrop{});

    // -- NavigationView --------------------------------------------------
    auto navView{NavigationView{}};
    navView.IsSettingsVisible(false);
    navView.IsBackButtonVisible(NavigationViewBackButtonVisible::Collapsed);

    // Environment (main menu, default selected)
    auto envItem{NavigationViewItem{}};
    envItem.Content(winrt::box_value(L"Environment"));
    auto globeIcon{FontIcon{}};
    globeIcon.Glyph(L"\uE774");
    envItem.Icon(globeIcon);
    envItem.Tag(winrt::box_value(L"env"));

    // Settings (footer)
    auto settingsItem{NavigationViewItem{}};
    settingsItem.Content(winrt::box_value(L"Settings"));
    auto settingIcon{FontIcon{}};
    settingIcon.Glyph(L"\uE713");
    settingsItem.Icon(settingIcon);
    settingsItem.Tag(winrt::box_value(L"settings"));

    // About (footer)
    auto aboutItem{NavigationViewItem{}};
    aboutItem.Content(winrt::box_value(L"About"));
    auto infoIcon{FontIcon{}};
    infoIcon.Glyph(L"\uE946");
    aboutItem.Icon(infoIcon);
    aboutItem.Tag(winrt::box_value(L"about"));

    navView.MenuItems().Append(envItem);
    navView.FooterMenuItems().Append(settingsItem);
    navView.FooterMenuItems().Append(aboutItem);

    // -- Content area ----------------------------------------------------
    m_contentText = TextBlock{};
    m_contentText.Text(L"Environment");
    m_contentText.FontSize(24);
    m_contentText.Margin(ThicknessHelper::FromLengths(24, 24, 24, 24));
    m_contentText.VerticalAlignment(VerticalAlignment::Top);
    m_contentText.HorizontalAlignment(HorizontalAlignment::Left);

    navView.Content(m_contentText);

    // -- Selection handler -----------------------------------------------
    navView.SelectionChanged(
        [this](NavigationView const&,
               NavigationViewSelectionChangedEventArgs const& args) {
            if (auto item{args.SelectedItem().try_as<NavigationViewItem>()}) {
                auto tag{winrt::unbox_value<winrt::hstring>(item.Tag())};
                if (tag == L"env") {
                    m_contentText.Text(L"Environment");
                } else if (tag == L"settings") {
                    m_contentText.Text(L"Settings");
                } else if (tag == L"about") {
                    m_contentText.Text(L"About");
                }
            }
        });

    navView.SelectedItem(envItem);
    Content(navView);
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
