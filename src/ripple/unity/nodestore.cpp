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

#include <BeastConfig.h>

#include <ripple/nodestore/backend/MemoryFactory.cpp>
#include <ripple/nodestore/backend/NuDBFactory.cpp>
#include <ripple/nodestore/backend/NullFactory.cpp>
#include <ripple/nodestore/backend/RocksDBFactory.cpp>
#include <ripple/nodestore/backend/RocksDBQuickFactory.cpp>

#include <ripple/nodestore/impl/BatchWriter.cpp>
#include <ripple/nodestore/impl/Database.cpp>
#include <ripple/nodestore/impl/DatabaseNodeImp.cpp>
#include <ripple/nodestore/impl/DatabaseRotatingImp.cpp>
#include <ripple/nodestore/impl/DatabaseShardImp.cpp>
#include <ripple/nodestore/impl/DummyScheduler.cpp>
#include <ripple/nodestore/impl/DecodedBlob.cpp>
#include <ripple/nodestore/impl/EncodedBlob.cpp>
#include <ripple/nodestore/impl/ManagerImp.cpp>
#include <ripple/nodestore/impl/NodeObject.cpp>
#include <ripple/nodestore/impl/Shard.cpp>
