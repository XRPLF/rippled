#ifndef BEAST_CONTAINER_AGED_UNORDERED_MULTISET_H_INCLUDED
#define BEAST_CONTAINER_AGED_UNORDERED_MULTISET_H_INCLUDED

#include <xrpl/beast/container/detail/aged_unordered_container.h>

#include <chrono>
#include <functional>
#include <memory>

namespace beast {

template <
    class Key,
    class Clock = std::chrono::steady_clock,
    class Hash = std::hash<Key>,
    class KeyEqual = std::equal_to<Key>,
    class Allocator = std::allocator<Key>>
using aged_unordered_multiset = detail::aged_unordered_container<
    true,
    false,
    Key,
    void,
    Clock,
    Hash,
    KeyEqual,
    Allocator>;

}

#endif
