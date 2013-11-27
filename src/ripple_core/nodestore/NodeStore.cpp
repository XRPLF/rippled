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

// backend support
#include "beast/modules/beast_db/beast_db.h"
#include "../ripple_hyperleveldb/ripple_hyperleveldb.h"
#include "../ripple_leveldb/ripple_leveldb.h"
#include "../ripple_mdb/ripple_mdb.h"
#include "../ripple/rocksdb/ripple_rocksdb.h"

namespace ripple {

#  include "impl/DecodedBlob.h"
#  include "impl/EncodedBlob.h"
#  include "impl/BatchWriter.h"
# include "backend/HyperDBFactory.h"
#include "backend/HyperDBFactory.cpp"
# include "backend/KeyvaDBFactory.h"
#include "backend/KeyvaDBFactory.cpp"
# include "backend/LevelDBFactory.h"
#include "backend/LevelDBFactory.cpp"
# include "backend/MemoryFactory.h"
#include "backend/MemoryFactory.cpp"
# include "backend/NullFactory.h"
#include "backend/NullFactory.cpp"
# include "backend/MdbFactory.h"
#include "backend/MdbFactory.cpp"
# include "backend/RocksDBFactory.h"
#include "backend/RocksDBFactory.cpp"

#include "impl/BatchWriter.cpp"
# include "impl/Factories.h"
# include "impl/DatabaseImp.h"
#include "impl/DummyScheduler.cpp"
#include "impl/DecodedBlob.cpp"
#include "impl/EncodedBlob.cpp"
#include "impl/NodeObject.cpp"

# include "tests/TestBase.h"
#include "tests/BackendTests.cpp"
#include "tests/BasicTests.cpp"
#include "tests/DatabaseTests.cpp"
#include "tests/TimingTests.cpp"

}
