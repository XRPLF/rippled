//------------------------------------------------------------------------------
/*
	Copyright (c) 2011-2013, OpenCoin, Inc.

	Permission to use, copy, modify, and/or distribute this software for any
	purpose with  or without fee is hereby granted,  provided that the above
	copyright notice and this permission notice appear in all copies.

	THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
	WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES OF
	MERCHANTABILITY  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
	ANY SPECIAL,  DIRECT, INDIRECT,  OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
	WHATSOEVER  RESULTING  FROM LOSS OF USE, DATA OR PROFITS,  WHETHER IN AN
	ACTION OF CONTRACT, NEGLIGENCE  OR OTHER TORTIOUS ACTION, ARISING OUT OF
	OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

/**	Add this to get the @ref ripple_main module.

    @file ripple_main.cpp
    @ingroup ripple_main
*/

//------------------------------------------------------------------------------

#include <algorithm>
#include <bitset>
#include <cassert>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/bimap.hpp>
#include <boost/bimap/unordered_set_of.hpp>
#include <boost/bimap/multiset_of.hpp>
#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/make_shared.hpp>
#include <boost/pointer_cast.hpp>
#include <boost/ref.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/thread.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>

#include <openssl/ec.h>
#include <openssl/ripemd.h>
#include <openssl/sha.h>

//------------------------------------------------------------------------------

// VFALCO: TODO, prepare a unity header for LevelDB
#ifdef USE_LEVELDB
#include "leveldb/cache.h"
#include "leveldb/filter_policy.h"
#endif

//------------------------------------------------------------------------------

// VFALCO: TODO, fix these warnings!
#ifdef _MSC_VER
//#pragma warning (push) // Causes spurious C4503 "decorated name exceeds maximum length"
#pragma warning (disable: 4018) // signed/unsigned mismatch
#pragma warning (disable: 4309) // truncation of constant value (websocket)
#pragma warning (disable: 4244) // conversion, possible loss of data
#pragma warning (disable: 4535) // call requires /EHa
#endif

// VFALCO: NOTE, these includes generate warnings, unfortunately.
#include "ripple_main.h"
#include "../ripple_data/ripple_data.h"

//------------------------------------------------------------------------------

// Order and position matter here
#include "src/cpp/ripple/Ledger.h"
#include "src/cpp/ripple/SerializedValidation.h"
#include "src/cpp/database/SqliteDatabase.h"

//------------------------------------------------------------------------------
//
// VFALCO: BEGIN CLEAN AREA

#include "src/cpp/ripple/ripple_Config.h"
#include "src/cpp/ripple/ripple_DatabaseCon.h"
#include "src/cpp/ripple/ripple_LoadEvent.h"
#include "src/cpp/ripple/ripple_LoadMonitor.h"
#include "src/cpp/ripple/ripple_ProofOfWork.h"
#include "src/cpp/ripple/ripple_Job.h"
#include "src/cpp/ripple/ripple_JobQueue.h"

#include "src/cpp/ripple/ripple_IFeatures.h"
#include "src/cpp/ripple/ripple_IFeeVote.h"
#include "src/cpp/ripple/ripple_IHashRouter.h"
#include "src/cpp/ripple/ripple_ILoadFeeTrack.h"
#include "src/cpp/ripple/ripple_Peer.h" // VFALCO: TODO Rename to IPeer
#include "src/cpp/ripple/ripple_IPeers.h"
#include "src/cpp/ripple/ripple_IProofOfWorkFactory.h"
#include "src/cpp/ripple/ripple_IUniqueNodeList.h"
#include "src/cpp/ripple/ripple_IValidations.h"

// VFALCO: END CLEAN AREA
//
//------------------------------------------------------------------------------

