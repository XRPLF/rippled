//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs

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

#ifndef RIPPLE_APP_H_INCLUDED
#define RIPPLE_APP_H_INCLUDED

#if BEAST_LINUX || BEAST_MAC || BEAST_BSD
#include <sys/resource.h>
#endif

// VFALCO TODO Reduce these boost dependencies. Make more interfaces
//             purely abstract and move implementation into .cpp files.
//

#include <boost/algorithm/string/predicate.hpp>
#include <boost/array.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/mem_fn.hpp>
#include <boost/pointer_cast.hpp>
#include <boost/program_options.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/thread/shared_mutex.hpp>

//------------------------------------------------------------------------------

#include <ripple/unity/types.h>
#include <ripple/unity/data.h>
#include <ripple/unity/net.h>

#include <ripple/common/Resolver.h>

// VFALCO TODO Remove this include
#include <beast/module/sqlite/sqlite.h>

// Order matters here. If you get compile errors,
// reorder the include lines until the order is correct.

#include <ripple/common/KeyCache.h>
#include <ripple/common/TaggedCache.h>

#include <ripple/app/data/Database.h>
#include <ripple/app/data/DatabaseCon.h>
#include <ripple/app/data/SqliteDatabase.h>
#include <ripple/app/data/DBInit.h>
#include <ripple/app/shamap/SHAMapItem.h>
#include <ripple/app/shamap/SHAMapNodeID.h>
#include <ripple/app/shamap/SHAMapTreeNode.h>
#include <ripple/app/shamap/SHAMapMissingNode.h>
#include <ripple/app/shamap/SHAMapSyncFilter.h>
#include <ripple/app/shamap/SHAMapAddNode.h>
#include <ripple/app/shamap/SHAMap.h>
#include <ripple/app/misc/SerializedTransaction.h>
#include <ripple/app/misc/SerializedLedger.h>
#include <ripple/app/tx/TransactionMeta.h>
#include <ripple/app/tx/Transaction.h>
#include <ripple/app/misc/AccountState.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/ledger/SerializedValidation.h>
#include <ripple/app/main/LoadManager.h>
#include <ripple/app/misc/OrderBook.h>
#include <ripple/app/shamap/SHAMapSyncFilters.h>
#include <ripple/app/misc/AmendmentTable.h>
#include <ripple/app/misc/FeeVote.h>
#include <ripple/app/misc/IHashRouter.h>
#include <ripple/app/peers/ClusterNodeStatus.h>
#include <ripple/app/peers/UniqueNodeList.h>
#include <ripple/app/misc/Validations.h>
#include <ripple/app/peers/PeerSet.h>
#include <ripple/app/ledger/InboundLedger.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/AcceptedLedgerTx.h>
#include <ripple/app/ledger/AcceptedLedger.h>
#include <ripple/app/ledger/LedgerEntrySet.h>
#include <ripple/app/ledger/DirectoryEntryIterator.h>
#include <ripple/app/ledger/OrderBookIterator.h>
#include <ripple/app/tx/TransactionEngine.h>
#include <ripple/app/misc/CanonicalTXSet.h>
#include <ripple/app/ledger/LedgerHolder.h>
#include <ripple/app/ledger/LedgerHistory.h>
#include <ripple/app/ledger/LedgerCleaner.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/LedgerProposal.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/tx/TransactionMaster.h>
#include <ripple/app/main/LocalCredentials.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/ledger/OrderBookDB.h>
#include <ripple/app/tx/TransactionAcquire.h>
#include <ripple/app/tx/LocalTxs.h>
#include <ripple/app/consensus/DisputedTx.h>
#include <ripple/app/consensus/LedgerConsensus.h>
#include <ripple/app/ledger/LedgerTiming.h>
#include <ripple/app/paths/RippleState.h>
#include <ripple/app/paths/RippleLineCache.h>
#include <ripple/app/paths/PathRequest.h>
#include <ripple/app/paths/PathRequests.h>
#include <ripple/app/main/ParameterTable.h>
 #include <ripple/app/paths/PathState.h>
 #include <ripple/app/paths/RippleCalc.h>
#include  <ripple/app/paths/Pathfinder.h>


#endif
