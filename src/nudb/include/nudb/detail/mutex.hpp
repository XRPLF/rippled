//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_DETAIL_MUTEX_HPP
#define NUDB_DETAIL_MUTEX_HPP

#include <boost/thread/lock_types.hpp>
#include <boost/thread/shared_mutex.hpp>

namespace nudb {
namespace detail {

using shared_lock_type =
    boost::shared_lock<boost::shared_mutex>;

using unique_lock_type =
    boost::unique_lock<boost::shared_mutex>;

} // detail
} // nudb

#endif
