//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <BeastConfig.h>

#if BEAST_MSVC
#define SOCI_LIB_PREFIX ""
#define SOCI_LIB_SUFFIX ".dll"
#else
#define SOCI_LIB_PREFIX "lib"
#define SOCI_LIB_SUFFIX ".so"
#endif

#include <algorithm> // needed for std::max

// Core soci
#include <core/backend-loader.cpp>
#include <core/blob.cpp>
#include <core/connection-parameters.cpp>
#include <core/connection-pool.cpp>
#include <core/error.cpp>
#include <core/into-type.cpp>
#include <core/once-temp-type.cpp>
#include <core/prepare-temp-type.cpp>
#include <core/procedure.cpp>
#include <core/ref-counted-prepare-info.cpp>
#include <core/ref-counted-statement.cpp>
#include <core/row.cpp>
#include <core/rowid.cpp>
#include <core/session.cpp>
#include <core/soci-simple.cpp>
#include <core/statement.cpp>
#include <core/transaction.cpp>
#include <core/use-type.cpp>
#include <core/values.cpp>


#include <backends/sqlite3/blob.cpp>
#include <backends/sqlite3/common.cpp>
#include <backends/sqlite3/factory.cpp>
#include <backends/sqlite3/row-id.cpp>
#include <backends/sqlite3/session.cpp>
#include <backends/sqlite3/standard-into-type.cpp>
#include <backends/sqlite3/standard-use-type.cpp>
#include <backends/sqlite3/statement.cpp>
#include <backends/sqlite3/vector-into-type.cpp>
#include <backends/sqlite3/vector-use-type.cpp>

#include <ripple/app/data/SociDB.cpp>
#include <ripple/app/data/tests/SociDB.test.cpp>
