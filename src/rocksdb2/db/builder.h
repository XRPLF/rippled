//  Copyright (c) 2013, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
#pragma once
#include "rocksdb/comparator.h"
#include "rocksdb/env.h"
#include "rocksdb/status.h"
#include "rocksdb/types.h"
#include "rocksdb/options.h"

namespace rocksdb {

struct Options;
struct FileMetaData;

class Env;
struct EnvOptions;
class Iterator;
class TableCache;
class VersionEdit;
class TableBuilder;
class WritableFile;

extern TableBuilder* NewTableBuilder(
    const Options& options, const InternalKeyComparator& internal_comparator,
    WritableFile* file, CompressionType compression_type);

// Build a Table file from the contents of *iter.  The generated file
// will be named according to number specified in meta. On success, the rest of
// *meta will be filled with metadata about the generated table.
// If no data is present in *iter, meta->file_size will be set to
// zero, and no Table file will be produced.
extern Status BuildTable(const std::string& dbname, Env* env,
                         const Options& options, const EnvOptions& soptions,
                         TableCache* table_cache, Iterator* iter,
                         FileMetaData* meta,
                         const InternalKeyComparator& internal_comparator,
                         const SequenceNumber newest_snapshot,
                         const SequenceNumber earliest_seqno_in_memtable,
                         const CompressionType compression,
                         const Env::IOPriority io_priority = Env::IO_HIGH);

}  // namespace rocksdb
