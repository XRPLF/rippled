//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_VISIT_HPP
#define NUDB_VISIT_HPP

#include <nudb/error.hpp>
#include <nudb/file.hpp>

namespace nudb {

/** Visit each key/data pair in a data file.

    This function will open and iterate the contents of a
    data file, invoking the callback for each key/value
    pair found. Only a data file is necessary, the key
    file may be omitted.

    @param path The path to the data file.

    @param callback A function which will be called with
    each item found in the data file. The equivalent signature
    of the callback must be:
    @code
    void callback(
        void const* key,        // A pointer to the item key
        std::size_t key_size,   // The size of the key (always the same)
        void const* data,       // A pointer to the item data
        std::size_t data_size,  // The size of the item data
        error_code& ec          // Indicates an error (out parameter)
    );    
    @endcode
    If the callback sets ec to an error, the visit is terminated.

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
template<class Callback, class Progress>
void
visit(
    path_type const& path,
    Callback&& callback,
    Progress&& progress,
    error_code& ec);

} // nudb

#include <nudb/impl/visit.ipp>

#endif
