//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_PROGRESS_HPP
#define NUDB_PROGRESS_HPP

namespace nudb {

/** Progress function that does nothing.

    This type meets the requirements of @b Progress,
    and does nothing when invoked.
*/
struct
no_progress
{
    no_progress() = default;

    /// Called to indicate progress
    void
    operator()(std::uint64_t, std::uint64_t) const noexcept
    {
    };
};

} // nudb

#endif
