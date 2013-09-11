//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_MDB_H_INCLUDED
#define RIPPLE_MDB_H_INCLUDED

#include "beast/modules/beast_core/system/TargetPlatform.h"

#if ! BEAST_WIN32
#define RIPPLE_MDB_AVAILABLE 1

#include "mdb/libraries/liblmdb/lmdb.h"

#else
// mdb is unsupported on Win32
#define RIPPLE_MDB_AVAILBLE 0

#endif

#endif
