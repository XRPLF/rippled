//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_REKEY_HPP
#define NUDB_REKEY_HPP

#include <nudb/error.hpp>
#include <nudb/file.hpp>
#include <cstddef>
#include <cstdint>

namespace nudb {

/** Create a new key file from a data file.

    This algorithm rebuilds a key file for the given data file.
    It works efficiently by iterating the data file multiple times.
    During the iteration, a contiguous block of the key file is
    rendered in memory, then flushed to disk when the iteration is
    complete. The size of this memory buffer is controlled by the
    `bufferSize` parameter, larger is better. The algorithm works
    the fastest when `bufferSize` is large enough to hold the entire
    key file in memory; only a single iteration of the data file
    is needed in this case.

    During the rekey, spill records may be appended to the data
    file. If the rekey operation is abnormally terminated, this
    would normally result in a corrupted data file. To prevent this,
    the function creates a log file using the specified path so
    that the database can be fixed in a subsequent call to
    @ref recover.

    @note If a log file is already present, this function will
    fail with @ref error::log_file_exists.

    @par Template Parameters

    @tparam Hasher The hash function to use. This type must
    meet the requirements of @b Hasher. The hash function
    must be the same as that used to create the database, or
    else an error is returned.

    @tparam File The type of file to use. This type must meet
    the requirements of @b File.

    @param dat_path The path to the data file.

    @param key_path The path to the key file.

    @param log_path The path to the log file.

    @param blockSize The size of a key file block. Larger
    blocks hold more keys but require more I/O cycles per
    operation. The ideal block size the largest size that
    may be read in a single I/O cycle, and device dependent.
    The return value of @ref block_size returns a suitable
    value for the volume of a given path.

    @param loadFactor A number between zero and one
    representing the average bucket occupancy (number of
    items). A value of 0.5 is perfect. Lower numbers
    waste space, and higher numbers produce negligible
    savings at the cost of increased I/O cycles.

    @param itemCount The number of items in the data file.

    @param bufferSize The number of bytes to allocate for the buffer.

    @param ec Set to the error if any occurred.

    @param progress A function which will be called periodically
    as the algorithm proceeds. The equivalent signature of the
    progress function must be:
    @code
    void progress(
        std::uint64_t amount,   // Amount of work done so far
        std::uint64_t total     // Total amount of work to do
    );
    @endcode

    @param args Optional arguments passed to @b File constructors.
*/
template<
    class Hasher,
    class File,
    class Progress,
    class... Args
>
void
rekey(
    path_type const& dat_path,
    path_type const& key_path,
    path_type const& log_path,
    std::size_t blockSize,
    float loadFactor,
    std::uint64_t itemCount,
    std::size_t bufferSize,
    error_code& ec,
    Progress&& progress,
    Args&&... args);

} // nudb

#include <nudb/impl/rekey.ipp>

#endif
