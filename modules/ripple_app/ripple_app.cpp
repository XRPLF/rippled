//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#include "BeastConfig.h"

#include "ripple_app.h"

// VFALCO TODO fix these warnings!
#ifdef _MSC_VER
//#pragma warning (push) // Causes spurious C4503 "decorated name exceeds maximum length"
#pragma warning (disable: 4018) // signed/unsigned mismatch
#pragma warning (disable: 4309) // truncation of constant value (websocket)
#pragma warning (disable: 4244) // conversion, possible loss of data
#pragma warning (disable: 4535) // call requires /EHa
#endif

#include "../ripple_data/ripple_data.h"
#include "../ripple_net/ripple_net.h"
#include "../modules/ripple_websocket/ripple_websocket.h"

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

// The "real" contents of ripple_app.cpp
# include "boost/ripple_IoService.h"
#include "boost/ripple_IoService.cpp"
#include "main/ripple_Application.cpp"

// Here down is just to split things up for using less build memory
#include "node/ripple_NodeObject.cpp"
#include "node/ripple_NodeStore.cpp"

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

#endif

//------------------------------------------------------------------------------

#if ! defined (RIPPLE_MAIN_PART) || RIPPLE_MAIN_PART == 3

#include "rpc/RPCErr.h" // private
#include "rpc/RPCUtil.h" // private
#include "websocket/WSConnection.h" // private

#include "rpc/RPCErr.cpp"
#include "rpc/RPCUtil.cpp"

#include "rpc/CallRPC.cpp"
#include "rpc/RPCHandler.cpp"
#include "rpc/RPCSub.cpp"

#include "basics/ripple_RPCServerHandler.cpp" // needs RPCUtil
#include "paths/ripple_PathRequest.cpp" // needs RPCErr.h
#include "paths/ripple_RippleCalc.cpp"
#include "paths/ripple_PathState.cpp"

#include "main/ParameterTable.cpp"
#include "peers/PeerDoor.cpp"
#include "paths/ripple_RippleLineCache.cpp"
#include "ledger/SerializedValidation.cpp"
#include "websocket/WSConnection.cpp"
#include "websocket/WSDoor.cpp"
#include "websocket/WSServerHandler.cpp"

#endif

//------------------------------------------------------------------------------

#if ! defined (RIPPLE_MAIN_PART) || RIPPLE_MAIN_PART == 4

#include "paths/ripple_RippleState.cpp"
#include "peers/ripple_UniqueNodeList.cpp"
#include "ledger/ripple_InboundLedger.cpp"

#include "tx/PaymentTransactor.cpp"
#include "tx/RegularKeySetTransactor.cpp"
#include "tx/TransactionCheck.cpp"
#include "tx/TransactionMaster.cpp"
#include "tx/TransactionQueue.cpp"
#include "tx/TrustSetTransactor.cpp"
#include "tx/Transaction.cpp"
#include "tx/TransactionEngine.cpp"
#include "tx/TransactionMeta.cpp"
#include "tx/Transactor.cpp"

#endif

//------------------------------------------------------------------------------

#if ! defined (RIPPLE_MAIN_PART) || RIPPLE_MAIN_PART == 5

#include "ledger/LedgerTiming.cpp"
#include "ledger/ripple_AcceptedLedgerTx.cpp"
#include "main/ripple_LocalCredentials.cpp"
#include "misc/ripple_FeeVote.cpp"
#include "misc/ripple_Validations.cpp"
#include "peers/ripple_Peer.cpp"
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
#include "misc/ripple_SerializedLedger.cpp"
#include "tx/ripple_TransactionAcquire.cpp"

#endif

//------------------------------------------------------------------------------

#if ! defined (RIPPLE_MAIN_PART) || RIPPLE_MAIN_PART == 8

}

#include "beast/modules/beast_db/beast_db.h"
#include "../ripple_mdb/ripple_mdb.h"
#include "../ripple_leveldb/ripple_leveldb.h"
#include "../ripple_hyperleveldb/ripple_hyperleveldb.h"

namespace ripple
{

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
 #include "node/ripple_HyperLevelDBBackendFactory.h" // private
 #include "node/ripple_KeyvaDBBackendFactory.h" // private
 #include "node/ripple_LevelDBBackendFactory.h" // private
 #include "node/ripple_MdbBackendFactory.h" // private
 #include "node/ripple_MemoryBackendFactory.h" // private
 #include "node/ripple_NullBackendFactory.h" // private
 #include "node/ripple_SqliteBackendFactory.h" // private
#include "main/ripple_RippleMain.cpp"

#include "node/ripple_HyperLevelDBBackendFactory.cpp"
#include "node/ripple_KeyvaDBBackendFactory.cpp"
#include "node/ripple_LevelDBBackendFactory.cpp"
#include "node/ripple_MemoryBackendFactory.cpp"
#include "node/ripple_NullBackendFactory.cpp"
#include "node/ripple_MdbBackendFactory.cpp"
#include "node/ripple_SqliteBackendFactory.cpp"

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

