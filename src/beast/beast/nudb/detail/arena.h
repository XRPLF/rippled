//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2014, Vinnie Falco <vinnie.falco@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef BEAST_NUDB_DETAIL_ARENA_H_INCLUDED
#define BEAST_NUDB_DETAIL_ARENA_H_INCLUDED

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace beast {
namespace nudb {
namespace detail {

/*  Custom memory manager that allocates in large blocks.

    No limit is placed on the size of an allocation but
    alloc_size should be tuned upon construction to be a
    significant multiple of the average allocation size.

    When the arena is cleared, allocated memory is placed
    on a free list for re-use, avoiding future system calls.
*/
template <class = void>
class arena_t
{
private:
    class element;

    std::size_t alloc_size_;
    element* used_ = nullptr;
    element* free_ = nullptr;

public:
    arena_t (arena_t const&);
    arena_t& operator= (arena_t const&);

    ~arena_t();

    explicit
    arena_t (std::size_t alloc_size);

    arena_t& operator= (arena_t&& other);

    // Makes used blocks free
    void
    clear();

    // deletes free blocks
    void
    shrink_to_fit();

    std::uint8_t*
    alloc (std::size_t n);

    template <class U>
    friend
    void
    swap (arena_t<U>& lhs, arena_t<U>& rhs);

private:
    void
    dealloc (element*& list);
};

//------------------------------------------------------------------------------

template <class _>
class arena_t<_>::element
{
private:
    std::size_t const capacity_;
    std::size_t used_ = 0;

public:
    element* next = nullptr;

    explicit
    element (std::size_t alloc_size)
        : capacity_ (
            alloc_size - sizeof(*this))
    {
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
    alloc (std::size_t n);
};

template <class _>
std::uint8_t*
arena_t<_>::element::alloc (std::size_t n)
{
    if (n > capacity_ - used_)
        return nullptr;
    auto const p = const_cast<std::uint8_t*>(
        reinterpret_cast<uint8_t const*>(this + 1)
            ) + used_;
    used_ += n;
    return p;
}

//------------------------------------------------------------------------------

template <class _>
arena_t<_>::arena_t (std::size_t alloc_size)
    : alloc_size_ (alloc_size)
{
    if (alloc_size <= sizeof(element))
        throw std::domain_error(
            "arena: bad alloc size");
}

template <class _>
arena_t<_>::~arena_t()
{
    dealloc (used_);
    dealloc (free_);
}

template <class _>
arena_t<_>&
arena_t<_>::operator= (arena_t&& other)
{
    dealloc (used_);
    dealloc (free_);
    alloc_size_ = other.alloc_size_;
    used_ = other.used_;
    free_ = other.free_;
    other.used_ = nullptr;
    other.free_ = nullptr;
    return *this;
}

template <class _>
void
arena_t<_>::clear()
{
    while (used_)
    {
        auto const e = used_;
        used_ = used_->next;
        e->clear();
        e->next = free_;
        free_ = e;
    }
}

template <class _>
void
arena_t<_>::shrink_to_fit()
{
    dealloc (free_);
}

template <class _>
std::uint8_t*
arena_t<_>::alloc (std::size_t n)
{
    // Undefined behavior: Zero byte allocations
    assert(n != 0);
    n = 8 * ((n + 7) / 8);
    if (used_ && used_->remain() >= n)
        return used_->alloc(n);
    if (free_ && free_->remain() >= n)
    {
        auto const e = free_;
        free_ = free_->next;
        e->next = used_;
        used_ = e;
        return used_->alloc(n);
    }
    std::size_t const size = std::max(
        alloc_size_, sizeof(element) + n);
    element* const e = reinterpret_cast<element*>(
        new std::uint8_t[size]);
    ::new(e) element(size);
    e->next = used_;
    used_ = e;
    return used_->alloc(n);
}

template <class _>
void
swap (arena_t<_>& lhs, arena_t<_>& rhs)
{
    using std::swap;
    swap(lhs.alloc_size_, rhs.alloc_size_);
    swap(lhs.used_, rhs.used_);
    swap(lhs.free_, rhs.free_);
}

template <class _>
void
arena_t<_>::dealloc (element*& list)
{
    while (list)
    {
        auto const e = list;
        list = list->next;
        e->~element();
        delete[] reinterpret_cast<std::uint8_t*>(e);
    }
}

using arena = arena_t<>;

} // detail
} // nudb
} // beast

#endif
