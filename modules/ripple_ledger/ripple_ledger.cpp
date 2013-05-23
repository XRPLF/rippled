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

/**	Add this to get the @ref ripple_ledger module.

    @file ripple_ledger.cpp
    @ingroup ripple_ledger
*/

// VFALCO: TODO, fix these warnings!
#ifdef _MSC_VER
//#pragma warning (push) // Causes spurious C4503 "decorated name exceeds maximum length"
#pragma warning (disable: 4018) // signed/unsigned mismatch
#pragma warning (disable: 4244) // conversion, possible loss of data
#endif



#include "ripple_ledger.h"

//#define WIN32_LEAN_AND_MEAN 

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <openssl/ec.h>
#include <openssl/ripemd.h>
#include <openssl/sha.h>
#include <string>
#include <vector>

//#include "uint256.h"

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
#include <boost/tuple/tuple_comparison.hpp>
#include <boost/unordered_set.hpp>

#include "src/cpp/database/SqliteDatabase.h"

#include "src/cpp/json/writer.h"

// VFALCO: TODO, resolve the location of this file
//#include "src/cpp/ripple/ripple.pb.h"
#include "ripple.pb.h"

#include "src/cpp/ripple/AcceptedLedger.h"
#include "src/cpp/ripple/AccountItems.h"
#include "src/cpp/ripple/AccountSetTransactor.h"
#include "src/cpp/ripple/AccountState.h"
#include "src/cpp/ripple/Application.h"
#include "src/cpp/ripple/BitcoinUtil.h"
#include "src/cpp/ripple/CanonicalTXSet.h"
#include "src/cpp/ripple/ChangeTransactor.h"
#include "src/cpp/ripple/Config.h"
#include "src/cpp/ripple/FeatureTable.h"
#include "src/cpp/ripple/FieldNames.h"
#include "src/cpp/ripple/HashPrefixes.h"
#include "src/cpp/ripple/key.h"
#include "src/cpp/ripple/Ledger.h"
#include "src/cpp/ripple/LedgerAcquire.h"
#include "src/cpp/ripple/LedgerConsensus.h"
#include "src/cpp/ripple/LedgerEntrySet.h"
#include "src/cpp/ripple/LedgerFormats.h"
#include "src/cpp/ripple/LedgerHistory.h"
#include "src/cpp/ripple/LedgerMaster.h"
#include "src/cpp/ripple/LedgerProposal.h"
#include "src/cpp/ripple/LedgerTiming.h"
#include "src/cpp/ripple/Log.h"
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
#include "src/cpp/ripple/RippleAddress.h"
#include "src/cpp/ripple/RippleCalc.h"
#include "src/cpp/ripple/RippleState.h"
#include "src/cpp/ripple/SerializedLedger.h"
#include "src/cpp/ripple/SerializedObject.h"
#include "src/cpp/ripple/SerializedTransaction.h"
#include "src/cpp/ripple/SerializedTypes.h"
#include "src/cpp/ripple/SerializedValidation.h"
#include "src/cpp/ripple/Serializer.h"
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
#include "src/cpp/ripple/utils.h"
#include "src/cpp/ripple/ValidationCollection.h"
#include "src/cpp/ripple/Wallet.h"
#include "src/cpp/ripple/WalletAddTransactor.h"

// contract stuff, order matters
#include "src/cpp/ripple/ScriptData.h"
#include "src/cpp/ripple/Contract.h"
#include "src/cpp/ripple/Interpreter.h"
#include "src/cpp/ripple/Operation.h"

//------------------------------------------------------------------------------

// contracts
#include "src/cpp/ripple/Contract.cpp" // no log
#include "src/cpp/ripple/Interpreter.cpp" // no log
#include "src/cpp/ripple/ScriptData.cpp" // no log
#include "src/cpp/ripple/Operation.cpp" // no log

// processing
#include "src/cpp/ripple/AcceptedLedger.cpp" // no log
#include "src/cpp/ripple/AccountItems.cpp" // no log
#include "src/cpp/ripple/AccountState.cpp" // no log
#include "src/cpp/ripple/FeatureTable.cpp"
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
#include "src/cpp/ripple/Serializer.cpp"

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

#ifdef _MSC_VER
//#pragma warning (pop)
#endif