// VFALCO: NOTE, Order matters! If you get compile errors, move just 1
//               include upwards as little as possible to fix it.
//            
#include "src/cpp/ripple/ScriptData.h"
#include "src/cpp/ripple/Contract.h"
#include "src/cpp/ripple/Interpreter.h"
#include "src/cpp/ripple/Operation.h"
// VFALCO: NOTE, Order matters
#include "src/cpp/ripple/AcceptedLedger.h"
#include "src/cpp/ripple/AccountItems.h"
#include "src/cpp/ripple/AccountSetTransactor.h"
#include "src/cpp/ripple/AccountState.h"
#include "src/cpp/ripple/Application.h"
#include "src/cpp/ripple/AutoSocket.h"
#include "src/cpp/ripple/CallRPC.h"
#include "src/cpp/ripple/CanonicalTXSet.h"
#include "src/cpp/ripple/ChangeTransactor.h"
#include "src/cpp/ripple/HTTPRequest.h"
#include "src/cpp/ripple/HashPrefixes.h"
#include "src/cpp/ripple/HashedObject.h"
#include "src/cpp/ripple/HttpsClient.h"
#include "src/cpp/ripple/Ledger.h"
#include "src/cpp/ripple/LedgerAcquire.h"
#include "src/cpp/ripple/LedgerConsensus.h"
#include "src/cpp/ripple/LedgerEntrySet.h"
#include "src/cpp/ripple/LedgerHistory.h"
#include "src/cpp/ripple/LedgerMaster.h"
#include "src/cpp/ripple/LedgerProposal.h"
#include "src/cpp/ripple/LedgerTiming.h"
#include "src/cpp/ripple/LoadManager.h"
#include "src/cpp/ripple/NetworkOPs.h"
#include "src/cpp/ripple/NicknameState.h"
#include "src/cpp/ripple/Offer.h"
#include "src/cpp/ripple/OfferCancelTransactor.h"
#include "src/cpp/ripple/OfferCreateTransactor.h"
#include "src/cpp/ripple/OrderBook.h"
#include "src/cpp/ripple/OrderBookDB.h"
#include "src/cpp/ripple/PFRequest.h"
#include "src/cpp/ripple/ParameterTable.h"
#include "src/cpp/ripple/ParseSection.h"
#include "src/cpp/ripple/Pathfinder.h"
#include "src/cpp/ripple/PaymentTransactor.h"
#include "src/cpp/ripple/PeerDoor.h"
#include "src/cpp/ripple/RPC.h"
#include "src/cpp/ripple/RPCDoor.h"
#include "src/cpp/ripple/RPCErr.h"
#include "src/cpp/ripple/RPCHandler.h"
#include "src/cpp/ripple/RPCServer.h"
#include "src/cpp/ripple/RPCSub.h"
#include "src/cpp/ripple/RegularKeySetTransactor.h"
#include "src/cpp/ripple/RippleCalc.h"
#include "src/cpp/ripple/RippleState.h"
#include "src/cpp/ripple/SHAMap.h"
#include "src/cpp/ripple/SHAMapSync.h"
#include "src/cpp/ripple/SNTPClient.h"
#include "src/cpp/ripple/SerializedLedger.h"
#include "src/cpp/ripple/SerializedTransaction.h"
#include "src/cpp/ripple/SerializedValidation.h"
#include "src/cpp/ripple/Transaction.h"
#include "src/cpp/ripple/TransactionEngine.h"
#include "src/cpp/ripple/TransactionMaster.h"
#include "src/cpp/ripple/TransactionMeta.h"
#include "src/cpp/ripple/TransactionQueue.h"
#include "src/cpp/ripple/Transactor.h"
#include "src/cpp/ripple/TrustSetTransactor.h"
#include "src/cpp/ripple/Version.h"
#include "src/cpp/ripple/WSConnection.h"
#include "src/cpp/ripple/WSDoor.h"
#include "src/cpp/ripple/WSHandler.h"
#include "src/cpp/ripple/Wallet.h"
#include "src/cpp/ripple/WalletAddTransactor.h"

#include "../websocketpp/src/logger/logger.hpp" // for ripple_LogWebSockets.cpp

//------------------------------------------------------------------------------

// VFALCO: TODO, figure out who needs these and move to a sensible private header.
static const uint64 tenTo14 = 100000000000000ull;
static const uint64 tenTo14m1 = tenTo14 - 1;
static const uint64 tenTo17 = tenTo14 * 1000;
static const uint64 tenTo17m1 = tenTo17 - 1;

// This is for PeerDoor and WSDoor
// Generate DH for SSL connection.
static DH* handleTmpDh(SSL* ssl, int is_export, int iKeyLength)
{
// VFALCO: TODO, eliminate this horrendous dependency on theApp and Wallet
	return 512 == iKeyLength ? theApp->getWallet().getDh512() : theApp->getWallet().getDh1024();
}

//------------------------------------------------------------------------------

