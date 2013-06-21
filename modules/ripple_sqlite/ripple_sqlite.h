//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

/** Include this to get the @ref ripple_sqlite module.

    @file ripple_sqlite.h
    @ingroup ripple_sqlite
*/

/** Sqlite3 support.

    This module brings in the Sqlite embedded database engine.

    @defgroup ripple_sqlite
*/

#ifndef RIPPLE_SQLITE_RIPPLEHEADER
#define RIPPLE_SQLITE_RIPPLEHEADER

// Include this directly because we compile under both C and C++
#include "beast/modules/beast_core/system/beast_TargetPlatform.h"

#if ! RIPPLE_SQLITE_MODULE_INCLUDED
#include "sqlite/sqlite3.h"
#endif

#endif
