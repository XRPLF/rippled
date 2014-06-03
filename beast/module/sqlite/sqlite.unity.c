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

/** Add this to get the @ref vf_sqlite external module.

    @file beast_sqlite.c
    @ingroup beast_sqlite
*/

#if BEAST_INCLUDE_BEASTCONFIG
#include <BeastConfig.h>
#endif

// Prevents sqlite.h from being included, since it screws up the .c
#define BEAST_SQLITE_CPP_INCLUDED

#include <beast/module/sqlite/sqlite.h>

#if ! (BEAST_USE_NATIVE_SQLITE && BEAST_HAVE_NATIVE_SQLITE)

#if BEAST_MSVC
#pragma warning (push)
#pragma warning (disable: 4100) /* unreferenced formal parameter */
#pragma warning (disable: 4127) /* conditional expression is constant */
#pragma warning (disable: 4232) /* nonstandard extension used: dllimport address */
#pragma warning (disable: 4244) /* conversion from 'int': possible loss of data */
#pragma warning (disable: 4701) /* potentially uninitialized variable */
#pragma warning (disable: 4706) /* assignment within conditional expression */
#pragma warning (disable: 4996) /* 'GetVersionExA' was declared deprecated */
#endif

/* When compiled with SQLITE_THREADSAFE=1, SQLite operates in serialized mode.
   In this mode, SQLite can be safely used by multiple threads with no restriction.

   VFALCO NOTE This implies a global mutex!
*/
#define SQLITE_THREADSAFE 1

/* When compiled with SQLITE_THREADSAFE=2, SQLite can be used in a
   multithreaded program so long as no two threads attempt to use the
   same database connection at the same time.

   VFALCO NOTE This is the preferred threading model.
*/
//#define SQLITE_THREADSAFE 2

#if defined (BEAST_SQLITE_USE_NDEBUG) && BEAST_SQLITE_USE_NDEBUG && !defined (NDEBUG)
#define NDEBUG
#endif

#include <beast/module/sqlite/sqlite/sqlite3.c>

#if BEAST_MSVC
#pragma warning (pop)
#endif

#endif
