//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_MDB_H_INCLUDED
#define RIPPLE_MDB_H_INCLUDED

#if ! BEAST_WIN32
#define RIPPLE_MDB_AVAILABLE 1

#include "mdb/libraries/liblmdb/lmdb.h"

#else
// mdb is unsupported on Win32
#define RIPPLE_MDB_AVAILBLE 0

#endif

#endif
