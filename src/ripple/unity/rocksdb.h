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

#ifndef RIPPLE_UNITY_ROCKSDB_H_INCLUDED
#define RIPPLE_UNITY_ROCKSDB_H_INCLUDED

#include <ripple/beast/core/Config.h>

#ifdef __clang_major__
#if __clang_major__ >= 7
# pragma clang diagnostic push
# pragma clang diagnostic ignored "-Winconsistent-missing-override"
# pragma clang diagnostic ignored "-Wuninitialized"
#endif
#endif

#ifndef RIPPLE_ROCKSDB_AVAILABLE
#if BEAST_WIN32
# define ROCKSDB_PLATFORM_WINDOWS
#else
# define ROCKSDB_PLATFORM_POSIX
# define ROCKSDB_LIB_IO_POSIX
# if BEAST_MAC || BEAST_IOS
#  define OS_MACOSX 1
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
//#include <rocksdb2/port/port_posix.h>
#include <rocksdb2/include/rocksdb/cache.h>
#include <rocksdb2/include/rocksdb/compaction_filter.h>
#include <rocksdb2/include/rocksdb/comparator.h>
#include <rocksdb2/include/rocksdb/convenience.h>
#include <rocksdb2/include/rocksdb/db.h>
#include <rocksdb2/include/rocksdb/env.h>
#include <rocksdb2/include/rocksdb/filter_policy.h>
#include <rocksdb2/include/rocksdb/flush_block_policy.h>
#include <rocksdb2/include/rocksdb/iterator.h>
#include <rocksdb2/include/rocksdb/memtablerep.h>
#include <rocksdb2/include/rocksdb/merge_operator.h>
#include <rocksdb2/include/rocksdb/options.h>
#include <rocksdb2/include/rocksdb/perf_context.h>
#include <rocksdb2/include/rocksdb/slice.h>
#include <rocksdb2/include/rocksdb/slice_transform.h>
#include <rocksdb2/include/rocksdb/statistics.h>
#include <rocksdb2/include/rocksdb/status.h>
#include <rocksdb2/include/rocksdb/table.h>
#include <rocksdb2/include/rocksdb/table_properties.h>
#include <rocksdb2/include/rocksdb/transaction_log.h>
#include <rocksdb2/include/rocksdb/types.h>
#include <rocksdb2/include/rocksdb/universal_compaction.h>
#include <rocksdb2/include/rocksdb/write_batch.h>

#endif

#endif
