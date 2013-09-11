//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

// Unity build file for LevelDB

#include "BeastConfig.h"

#include "ripple_hyperleveldb.h"

#if RIPPLE_HYPERLEVELDB_AVAILABLE

// Set the appropriate LevelDB platform macro based on our platform.
//
#if BEAST_WIN32
 #define LEVELDB_PLATFORM_WINDOWS

#else
 #define LEVELDB_PLATFORM_POSIX

 #if BEAST_MAC || BEAST_IOS
  #define OS_MACOSX

 // VFALCO TODO Distinguish between BEAST_BSD and BEAST_FREEBSD
 #elif BEAST_BSD
  #define OS_FREEBSD

 #endif

#endif

#if BEAST_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreorder"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif

#include "hyperleveldb/db/builder.cc"
#include "hyperleveldb/db/db_impl.cc"
#include "hyperleveldb/db/db_iter.cc"
#include "hyperleveldb/db/dbformat.cc"
#include "hyperleveldb/db/filename.cc"
#include "hyperleveldb/db/log_reader.cc"
#include "hyperleveldb/db/log_writer.cc"
#include "hyperleveldb/db/memtable.cc"
#include "hyperleveldb/db/repair.cc"
#include "hyperleveldb/db/table_cache.cc"
#include "hyperleveldb/db/version_edit.cc"
#include "hyperleveldb/db/version_set.cc"
#include "hyperleveldb/db/write_batch.cc"

#include "hyperleveldb/table/block.cc"
#include "hyperleveldb/table/block_builder.cc"
#include "hyperleveldb/table/filter_block.cc"
#include "hyperleveldb/table/format.cc"
#include "hyperleveldb/table/iterator.cc"
#include "hyperleveldb/table/merger.cc"
#include "hyperleveldb/table/table.cc"
#include "hyperleveldb/table/table_builder.cc"
#include "hyperleveldb/table/two_level_iterator.cc"

#include "hyperleveldb/util/arena.cc"
#include "hyperleveldb/util/bloom.cc"
#include "hyperleveldb/util/cache.cc"
#include "hyperleveldb/util/coding.cc"
#include "hyperleveldb/util/comparator.cc"
#include "hyperleveldb/util/crc32c.cc"
#include "hyperleveldb/util/env.cc"
#include "hyperleveldb/util/filter_policy.cc"
#include "hyperleveldb/util/hash.cc"
#include "hyperleveldb/util/histogram.cc"
#include "hyperleveldb/util/logging.cc"
#include "hyperleveldb/util/options.cc"
#include "hyperleveldb/util/status.cc"

// Platform Specific

#if defined (LEVELDB_PLATFORM_WINDOWS)
# include "hyperleveldb/util/env_win.cc"
# include "hyperleveldb/port/port_win.cc"

#elif defined (LEVELDB_PLATFORM_POSIX)
# include "hyperleveldb/util/env_posix.cc"
# include "hyperleveldb/port/port_posix.cc"

#elif defined (LEVELDB_PLATFORM_ANDROID)
# error Missing Android port!

#endif

#if BEAST_GCC
#pragma GCC diagnostic pop
#endif

#endif
