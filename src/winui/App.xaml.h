#pragma once

#include "App.xaml.g.h"

namespace winrt::EnvironApp::implementation
{
    struct App : AppT<App>
    {
        App();
        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

    private:
        winrt::Microsoft::UI::Xaml::Window m_window{nullptr};
    };
}
