#include "configurer.h"

#include <Arduino.h>

namespace configurer
{

bool Style::set(short pot)
{
    constexpr auto Count = (unsigned)midiate::Style::Count;

    const auto number = constrain(
        map(pot, 10, 1020, -1, Count),
        0, Count - 1
    );

    const auto style = static_cast<midiate::Style>(number);

    if (style != _config.style)
    {
        /* out */ _config.style = style;
        return true;
    }

    return false;
}

void Style::print(What what)
{
    if (what == What::Title)
    {
        _print(0, 1, 'S');
    }
    else if (what == What::Data)
    {
        _print(col(), row(), 2, static_cast<int>(_config.style) + 1);
    }
}

} // configurer
