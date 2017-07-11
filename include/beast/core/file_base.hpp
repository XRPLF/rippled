//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_CORE_FILE_BASE_HPP
#define BEAST_CORE_FILE_BASE_HPP

#include <beast/config.hpp>
#include <beast/core/string.hpp>

namespace beast {

/// The type of file path used by the library
using file_path = string_view;

/** File open modes

    These modes are used when opening files using
    instances of the @b File concept.

    @see file_stdio
*/
enum class file_mode
{
    /// Random reading
    read,

    /// Sequential reading
    scan,

    /** Random writing to a new or truncated file

        @li If the file does not exist, it is created.

        @li If the file exists, it is truncated to
        zero size upon opening.
    */
    write,

    /** Random writing to new file only

        If the file exists, an error is generated.
    */
    write_new,

    /** Random writing to existing file

        If the file does not exist, an error is generated.
    */
    write_existing,

    /** Appending to a new or truncated file

        The current file position shall be set to the end of
        the file prior to each write.

        @li If the file does not exist, it is created.

        @li If the file exists, it is truncated to
        zero size upon opening.
    */
    append,

    /** Appending to a new file only

        The current file position shall be set to the end of
        the file prior to each write.

        If the file exists, an error is generated.
    */
    append_new,

    /** Appending to an existing file

        The current file position shall be set to the end of
        the file prior to each write.

        If the file does not exist, an error is generated.
    */
    append_existing
};

} // beast

#endif
