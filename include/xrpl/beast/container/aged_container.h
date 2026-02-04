#pragma once

#include <type_traits>

namespace beast {

template <class T>
struct is_aged_container : std::false_type
{
    explicit is_aged_container() = default;
};

}  // namespace beast
