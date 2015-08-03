//
// Copyright 2014 SimpliVT Corporation
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_SQLITE3_SOURCE
#include "soci/sqlite3/soci-sqlite3.h"
#include <cstring>

using namespace soci;

sqlite3_soci_error::sqlite3_soci_error(
    std::string const & msg, int result)
    : soci_error(msg), result_(result)
{
}

int sqlite3_soci_error::result() const
{
    return result_;
}
