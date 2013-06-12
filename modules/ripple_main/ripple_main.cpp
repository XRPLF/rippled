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

#if 0
#include <boost/foreach.hpp>
#include <boost/bind.hpp>
#include <boost/thread.hpp>
#include <string>

#include <boost/test/unit_test.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

#include <openssl/rand.h>
#include <string>
#include <boost/test/unit_test.hpp>
#endif

//------------------------------------------------------------------------------

#include <algorithm>
#include <bitset>
#include <cassert>
#include <deque>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <queue>
#include <set>
#include <stack>
#include <string>
#include <vector>

// VFALCO NOTE Holy smokes...that's a lot of boost!!!

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/bimap.hpp>
#include <boost/bimap/unordered_set_of.hpp>
#include <boost/bimap/multiset_of.hpp>
#include <boost/bind.hpp>
#include <boost/cstdint.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/function.hpp>
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

#if ! defined (RIPPLE_MAIN_PART) || RIPPLE_MAIN_PART == 1
#include <boost/test/included/unit_test.hpp>
#endif

#include <boost/test/unit_test.hpp>

#include <openssl/ec.h>
#include <openssl/md5.h>
#include <openssl/ripemd.h>
#include <openssl/sha.h>

//------------------------------------------------------------------------------

// VFALCO TODO fix these warnings!
#ifdef _MSC_VER
//#pragma warning (push) // Causes spurious C4503 "decorated name exceeds maximum length"
#pragma warning (disable: 4018) // signed/unsigned mismatch
#pragma warning (disable: 4309) // truncation of constant value (websocket)
#pragma warning (disable: 4244) // conversion, possible loss of data
#pragma warning (disable: 4535) // call requires /EHa
#endif

// VFALCO NOTE these includes generate warnings, unfortunately.
#include "ripple_main.h"

#include "../ripple_data/ripple_data.h"

//------------------------------------------------------------------------------

// VFALCO NOTE The order of these includes is critical, since they do not
//             include their own dependencies. This is what allows us to
//             linearize the include sequence and view it in one place.
//

// VFALCO BEGIN CLEAN AREA These are all include-stripped

#include "src/cpp/ripple/ripple_HashedObject.h"

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
#include "src/cpp/ripple/NicknameState.h"
#include "src/cpp/ripple/Ledger.h"

#include "src/cpp/ripple/SerializedValidation.h"
#include "src/cpp/ripple/LoadManager.h" // VFALCO TODO Split this file up


// These have few dependencies
#include "src/cpp/ripple/ripple_Config.h"
#include "src/cpp/ripple/ripple_DatabaseCon.h"
#include "src/cpp/ripple/ripple_LoadEvent.h"
#include "src/cpp/ripple/ripple_LoadMonitor.h"
#include "src/cpp/ripple/ripple_ProofOfWork.h"
#include "src/cpp/ripple/ripple_Job.h"
#include "src/cpp/ripple/ripple_JobQueue.h"
#include "src/cpp/ripple/ripple_InfoSub.h"
#include "src/cpp/ripple/ripple_HashedObject.h"
#include "src/cpp/ripple/ripple_HashedObjectStore.h"
#include "src/cpp/ripple/ripple_OrderBook.h"
#include "src/cpp/ripple/ripple_SHAMapSyncFilters.h"

// Abstract interfaces
#include "src/cpp/ripple/ripple_IFeatures.h"
#include "src/cpp/ripple/ripple_IFeeVote.h"
#include "src/cpp/ripple/ripple_IHashRouter.h"
#include "src/cpp/ripple/ripple_ILoadFeeTrack.h"
#include "src/cpp/ripple/ripple_Peer.h" // VFALCO TODO Rename to IPeer
#include "src/cpp/ripple/ripple_IPeers.h"
#include "src/cpp/ripple/ripple_IProofOfWorkFactory.h"
#include "src/cpp/ripple/ripple_IUniqueNodeList.h"
#include "src/cpp/ripple/ripple_IValidations.h"

#include "src/cpp/ripple/ripple_PeerSet.h"
#include "src/cpp/ripple/ripple_LedgerAcquire.h"
#include "src/cpp/ripple/ripple_LedgerAcquireMaster.h"

#include "src/cpp/database/database.h"
#include "src/cpp/database/SqliteDatabase.h"

// VFALCO END CLEAN AREA

//------------------------------------------------------------------------------

// VFALCO NOTE Order matters! If you get compile errors, move just 1
//               include upwards as little as possible to fix it.
//            
#include "src/cpp/ripple/ScriptData.h"
#include "src/cpp/ripple/Contract.h"
#include "src/cpp/ripple/Interpreter.h"
#include "src/cpp/ripple/Operation.h"
// VFALCO NOTE Order matters

// -----------
// VFALCO NOTE These have all been include-stripped
// ORDER MATTERS A LOT!




