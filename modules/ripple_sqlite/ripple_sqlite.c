//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

/** Add this to get the @ref ripple_sqlite module.

    @file ripple_sqlite.cpp
    @ingroup ripple_sqlite
*/

// This prevents sqlite.h from being included
//
#define RIPPLE_SQLITE_MODULE_INCLUDED 1

#include "ripple_sqlite.h"

#if BEAST_MSVC
#pragma warning (push)
#pragma warning (disable: 4100) /* unreferenced formal parameter */
#pragma warning (disable: 4127) /* conditional expression is constant */
#pragma warning (disable: 4232) /* nonstandard extension used: dllimport address */
#pragma warning (disable: 4244) /* conversion from 'int': possible loss of data */
#pragma warning (disable: 4701) /* potentially uninitialized variable */
#pragma warning (disable: 4706) /* assignment within conditional expression */
#endif

/* When compiled with SQLITE_THREADSAFE=1, SQLite operates in serialized mode.
   In this mode, SQLite can be safely used by multiple threads with no restriction.

   VFALCO NOTE This implies a global mutex!

          NOTE Windows builds never had the threading model set. However, SQLite
               defaults to Serialized (SQLITE_THREADSAFE == 1). Does Ripple need
               Serialized mode? Because Ripple already uses a mutex with every
               instance of the sqlite database session object.
*/
#define SQLITE_THREADSAFE 1

/* When compiled with SQLITE_THREADSAFE=2, SQLite can be used in a
   multithreaded program so long as no two threads attempt to use the
   same database connection at the same time.

   VFALCO NOTE This is the preferred threading model.

          TODO Determine if Ripple can support this model.
*/
//#define SQLITE_THREADSAFE 2

// VFALCO TODO We should try running with SQLITE_THREADSAFE==2 and see what happens.
#if SQLITE_THREADSAFE != 2
#pragma message(BEAST_FILEANDLINE_ "Possible performance issue, SQLITE_THREADSAFE != 2")
#endif

#include "sqlite/sqlite3.c"

#if BEAST_MSVC
#pragma warning (pop)
#endif
