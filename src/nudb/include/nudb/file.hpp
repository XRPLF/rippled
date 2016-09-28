//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_FILE_HPP
#define NUDB_FILE_HPP

#include <cstddef>
#include <string>

namespace nudb {

/// The type used to hold paths to files
using path_type = std::string;

/** Returns the best guess at the volume's block size.

    @param path A path to a file on the device. The file does
    not need to exist.
*/
inline
std::size_t
block_size(path_type const& path)
{
    // A reasonable default for many SSD devices
    return 4096;
}

/** File create and open modes.

    These are used by @ref native_file.
*/
enum class file_mode
{
    /// Open the file for sequential reads
    scan,

    /// Open the file for random reads
    read,

    /// Open the file for random reads and appending writes
    append,

    /// Open the file for random reads and writes
    write
};

} // nudb

#endif
