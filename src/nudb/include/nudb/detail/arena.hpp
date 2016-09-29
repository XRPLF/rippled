//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_DETAIL_ARENA_HPP
#define NUDB_DETAIL_ARENA_HPP

#include <boost/assert.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>

#if NUDB_DEBUG_ARENA
#include <beast/unit_test/dstream.hpp>
#include <iostream>
#endif

namespace nudb {
namespace detail {

/*  Custom memory manager that allocates in large blocks.

    The implementation measures the rate of allocations in
    bytes per second and tunes the large block size to fit
    one second's worth of allocations.
*/
template<class = void>
class arena_t
{
    using clock_type =
        std::chrono::steady_clock;
    
    using time_point =
        typename clock_type::time_point;

    class element;

    char const* label_;         // diagnostic
    std::size_t alloc_ = 0;     // block size
    std::size_t used_ = 0;      // bytes allocated
    element* list_ = nullptr;   // list of blocks
    time_point when_ = clock_type::now();

public:
    arena_t(arena_t const&) = delete;
    arena_t& operator=(arena_t&&) = delete;
    arena_t& operator=(arena_t const&) = delete;

    ~arena_t();

    explicit
    arena_t(char const* label = "");

    arena_t(arena_t&& other);

    // Set the allocation size
    void
    hint(std::size_t alloc)
    {
        alloc_ = alloc;
    }

    // Free all memory
    void
    clear();

    void
    periodic_activity();

    std::uint8_t*
    alloc(std::size_t n);

    template<class U>
    friend
    void
    swap(arena_t<U>& lhs, arena_t<U>& rhs);
};

//------------------------------------------------------------------------------

template<class _>
class arena_t<_>::element
{
    std::size_t const capacity_;
    std::size_t used_ = 0;
    element* next_;

public:
    element(std::size_t capacity, element* next)
        : capacity_(capacity)
        , next_(next)
    {
    }

    element*
    next() const
    {
        return next_;
    }

    void
    clear()
    {
        used_ = 0;
    }

    std::size_t
    remain() const
    {
        return capacity_ - used_;
    }

    std::size_t
    capacity() const
    {
        return capacity_;
    }

    std::uint8_t*
    alloc(std::size_t n);
};

template<class _>
std::uint8_t*
arena_t<_>::element::
alloc(std::size_t n)
{
    if(n > capacity_ - used_)
        return nullptr;
    auto const p = const_cast<std::uint8_t*>(
        reinterpret_cast<uint8_t const*>(this + 1)
            ) + used_;
    used_ += n;
    return p;
}

//------------------------------------------------------------------------------

template<class _>
arena_t<_>::
arena_t(char const* label)
    : label_(label)
{
}

template<class _>
arena_t<_>::
~arena_t()
{
    clear();
}

template<class _>
arena_t<_>::
arena_t(arena_t&& other)
    : label_(other.label_)
    , alloc_(other.alloc_)
    , used_(other.used_)
    , list_(other.list_)
    , when_(other.when_)
{
    other.used_ = 0;
    other.list_ = nullptr;
    other.when_ = clock_type::now();
    other.alloc_ = 0;
}

template<class _>
void
arena_t<_>::
clear()
{
    used_ = 0;
    while(list_)
    {
        auto const e = list_;
        list_ = list_->next();
        e->~element();
        delete[] reinterpret_cast<std::uint8_t*>(e);
    }
}

template<class _>
void
arena_t<_>::
periodic_activity()
{
    using namespace std::chrono;
    auto const now = clock_type::now();
    auto const elapsed = now - when_;
    if(elapsed < milliseconds{500})
        return;
    when_ = now;
    auto const rate = static_cast<std::size_t>(std::ceil(
        used_ / duration_cast<duration<float>>(elapsed).count()));
#if NUDB_DEBUG_ARENA
    beast::unit_test::dstream dout{std::cout};
    auto const size =
        [](element* e)
        {
            std::size_t n = 0;
            while(e)
            {
                ++n;
                e = e->next();
            }
            return n;
        };
#endif
    if(rate >= alloc_ * 2)
    {
        // adjust up
        alloc_ = std::max(rate, alloc_ * 2);
    #if NUDB_DEBUG_ARENA
        dout << label_ << ": "
            "rate=" << rate <<
            ", alloc=" << alloc_ << " UP"
            ", nused=" << used_ <<
            ", used=" << size(list_) <<
            "\n";
    #endif
    }
    else if(rate <= alloc_ / 2)
    {
        // adjust down
        alloc_ /= 2;
    #if NUDB_DEBUG_ARENA
        dout << label_ << ": "
            "rate=" << rate <<
            ", alloc=" << alloc_ << " DOWN"
            ", nused=" << used_ <<
            ", used=" << size(list_) <<
            "\n";
    #endif
    }
    else
    {
    #if NUDB_DEBUG_ARENA
        dout << label_ << ": "
            "rate=" << rate <<
            ", alloc=" << alloc_ <<
            ", nused=" << used_ <<
            ", used=" << size(list_) <<
            "\n";
    #endif
    }
}

template<class _>
std::uint8_t*
arena_t<_>::
alloc(std::size_t n)
{
    // Undefined behavior: Zero byte allocations
    BOOST_ASSERT(n != 0);
    n = 8 *((n + 7) / 8);
    std::uint8_t* p;
    if(list_)
    {
        p = list_->alloc(n);
        if(p)
        {
            used_ += n;
            return p;
        }
    }
    auto const size = std::max(alloc_, n);
    auto const e = reinterpret_cast<element*>(
        new std::uint8_t[sizeof(element) + size]);
    list_ = ::new(e) element{size, list_};
    used_ += n;
    return list_->alloc(n);
}

template<class _>
void
swap(arena_t<_>& lhs, arena_t<_>& rhs)
{
    using std::swap;
    swap(lhs.used_, rhs.used_);
    swap(lhs.list_, rhs.list_);
    // don't swap alloc_ or when_
}

using arena = arena_t<>;

} // detail
} // nudb

#endif
