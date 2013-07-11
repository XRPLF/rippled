//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

// Unity build file for MDB

#include "BeastConfig.h"

#include "ripple_mdb.h"

#include "beast/modules/beast_core/system/beast_TargetPlatform.h"

#define __LITTLE_ENDIAN 1
#define __BIG_ENDIAN 2
typedef unsigned short uint16_t;

#if BEAST_LITTLE_ENDIAN
# define BYTE_ORDER __LITTLE_ENDIAN
#elif BEAST_BIG_ENDIAN
# define BYTE_ORDER __BIG_ENDIAN
#else
# error "Unknown endianness"
#endif

#if BEAST_MSVC
#pragma warning (push)
//#pragma warning (disable: 4092)
#endif

#include "mdb/libraries/liblmdb/mdb.c"
#include "mdb/libraries/liblmdb/midl.c"
#include "mdb/libraries/liblmdb/mdb.c"

#if BEAST_MSVC
#pragma warning (pop)
#endif
