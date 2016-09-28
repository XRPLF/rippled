//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_VERIFY_HPP
#define NUDB_VERIFY_HPP

#include <nudb/file.hpp>
#include <nudb/type_traits.hpp>
#include <nudb/detail/bucket.hpp>
#include <nudb/detail/bulkio.hpp>
#include <nudb/detail/format.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

namespace nudb {

/// Describes database statistics calculated by @ref verify.
struct verify_info
{
    /** Indicates the verify algorithm used.

        @li @b 0 Normal algorithm
        @li @b 1 Fast algorith
    */
    int algorithm;                      // 0 = normal, 1 = fast

    /// The path to the data file
    path_type dat_path;

    /// The path to the key file
    path_type key_path;

    /// The API version used to create the database
    std::size_t version = 0;

    /// The unique identifier
    std::uint64_t uid = 0;

    /// The application-defined constant
    std::uint64_t appnum = 0;

    /// The size of each key, in bytes
    nsize_t key_size = 0;

    /// The salt used in the key file
    std::uint64_t salt = 0;

    /// The salt fingerprint
    std::uint64_t pepper = 0;

    /// The block size used in the key file
    nsize_t block_size = 0;

    /// The target load factor used in the key file
    float load_factor = 0;

    /// The maximum number of keys each bucket can hold
    nkey_t capacity = 0;

    /// The number of buckets in the key file
    nbuck_t buckets = 0;

    /// The size of a bucket in bytes
    nsize_t bucket_size = 0;

    /// The size of the key file
    noff_t key_file_size = 0;

    /// The size of the data file
    noff_t dat_file_size = 0;

    /// The number of keys found
    std::uint64_t key_count = 0;

    /// The number of values found
    std::uint64_t value_count = 0;

    /// The total number of bytes occupied by values
    std::uint64_t value_bytes = 0;

    /// The number of spill records in use
    std::uint64_t spill_count = 0;

    /// The total number of spill records
    std::uint64_t spill_count_tot = 0;

    /// The number of bytes occupied by spill records in use
    std::uint64_t spill_bytes = 0;

    /// The number of bytes occupied by all spill records
    std::uint64_t spill_bytes_tot = 0;

    /// Average number of key file reads per fetch
    float avg_fetch = 0;

    /// The fraction of the data file that is wasted
    float waste = 0;

    /// The data amplification ratio
    float overhead = 0;

    /// The measured bucket load fraction
    float actual_load = 0;

    /// A histogram of the number of buckets having N spill records
    std::array<nbuck_t, 10> hist;

    /// Default constructor
    verify_info()
    {
        hist.fill(0);
    }
};

/** Verify consistency of the key and data files.

    This function opens the key and data files, and
    performs the following checks on the contents:

    @li Data file header validity

    @li Key file header validity

    @li Data and key file header agreements

    @li Check that each value is contained in a bucket

    @li Check that each bucket item reflects a value

    @li Ensure no values with duplicate keys

    Undefined behavior results when verifying a database
    that still has a log file. Use @ref recover on such
    databases first.

    This function selects one of two algorithms to use, the
    normal version, and a faster version that can take advantage
    of a buffer of sufficient size. Depending on the value of
    the bufferSize argument, the appropriate algorithm is chosen.

    A good value of bufferSize is one that is a large fraction
    of the key file size. For example, 20% of the size of the
    key file. Larger is better, with the highest usable value
    depending on the size of the key file. If presented with
    a buffer size that is too large to be of extra use, the
    fast algorithm will simply allocate what it needs.

    @par Template Parameters

    @tparam Hasher The hash function to use. This type must
    meet the requirements of @b HashFunction. The hash function
    must be the same as that used to create the database, or
    else an error is returned.

    @param info A structure which will be default constructed
    inside this function, and filled in if the operation completes
    successfully. If an error is indicated, the contents of this
    variable are undefined.

    @param dat_path The path to the data file.

    @param key_path The path to the key file.

    @param bufferSize The number of bytes to allocate for the buffer.
    If this number is too small, or zero, a slower algorithm will be
    used that does not require a buffer.

    @param progress A function which will be called periodically
    as the algorithm proceeds. The equivalent signature of the
    progress function must be:
    @code
    void progress(
        std::uint64_t amount,   // Amount of work done so far
        std::uint64_t total     // Total amount of work to do
    );
    @endcode

    @param ec Set to the error, if any occurred.
*/
template<class Hasher, class Progress>
void
verify(
    verify_info& info,
    path_type const& dat_path,
    path_type const& key_path,
    std::size_t bufferSize,
    Progress&& progress,
    error_code& ec);

} // nudb

#include <nudb/impl/verify.ipp>

#endif
