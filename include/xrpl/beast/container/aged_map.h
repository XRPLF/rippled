#pragma once

#include <xrpl/beast/container/detail/aged_ordered_container.h>

#include <chrono>
#include <functional>
#include <memory>

namespace beast {

template <
    class Key,
    class T,
    class Clock = std::chrono::steady_clock,
    class Compare = std::less<Key>,
    class Allocator = std::allocator<std::pair<Key const, T>>>
using aged_map = detail::aged_ordered_container<false, true, Key, T, Clock, Compare, Allocator>;

}
