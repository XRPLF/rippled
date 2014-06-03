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

#include <ripple/module/core/nodestore/impl/Tuning.h>
#include <ripple/module/core/nodestore/impl/DecodedBlob.h>
#include <ripple/module/core/nodestore/impl/EncodedBlob.h>
#include <ripple/module/core/nodestore/impl/BatchWriter.h>
#include <ripple/module/core/nodestore/backend/HyperDBFactory.h>
#include <ripple/module/core/nodestore/backend/HyperDBFactory.cpp>
#include <ripple/module/core/nodestore/backend/LevelDBFactory.h>
#include <ripple/module/core/nodestore/backend/LevelDBFactory.cpp>
#include <ripple/module/core/nodestore/backend/MemoryFactory.h>
#include <ripple/module/core/nodestore/backend/MemoryFactory.cpp>
#include <ripple/module/core/nodestore/backend/NullFactory.h>
#include <ripple/module/core/nodestore/backend/NullFactory.cpp>
#include <ripple/module/core/nodestore/backend/RocksDBFactory.h>
#include <ripple/module/core/nodestore/backend/RocksDBFactory.cpp>

#include <ripple/module/core/nodestore/impl/Backend.cpp>
#include <ripple/module/core/nodestore/impl/BatchWriter.cpp>
#include <ripple/module/core/nodestore/impl/DatabaseImp.h>
#include <ripple/module/core/nodestore/impl/Database.cpp>
#include <ripple/module/core/nodestore/impl/DummyScheduler.cpp>
#include <ripple/module/core/nodestore/impl/DecodedBlob.cpp>
#include <ripple/module/core/nodestore/impl/EncodedBlob.cpp>
#include <ripple/module/core/nodestore/impl/Factory.cpp>
#include <ripple/module/core/nodestore/impl/Manager.cpp>
#include <ripple/module/core/nodestore/impl/NodeObject.cpp>
#include <ripple/module/core/nodestore/impl/Scheduler.cpp>
#include <ripple/module/core/nodestore/impl/Task.cpp>

#include <ripple/module/core/nodestore/tests/TestBase.h>
#include <ripple/module/core/nodestore/tests/BackendTests.cpp>
#include <ripple/module/core/nodestore/tests/BasicTests.cpp>
#include <ripple/module/core/nodestore/tests/DatabaseTests.cpp>
#include <ripple/module/core/nodestore/tests/TimingTests.cpp>
