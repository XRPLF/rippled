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

#ifndef BEAST_NUDB_DETAIL_POOL_H_INCLUDED
#define BEAST_NUDB_DETAIL_POOL_H_INCLUDED

#include <beast/nudb/detail/arena.h>
#include <beast/nudb/detail/bucket.h>
#include <beast/nudb/detail/format.h>
#include <cstdint>
#include <cstring>
#include <memory>
#include <map>
#include <utility>

namespace beast {
namespace nudb {
namespace detail {

// Buffers key/value pairs in a map, associating
// them with a modifiable data file offset.
template <class = void>
class pool_t
{
public:
    struct value_type;
    class compare;

private:
    using map_type = std::map<
        value_type, std::size_t, compare>;

    arena arena_;
    std::size_t key_size_;
    std::size_t data_size_ = 0;
    map_type map_;

public:
    using iterator =
        typename map_type::iterator;

    pool_t (pool_t const&) = delete;
    pool_t& operator= (pool_t const&) = delete;

    explicit
    pool_t (std::size_t key_size,
        std::size_t alloc_size);

    pool_t& operator= (pool_t&& other);

    iterator
    begin()
    {
        return map_.begin();
    }

    iterator
    end()
    {
        return map_.end();
    }

    bool
    empty()
    {
        return map_.size() == 0;
    }

    // Returns the number of elements in the pool
    std::size_t
    size() const
    {
        return map_.size();
    }

    // Returns the sum of data sizes in the pool
    std::size_t
    data_size() const
    {
        return data_size_;
    }

    void
    clear();

    void
    shrink_to_fit();

    iterator
    find (void const* key);

    // Insert a value
    // @param h The hash of the key
    void
    insert (std::size_t h, void const* key,
        void const* buffer, std::size_t size);

    template <class U>
    friend
    void
    swap (pool_t<U>& lhs, pool_t<U>& rhs);
};

template <class _>
struct pool_t<_>::value_type
{
    std::size_t hash;
    std::size_t size;
    void const* key;
    void const* data;

    value_type (value_type const&) = default;
    value_type& operator= (value_type const&) = default;

    value_type (std::size_t hash_, std::size_t size_,
        void const* key_, void const* data_)
        : hash (hash_)
        , size (size_)
        , key (key_)
        , data (data_)
    {
    }
};

template <class _>
class pool_t<_>::compare
{
private:
    std::size_t key_size_;

public:
    using result_type = bool;
    using first_argument_type = value_type;
    using second_argument_type = value_type;

    compare (compare const&) = default;
    compare& operator= (compare const&) = default;

    compare (std::size_t key_size)
        : key_size_ (key_size)
    {
    }

    bool
    operator()(value_type const& lhs,
        value_type const& rhs) const
    {
        return std::memcmp(
            lhs.key, rhs.key, key_size_) < 0;
    }
};

//------------------------------------------------------------------------------

template <class _>
pool_t<_>::pool_t (std::size_t key_size,
        std::size_t alloc_size)
    : arena_ (alloc_size)
    , key_size_ (key_size)
    , map_ (compare(key_size))
{
}

template <class _>
pool_t<_>&
pool_t<_>::operator= (pool_t&& other)
{
    arena_ = std::move(other.arena_);
    key_size_ = other.key_size_;
    data_size_ = other.data_size_;
    map_ = std::move(other.map_);
    return *this;
}

template <class _>
void
pool_t<_>::clear()
{
    arena_.clear();
    data_size_ = 0;
    map_.clear();
}

template <class _>
void
pool_t<_>::shrink_to_fit()
{
    arena_.shrink_to_fit();
}

template <class _>
auto
pool_t<_>::find (void const* key) ->
    iterator
{
    // VFALCO need is_transparent here
    value_type tmp (0, 0, key, nullptr);
    auto const iter = map_.find(tmp);
    return iter;
}

template <class _>
void
pool_t<_>::insert (std::size_t h,
    void const* key, void const* data,
        std::size_t size)
{
    auto const k = arena_.alloc(key_size_);
    auto const d = arena_.alloc(size);
    std::memcpy(k, key, key_size_);
    std::memcpy(d, data, size);
    auto const result = map_.emplace(
        std::piecewise_construct,
            std::make_tuple(h, size, k, d),
                std::make_tuple(0));
    (void)result.second;
    // Must not already exist!
    assert(result.second);
    data_size_ += size;
}

template <class _>
void
swap (pool_t<_>& lhs, pool_t<_>& rhs)
{
    using std::swap;
    swap(lhs.arena_, rhs.arena_);
    swap(lhs.key_size_, rhs.key_size_);
    swap(lhs.data_size_, rhs.data_size_);
    swap(lhs.map_, rhs.map_);
}

using pool = pool_t<>;

} // detail
} // nudb
} // beast

#endif
