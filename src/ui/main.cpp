#include "pch.h"
#include "App.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    winrt::Microsoft::UI::Xaml::Application::Start(
        [](auto&&) { winrt::make<App>(); });

    return 0;
}
