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

#include <modules/beast_core/system/BeforeBoost.h>
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

#include <ripple_basics/ripple_basics.h>
#include <ripple_core/ripple_core.h>
#include <ripple_data/ripple_data.h>
#include <ripple_net/ripple_net.h>

#include <ripple/common/ResolverAsio.h>

// VFALCO TODO Remove this include
#include <modules/beast_sqlite/beast_sqlite.h>

// Order matters here. If you get compile errors,
// reorder the include lines until the order is correct.

#include <ripple/common/KeyCache.h>
#include <ripple/common/TaggedCache.h>

#include <ripple_app/data/Database.h>
#include <ripple_app/data/DatabaseCon.h>
#include <ripple_app/data/SqliteDatabase.h>
#include <ripple_app/data/DBInit.h>
#include <ripple_app/shamap/SHAMapItem.h>
#include <ripple_app/shamap/SHAMapNode.h>
#include <ripple_app/shamap/SHAMapTreeNode.h>
#include <ripple_app/shamap/SHAMapMissingNode.h>
#include <ripple_app/shamap/SHAMapSyncFilter.h>
#include <ripple_app/shamap/SHAMapAddNode.h>
#include <ripple_app/shamap/SHAMap.h>
#include <ripple_app/misc/SerializedTransaction.h>
#include <ripple_app/misc/SerializedLedger.h>
#include <ripple_app/tx/TransactionMeta.h>
#include <ripple_app/tx/Transaction.h>
#include <ripple_app/misc/AccountState.h>
#include <ripple_app/misc/NicknameState.h>
#include <ripple_app/ledger/Ledger.h>
#include <ripple_app/ledger/SerializedValidation.h>
#include <ripple_app/main/LoadManager.h>
#include <ripple_app/misc/OrderBook.h>
#include <ripple_app/shamap/SHAMapSyncFilters.h>
#include <ripple_app/misc/AmendmentTable.h>
#include <ripple_app/misc/FeeVote.h>
#include <ripple_app/misc/IHashRouter.h>
#include <ripple_app/peers/ClusterNodeStatus.h>
#include <ripple_app/peers/UniqueNodeList.h>
#include <ripple_app/misc/Validations.h>
#include <ripple_app/peers/PeerSet.h>
#include <ripple_app/ledger/InboundLedger.h>
#include <ripple_app/ledger/InboundLedgers.h>
#include <ripple_app/misc/AccountItem.h>
#include <ripple_app/misc/AccountItems.h>
#include <ripple_app/ledger/AcceptedLedgerTx.h>
#include <ripple_app/ledger/AcceptedLedger.h>
#include <ripple_app/ledger/LedgerEntrySet.h>
#include <ripple_app/ledger/DirectoryEntryIterator.h>
#include <ripple_app/ledger/OrderBookIterator.h>
#include <ripple_app/tx/TransactionEngine.h>
#include <ripple_app/misc/CanonicalTXSet.h>
#include <ripple_app/ledger/LedgerHolder.h>
#include <ripple_app/ledger/LedgerHistory.h>
#include <ripple_app/ledger/LedgerCleaner.h>
#include <ripple_app/ledger/LedgerMaster.h>
#include <ripple_app/ledger/LedgerProposal.h>
#include <ripple_app/misc/NetworkOPs.h>
#include <ripple_app/tx/TransactionMaster.h>
#include <ripple_app/main/LocalCredentials.h>
#include <ripple_app/main/Application.h>
#include <ripple_app/ledger/OrderBookDB.h>
#include <ripple_app/tx/TransactionAcquire.h>
#include <ripple_app/tx/LocalTxs.h>
#include <ripple_app/consensus/DisputedTx.h>
#include <ripple_app/consensus/LedgerConsensus.h>
#include <ripple_app/ledger/LedgerTiming.h>
#include <ripple_app/misc/Offer.h>
#include <ripple_app/paths/RippleLineCache.h>
#include <ripple_app/paths/PathRequest.h>
#include <ripple_app/paths/PathRequests.h>
#include <ripple_app/main/ParameterTable.h>
 #include <ripple_app/paths/RippleLineCache.h>
 #include <ripple_app/paths/PathState.h>
 #include <ripple_app/paths/RippleCalc.h>
#include  <ripple_app/paths/Pathfinder.h>
#include <ripple_app/paths/RippleState.h>
// VFALCO NOTE These contracts files are bunk
#include <ripple_app/contracts/ScriptData.h>
#include <ripple_app/contracts/Contract.h>
#include <ripple_app/contracts/Interpreter.h>
#include <ripple_app/contracts/Operation.h>

#endif
