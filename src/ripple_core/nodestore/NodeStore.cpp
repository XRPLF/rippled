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

#include <memory>
#include <vector>

// backend support
#include <ripple_hyperleveldb/ripple_hyperleveldb.h>
#include <ripple_leveldb/ripple_leveldb.h>
#include <ripple/rocksdb/ripple_rocksdb.h>

#include <beast/cxx14/memory.h>

#include <ripple/common/seconds_clock.h>
#include <ripple/common/TaggedCache.h>
#include <ripple/common/KeyCache.h>

#include <ripple_core/nodestore/impl/Tuning.h>
#include <ripple_core/nodestore/impl/DecodedBlob.h>
#include <ripple_core/nodestore/impl/EncodedBlob.h>
#include <ripple_core/nodestore/impl/BatchWriter.h>
#include <ripple_core/nodestore/backend/HyperDBFactory.h>
#include <ripple_core/nodestore/backend/HyperDBFactory.cpp>
#include <ripple_core/nodestore/backend/LevelDBFactory.h>
#include <ripple_core/nodestore/backend/LevelDBFactory.cpp>
#include <ripple_core/nodestore/backend/MemoryFactory.h>
#include <ripple_core/nodestore/backend/MemoryFactory.cpp>
#include <ripple_core/nodestore/backend/NullFactory.h>
#include <ripple_core/nodestore/backend/NullFactory.cpp>
#include <ripple_core/nodestore/backend/RocksDBFactory.h>
#include <ripple_core/nodestore/backend/RocksDBFactory.cpp>

#include <ripple_core/nodestore/impl/Backend.cpp>
#include <ripple_core/nodestore/impl/BatchWriter.cpp>
#include <ripple_core/nodestore/impl/DatabaseImp.h>
#include <ripple_core/nodestore/impl/Database.cpp>
#include <ripple_core/nodestore/impl/DummyScheduler.cpp>
#include <ripple_core/nodestore/impl/DecodedBlob.cpp>
#include <ripple_core/nodestore/impl/EncodedBlob.cpp>
#include <ripple_core/nodestore/impl/Factory.cpp>
#include <ripple_core/nodestore/impl/Manager.cpp>
#include <ripple_core/nodestore/impl/NodeObject.cpp>
#include <ripple_core/nodestore/impl/Scheduler.cpp>
#include <ripple_core/nodestore/impl/Task.cpp>

#include <ripple_core/nodestore/tests/TestBase.h>
#include <ripple_core/nodestore/tests/BackendTests.cpp>
#include <ripple_core/nodestore/tests/BasicTests.cpp>
#include <ripple_core/nodestore/tests/DatabaseTests.cpp>
#include <ripple_core/nodestore/tests/TimingTests.cpp>
