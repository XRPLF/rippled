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
#include <cassert>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <list>

#include <openssl/ec.h>
#include <openssl/ripemd.h>
#include <openssl/sha.h>

#include <boost/algorithm/string.hpp>
#include <boost/bind.hpp>
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
#include <boost/thread/mutex.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <boost/unordered_set.hpp>
#include <boost/unordered_map.hpp>

//------------------------------------------------------------------------------

// VFALCO: TODO, fix these warnings!
#ifdef _MSC_VER
//#pragma warning (push) // Causes spurious C4503 "decorated name exceeds maximum length"
#pragma warning (disable: 4018) // signed/unsigned mismatch
#pragma warning (disable: 4309) // truncation of constant value (websocket)
#pragma warning (disable: 4244) // conversion, possible loss of data
#pragma warning (disable: 4535) // call requires /EHa
#endif

//------------------------------------------------------------------------------

#include "ripple_main.h"

#include "../ripple_data/ripple_data.h"

#include "src/cpp/database/SqliteDatabase.h"

#include "src/cpp/ripple/ripple_LoadEvent.h"

#include "src/cpp/ripple/AcceptedLedger.h"
#include "src/cpp/ripple/AccountItems.h"
#include "src/cpp/ripple/AccountSetTransactor.h"
#include "src/cpp/ripple/AccountState.h"
#include "src/cpp/ripple/Application.h"
#include "src/cpp/ripple/CanonicalTXSet.h"
#include "src/cpp/ripple/ChangeTransactor.h"
#include "src/cpp/ripple/Config.h"
#include "src/cpp/ripple/HashPrefixes.h"
#include "src/cpp/ripple/Ledger.h"
#include "src/cpp/ripple/LedgerAcquire.h"
#include "src/cpp/ripple/LedgerConsensus.h"
#include "src/cpp/ripple/LedgerEntrySet.h"
#include "src/cpp/ripple/LedgerFormats.h"
#include "src/cpp/ripple/LedgerHistory.h"
#include "src/cpp/ripple/LedgerMaster.h"
#include "src/cpp/ripple/LedgerProposal.h"
#include "src/cpp/ripple/LedgerTiming.h"
#include "src/cpp/ripple/NetworkOPs.h"
#include "src/cpp/ripple/NicknameState.h"
#include "src/cpp/ripple/Offer.h"
#include "src/cpp/ripple/OfferCancelTransactor.h"
#include "src/cpp/ripple/OfferCreateTransactor.h"
#include "src/cpp/ripple/OrderBook.h"
#include "src/cpp/ripple/OrderBookDB.h"
#include "src/cpp/ripple/PackedMessage.h"
#include "src/cpp/ripple/PaymentTransactor.h"
#include "src/cpp/ripple/PFRequest.h"
#include "src/cpp/ripple/RegularKeySetTransactor.h"
#include "src/cpp/ripple/RippleCalc.h"
#include "src/cpp/ripple/RippleState.h"
#include "src/cpp/ripple/SerializedLedger.h"
#include "src/cpp/ripple/SerializedObject.h"
#include "src/cpp/ripple/SerializedTransaction.h"
#include "src/cpp/ripple/SerializedTypes.h"
#include "src/cpp/ripple/SerializedValidation.h"
#include "src/cpp/ripple/SHAMapSync.h"
#include "src/cpp/ripple/Transaction.h"
#include "src/cpp/ripple/TransactionEngine.h"
#include "src/cpp/ripple/TransactionErr.h"
#include "src/cpp/ripple/TransactionFormats.h"
#include "src/cpp/ripple/TransactionMaster.h"
#include "src/cpp/ripple/TransactionMeta.h"
#include "src/cpp/ripple/TransactionQueue.h"
#include "src/cpp/ripple/Transactor.h"
#include "src/cpp/ripple/TrustSetTransactor.h"
#include "src/cpp/ripple/Wallet.h"
#include "src/cpp/ripple/WalletAddTransactor.h"

// contract stuff, order matters
#include "src/cpp/ripple/ScriptData.h"
#include "src/cpp/ripple/Contract.h"
#include "src/cpp/ripple/Interpreter.h"
#include "src/cpp/ripple/Operation.h"

#include "../websocketpp/src/logger/logger.hpp" // for ripple_LogWebSockets.cpp

// New abstract interfaces
#include "src/cpp/ripple/ripple_IFeatures.h"
#include "src/cpp/ripple/ripple_IFeeVote.h"
#include "src/cpp/ripple/ripple_IHashRouter.h"
#include "src/cpp/ripple/ripple_ILoadFeeTrack.h"
#include "src/cpp/ripple/ripple_IValidations.h"
#include "src/cpp/ripple/ripple_IUniqueNodeList.h"

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

// main
#include "src/cpp/ripple/ripple_DatabaseCon.cpp"
#include "src/cpp/ripple/Application.cpp"
#include "src/cpp/ripple/LoadManager.cpp"
#include "src/cpp/ripple/Config.cpp" // no log
#include "src/cpp/ripple/JobQueue.cpp"
#include "src/cpp/ripple/LoadMonitor.cpp"
#include "src/cpp/ripple/UpdateTables.cpp"
#include "src/cpp/ripple/main.cpp"

// contracts
#include "src/cpp/ripple/Contract.cpp" // no log
#include "src/cpp/ripple/Interpreter.cpp" // no log
#include "src/cpp/ripple/ScriptData.cpp" // no log
#include "src/cpp/ripple/Operation.cpp" // no log

