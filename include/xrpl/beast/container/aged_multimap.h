#pragma once

#include <xrpl/beast/container/detail/aged_ordered_container.h>

#include <chrono>
#include <functional>
#include <memory>
#include <utility>

namespace beast {

template <
    class Key,
    class T,
    class Clock = std::chrono::steady_clock,
    class Compare = std::less<Key>,
    class Allocator = std::allocator<std::pair<Key const, T>>>
using aged_multimap = detail::AgedOrderedContainer<true, true, Key, T, Clock, Compare, Allocator>;

}  // namespace beast
