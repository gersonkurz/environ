#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "EnvironmentPage.xaml.h"
#include "HistoryPage.xaml.h"
#include "SettingsPage.xaml.h"
#include "AboutPage.xaml.h"
#include "AppSettings.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::Environ::implementation
{
    MainWindow::MainWindow()
    {
    }

    void MainWindow::ConfigureTitleBar()
    {
        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());

        if (Microsoft::UI::Windowing::AppWindowTitleBar::IsCustomizationSupported()) {
            auto titleBar{AppWindow().TitleBar()};
            titleBar.ExtendsContentIntoTitleBar(true);
            titleBar.PreferredHeightOption(Microsoft::UI::Windowing::TitleBarHeightOption::Tall);
            titleBar.ButtonBackgroundColor(Microsoft::UI::Colors::Transparent());
            titleBar.ButtonInactiveBackgroundColor(Microsoft::UI::Colors::Transparent());
        }
    }

    void MainWindow::UpdateCaptionButtonColors()
    {
        if (!Microsoft::UI::Windowing::AppWindowTitleBar::IsCustomizationSupported())
            return;

        bool isDark{false};
        if (auto root{Content().try_as<FrameworkElement>()}) {
            isDark = (root.ActualTheme() == ElementTheme::Dark);
        }

        auto titleBar{AppWindow().TitleBar()};
        titleBar.ButtonForegroundColor(isDark
            ? Microsoft::UI::Colors::White()
            : Microsoft::UI::Colors::Black());
        titleBar.ButtonInactiveForegroundColor(isDark
            ? Windows::UI::Color{0xFF, 0x99, 0x99, 0x99}
            : Windows::UI::Color{0xFF, 0x66, 0x66, 0x66});
    }

    void MainWindow::ApplyTheme()
    {
        auto theme_str{::Environ::core::app_settings().appearance.theme.get()};
        if (auto root{Content().try_as<FrameworkElement>()}) {
            if (theme_str == "Light") root.RequestedTheme(ElementTheme::Light);
            else if (theme_str == "Dark") root.RequestedTheme(ElementTheme::Dark);
        }
    }

    void MainWindow::RestoreWindowPlacement()
    {
        auto& ws{::Environ::core::app_settings().window};

        if (ws.maximized.get()) {
            if (auto presenter{AppWindow().Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>()}) {
                presenter.Maximize();
            }
        } else if (ws.x.get() >= 0 && ws.y.get() >= 0) {
            AppWindow().MoveAndResize({ws.x.get(), ws.y.get(), ws.width.get(), ws.height.get()});
        } else {
            AppWindow().Resize({ws.width.get(), ws.height.get()});
        }
    }

    void MainWindow::SaveWindowPlacement()
    {
        auto& ws{::Environ::core::app_settings().window};

        bool maximized{false};
        if (auto presenter{AppWindow().Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>()}) {
            maximized = (presenter.State() == Microsoft::UI::Windowing::OverlappedPresenterState::Maximized);
        }

        ws.maximized.set(maximized);

        if (!maximized) {
            auto pos{AppWindow().Position()};
            auto size{AppWindow().Size()};
            ws.x.set(pos.X);
            ws.y.set(pos.Y);
            ws.width.set(size.Width);
            ws.height.set(size.Height);
        }

        ::Environ::core::app_settings().save();
    }

    void MainWindow::OnNavLoaded(
        [[maybe_unused]] IInspectable const& sender,
        [[maybe_unused]] RoutedEventArgs const& args)
    {
        ConfigureTitleBar();
        ApplyTheme();
        UpdateCaptionButtonColors();

        // Re-sync caption button colors whenever the effective theme changes
        if (auto root{Content().try_as<FrameworkElement>()}) {
            root.ActualThemeChanged([this](FrameworkElement const&, IInspectable const&) {
                UpdateCaptionButtonColors();
            });
        }

        RestoreWindowPlacement();

        // Save window placement on close; intercept if unsaved changes
        this->Closed([this](auto&&, Microsoft::UI::Xaml::WindowEventArgs const& args) {
            if (m_closing) {
                SaveWindowPlacement();
                return;
            }
            if (m_envPage && m_envPage.HasUnsavedChanges()) {
                args.Handled(true);
                ShowUnsavedChangesDialog();
            } else {
                SaveWindowPlacement();
            }
        });

        // Select the first item (Environment) on startup
        NavView().SelectedItem(NavView().MenuItems().GetAt(0));
    }

    void MainWindow::OnNavSelectionChanged(
        [[maybe_unused]] NavigationView const& sender,
        NavigationViewSelectionChangedEventArgs const& args)
    {
        if (auto item{args.SelectedItem().try_as<NavigationViewItem>()}) {
            auto tag{unbox_value<hstring>(item.Tag())};
            NavigateTo(tag);
        }
    }

    fire_and_forget MainWindow::ShowUnsavedChangesDialog()
    {
        auto strong_this{get_strong()};

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Unsaved Changes"));
        dialog.Content(box_value(L"You have unsaved environment variable changes."));
        dialog.PrimaryButtonText(L"Save");
        dialog.SecondaryButtonText(L"Don\u2019t Save");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(ContentDialogButton::Primary);

        auto result{co_await dialog.ShowAsync()};

        if (result == ContentDialogResult::Primary) {
            // Save, then close
            co_await m_envPage.SaveChangesAsync();
            m_closing = true;
            Close();
        } else if (result == ContentDialogResult::Secondary) {
            // Don't save, just close
            m_closing = true;
            Close();
        }
        // Cancel (ContentDialogResult::None) — do nothing, stay open
    }

    void MainWindow::NavigateTo(hstring const& tag)
    {
        if (tag == L"env") {
            if (!m_envPage) {
                m_envPage = Environ::EnvironmentPage();
            }
            ContentArea().Content(m_envPage);
        }
        else if (tag == L"history") {
            if (!m_historyPage) {
                m_historyPage = Environ::HistoryPage();
            }
            ContentArea().Content(m_historyPage);
        }
        else if (tag == L"settings") {
            if (!m_settingsPage) {
                m_settingsPage = Environ::SettingsPage();
            }
            ContentArea().Content(m_settingsPage);
        }
        else if (tag == L"about") {
            if (!m_aboutPage) {
                m_aboutPage = Environ::AboutPage();
            }
            ContentArea().Content(m_aboutPage);
        }
    }
}
