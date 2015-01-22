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

#if defined(ENABLE_SOCI_POSTGRESQL) && ENABLE_SOCI_POSTGRESQL

#if BEAST_INCLUDE_BEASTCONFIG
#include <BeastConfig.h>
#endif

#include <algorithm>

#if BEAST_MSVC
#define SOCI_LIB_PREFIX ""
#define SOCI_LIB_SUFFIX ".dll"
#else
#define SOCI_LIB_PREFIX "lib"
#define SOCI_LIB_SUFFIX ".so"
#endif

#include<backends/postgresql/blob.cpp>
#include<backends/postgresql/common.cpp>
#include<backends/postgresql/error.cpp>
#include<backends/postgresql/factory.cpp>
#include<backends/postgresql/row-id.cpp>
#include<backends/postgresql/session.cpp>
#include<backends/postgresql/standard-into-type.cpp>
#include<backends/postgresql/standard-use-type.cpp>
#include<backends/postgresql/statement.cpp>
#include<backends/postgresql/vector-into-type.cpp>
#include<backends/postgresql/vector-use-type.cpp>

#endif // ENABLE_SOCI_POSTGRESQL
