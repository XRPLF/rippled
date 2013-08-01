//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

// Unity build file for MDB

#include "BeastConfig.h"

#include "ripple_mdb.h"

#if RIPPLE_MDB_AVAILABLE

#if BEAST_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Waddress"
#endif

#include "mdb/libraries/liblmdb/mdb.c"
#include "mdb/libraries/liblmdb/midl.c"

#if BEAST_GCC
#pragma GCC diagnostic pop
#endif

#endif
