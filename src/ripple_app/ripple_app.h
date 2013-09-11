//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
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

#include "beast/modules/beast_core/system/BeforeBoost.h" // must come first
#include <boost/algorithm/string/predicate.hpp>
#include <boost/array.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/mem_fn.hpp>
#include <boost/pointer_cast.hpp>
#include <boost/program_options.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <boost/unordered_set.hpp>
#include <boost/weak_ptr.hpp>

//------------------------------------------------------------------------------

#include "../ripple_basics/ripple_basics.h"
#include "../ripple_core/ripple_core.h"
#include "../ripple_data/ripple_data.h"
#include "../ripple_net/ripple_net.h"

#include "beast/modules/beast_sqdb/beast_sqdb.h"
#include "beast/modules/beast_sqlite/beast_sqlite.h"

namespace ripple
{

// Order matters here. If you get compile errors,
// reorder the include lines until the order is correct.

#include "data/ripple_Database.h"
#include "data/ripple_DatabaseCon.h"
#include "data/ripple_SqliteDatabase.h"
#include "data/ripple_DBInit.h"

#include "shamap/ripple_SHAMapItem.h"
#include "shamap/ripple_SHAMapNode.h"
#include "shamap/ripple_SHAMapTreeNode.h"
#include "shamap/ripple_SHAMapMissingNode.h"
#include "shamap/ripple_SHAMapSyncFilter.h"
#include "shamap/ripple_SHAMapAddNode.h"
#include "shamap/ripple_SHAMap.h"
#include "misc/ripple_SerializedTransaction.h"
#include "misc/ripple_SerializedLedger.h"
#include "tx/TransactionMeta.h"
#include "tx/Transaction.h"
#include "misc/ripple_AccountState.h"
#include "misc/ripple_NicknameState.h"
#include "ledger/Ledger.h"
#include "ledger/SerializedValidation.h"
#include "main/ripple_LoadManager.h"
#include "misc/ripple_OrderBook.h"
#include "shamap/ripple_SHAMapSyncFilters.h"
#include "misc/ripple_IFeatures.h"
#include "misc/ripple_IFeeVote.h"
#include "misc/ripple_IHashRouter.h"
#include "peers/ripple_Peer.h"
#include "peers/ripple_Peers.h"
#include "peers/ripple_ClusterNodeStatus.h"
#include "peers/ripple_UniqueNodeList.h"
#include "misc/ripple_Validations.h"
#include "peers/ripple_PeerSet.h"
#include "ledger/ripple_InboundLedger.h"
#include "ledger/ripple_InboundLedgers.h"
#include "misc/ripple_AccountItem.h"
#include "misc/ripple_AccountItems.h"
#include "ledger/ripple_AcceptedLedgerTx.h"
#include "ledger/ripple_AcceptedLedger.h"
#include "ledger/ripple_LedgerEntrySet.h"
#include "tx/TransactionEngine.h"
#include "misc/ripple_CanonicalTXSet.h"
#include "ledger/ripple_LedgerHistory.h"
#include "ledger/LedgerMaster.h"
#include "ledger/LedgerProposal.h"
#include "misc/NetworkOPs.h"
#include "tx/TransactionMaster.h"
#include "main/ripple_LocalCredentials.h"
#include "main/ripple_Application.h"
#include "ledger/OrderBookDB.h"
#include "tx/Transactor.h"
#include "tx/ChangeTransactor.h"
#include "tx/ripple_TransactionAcquire.h"
#include "consensus/ripple_DisputedTx.h"
#include "consensus/ripple_LedgerConsensus.h"
#include "ledger/LedgerTiming.h"
#include "misc/ripple_Offer.h"
#include "tx/OfferCancelTransactor.h"
#include "tx/OfferCreateTransactor.h"
#include "paths/ripple_PathRequest.h"
#include "main/ParameterTable.h"
 #include "paths/ripple_RippleLineCache.h"
 #include "paths/ripple_PathState.h"
 #include "paths/ripple_RippleCalc.h"
#include  "paths/ripple_Pathfinder.h"
#include "tx/PaymentTransactor.h"
#include "tx/RegularKeySetTransactor.h"
#include "paths/ripple_RippleState.h"
#include "tx/AccountSetTransactor.h"
#include "tx/TrustSetTransactor.h"
#include "tx/WalletAddTransactor.h"

#include "contracts/ripple_ScriptData.h"
#include "contracts/ripple_Contract.h"
#include "contracts/ripple_Interpreter.h"
#include "contracts/ripple_Operation.h"

}

#endif
