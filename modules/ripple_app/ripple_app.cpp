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
#include "beast/modules/beast_basics/beast_basics.h"

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
#include <boost/lexical_cast.hpp>
#include <boost/make_shared.hpp>
#include <boost/mem_fn.hpp>
#include <boost/pointer_cast.hpp>
#include <boost/program_options.hpp>
#include <boost/ptr_container/ptr_vector.hpp>
#include <boost/ref.hpp>
#include <boost/regex.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <boost/thread.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>
#include <boost/weak_ptr.hpp>

#include "../ripple_sqlite/ripple_sqlite.h" // for SqliteDatabase.cpp

#include "../ripple_core/ripple_core.h"

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

#include "../ripple_net/ripple_net.h"

#include "../modules/ripple_websocket/ripple_websocket.h"

//------------------------------------------------------------------------------

namespace ripple
{

// VFALCO NOTE The order of these includes is critical, since they do not
//             include their own dependencies. This is what allows us to
//             linearize the include sequence and view it in one place.
//

#include "src/cpp/ripple/ripple_Database.h"
#include "src/cpp/ripple/ripple_DatabaseCon.h"
#include "src/cpp/ripple/ripple_SqliteDatabase.h"
#include "src/cpp/ripple/ripple_DBInit.h"

#include "node/ripple_NodeObject.h"
#include "node/ripple_NodeStore.h"
#include "node/ripple_NodeStoreLevelDB.h"
#include "node/ripple_NodeStoreSqlite.h"

#include "src/cpp/ripple/ripple_SHAMapItem.h"
#include "src/cpp/ripple/ripple_SHAMapNode.h"
#include "src/cpp/ripple/ripple_SHAMapTreeNode.h"
#include "src/cpp/ripple/ripple_SHAMapMissingNode.h"
#include "src/cpp/ripple/ripple_SHAMapSyncFilter.h"
#include "src/cpp/ripple/ripple_SHAMapAddNode.h"
#include "src/cpp/ripple/ripple_SHAMap.h"
#include "src/cpp/ripple/ripple_SerializedTransaction.h"
#include "src/cpp/ripple/ripple_SerializedLedger.h"
#include "src/cpp/ripple/TransactionMeta.h"
#include "src/cpp/ripple/Transaction.h"
#include "src/cpp/ripple/ripple_AccountState.h"
#include "src/cpp/ripple/ripple_NicknameState.h"
#include "src/cpp/ripple/Ledger.h"
#include "src/cpp/ripple/SerializedValidation.h"
#include "src/cpp/ripple/ripple_ILoadManager.h"
#include "src/cpp/ripple/ripple_ProofOfWork.h"
#include "src/cpp/ripple/ripple_InfoSub.h"
#include "src/cpp/ripple/ripple_OrderBook.h"
#include "src/cpp/ripple/ripple_SHAMapSyncFilters.h"
#include "src/cpp/ripple/ripple_IFeatures.h"
#include "src/cpp/ripple/ripple_IFeeVote.h"
#include "src/cpp/ripple/ripple_IHashRouter.h"
#include "src/cpp/ripple/ripple_Peer.h" // VFALCO TODO Rename to IPeer
#include "src/cpp/ripple/ripple_IPeers.h"
#include "src/cpp/ripple/ripple_IProofOfWorkFactory.h"
#include "src/cpp/ripple/ripple_ClusterNodeStatus.h"
#include "src/cpp/ripple/ripple_UniqueNodeList.h"
#include "src/cpp/ripple/ripple_IValidations.h"
#include "src/cpp/ripple/ripple_PeerSet.h"
#include "src/cpp/ripple/ripple_InboundLedger.h"
#include "src/cpp/ripple/ripple_InboundLedgers.h"
#include "src/cpp/ripple/ScriptData.h"
#include "src/cpp/ripple/Contract.h"
#include "src/cpp/ripple/Interpreter.h"
#include "src/cpp/ripple/Operation.h"
#include "src/cpp/ripple/ripple_AccountItem.h"
#include "src/cpp/ripple/ripple_AccountItems.h"
#include "src/cpp/ripple/ripple_AcceptedLedgerTx.h"
#include "src/cpp/ripple/ripple_AcceptedLedger.h"
#include "src/cpp/ripple/ripple_LedgerEntrySet.h"
#include "src/cpp/ripple/TransactionEngine.h"
#include "src/cpp/ripple/ripple_CanonicalTXSet.h"
#include "src/cpp/ripple/ripple_LedgerHistory.h"
#include "src/cpp/ripple/LedgerMaster.h"
#include "src/cpp/ripple/LedgerProposal.h"
#include "src/cpp/ripple/NetworkOPs.h"
#include "src/cpp/ripple/TransactionMaster.h"
#include "src/cpp/ripple/ripple_LocalCredentials.h"
#include "src/cpp/ripple/WSDoor.h"
#include "src/cpp/ripple/RPCHandler.h"
#include "src/cpp/ripple/TransactionQueue.h"
#include "src/cpp/ripple/OrderBookDB.h"
#include "src/cpp/ripple/ripple_DatabaseCon.h"
#include "src/cpp/ripple/ripple_IApplication.h"
#include "src/cpp/ripple/CallRPC.h"
#include "src/cpp/ripple/Transactor.h"
#include "src/cpp/ripple/ChangeTransactor.h"
#include "src/cpp/ripple/ripple_TransactionAcquire.h"
#include "src/cpp/ripple/ripple_DisputedTx.h"
#include "src/cpp/ripple/ripple_LedgerConsensus.h"
#include "src/cpp/ripple/LedgerTiming.h"
#include "src/cpp/ripple/ripple_Offer.h"
#include "src/cpp/ripple/OfferCancelTransactor.h"
#include "src/cpp/ripple/OfferCreateTransactor.h"
#include "src/cpp/ripple/ripple_PathRequest.h"
#include "src/cpp/ripple/ParameterTable.h"
 #include "src/cpp/ripple/ripple_RippleLineCache.h"
 #include "src/cpp/ripple/ripple_PathState.h"
 #include "src/cpp/ripple/ripple_RippleCalc.h"
#include  "src/cpp/ripple/ripple_Pathfinder.h"
#include "src/cpp/ripple/PaymentTransactor.h"
#include "src/cpp/ripple/PeerDoor.h"
#include "src/cpp/ripple/RPC.h"
#include "src/cpp/ripple/RPCErr.h"
#include "src/cpp/ripple/RPCSub.h"
#include "src/cpp/ripple/RegularKeySetTransactor.h"
#include "src/cpp/ripple/ripple_RippleState.h"
#include "src/cpp/ripple/SerializedValidation.h"
#include "src/cpp/ripple/AccountSetTransactor.h"
#include "src/cpp/ripple/TrustSetTransactor.h"
#include "src/cpp/ripple/WSConnection.h"
#include "src/cpp/ripple/ripple_WSHandler.h"
#include "src/cpp/ripple/WalletAddTransactor.h"

#include "basics/ripple_Version.h" // VFALCO TODO Should this be private?
#include "basics/ripple_BuildVersion.h" // private
#include "basics/ripple_RPCServerHandler.h"

#include "src/cpp/ripple/RPCDoor.h" // needs RPCServer

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
#include "node/ripple_NodeStoreLevelDB.cpp"
#include "node/ripple_NodeStoreSqlite.cpp"

#include "src/cpp/ripple/Ledger.cpp"
#include "src/cpp/ripple/ripple_SHAMapDelta.cpp"
#include "src/cpp/ripple/ripple_SHAMapNode.cpp"
#include "src/cpp/ripple/ripple_SHAMapTreeNode.cpp"

#include "src/cpp/ripple/ripple_Database.cpp"
#include "src/cpp/ripple/ripple_AccountItems.cpp"
#include "src/cpp/ripple/ripple_AccountState.cpp"
#include "src/cpp/ripple/ChangeTransactor.cpp"

#endif

//------------------------------------------------------------------------------

#if ! defined (RIPPLE_MAIN_PART) || RIPPLE_MAIN_PART == 2

#include "src/cpp/ripple/RPCHandler.cpp"
#include "src/cpp/ripple/ripple_SHAMap.cpp" // Uses theApp
#include "src/cpp/ripple/ripple_SHAMapItem.cpp"
#include "src/cpp/ripple/ripple_SHAMapSync.cpp"
#include "src/cpp/ripple/ripple_SHAMapMissingNode.cpp"

#include "src/cpp/ripple/ripple_AccountItem.cpp"
#include "src/cpp/ripple/AccountSetTransactor.cpp"
#include "src/cpp/ripple/ripple_CanonicalTXSet.cpp"
#include "src/cpp/ripple/Contract.cpp"
#include "src/cpp/ripple/LedgerProposal.cpp"
#include "src/cpp/ripple/ripple_LoadManager.cpp"
#include "src/cpp/ripple/ripple_NicknameState.cpp"
#include "src/cpp/ripple/OfferCancelTransactor.cpp"
#include "src/cpp/ripple/OrderBookDB.cpp"

#endif

//------------------------------------------------------------------------------

#if ! defined (RIPPLE_MAIN_PART) || RIPPLE_MAIN_PART == 3

// This is for PeerDoor and WSDoor
// Generate DH for SSL connection.
static DH* handleTmpDh (SSL* ssl, int is_export, int iKeyLength)
{
    // VFALCO TODO eliminate this horrendous dependency on theApp and LocalCredentials
    return 512 == iKeyLength ? getApp().getLocalCredentials ().getDh512 () : getApp().getLocalCredentials ().getDh1024 ();
}

#include "src/cpp/ripple/ripple_RippleCalc.cpp"
#include "src/cpp/ripple/CallRPC.cpp"
#include "src/cpp/ripple/ripple_PathState.cpp"

#include "src/cpp/ripple/ParameterTable.cpp"
#include "src/cpp/ripple/PeerDoor.cpp"
#include "src/cpp/ripple/ripple_RippleLineCache.cpp"
#include "src/cpp/ripple/rpc.cpp"
#include "src/cpp/ripple/RPCErr.cpp"
#include "src/cpp/ripple/RPCSub.cpp"
#include "src/cpp/ripple/SerializedValidation.cpp"
#include "src/cpp/ripple/Transaction.cpp"
#include "src/cpp/ripple/TransactionEngine.cpp"
#include "src/cpp/ripple/TransactionMeta.cpp"
#include "src/cpp/ripple/Transactor.cpp"
#include "src/cpp/ripple/WSConnection.cpp"
#include "src/cpp/ripple/WSDoor.cpp"

#endif

//------------------------------------------------------------------------------

#if ! defined (RIPPLE_MAIN_PART) || RIPPLE_MAIN_PART == 4

#include "src/cpp/ripple/ripple_UniqueNodeList.cpp"
#include "src/cpp/ripple/ripple_InboundLedger.cpp"
#include "src/cpp/ripple/ripple_SqliteDatabase.cpp"

#include "src/cpp/ripple/PaymentTransactor.cpp"
#include "src/cpp/ripple/RegularKeySetTransactor.cpp"
#include "src/cpp/ripple/ripple_RippleState.cpp"
#include "src/cpp/ripple/RPCDoor.cpp"
#include "src/cpp/ripple/ScriptData.cpp"
#include "src/cpp/ripple/TransactionCheck.cpp"
#include "src/cpp/ripple/TransactionMaster.cpp"
#include "src/cpp/ripple/TransactionQueue.cpp"
#include "src/cpp/ripple/TrustSetTransactor.cpp"
#include "src/cpp/ripple/ripple_WSHandler.cpp"

#endif

//------------------------------------------------------------------------------

#if ! defined (RIPPLE_MAIN_PART) || RIPPLE_MAIN_PART == 5

#include "src/cpp/ripple/ripple_Peer.cpp"
#include "src/cpp/ripple/ripple_Application.cpp"
#include "src/cpp/ripple/OfferCreateTransactor.cpp"
#include "src/cpp/ripple/ripple_Validations.cpp"

#include "src/cpp/ripple/WalletAddTransactor.cpp"
#include "src/cpp/ripple/ripple_AcceptedLedgerTx.cpp"
#include "src/cpp/ripple/ripple_DatabaseCon.cpp"
#include "src/cpp/ripple/ripple_FeeVote.cpp"
#include "src/cpp/ripple/ripple_DBInit.cpp"
#include "src/cpp/ripple/Interpreter.cpp"
#include "src/cpp/ripple/LedgerTiming.cpp"

#endif

//------------------------------------------------------------------------------

#if ! defined (RIPPLE_MAIN_PART) || RIPPLE_MAIN_PART == 6

#include "src/cpp/ripple/ripple_LedgerEntrySet.cpp"
#include "src/cpp/ripple/ripple_Pathfinder.cpp"
#include "src/cpp/ripple/ripple_Features.cpp"

#include "src/cpp/ripple/ripple_LocalCredentials.cpp"
#include "src/cpp/ripple/ripple_AcceptedLedger.cpp"
#include "src/cpp/ripple/ripple_DisputedTx.cpp"
#include "src/cpp/ripple/ripple_HashRouter.cpp"
#include "src/cpp/ripple/ripple_Main.cpp"
#include "src/cpp/ripple/ripple_Offer.cpp"

#endif

//------------------------------------------------------------------------------

#if ! defined (RIPPLE_MAIN_PART) || RIPPLE_MAIN_PART == 7

#include "src/cpp/ripple/NetworkOPs.cpp"
#include "src/cpp/ripple/ripple_Peers.cpp"

#include "src/cpp/ripple/ripple_InboundLedgers.cpp"
#include "src/cpp/ripple/ripple_LedgerHistory.cpp"
#include "src/cpp/ripple/ripple_PathRequest.cpp"
#include "src/cpp/ripple/ripple_SerializedLedger.cpp"
#include "src/cpp/ripple/ripple_TransactionAcquire.cpp"
#include "src/cpp/ripple/Operation.cpp"

#endif

//------------------------------------------------------------------------------

#if ! defined (RIPPLE_MAIN_PART) || RIPPLE_MAIN_PART == 8

#include "src/cpp/ripple/ripple_LedgerConsensus.cpp"
#include "src/cpp/ripple/LedgerMaster.cpp"

#include "src/cpp/ripple/ripple_InfoSub.cpp"
#include "src/cpp/ripple/ripple_OrderBook.cpp"
#include "src/cpp/ripple/ripple_PeerSet.cpp"
#include "src/cpp/ripple/ripple_ProofOfWork.cpp"
#include "src/cpp/ripple/ripple_ProofOfWorkFactory.h" // private
#include "src/cpp/ripple/ripple_ProofOfWorkFactory.cpp" // requires ProofOfWork.cpp for ProofOfWork::sMaxDifficulty
#include "src/cpp/ripple/ripple_SerializedTransaction.cpp"

#include "src/cpp/ripple/ripple_SHAMapSyncFilters.cpp" // requires Application

#endif

//------------------------------------------------------------------------------

}

//------------------------------------------------------------------------------

#if ! defined (RIPPLE_MAIN_PART) || RIPPLE_MAIN_PART == 8

// Unit Tests
//
// These must be outside the namespace
//
// VFALCO TODO Eliminate the need for boost for unit tests.
//
#include "src/cpp/ripple/LedgerUnitTests.cpp"
#include "src/cpp/ripple/ripple_SHAMapUnitTests.cpp"
#include "src/cpp/ripple/ripple_SHAMapSyncUnitTests.cpp"
#include "src/cpp/ripple/ripple_ProofOfWorkFactoryUnitTests.cpp" // Requires ProofOfWorkFactory.h
#include "src/cpp/ripple/ripple_SerializedTransactionUnitTests.cpp"

//------------------------------------------------------------------------------

namespace ripple
{
    extern int rippleMain (int argc, char** argv);
}

// Must be outside the namespace for obvious reasons
int main (int argc, char** argv)
{
    return ripple::rippleMain (argc, argv);
}

#endif

//------------------------------------------------------------------------------

#ifdef _MSC_VER
//#pragma warning (pop)
#endif