#include "src/cpp/database/database.cpp"
#include "src/cpp/database/SqliteDatabase.cpp"
#include "src/cpp/ripple/AcceptedLedger.cpp" // no log
#include "src/cpp/ripple/AccountItems.cpp" // no log
#include "src/cpp/ripple/AccountSetTransactor.cpp"
#include "src/cpp/ripple/AccountState.cpp" // no log
#include "src/cpp/ripple/Application.cpp"
#include "src/cpp/ripple/CallRPC.cpp"
#include "src/cpp/ripple/CanonicalTXSet.cpp"
#include "src/cpp/ripple/ChangeTransactor.cpp" // no log
#include "src/cpp/ripple/Contract.cpp" // no log
#include "src/cpp/ripple/DBInit.cpp"
#include "src/cpp/ripple/HashedObject.cpp"
#include "src/cpp/ripple/HTTPRequest.cpp"
#include "src/cpp/ripple/HttpsClient.cpp"
#include "src/cpp/ripple/Interpreter.cpp" // no log
#include "src/cpp/ripple/Ledger.cpp"
#include "src/cpp/ripple/LedgerAcquire.cpp"
#include "src/cpp/ripple/LedgerConsensus.cpp"
#include "src/cpp/ripple/LedgerEntrySet.cpp"
#include "src/cpp/ripple/LedgerHistory.cpp" // no log
#include "src/cpp/ripple/LedgerMaster.cpp"
#include "src/cpp/ripple/LedgerProposal.cpp" // no log
#include "src/cpp/ripple/LedgerTiming.cpp"
#include "src/cpp/ripple/LoadManager.cpp"
#include "src/cpp/ripple/main.cpp"
#include "src/cpp/ripple/NetworkOPs.cpp"
#include "src/cpp/ripple/NicknameState.cpp" // no log
#include "src/cpp/ripple/Offer.cpp" // no log
#include "src/cpp/ripple/OfferCancelTransactor.cpp"
#include "src/cpp/ripple/OfferCreateTransactor.cpp"
#include "src/cpp/ripple/Operation.cpp" // no log
#include "src/cpp/ripple/OrderBook.cpp" // no log
#include "src/cpp/ripple/OrderBookDB.cpp"
#include "src/cpp/ripple/ParameterTable.cpp" // no log
#include "src/cpp/ripple/ParseSection.cpp"
#include "src/cpp/ripple/Pathfinder.cpp"
#include "src/cpp/ripple/PaymentTransactor.cpp"
#include "src/cpp/ripple/PeerDoor.cpp"
#include "src/cpp/ripple/PFRequest.cpp"
#include "src/cpp/ripple/RegularKeySetTransactor.cpp"
#include "src/cpp/ripple/RippleCalc.cpp"
#include "src/cpp/ripple/RippleState.cpp" // no log
#include "src/cpp/ripple/rpc.cpp"
#include "src/cpp/ripple/RPCDoor.cpp"
#include "src/cpp/ripple/RPCErr.cpp"
#include "src/cpp/ripple/RPCHandler.cpp"
#include "src/cpp/ripple/RPCServer.cpp"
#include "src/cpp/ripple/RPCSub.cpp"
#include "src/cpp/ripple/ScriptData.cpp" // no log
#include "src/cpp/ripple/SerializedLedger.cpp"
#include "src/cpp/ripple/SerializedTransaction.cpp"
#include "src/cpp/ripple/SerializedValidation.cpp"
#include "src/cpp/ripple/SHAMap.cpp"
#include "src/cpp/ripple/SHAMapDiff.cpp" // no log
#include "src/cpp/ripple/SHAMapNodes.cpp" // no log
#include "src/cpp/ripple/SHAMapSync.cpp"
#include "src/cpp/ripple/SNTPClient.cpp"
#include "src/cpp/ripple/Transaction.cpp"
#include "src/cpp/ripple/TransactionAcquire.cpp"
#include "src/cpp/ripple/TransactionCheck.cpp"
#include "src/cpp/ripple/TransactionEngine.cpp"
#include "src/cpp/ripple/TransactionMaster.cpp" // no log
#include "src/cpp/ripple/TransactionMeta.cpp"
#include "src/cpp/ripple/TransactionQueue.cpp" // no log
#include "src/cpp/ripple/Transactor.cpp"
#include "src/cpp/ripple/TrustSetTransactor.cpp"
#include "src/cpp/ripple/UpdateTables.cpp"
#include "src/cpp/ripple/Wallet.cpp"
#include "src/cpp/ripple/WalletAddTransactor.cpp"
#include "src/cpp/ripple/WSDoor.cpp" // uses logging in WSConnection.h 

//------------------------------------------------------------------------------

// Refactored sources

#include "src/cpp/ripple/ripple_Config.cpp" // no log
#include "src/cpp/ripple/ripple_DatabaseCon.cpp"
#include "src/cpp/ripple/ripple_Features.cpp"
#include "src/cpp/ripple/ripple_FeeVote.cpp"
#include "src/cpp/ripple/ripple_HashRouter.cpp"
#include "src/cpp/ripple/ripple_Job.cpp"
#include "src/cpp/ripple/ripple_JobQueue.cpp"
#include "src/cpp/ripple/ripple_LoadEvent.cpp"
#include "src/cpp/ripple/ripple_LoadMonitor.cpp"
#include "src/cpp/ripple/ripple_LogWebsockets.cpp"
#include "src/cpp/ripple/ripple_LoadFeeTrack.cpp"
#include "src/cpp/ripple/ripple_Peer.cpp"
#include "src/cpp/ripple/ripple_Peers.cpp"
#include "src/cpp/ripple/ripple_ProofOfWork.cpp"
#include "src/cpp/ripple/ripple_ProofOfWorkFactory.cpp"
#include "src/cpp/ripple/ripple_Validations.cpp"
#include "src/cpp/ripple/ripple_UniqueNodeList.cpp"

//------------------------------------------------------------------------------

#ifdef _MSC_VER
//#pragma warning (pop)
#endif

