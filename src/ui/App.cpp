#include "App.h"
#include "MainWindow.h"

App::App() {
    UnhandledException(
        [](winrt::Windows::Foundation::IInspectable const&,
           winrt::Microsoft::UI::Xaml::UnhandledExceptionEventArgs const& e) {
            throw winrt::hresult_error(e.Exception(), e.Message());
        });
}

void App::OnLaunched(
    winrt::Microsoft::UI::Xaml::LaunchActivatedEventArgs const&) {
    // Load WinUI control theme resources (equivalent of <XamlControlsResources/>
    // in App.xaml). Must happen before creating any WinUI controls.
    Resources().MergedDictionaries().Append(
        winrt::Microsoft::UI::Xaml::Controls::XamlControlsResources{});

    m_window = winrt::make<MainWindow>();
    m_window.Activate();
}

winrt::Microsoft::UI::Xaml::Markup::IXamlType
App::GetXamlType(winrt::Windows::UI::Xaml::Interop::TypeName const& type) {
    return m_provider.GetXamlType(type);
}

winrt::Microsoft::UI::Xaml::Markup::IXamlType
App::GetXamlType(winrt::hstring const& fullname) {
    return m_provider.GetXamlType(fullname);
}

winrt::com_array<winrt::Microsoft::UI::Xaml::Markup::XmlnsDefinition>
App::GetXmlnsDefinitions() {
    return m_provider.GetXmlnsDefinitions();
}
