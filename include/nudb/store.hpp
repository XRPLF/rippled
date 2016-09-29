//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_STORE_HPP
#define NUDB_STORE_HPP

#include <nudb/basic_store.hpp>
#include <nudb/native_file.hpp>
#include <nudb/xxhasher.hpp>

namespace nudb {

/** A key/value database.

    The @b Hasher used is is @ref xxhasher, which works very
    well for almost all cases. The @b File is @ref native_file which
    works on Windows and POSIX platforms.
*/
using store = basic_store<xxhasher, native_file>;

} // nudb

#endif