#include "src/cpp/ripple/ripple_AccountItem.h"
#include "src/cpp/ripple/ripple_AccountItems.h"
#include "src/cpp/ripple/ripple_AcceptedLedgerTx.h"
#include "src/cpp/ripple/ripple_AcceptedLedger.h"
#include "src/cpp/ripple/LedgerEntrySet.h"
#include "src/cpp/ripple/TransactionEngine.h"
#include "src/cpp/ripple/ripple_CanonicalTXSet.h"

#include "src/cpp/ripple/ripple_LedgerHistory.h"
#include "src/cpp/ripple/LedgerMaster.h"

#include "src/cpp/ripple/LedgerProposal.h"
#include "src/cpp/ripple/NetworkOPs.h"

//
// -----------

#include "src/cpp/ripple/TransactionMaster.h"
#include "src/cpp/ripple/Wallet.h"
#include "src/cpp/ripple/WSDoor.h"
#include "src/cpp/ripple/SNTPClient.h"
#include "src/cpp/ripple/RPCHandler.h"
#include "src/cpp/ripple/TransactionQueue.h"
#include "src/cpp/ripple/OrderBookDB.h"
#include "src/cpp/ripple/ripple_DatabaseCon.h"

#include "src/cpp/ripple/ripple_IApplication.h"
#include "src/cpp/ripple/AutoSocket.h"
#include "src/cpp/ripple/CallRPC.h"
#include "src/cpp/ripple/ChangeTransactor.h"
#include "src/cpp/ripple/HTTPRequest.h"
#include "src/cpp/ripple/HashPrefixes.h"
#include "src/cpp/ripple/HttpsClient.h"
#include "src/cpp/ripple/ripple_TransactionAcquire.h"
#include "src/cpp/ripple/ripple_DisputedTx.h"
#include "src/cpp/ripple/LedgerConsensus.h"
#include "src/cpp/ripple/LedgerTiming.h"
#include "src/cpp/ripple/Offer.h"
#include "src/cpp/ripple/OfferCancelTransactor.h"
#include "src/cpp/ripple/OfferCreateTransactor.h"
#include "src/cpp/ripple/ripple_PathRequest.h"
#include "src/cpp/ripple/ParameterTable.h"
#include "src/cpp/ripple/ParseSection.h"
#include "src/cpp/ripple/Pathfinder.h"
#include "src/cpp/ripple/PaymentTransactor.h"
#include "src/cpp/ripple/PeerDoor.h"
#include "src/cpp/ripple/RPC.h"
#include "src/cpp/ripple/RPCDoor.h"
#include "src/cpp/ripple/RPCErr.h"
#include "src/cpp/ripple/RPCServer.h"
#include "src/cpp/ripple/RPCSub.h"
#include "src/cpp/ripple/RegularKeySetTransactor.h"
#include "src/cpp/ripple/RippleCalc.h"
#include "src/cpp/ripple/RippleState.h"
#include "src/cpp/ripple/SerializedValidation.h"
#include "src/cpp/ripple/Transactor.h"
#include "src/cpp/ripple/AccountSetTransactor.h"
#include "src/cpp/ripple/TrustSetTransactor.h"
#include "src/cpp/ripple/Version.h"
#include "src/cpp/ripple/WSConnection.h"
#include "src/cpp/ripple/WSHandler.h"
#include "src/cpp/ripple/WalletAddTransactor.h"

#include "../websocketpp/src/logger/logger.hpp" // for ripple_LogWebSockets.cpp

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

#include "src/cpp/database/database.cpp"
#include "src/cpp/database/SqliteDatabase.cpp"

#include "src/cpp/ripple/ripple_AccountItem.cpp"
#include "src/cpp/ripple/ripple_AccountItems.cpp"
#include "src/cpp/ripple/AccountSetTransactor.cpp"
#include "src/cpp/ripple/ripple_AccountState.cpp"
#include "src/cpp/ripple/CallRPC.cpp"
#include "src/cpp/ripple/ripple_CanonicalTXSet.cpp"
#include "src/cpp/ripple/ChangeTransactor.cpp" // no log
#include "src/cpp/ripple/Contract.cpp" // no log
#include "src/cpp/ripple/DBInit.cpp"
#include "src/cpp/ripple/HTTPRequest.cpp"
#include "src/cpp/ripple/HttpsClient.cpp"
#include "src/cpp/ripple/Interpreter.cpp" // no log
#include "src/cpp/ripple/Ledger.cpp"
#include "src/cpp/ripple/LedgerConsensus.cpp"
#include "src/cpp/ripple/LedgerEntrySet.cpp"
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
#include "src/cpp/ripple/OrderBookDB.cpp"

#endif

//------------------------------------------------------------------------------

#if ! defined (RIPPLE_MAIN_PART) || RIPPLE_MAIN_PART == 2

// This is for PeerDoor and WSDoor
// Generate DH for SSL connection.
static DH* handleTmpDh(SSL* ssl, int is_export, int iKeyLength)
{
// VFALCO TODO eliminate this horrendous dependency on theApp and Wallet
	return 512 == iKeyLength ? theApp->getWallet().getDh512() : theApp->getWallet().getDh1024();
}

