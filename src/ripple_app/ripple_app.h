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

//------------------------------------------------------------------------------

// VFALCO TODO Reduce these boost dependencies. Make more interfaces
//             purely abstract and move implementation into .cpp files.
//

#include "beast/modules/beast_core/system/BeforeBoost.h"
#include <boost/algorithm/string/predicate.hpp>
#include <boost/array.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
//#include <boost/iostreams/concepts.hpp>
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

#include "../ripple_basics/ripple_basics.h"
#include "../ripple_core/ripple_core.h"
#include "../ripple_data/ripple_data.h"
#include "../ripple_net/ripple_net.h"

//#include "beast/modules/beast_sqdb/beast_sqdb.h"
#include "beast/modules/beast_sqlite/beast_sqlite.h"

// VFALCO NOTE If we forward declare PackedMessage and write out shared_ptr
//             instead of using the in-class typedef, we can remove the entire
//             ripple.pb.h from the main headers.
//
#include "peers/PackedMessage.h"

namespace ripple {

// Order matters here. If you get compile errors,
// reorder the include lines until the order is correct.

#include "data/Database.h"
#include "data/DatabaseCon.h"
#include "data/SqliteDatabase.h"
#include "data/DBInit.h"

#include "shamap/SHAMapItem.h"
#include "shamap/SHAMapNode.h"
#include "shamap/SHAMapTreeNode.h"
#include "shamap/SHAMapMissingNode.h"
#include "shamap/SHAMapSyncFilter.h"
#include "shamap/SHAMapAddNode.h"
#include "shamap/SHAMap.h"
#include "misc/SerializedTransaction.h"
#include "misc/SerializedLedger.h"
#include "tx/TransactionMeta.h"
#include "tx/Transaction.h"
#include "misc/AccountState.h"
#include "misc/NicknameState.h"
#include "ledger/Ledger.h"
#include "ledger/SerializedValidation.h"
#include "main/LoadManager.h"
#include "misc/OrderBook.h"
#include "shamap/SHAMapSyncFilters.h"
#include "misc/IFeatures.h"
#include "misc/IFeeVote.h"
#include "misc/IHashRouter.h"
#include "peers/Peer.h"
#include "peers/Peers.h"
#include "peers/ClusterNodeStatus.h"
#include "peers/UniqueNodeList.h"
#include "misc/Validations.h"
#include "peers/PeerSet.h"
#include "ledger/InboundLedger.h"
#include "ledger/InboundLedgers.h"
#include "misc/AccountItem.h"
#include "misc/AccountItems.h"
#include "ledger/AcceptedLedgerTx.h"
#include "ledger/AcceptedLedger.h"
#include "ledger/LedgerEntrySet.h"
#include "tx/TransactionEngine.h"
#include "misc/CanonicalTXSet.h"
#include "ledger/LedgerHistory.h"
#include "ledger/LedgerCleaner.h"
#include "ledger/LedgerMaster.h"
#include "ledger/LedgerProposal.h"
#include "misc/NetworkOPs.h"
#include "tx/TransactionMaster.h"
#include "main/LocalCredentials.h"
#include "main/Application.h"
#include "ledger/OrderBookDB.h"
#include "tx/Transactor.h"
#include "tx/ChangeTransactor.h"
#include "tx/TransactionAcquire.h"
#include "consensus/DisputedTx.h"
#include "consensus/LedgerConsensus.h"
#include "ledger/LedgerTiming.h"
#include "misc/Offer.h"
#include "tx/OfferCancelTransactor.h"
#include "tx/OfferCreateTransactor.h"
#include "paths/RippleLineCache.h"
#include "paths/PathRequest.h"
#include "main/ParameterTable.h"
 #include "paths/RippleLineCache.h"
 #include "paths/PathState.h"
 #include "paths/RippleCalc.h"
#include  "paths/Pathfinder.h"
#include "tx/PaymentTransactor.h"
#include "tx/RegularKeySetTransactor.h"
#include "paths/RippleState.h"
#include "tx/AccountSetTransactor.h"
#include "tx/TrustSetTransactor.h"
#include "tx/WalletAddTransactor.h"

#include "contracts/ScriptData.h"
#include "contracts/Contract.h"
#include "contracts/Interpreter.h"
#include "contracts/Operation.h"

}

#endif
