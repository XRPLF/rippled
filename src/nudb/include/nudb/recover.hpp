//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_RECOVER_HPP
#define NUDB_RECOVER_HPP

#include <nudb/error.hpp>
#include <nudb/native_file.hpp>

namespace nudb {

/** Perform recovery on a database.

    This implements the recovery algorithm by rolling back
    any partially committed data. If no log file is present,
    the function does nothing.

    During the commit phase of a NuDB database, a log file
    is generated with information that may be used to roll
    back the results of a partial commit. This function
    checks for the presence of a log file. If present, the
    log file is replayed on the key and data files belonging
    to the database, restoring the database to its state
    before the partial commit. When @ref recover is
    successful, it erases the log file.

    It is normally not necessary to call this function
    directly, it is called automatically when a database is
    opened in a call to @ref basic_store::open. Callers may
    use this function to implement auxiliary tools for
    manipulating the database.

    @par Template Parameters

    @tparam Hasher The hash function to use. This type must
    meet the requirements of @b Hasher. The hash function
    must be the same as that used to create the database, or
    else an error is returned.

    @tparam File The type of file to use. Use the default of
    @ref native_file unless customizing the file behavior.

    @param dat_path The path to the data file.

    @param key_path The path to the key file.

    @param log_path The path to the log file.

    @param args Optional parameters passed to File constructors.

    @param ec Set to the error, if any occurred.
*/
template<
    class Hasher,
    class File = native_file,
    class... Args>
void
recover(
    path_type const& dat_path,
    path_type const& key_path,
    path_type const& log_path,
    error_code& ec,
    Args&&... args);

} // nudb

#include <nudb/impl/recover.ipp>

#endif