#include "src/cpp/ripple/ParameterTable.cpp" // no log
#include "src/cpp/ripple/ParseSection.cpp"
#include "src/cpp/ripple/Pathfinder.cpp"
#include "src/cpp/ripple/PaymentTransactor.cpp"
#include "src/cpp/ripple/PeerDoor.cpp"
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
#include "src/cpp/ripple/SerializedValidation.cpp"
#include "src/cpp/ripple/SNTPClient.cpp"
#include "src/cpp/ripple/Transaction.cpp"
#include "src/cpp/ripple/TransactionCheck.cpp"
#include "src/cpp/ripple/TransactionEngine.cpp"
#include "src/cpp/ripple/TransactionMaster.cpp" // no log
#include "src/cpp/ripple/TransactionMeta.cpp"
#include "src/cpp/ripple/TransactionQueue.cpp" // no log
#include "src/cpp/ripple/Transactor.cpp"
#include "src/cpp/ripple/TrustSetTransactor.cpp"
#include "src/cpp/ripple/WSConnection.cpp"
#include "src/cpp/ripple/WSDoor.cpp" // uses logging in WSConnection.h 
#include "src/cpp/ripple/WSHandler.cpp"

#endif

//------------------------------------------------------------------------------

#if ! defined (RIPPLE_MAIN_PART) || RIPPLE_MAIN_PART == 3

#include "src/cpp/ripple/Wallet.cpp"
#include "src/cpp/ripple/WalletAddTransactor.cpp"

#include "src/cpp/ripple/ripple_HashedObject.cpp"

#include "src/cpp/ripple/ripple_SHAMap.cpp"                 // Uses theApp
#include "src/cpp/ripple/ripple_SHAMapDelta.cpp"
#include "src/cpp/ripple/ripple_SHAMapItem.cpp"
#include "src/cpp/ripple/ripple_SHAMapNode.cpp"
#include "src/cpp/ripple/ripple_SHAMapSync.cpp"
#include "src/cpp/ripple/ripple_SHAMapTreeNode.cpp"
#include "src/cpp/ripple/ripple_SHAMapMissingNode.cpp"

#include "src/cpp/ripple/ripple_AcceptedLedgerTx.cpp"
#include "src/cpp/ripple/ripple_AcceptedLedger.cpp"
#include "src/cpp/ripple/ripple_Application.cpp"
#include "src/cpp/ripple/ripple_Config.cpp"
#include "src/cpp/ripple/ripple_DatabaseCon.cpp"
#include "src/cpp/ripple/ripple_DisputedTx.cpp"
#include "src/cpp/ripple/ripple_Features.cpp"
#include "src/cpp/ripple/ripple_FeeVote.cpp"
#include "src/cpp/ripple/ripple_HashedObjectStore.cpp"
#include "src/cpp/ripple/ripple_HashRouter.cpp"
//#include "src/cpp/ripple/ripple_InfoSub.cpp"

#endif

//------------------------------------------------------------------------------

#if ! defined (RIPPLE_MAIN_PART) || RIPPLE_MAIN_PART == 4

#include "src/cpp/ripple/ripple_Job.cpp"
#include "src/cpp/ripple/ripple_JobQueue.cpp"
#include "src/cpp/ripple/ripple_LedgerAcquire.cpp"
#include "src/cpp/ripple/ripple_LedgerAcquireMaster.cpp"
#include "src/cpp/ripple/ripple_LedgerHistory.cpp"
#include "src/cpp/ripple/ripple_LoadEvent.cpp"
#include "src/cpp/ripple/ripple_LoadMonitor.cpp"
#include "src/cpp/ripple/ripple_LogWebsockets.cpp"
#include "src/cpp/ripple/ripple_LoadFeeTrack.cpp"
#include "src/cpp/ripple/ripple_OrderBook.cpp"
#include "src/cpp/ripple/ripple_PathRequest.cpp"
#include "src/cpp/ripple/ripple_Peer.cpp"
#include "src/cpp/ripple/ripple_Peers.cpp"
#include "src/cpp/ripple/ripple_PeerSet.cpp"
#include "src/cpp/ripple/ripple_ProofOfWork.cpp"
#include "src/cpp/ripple/ripple_ProofOfWorkFactory.cpp"
#include "src/cpp/ripple/ripple_SerializedLedger.cpp"
#include "src/cpp/ripple/ripple_SerializedTransaction.cpp"
#include "src/cpp/ripple/ripple_TransactionAcquire.cpp"
#include "src/cpp/ripple/ripple_Validations.cpp"
#include "src/cpp/ripple/ripple_UniqueNodeList.cpp"

#include "src/cpp/ripple/ripple_SHAMapSyncFilters.cpp" // requires Application

#endif

//------------------------------------------------------------------------------

//------------------------------------------------------------------------------

#ifdef _MSC_VER
//#pragma warning (pop)
#endif

