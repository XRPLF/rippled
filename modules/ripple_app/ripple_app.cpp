//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

/** Add this to get the @ref ripple_app module.

    @file ripple_app.cpp
    @ingroup ripple_app
*/

//------------------------------------------------------------------------------

#include "BeastConfig.h"

// This must come first to work around the boost placeholders issues
#include "beast/modules/beast_core/beast_core.h"

#if BEAST_LINUX || BEAST_MAC || BEAST_BSD
#include <sys/resource.h>
#endif

// VFALCO NOTE Holy smokes...that's a lot of boost!!!
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/bimap.hpp>
#include <boost/bimap/list_of.hpp>
#include <boost/bimap/multiset_of.hpp>
#include <boost/bimap/unordered_set_of.hpp>
#include <boost/cstdint.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/function.hpp>
#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/make_shared.hpp>
#include <boost/mem_fn.hpp>
#include <boost/pointer_cast.hpp>
#include <boost/program_options.hpp>
#include <boost/ptr_container/ptr_vector.hpp>
#include <boost/ref.hpp>
#include <boost/regex.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>
#include <boost/weak_ptr.hpp>

#include "../ripple_core/ripple_core.h"

#include "beast/modules/beast_asio/beast_asio.h"
#include "beast/modules/beast_db/beast_db.h"
#include "beast/modules/beast_sqdb/beast_sqdb.h"
#include "beast/modules/beast_sqlite/beast_sqlite.h"

// VFALCO TODO fix these warnings!
#ifdef _MSC_VER
//#pragma warning (push) // Causes spurious C4503 "decorated name exceeds maximum length"
#pragma warning (disable: 4018) // signed/unsigned mismatch
#pragma warning (disable: 4309) // truncation of constant value (websocket)
#pragma warning (disable: 4244) // conversion, possible loss of data
#pragma warning (disable: 4535) // call requires /EHa
#endif

// VFALCO NOTE these includes generate warnings, unfortunately.
#include "ripple_app.h"

#include "../ripple_data/ripple_data.h"
#include "../ripple_mdb/ripple_mdb.h"
#include "../ripple_leveldb/ripple_leveldb.h"
#include "../ripple_hyperleveldb/ripple_hyperleveldb.h"
#include "../ripple_net/ripple_net.h"

#include "../modules/ripple_websocket/ripple_websocket.h"

//------------------------------------------------------------------------------

namespace ripple
{

// VFALCO NOTE The order of these includes is critical, since they do not
//             include their own dependencies. This is what allows us to
//             linearize the include sequence and view it in one place.
//

#include "data/ripple_Database.h"
#include "data/ripple_DatabaseCon.h"
#include "data/ripple_SqliteDatabase.h"
#include "data/ripple_DBInit.h"

#include "node/ripple_NodeObject.h"
#include "node/ripple_NodeStore.h"
#include "node/ripple_HyperLevelDBBackendFactory.h"
#include "node/ripple_KeyvaDBBackendFactory.h"
#include "node/ripple_LevelDBBackendFactory.h"
#include "node/ripple_MdbBackendFactory.h"
#include "node/ripple_MemoryBackendFactory.h"
#include "node/ripple_NullBackendFactory.h"
#include "node/ripple_SqliteBackendFactory.h"

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
#include "misc/ripple_ProofOfWork.h"
#include "misc/ripple_InfoSub.h"
#include "misc/ripple_OrderBook.h"
#include "shamap/ripple_SHAMapSyncFilters.h"
#include "misc/ripple_IFeatures.h"
#include "misc/ripple_IFeeVote.h"
#include "misc/ripple_IHashRouter.h"
#include "peers/ripple_Peer.h"
#include "peers/ripple_Peers.h"
#include "misc/ripple_IProofOfWorkFactory.h"
#include "peers/ripple_ClusterNodeStatus.h"
#include "peers/ripple_UniqueNodeList.h"
#include "misc/ripple_IValidations.h"
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
#include "websocket/WSDoor.h"
 #include "boost/ripple_IoService.h"
#include "main/ripple_Application.h"
#include "rpc/RPCHandler.h"
#include "tx/TransactionQueue.h"
#include "ledger/OrderBookDB.h"
#include "rpc/CallRPC.h"
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
#include "peers/PeerDoor.h"
#include "rpc/RPC.h"
#include "rpc/RPCErr.h"
#include "rpc/RPCSub.h"
#include "tx/RegularKeySetTransactor.h"
#include "paths/ripple_RippleState.h"
#include "tx/AccountSetTransactor.h"
#include "tx/TrustSetTransactor.h"
#include "websocket/WSConnection.h"
#include "websocket/WSServerHandler.h"
#include "tx/WalletAddTransactor.h"

#include "contracts/ripple_ScriptData.h"
#include "contracts/ripple_Contract.h"
#include "contracts/ripple_Interpreter.h"
#include "contracts/ripple_Operation.h"

#include "basics/ripple_RPCServerHandler.h"

#include "rpc/RPCDoor.h" // needs RPCServer

}

