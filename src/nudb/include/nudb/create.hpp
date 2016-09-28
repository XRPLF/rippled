//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_CREATE_HPP
#define NUDB_CREATE_HPP

#include <nudb/native_file.hpp>
#include <nudb/detail/bucket.hpp>
#include <nudb/detail/format.hpp>
#include <algorithm>
#include <cstring>
#include <random>
#include <stdexcept>
#include <utility>

namespace nudb {

/** Return a random salt.

    This function will use the system provided random
    number device to generate a uniformly distributed
    64-bit unsigned value suitable for use the salt
    value in a call to @ref create.
*/
template<class = void>
std::uint64_t
make_salt();

/** Create a new database.

    This function creates a set of new database files with
    the given parameters. The files must not already exist or
    else an error is returned.

    If an error occurs while the files are being created,
    the function attempts to remove the files before
    returning.

    @par Example
    @code
        error_code ec;
        create<xxhasher>(
            "db.dat", "db.key", "db.log",
                1, make_salt(), 8, 4096, 0.5f, ec);
    @endcode

    @par Template Parameters

    @tparam Hasher The hash function to use. This type must
    meet the requirements of @b Hasher. The same hash
    function must be used every time the database is opened,
    or else an error is returned. The provided @ref xxhasher
    is a suitable general purpose hash function.

    @tparam File The type of file to use. Use the default of
    @ref native_file unless customizing the file behavior.

    @param dat_path The path to the data file.

    @param key_path The path to the key file.

    @param log_path The path to the log file.

    @param appnum A caller-defined value stored in the file
    headers. When opening the database, the same value is
    preserved and returned to the caller.

    @param salt A random unsigned integer used to permute
    the hash function to make it unpredictable. The return
    value of @ref make_salt returns a suitable value.

    @param key_size The number of bytes in each key.

    @param blockSize The size of a key file block. Larger
    blocks hold more keys but require more I/O cycles per
    operation. The ideal block size the largest size that
    may be read in a single I/O cycle, and device dependent.
    The return value of @ref block_size returns a suitable
    value for the volume of a given path.
    
    @param load_factor A number between zero and one
    representing the average bucket occupancy (number of
    items). A value of 0.5 is perfect. Lower numbers
    waste space, and higher numbers produce negligible
    savings at the cost of increased I/O cycles.

    @param ec Set to the error, if any occurred.

    @param args Optional arguments passed to @b File constructors.
*/
template<
    class Hasher,
    class File = native_file,
    class... Args
>
void
create(
    path_type const& dat_path,
    path_type const& key_path,
    path_type const& log_path,
    std::uint64_t appnum,
    std::uint64_t salt,
    nsize_t key_size,
    nsize_t blockSize,
    float load_factor,
    error_code& ec,
    Args&&... args);

} // nudb

#include <nudb/impl/create.ipp>

#endif
