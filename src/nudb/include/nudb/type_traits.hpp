//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_TYPE_TRAITS_HPP
#define NUDB_TYPE_TRAITS_HPP

#include <cstddef>
#include <cstdint>

namespace nudb {

#if ! GENERATING_DOCS

namespace detail {

// Holds a full digest
using nhash_t = std::uint64_t;

} // detail

/** Holds a bucket index or bucket count.

    The maximum number of buckets in a key file is 2^32-1.
*/
//using nbuck_t = std::uint32_t;
using nbuck_t = std::size_t;

/** Holds a key index or count in bucket.

    A bucket is limited to 2^16-1 items. The practical
    limit is lower, since a bucket cannot be larger than
    the block size.
*/
//using nkey_t = std::uint16_t;
using nkey_t = std::size_t;

/** Holds a file size or offset.

    Operating system support for large files is required.
    Practically, data files cannot exceed 2^48 since offsets
    are stored as 48 bit unsigned values.
*/
using noff_t = std::uint64_t;

/** Holds a block, key, or value size.

    Block size is limited to 2^16

    Key file blocks are limited to the block size.

    Value sizes are limited to 2^31-1.
*/
using nsize_t = std::size_t;

#endif

} // nudb

#endif
