#pragma once

#include <type_traits>

namespace beast {

template <class T>
struct IsAgedContainer : std::false_type
{
    explicit IsAgedContainer() = default;
};

}  // namespace beast
