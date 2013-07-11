//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

// Unity build file for MDB

#include "BeastConfig.h"

#include "ripple_mdb.h"

#include "beast/modules/beast_core/system/beast_TargetPlatform.h"

#if RIPPLE_MDB_AVAILABLE

#include "mdb/libraries/liblmdb/mdb.c"
#include "mdb/libraries/liblmdb/midl.c"

#endif