//------------------------------------------------------------------------------

// VFALCO TODO Move this to an appropriate header
namespace boost
{
    template <>
    struct range_mutable_iterator <ripple::LedgerEntrySet>
    {
        typedef ripple::LedgerEntrySet::iterator type;
    };

    template <>
    struct range_const_iterator <ripple::LedgerEntrySet>
    {
        typedef ripple::LedgerEntrySet::const_iterator type;
    };
}

//------------------------------------------------------------------------------

namespace ripple
{

//------------------------------------------------------------------------------

// VFALCO TODO figure out who needs these and move to a sensible private header.
static const uint64 tenTo14 = 100000000000000ull;
static const uint64 tenTo14m1 = tenTo14 - 1;
static const uint64 tenTo17 = tenTo14 * 1000;
static const uint64 tenTo17m1 = tenTo17 - 1;

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------

#if ! defined (RIPPLE_MAIN_PART) || RIPPLE_MAIN_PART == 1

#include "basics/ripple_RPCServerHandler.cpp"
#include "node/ripple_NodeObject.cpp"
#include "node/ripple_NodeStore.cpp"
#include "node/ripple_HyperLevelDBBackendFactory.cpp"
#include "node/ripple_KeyvaDBBackendFactory.cpp"
#include "node/ripple_LevelDBBackendFactory.cpp"
#include "node/ripple_MemoryBackendFactory.cpp"
#include "node/ripple_NullBackendFactory.cpp"
#include "node/ripple_MdbBackendFactory.cpp"
#include "node/ripple_SqliteBackendFactory.cpp"

#include "ledger/Ledger.cpp"
#include "shamap/ripple_SHAMapDelta.cpp"
#include "shamap/ripple_SHAMapNode.cpp"
#include "shamap/ripple_SHAMapTreeNode.cpp"

#include "misc/ripple_AccountItems.cpp"
#include "misc/ripple_AccountState.cpp"
#include "tx/ChangeTransactor.cpp"

#include "contracts/ripple_Contract.cpp"
#include "contracts/ripple_Operation.cpp"
#include "contracts/ripple_ScriptData.cpp"
#include "contracts/ripple_Interpreter.cpp"

#endif

//------------------------------------------------------------------------------

#if ! defined (RIPPLE_MAIN_PART) || RIPPLE_MAIN_PART == 2

#include "rpc/RPCHandler.cpp"
#include "shamap/ripple_SHAMap.cpp" // Uses theApp
#include "shamap/ripple_SHAMapItem.cpp"
#include "shamap/ripple_SHAMapSync.cpp"
#include "shamap/ripple_SHAMapMissingNode.cpp"

#include "misc/ripple_AccountItem.cpp"
#include "tx/AccountSetTransactor.cpp"
#include "misc/ripple_CanonicalTXSet.cpp"
#include "ledger/LedgerProposal.cpp"
#include "main/ripple_LoadManager.cpp"
#include "misc/ripple_NicknameState.cpp"
#include "tx/OfferCancelTransactor.cpp"
#include "ledger/OrderBookDB.cpp"

#include "data/ripple_Database.cpp"
#include "data/ripple_DatabaseCon.cpp"
#include "data/ripple_SqliteDatabase.cpp"
#include "data/ripple_DBInit.cpp"

#include "boost/ripple_IoService.cpp"

#endif

//------------------------------------------------------------------------------

#if ! defined (RIPPLE_MAIN_PART) || RIPPLE_MAIN_PART == 3

#include "paths/ripple_RippleCalc.cpp"
#include "paths/ripple_PathState.cpp"
#include "rpc/CallRPC.cpp"

#include "main/ParameterTable.cpp"
#include "peers/PeerDoor.cpp"
#include "paths/ripple_RippleLineCache.cpp"
#include "rpc/rpc.cpp"
#include "rpc/RPCErr.cpp"
#include "rpc/RPCSub.cpp"
#include "ledger/SerializedValidation.cpp"
#include "tx/Transaction.cpp"
#include "tx/TransactionEngine.cpp"
#include "tx/TransactionMeta.cpp"
#include "tx/Transactor.cpp"
#include "websocket/WSConnection.cpp"
#include "websocket/WSDoor.cpp"

#endif

//------------------------------------------------------------------------------

#if ! defined (RIPPLE_MAIN_PART) || RIPPLE_MAIN_PART == 4

#include "peers/ripple_UniqueNodeList.cpp"
#include "ledger/ripple_InboundLedger.cpp"

#include "tx/PaymentTransactor.cpp"
#include "tx/RegularKeySetTransactor.cpp"
#include "paths/ripple_RippleState.cpp"
#include "tx/TransactionCheck.cpp"
#include "tx/TransactionMaster.cpp"
#include "tx/TransactionQueue.cpp"
#include "tx/TrustSetTransactor.cpp"
#include "websocket/WSServerHandler.cpp"

#endif

//------------------------------------------------------------------------------

#if ! defined (RIPPLE_MAIN_PART) || RIPPLE_MAIN_PART == 5

#include "ledger/LedgerTiming.cpp"
#include "ledger/ripple_AcceptedLedgerTx.cpp"
#include "main/ripple_Application.cpp"
#include "main/ripple_LocalCredentials.cpp"
#include "misc/ripple_FeeVote.cpp"
#include "misc/ripple_Validations.cpp"
#include "peers/ripple_Peer.cpp"
#include "rpc/RPCDoor.cpp"
#include "tx/OfferCreateTransactor.cpp"
#include "tx/WalletAddTransactor.cpp"

#endif

//------------------------------------------------------------------------------

#if ! defined (RIPPLE_MAIN_PART) || RIPPLE_MAIN_PART == 6

#include "ledger/ripple_LedgerEntrySet.cpp"
#include "paths/ripple_Pathfinder.cpp"
#include "misc/ripple_Features.cpp"

#include "ledger/ripple_AcceptedLedger.cpp"
#include "consensus/ripple_DisputedTx.cpp"
#include "misc/ripple_HashRouter.cpp"
#include "misc/ripple_Offer.cpp"

#endif

//------------------------------------------------------------------------------

#if ! defined (RIPPLE_MAIN_PART) || RIPPLE_MAIN_PART == 7

#include "misc/NetworkOPs.cpp"
#include "peers/ripple_Peers.cpp"

#include "ledger/ripple_InboundLedgers.cpp"
#include "ledger/ripple_LedgerHistory.cpp"
#include "paths/ripple_PathRequest.cpp"
#include "misc/ripple_SerializedLedger.cpp"
#include "tx/ripple_TransactionAcquire.cpp"

#endif

//------------------------------------------------------------------------------

#if ! defined (RIPPLE_MAIN_PART) || RIPPLE_MAIN_PART == 8

#include "consensus/ripple_LedgerConsensus.cpp"
#include "ledger/LedgerMaster.cpp"
#include "peers/ripple_PeerSet.cpp"
#include "misc/ripple_InfoSub.cpp"
#include "misc/ripple_OrderBook.cpp"
#include "misc/ripple_ProofOfWork.cpp"
#include "misc/ripple_ProofOfWorkFactory.h" // private
#include "misc/ripple_ProofOfWorkFactory.cpp" // requires ProofOfWork.cpp for ProofOfWork::sMaxDifficulty
#include "misc/ripple_SerializedTransaction.cpp"

#include "shamap/ripple_SHAMapSyncFilters.cpp" // requires Application

 #include "main/ripple_FatalErrorReporter.h" // private
#include "main/ripple_FatalErrorReporter.cpp"
 #include "main/ripple_RippleMain.h" // private
#include "main/ripple_RippleMain.cpp"

#endif

//------------------------------------------------------------------------------

}

#if ! defined (RIPPLE_MAIN_PART) || RIPPLE_MAIN_PART == 8

// Must be outside the namespace for obvious reasons
int main (int argc, char** argv)
{
    ripple::RippleMain rippled;
    return rippled.runFromMain (argc, argv);
}

#endif

//------------------------------------------------------------------------------

#ifdef _MSC_VER
//#pragma warning (pop)
#endif

