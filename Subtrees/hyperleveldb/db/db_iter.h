// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_HYPERLEVELDB_DB_DB_ITER_H_
#define STORAGE_HYPERLEVELDB_DB_DB_ITER_H_

#include <stdint.h>
#include "../hyperleveldb/db.h"
#include "dbformat.h"

namespace hyperleveldb {

// Return a new iterator that converts internal keys (yielded by
// "*internal_iter") that were live at the specified "sequence" number
// into appropriate user keys.
extern Iterator* NewDBIterator(
    const std::string* dbname,
    Env* env,
    const Comparator* user_key_comparator,
    Iterator* internal_iter,
    const SequenceNumber& sequence);

}  // namespace hyperleveldb

#endif  // STORAGE_HYPERLEVELDB_DB_DB_ITER_H_
