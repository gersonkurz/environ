#pragma once

#include "AboutPage.g.h"

namespace winrt::Environ::implementation
{
    struct AboutPage : AboutPageT<AboutPage>
    {
        AboutPage() = default;
    };
}

namespace winrt::Environ::factory_implementation
{
    struct AboutPage : AboutPageT<AboutPage, implementation::AboutPage>
    {
    };
}
