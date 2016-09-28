//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_XXHASHER_HPP
#define NUDB_XXHASHER_HPP

#include <nudb/detail/xxhash.hpp>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace nudb {

/** A Hasher that uses xxHash.

    This object meets the requirements of @b Hasher. It is
    the default hash function unless otherwise specified.
*/
class xxhasher
{
    std::uint64_t seed_;

public:
    using result_type = std::uint64_t;

    explicit
    xxhasher(std::uint64_t seed)
        : seed_(seed)
    {
    }

    result_type
    operator()(void const* data, std::size_t bytes) const noexcept
    {
        return detail::XXH64(data, bytes, seed_);
    }
};

} // nudb

#endif
