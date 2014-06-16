//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

// Unity build file for LevelDB

#include <BeastConfig.h>

#include <ripple/unity/leveldb.h>

#include <beast/Config.h>

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

#include <leveldb/db/builder.cc>
#include <leveldb/db/db_impl.cc>
#include <leveldb/db/db_iter.cc>
#include <leveldb/db/dbformat.cc>
#include <leveldb/db/filename.cc>
#include <leveldb/db/log_reader.cc>
#include <leveldb/db/log_writer.cc>
#include <leveldb/db/memtable.cc>
#include <leveldb/db/repair.cc>
#include <leveldb/db/table_cache.cc>
#include <leveldb/db/version_edit.cc>
#include <leveldb/db/version_set.cc>
#include <leveldb/db/write_batch.cc>

#include <leveldb/table/block.cc>
#include <leveldb/table/block_builder.cc>
#include <leveldb/table/filter_block.cc>
#include <leveldb/table/format.cc>
#include <leveldb/table/iterator.cc>
#include <leveldb/table/merger.cc>
#include <leveldb/table/table.cc>
#include <leveldb/table/table_builder.cc>
#include <leveldb/table/two_level_iterator.cc>

#include <leveldb/util/arena.cc>
#include <leveldb/util/bloom.cc>
#include <leveldb/util/cache.cc>
#include <leveldb/util/coding.cc>
#include <leveldb/util/comparator.cc>
#include <leveldb/util/crc32c.cc>
#include <leveldb/util/env.cc>
#include <leveldb/util/filter_policy.cc>
#include <leveldb/util/hash.cc>
#include <leveldb/util/histogram.cc>
#include <leveldb/util/logging.cc>
#include <leveldb/util/options.cc>
#include <leveldb/util/status.cc>

// Platform Specific

#if defined (LEVELDB_PLATFORM_WINDOWS)
#include <leveldb/util/env_win.cc>
#include <leveldb/port/port_win.cc>

#elif defined (LEVELDB_PLATFORM_POSIX)
#include <leveldb/util/env_posix.cc>
#include <leveldb/port/port_posix.cc>

#elif defined (LEVELDB_PLATFORM_ANDROID)
# error Missing Android port!

#endif
