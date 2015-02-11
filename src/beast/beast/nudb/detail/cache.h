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

#ifndef BEAST_NUDB_DETAIL_CACHE_H_INCLUDED
#define BEAST_NUDB_DETAIL_CACHE_H_INCLUDED

#include <beast/nudb/detail/arena.h>
#include <beast/nudb/detail/bucket.h>
#include <boost/iterator/transform_iterator.hpp>
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>
#include <unordered_map>

namespace beast {
namespace nudb {
namespace detail {

// Associative container storing
// bucket blobs keyed by bucket index.
template <class = void>
class cache_t
{
public:
    using value_type = std::pair<
        std::size_t, bucket>;

private:
    enum
    {
        // The arena's alloc size will be this
        // multiple of the block size.
        factor = 64
    };

    using map_type = std::unordered_map <
        std::size_t, void*>;

    struct transform
    {
        using argument_type =
            typename map_type::value_type;
        using result_type = value_type;

        cache_t* cache_;

        transform()
            : cache_ (nullptr)
        {
        }

        explicit
        transform (cache_t& cache)
            : cache_ (&cache)
        {
        }

        value_type
        operator() (argument_type const& e) const
        {
            return std::make_pair(e.first,
                bucket (cache_->block_size_,
                    e.second));
        }
    };

    std::size_t key_size_;
    std::size_t block_size_;
    arena arena_;
    map_type map_;

public:
    using iterator = boost::transform_iterator<
        transform, typename map_type::iterator,
            value_type, value_type>;

    cache_t (cache_t const&) = delete;
    cache_t& operator= (cache_t const&) = delete;

    cache_t();

    explicit
    cache_t (std::size_t key_size,
        std::size_t block_size);

    cache_t& operator= (cache_t&& other);

    iterator
    begin()
    {
        return iterator(map_.begin(),
            transform(*this));
    }

    iterator
    end()
    {
        return iterator(map_.end(),
            transform(*this));
    }

    bool
    empty() const
    {
        return map_.empty();
    }

    void
    clear();

    void
    shrink_to_fit();

    iterator
    find (std::size_t n);

    // Create an empty bucket
    //
    bucket
    create (std::size_t n);

    // Insert a copy of a bucket.
    //
    iterator
    insert (std::size_t n, bucket const& b);

    template <class U>
    friend
    void
    swap (cache_t<U>& lhs, cache_t<U>& rhs);
};

// Constructs a cache that will never have inserts
template <class _>
cache_t<_>::cache_t()
    : key_size_ (0)
    , block_size_ (0)
    , arena_ (32) // arbitrary small number
{
}

template <class _>
cache_t<_>::cache_t (std::size_t key_size,
    std::size_t block_size)
    : key_size_ (key_size)
    , block_size_ (block_size)
    , arena_ (block_size * factor)
{
}

template <class _>
cache_t<_>&
cache_t<_>::operator=(cache_t&& other)
{
    arena_ = std::move(other.arena_);
    map_ = std::move(other.map_);
    return *this;
}

template <class _>
void
cache_t<_>::clear()
{
    arena_.clear();
    map_.clear();
}

template <class _>
void
cache_t<_>::shrink_to_fit()
{
    arena_.shrink_to_fit();
}

template <class _>
auto
cache_t<_>::find (std::size_t n) ->
    iterator
{
    auto const iter = map_.find(n);
    if (iter == map_.end())
        return iterator (map_.end(),
            transform(*this));
    return iterator (iter,
        transform(*this));
}

template <class _>
bucket
cache_t<_>::create (std::size_t n)
{
    auto const p = arena_.alloc (block_size_);
    map_.emplace (n, p);
    return bucket (block_size_,
        p, detail::empty);
}

template <class _>
auto
cache_t<_>::insert (std::size_t n,
    bucket const& b) ->
        iterator
{
    void* const p = arena_.alloc(
        b.block_size());
    ostream os(p, b.block_size());
    b.write(os);
    auto const result = map_.emplace(n, p);
    return iterator(result.first,
        transform(*this));
}

template <class U>
void
swap (cache_t<U>& lhs, cache_t<U>& rhs)
{
    using std::swap;
    swap(lhs.key_size_, rhs.key_size_);
    swap(lhs.block_size_, rhs.block_size_);
    swap(lhs.arena_, rhs.arena_);
    swap(lhs.map_, rhs.map_);
}

using cache = cache_t<>;

} // detail
} // nudb
} // beast

#endif