// processing
#include "src/cpp/ripple/AcceptedLedger.cpp" // no log
#include "src/cpp/ripple/AccountItems.cpp" // no log
#include "src/cpp/ripple/AccountState.cpp" // no log
#include "src/cpp/ripple/Ledger.cpp"
#include "src/cpp/ripple/LedgerAcquire.cpp"
#include "src/cpp/ripple/LedgerConsensus.cpp"
#include "src/cpp/ripple/LedgerEntrySet.cpp"
#include "src/cpp/ripple/LedgerFormats.cpp" // no log
#include "src/cpp/ripple/LedgerHistory.cpp" // no log
#include "src/cpp/ripple/LedgerMaster.cpp"
#include "src/cpp/ripple/LedgerProposal.cpp" // no log
#include "src/cpp/ripple/LedgerTiming.cpp"
#include "src/cpp/ripple/NicknameState.cpp" // no log
#include "src/cpp/ripple/Offer.cpp" // no log
#include "src/cpp/ripple/OrderBook.cpp" // no log
#include "src/cpp/ripple/OrderBookDB.cpp"
#include "src/cpp/ripple/Pathfinder.cpp"
#include "src/cpp/ripple/PFRequest.cpp"
#include "src/cpp/ripple/RippleCalc.cpp"
#include "src/cpp/ripple/RippleState.cpp" // no log

// serialization
#include "src/cpp/ripple/SerializedLedger.cpp"
#include "src/cpp/ripple/SerializedObject.cpp"
#include "src/cpp/ripple/SerializedTransaction.cpp"
#include "src/cpp/ripple/SerializedTypes.cpp"
#include "src/cpp/ripple/SerializedValidation.cpp"

// transactions
#include "src/cpp/ripple/AccountSetTransactor.cpp"
#include "src/cpp/ripple/ChangeTransactor.cpp" // no log
#include "src/cpp/ripple/CanonicalTXSet.cpp"
#include "src/cpp/ripple/OfferCancelTransactor.cpp"
#include "src/cpp/ripple/OfferCreateTransactor.cpp"
#include "src/cpp/ripple/PaymentTransactor.cpp"
#include "src/cpp/ripple/RegularKeySetTransactor.cpp"
#include "src/cpp/ripple/Transaction.cpp"
#include "src/cpp/ripple/TransactionAcquire.cpp"
#include "src/cpp/ripple/TransactionCheck.cpp"
#include "src/cpp/ripple/TransactionEngine.cpp"
#include "src/cpp/ripple/TransactionErr.cpp" // no log
#include "src/cpp/ripple/TransactionFormats.cpp" // no log
#include "src/cpp/ripple/TransactionMaster.cpp" // no log
#include "src/cpp/ripple/TransactionMeta.cpp"
#include "src/cpp/ripple/TransactionQueue.cpp" // no log
#include "src/cpp/ripple/Transactor.cpp"
#include "src/cpp/ripple/TrustSetTransactor.cpp"
#include "src/cpp/ripple/Wallet.cpp"
#include "src/cpp/ripple/WalletAddTransactor.cpp"

// types
#include "src/cpp/ripple/Amount.cpp"
#include "src/cpp/ripple/AmountRound.cpp"
#include "src/cpp/ripple/HashedObject.cpp"
#include "src/cpp/ripple/PackedMessage.cpp" // no log
#include "src/cpp/ripple/ParameterTable.cpp" // no log
#include "src/cpp/ripple/ParseSection.cpp"
#include "src/cpp/ripple/ProofOfWork.cpp"

// containers
#include "src/cpp/ripple/SHAMap.cpp"
#include "src/cpp/ripple/SHAMapDiff.cpp" // no log
#include "src/cpp/ripple/SHAMapNodes.cpp" // no log
#include "src/cpp/ripple/SHAMapSync.cpp"

// misc
#include "src/cpp/ripple/ripple_HashValue.cpp"

// sockets
#include "src/cpp/ripple/SNTPClient.cpp"
#include "src/cpp/ripple/ConnectionPool.cpp"
#include "src/cpp/ripple/NetworkOPs.cpp"
#include "src/cpp/ripple/Peer.cpp"
#include "src/cpp/ripple/PeerDoor.cpp"
#include "src/cpp/ripple/WSDoor.cpp" // uses logging in WSConnection.h 
#include "src/cpp/ripple/ripple_LogWebsockets.cpp"

// http
#include "src/cpp/ripple/HTTPRequest.cpp"
#include "src/cpp/ripple/HttpsClient.cpp"

// rpc
#include "src/cpp/ripple/CallRPC.cpp"
#include "src/cpp/ripple/rpc.cpp"
#include "src/cpp/ripple/RPCDoor.cpp"
#include "src/cpp/ripple/RPCErr.cpp"
#include "src/cpp/ripple/RPCHandler.cpp"
#include "src/cpp/ripple/RPCServer.cpp"
#include "src/cpp/ripple/RPCSub.cpp"

//------------------------------------------------------------------------------

// Refactored sources

#include "src/cpp/ripple/ripple_LoadEvent.cpp"

// Implementation of interfaces

#include "src/cpp/ripple/ripple_Features.cpp"
#include "src/cpp/ripple/ripple_FeeVote.cpp"
#include "src/cpp/ripple/ripple_HashRouter.cpp"
#include "src/cpp/ripple/ripple_LoadFeeTrack.cpp"
#include "src/cpp/ripple/ripple_Validations.cpp"
#include "src/cpp/ripple/ripple_UniqueNodeList.cpp"

//------------------------------------------------------------------------------

#ifdef _MSC_VER
//#pragma warning (pop)
#endif

