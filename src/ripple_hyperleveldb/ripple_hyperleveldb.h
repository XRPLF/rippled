//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_HYPERLEVELDB_H_INCLUDED
#define RIPPLE_HYPERLEVELDB_H_INCLUDED

#include "beast/beast/Config.h"

#if ! BEAST_WIN32

#define RIPPLE_HYPERLEVELDB_AVAILABLE 1

#include "hyperleveldb/hyperleveldb/cache.h"
#include "hyperleveldb/hyperleveldb/filter_policy.h"
#include "hyperleveldb/hyperleveldb/db.h"
#include "hyperleveldb/hyperleveldb/write_batch.h"

#else

#define RIPPLE_HYPERLEVELDB_AVAILABLE 0

#endif

#endif
