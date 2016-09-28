//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_DETAIL_POOL_HPP
#define NUDB_DETAIL_POOL_HPP

#include <nudb/detail/arena.hpp>
#include <nudb/detail/bucket.hpp>
#include <nudb/detail/format.hpp>
#include <boost/assert.hpp>
#include <boost/thread/lock_types.hpp>
#include <cstdint>
#include <cstring>
#include <memory>
#include <map>
#include <utility>

namespace nudb {
namespace detail {

// Buffers key/value pairs in a map, associating
// them with a modifiable data file offset.
template<class = void>
class pool_t
{
public:
    struct value_type;
    class compare;

private:
    using map_type = std::map<
        value_type, noff_t, compare>;

    arena arena_;
    nsize_t key_size_;
    nsize_t data_size_ = 0;
    map_type map_;

public:
    using iterator =
        typename map_type::iterator;

    pool_t(pool_t const&) = delete;
    pool_t& operator=(pool_t const&) = delete;

    pool_t(pool_t&& other);

    pool_t(nsize_t key_size, char const* label);

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
    empty() const
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
    periodic_activity();

    iterator
    find(void const* key);

    // Insert a value
    // @param h The hash of the key
    void
    insert(nhash_t h, void const* key,
        void const* buffer, nsize_t size);

    template<class U>
    friend
    void
    swap(pool_t<U>& lhs, pool_t<U>& rhs);
};

template<class _>
struct pool_t<_>::value_type
{
    nhash_t hash;
    nsize_t size;
    void const* key;
    void const* data;

    value_type(value_type const&) = default;
    value_type& operator=(value_type const&) = default;

    value_type(nhash_t hash_, nsize_t size_,
            void const* key_, void const* data_)
        : hash(hash_)
        , size(size_)
        , key(key_)
        , data(data_)
    {
    }
};

template<class _>
class pool_t<_>::compare
{
    std::size_t key_size_;

public:
    using result_type = bool;
    using first_argument_type = value_type;
    using second_argument_type = value_type;

    compare(compare const&) = default;
    compare& operator=(compare const&) = default;

    explicit
    compare(nsize_t key_size)
        : key_size_(key_size)
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

template<class _>
pool_t<_>::
pool_t(pool_t&& other)
    : arena_(std::move(other.arena_))
    , key_size_(other.key_size_)
    , data_size_(other.data_size_)
    , map_(std::move(other.map_))
{
}

template<class _>
pool_t<_>::
pool_t(nsize_t key_size, char const* label)
    : arena_(label)
    , key_size_(key_size)
    , map_(compare{key_size})
{
}

template<class _>
void
pool_t<_>::
clear()
{
    arena_.clear();
    data_size_ = 0;
    map_.clear();
}

template<class _>
void
pool_t<_>::
periodic_activity()
{
    arena_.periodic_activity();
}

template<class _>
auto
pool_t<_>::
find(void const* key) ->
    iterator
{
    // VFALCO need is_transparent here
    value_type tmp{0, 0, key, nullptr};
    auto const iter = map_.find(tmp);
    return iter;
}

template<class _>
void
pool_t<_>::
insert(nhash_t h,
    void const* key, void const* data, nsize_t size)
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
    BOOST_ASSERT(result.second);
    data_size_ += size;
}

template<class _>
void
swap(pool_t<_>& lhs, pool_t<_>& rhs)
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

#endif
