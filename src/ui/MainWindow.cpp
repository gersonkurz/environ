#include "MainWindow.h"

#include <filesystem>

#include <microsoft.ui.xaml.window.h>
#include <shellapi.h>

MainWindow::MainWindow() {
    m_settings.load();
    m_snapshotStore.open();

    // Load knowledge.toml from next to the executable
    wchar_t exe_path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    std::filesystem::path knowledge_path{exe_path};
    knowledge_path.replace_filename(L"knowledge.toml");
    m_knowledgeBase.load(knowledge_path.string());

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

    if (m_root) {
        m_root.RequestedTheme(theme);
    }
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
    m_root = Grid{};
    m_contentHost = ContentControl{};

    auto header_row{RowDefinition{}};
    header_row.Height(GridLengthHelper::Auto());
    auto content_row{RowDefinition{}};
    content_row.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    m_root.RowDefinitions().Append(header_row);
    m_root.RowDefinitions().Append(content_row);

    // Apply theme BEFORE creating pages so ThemeBrush lookups resolve correctly
    ApplyTheme();

    // Get HWND for file dialogs
    HWND hwnd{nullptr};
    auto interop{this->try_as<IWindowNative>()};
    if (interop) {
        interop->get_WindowHandle(&hwnd);
    }

    m_envPage = std::make_unique<EnvironmentPage>(m_snapshotStore, m_knowledgeBase, hwnd);
    m_historyPage = std::make_unique<HistoryPage>(m_snapshotStore, [this]() {
        m_envPage->Refresh();
    });
    m_settingsPage = std::make_unique<SettingsPage>(m_settings, [this]() {
        ApplyTheme();
        m_envPage->Refresh();
    });

    auto header{Grid{}};
    header.Padding(ThicknessHelper::FromLengths(16, 12, 16, 12));

    auto title_col{ColumnDefinition{}};
    title_col.Width(GridLengthHelper::Auto());
    auto spacer_col{ColumnDefinition{}};
    spacer_col.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    auto actions_col{ColumnDefinition{}};
    actions_col.Width(GridLengthHelper::Auto());
    header.ColumnDefinitions().Append(title_col);
    header.ColumnDefinitions().Append(spacer_col);
    header.ColumnDefinitions().Append(actions_col);

    auto title{TextBlock{}};
    title.Text(L"Environ");
    title.FontSize(20);
    title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    title.VerticalAlignment(VerticalAlignment::Center);
    Grid::SetColumn(title, 0);
    header.Children().Append(title);

    auto actions{StackPanel{}};
    actions.Orientation(Orientation::Horizontal);
    actions.Spacing(8);

    auto env_button{Button{}};
    env_button.Content(winrt::box_value(L"Environment"));
    env_button.Click([this](winrt::Windows::Foundation::IInspectable const&,
                            RoutedEventArgs const&) {
        ShowEnvironmentPage();
    });
    actions.Children().Append(env_button);

    auto history_button{Button{}};
    history_button.Content(winrt::box_value(L"History"));
    history_button.Click([this](winrt::Windows::Foundation::IInspectable const&,
                                RoutedEventArgs const&) {
        ShowHistoryPage();
    });
    actions.Children().Append(history_button);

    auto settings_button{Button{}};
    settings_button.Content(winrt::box_value(L"Settings"));
    settings_button.Click([this](winrt::Windows::Foundation::IInspectable const&,
                                 RoutedEventArgs const&) {
        ShowSettingsPage();
    });
    actions.Children().Append(settings_button);

    if (!Environ::core::is_elevated()) {
        auto admin_button{Button{}};
        admin_button.Content(winrt::box_value(L"Restart as Admin"));
        admin_button.Click([](winrt::Windows::Foundation::IInspectable const&,
                              RoutedEventArgs const&) {
            wchar_t exe_path[MAX_PATH]{};
            GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
            ShellExecuteW(nullptr, L"runas", exe_path, nullptr, nullptr, SW_SHOWNORMAL);
        });
        actions.Children().Append(admin_button);
    }

    auto about_button{Button{}};
    about_button.Content(winrt::box_value(L"About"));
    about_button.Click([](winrt::Windows::Foundation::IInspectable const&,
                          RoutedEventArgs const&) {
        ShellExecuteW(nullptr, L"open", L"https://github.com/gersonkurz/environ", nullptr, nullptr, SW_SHOWNORMAL);
    });
    actions.Children().Append(about_button);

    Grid::SetColumn(actions, 2);
    header.Children().Append(actions);

    Grid::SetRow(header, 0);
    m_root.Children().Append(header);

    Grid::SetRow(m_contentHost, 1);
    m_root.Children().Append(m_contentHost);

    auto list_view{ListView{}};
    list_view.SelectionMode(ListViewSelectionMode::None);
    list_view.IsItemClickEnabled(false);

    auto items{winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()};
    for (int i{0}; i < 200; ++i) {
        auto text{TextBlock{}};
        text.Text(std::format(L"Item {}", i));
        text.Margin(ThicknessHelper::FromLengths(8, 6, 8, 6));
        items.Append(text);
    }
    list_view.ItemsSource(items);
    m_contentHost.Content(list_view);
    Content(m_root);
}

void MainWindow::ShowEnvironmentPage() {
    m_contentHost.Content(m_envPage->Root());
}

void MainWindow::ShowHistoryPage() {
    m_historyPage->Refresh();
    m_contentHost.Content(m_historyPage->Root());
}

void MainWindow::ShowSettingsPage() {
    m_contentHost.Content(m_settingsPage->Root());
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
