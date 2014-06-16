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
#include <boost/unordered_set.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/thread/shared_mutex.hpp>

//------------------------------------------------------------------------------

#include <ripple/unity/basics.h>
#include <ripple/unity/core.h>
#include <ripple/unity/data.h>
#include <ripple/unity/net.h>

#include <ripple/common/ResolverAsio.h>

// VFALCO TODO Remove this include
#include <beast/module/sqlite/sqlite.h>

// Order matters here. If you get compile errors,
// reorder the include lines until the order is correct.

#include <ripple/common/KeyCache.h>
#include <ripple/common/TaggedCache.h>

#include <ripple/module/app/data/Database.h>
#include <ripple/module/app/data/DatabaseCon.h>
#include <ripple/module/app/data/SqliteDatabase.h>
#include <ripple/module/app/data/DBInit.h>
#include <ripple/module/app/shamap/SHAMapItem.h>
#include <ripple/module/app/shamap/SHAMapNode.h>
#include <ripple/module/app/shamap/SHAMapTreeNode.h>
#include <ripple/module/app/shamap/SHAMapMissingNode.h>
#include <ripple/module/app/shamap/SHAMapSyncFilter.h>
#include <ripple/module/app/shamap/SHAMapAddNode.h>
#include <ripple/module/app/shamap/SHAMap.h>
#include <ripple/module/app/misc/SerializedTransaction.h>
#include <ripple/module/app/misc/SerializedLedger.h>
#include <ripple/module/app/tx/TransactionMeta.h>
#include <ripple/module/app/tx/Transaction.h>
#include <ripple/module/app/misc/AccountState.h>
#include <ripple/module/app/misc/NicknameState.h>
#include <ripple/module/app/ledger/Ledger.h>
#include <ripple/module/app/ledger/SerializedValidation.h>
#include <ripple/module/app/main/LoadManager.h>
#include <ripple/module/app/misc/OrderBook.h>
#include <ripple/module/app/shamap/SHAMapSyncFilters.h>
#include <ripple/module/app/misc/AmendmentTable.h>
#include <ripple/module/app/misc/FeeVote.h>
#include <ripple/module/app/misc/IHashRouter.h>
#include <ripple/module/app/peers/ClusterNodeStatus.h>
#include <ripple/module/app/peers/UniqueNodeList.h>
#include <ripple/module/app/misc/Validations.h>
#include <ripple/module/app/peers/PeerSet.h>
#include <ripple/module/app/ledger/InboundLedger.h>
#include <ripple/module/app/ledger/InboundLedgers.h>
#include <ripple/module/app/misc/AccountItem.h>
#include <ripple/module/app/misc/AccountItems.h>
#include <ripple/module/app/ledger/AcceptedLedgerTx.h>
#include <ripple/module/app/ledger/AcceptedLedger.h>
#include <ripple/module/app/ledger/LedgerEntrySet.h>
#include <ripple/module/app/ledger/DirectoryEntryIterator.h>
#include <ripple/module/app/ledger/OrderBookIterator.h>
#include <ripple/module/app/tx/TransactionEngine.h>
#include <ripple/module/app/misc/CanonicalTXSet.h>
#include <ripple/module/app/ledger/LedgerHolder.h>
#include <ripple/module/app/ledger/LedgerHistory.h>
#include <ripple/module/app/ledger/LedgerCleaner.h>
#include <ripple/module/app/ledger/LedgerMaster.h>
#include <ripple/module/app/ledger/LedgerProposal.h>
#include <ripple/module/app/misc/NetworkOPs.h>
#include <ripple/module/app/tx/TransactionMaster.h>
#include <ripple/module/app/main/LocalCredentials.h>
#include <ripple/module/app/main/Application.h>
#include <ripple/module/app/ledger/OrderBookDB.h>
#include <ripple/module/app/tx/TransactionAcquire.h>
#include <ripple/module/app/tx/LocalTxs.h>
#include <ripple/module/app/consensus/DisputedTx.h>
#include <ripple/module/app/consensus/LedgerConsensus.h>
#include <ripple/module/app/ledger/LedgerTiming.h>
#include <ripple/module/app/misc/Offer.h>
#include <ripple/module/app/paths/RippleLineCache.h>
#include <ripple/module/app/paths/PathRequest.h>
#include <ripple/module/app/paths/PathRequests.h>
#include <ripple/module/app/main/ParameterTable.h>
 #include <ripple/module/app/paths/RippleLineCache.h>
 #include <ripple/module/app/paths/PathState.h>
 #include <ripple/module/app/paths/RippleCalc.h>
#include  <ripple/module/app/paths/Pathfinder.h>
#include <ripple/module/app/paths/RippleState.h>
// VFALCO NOTE These contracts files are bunk
#include <ripple/module/app/contracts/ScriptData.h>
#include <ripple/module/app/contracts/Contract.h>
#include <ripple/module/app/contracts/Interpreter.h>
#include <ripple/module/app/contracts/Operation.h>

#endif
