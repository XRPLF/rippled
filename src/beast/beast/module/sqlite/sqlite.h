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

#ifndef BEAST_SQLITE_H_INCLUDED
#define BEAST_SQLITE_H_INCLUDED

/**  A self-contained, serverless, zero configuration, transactional SQL engine.

    This external module provides the SQLite embedded database library.

    SQLite is public domain software, visit http://sqlite.org
*/

#include <beast/config/PlatformConfig.h>

#if BEAST_IOS || BEAST_MAC
# define BEAST_HAVE_NATIVE_SQLITE 1
#else
# define BEAST_HAVE_NATIVE_SQLITE 0
#endif

#ifndef BEAST_SQLITE_CPP_INCLUDED
# if BEAST_USE_NATIVE_SQLITE && BEAST_HAVE_NATIVE_SQLITE
#include <sqlite3.h>
# else
#include <beast/module/sqlite/sqlite/sqlite3.h> // Amalgamated
# endif
#endif

#endif
