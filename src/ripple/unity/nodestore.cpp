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
#include <ripple/unity/leveldb.h>
#include <ripple/unity/hyperleveldb.h>
#include <ripple/unity/rocksdb.h>

#include <beast/cxx14/memory.h>

#include <ripple/common/seconds_clock.h>
#include <ripple/common/TaggedCache.h>
#include <ripple/common/KeyCache.h>

#include <ripple/nodestore/impl/Tuning.h>
#include <ripple/nodestore/impl/DecodedBlob.h>
#include <ripple/nodestore/impl/EncodedBlob.h>
#include <ripple/nodestore/impl/BatchWriter.h>
#include <ripple/nodestore/backend/HyperDBFactory.h>
#include <ripple/nodestore/backend/HyperDBFactory.cpp>
#include <ripple/nodestore/backend/LevelDBFactory.h>
#include <ripple/nodestore/backend/LevelDBFactory.cpp>
#include <ripple/nodestore/backend/MemoryFactory.h>
#include <ripple/nodestore/backend/MemoryFactory.cpp>
#include <ripple/nodestore/backend/NullFactory.h>
#include <ripple/nodestore/backend/NullFactory.cpp>
#include <ripple/nodestore/backend/RocksDBFactory.h>
#include <ripple/nodestore/backend/RocksDBFactory.cpp>

#include <ripple/nodestore/impl/Backend.cpp>
#include <ripple/nodestore/impl/BatchWriter.cpp>
#include <ripple/nodestore/impl/DatabaseImp.h>
#include <ripple/nodestore/impl/Database.cpp>
#include <ripple/nodestore/impl/DummyScheduler.cpp>
#include <ripple/nodestore/impl/DecodedBlob.cpp>
#include <ripple/nodestore/impl/EncodedBlob.cpp>
#include <ripple/nodestore/impl/Factory.cpp>
#include <ripple/nodestore/impl/Manager.cpp>
#include <ripple/nodestore/impl/NodeObject.cpp>
#include <ripple/nodestore/impl/Scheduler.cpp>
#include <ripple/nodestore/impl/Task.cpp>

#include <ripple/nodestore/tests/TestBase.h>
#include <ripple/nodestore/tests/BackendTests.cpp>
#include <ripple/nodestore/tests/BasicTests.cpp>
#include <ripple/nodestore/tests/DatabaseTests.cpp>
#include <ripple/nodestore/tests/TimingTests.cpp>
