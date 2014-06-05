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

#ifndef RIPPLE_ROCKSDB_H_INCLUDED
#define RIPPLE_ROCKSDB_H_INCLUDED

#include <beast/Config.h>

#ifndef RIPPLE_ROCKSDB_AVAILABLE
#if BEAST_WIN32
# define ROCKSDB_PLATFORM_WINDOWS
#else
# define ROCKSDB_PLATFORM_POSIX
# if BEAST_MAC || BEAST_IOS
#  define OS_MACOSX
# elif BEAST_BSD
#  define OS_FREEBSD
# else
#  define OS_LINUX
# endif
#endif
# if BEAST_WIN32
#  define RIPPLE_ROCKSDB_AVAILABLE 0
# else
#  if __cplusplus >= 201103L
#   define RIPPLE_ROCKSDB_AVAILABLE 1
#  else
#   define RIPPLE_ROCKSDB_AVAILABLE 0
#  endif
# endif
#endif

#if RIPPLE_ROCKSDB_AVAILABLE
#define SNAPPY
//#include <rocksdb/port/port_posix.h>
#include <rocksdb/include/rocksdb/cache.h>
#include <rocksdb/include/rocksdb/compaction_filter.h>
#include <rocksdb/include/rocksdb/comparator.h>
#include <rocksdb/include/rocksdb/db.h>
#include <rocksdb/include/rocksdb/env.h>
#include <rocksdb/include/rocksdb/filter_policy.h>
#include <rocksdb/include/rocksdb/flush_block_policy.h>
#include <rocksdb/include/rocksdb/iterator.h>
#include <rocksdb/include/rocksdb/memtablerep.h>
#include <rocksdb/include/rocksdb/merge_operator.h>
#include <rocksdb/include/rocksdb/options.h>
#include <rocksdb/include/rocksdb/perf_context.h>
#include <rocksdb/include/rocksdb/slice.h>
#include <rocksdb/include/rocksdb/slice_transform.h>
#include <rocksdb/include/rocksdb/statistics.h>
#include <rocksdb/include/rocksdb/status.h>
#include <rocksdb/include/rocksdb/table.h>
#include <rocksdb/include/rocksdb/table_properties.h>
#include <rocksdb/include/rocksdb/transaction_log.h>
#include <rocksdb/include/rocksdb/types.h>
#include <rocksdb/include/rocksdb/universal_compaction.h>
#include <rocksdb/include/rocksdb/write_batch.h>

#endif

#endif
