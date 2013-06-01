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

//------------------------------------------------------------------------------

// main
#include "src/cpp/ripple/ripple_DatabaseCon.cpp"
#include "src/cpp/ripple/Application.cpp"
#include "src/cpp/ripple/LoadManager.cpp"

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

// Implementation of interfaces

#include "src/cpp/ripple/ripple_FeeVote.cpp"
#include "src/cpp/ripple/ripple_LoadFeeTrack.cpp"
#include "src/cpp/ripple/ripple_Validations.cpp"

#ifdef _MSC_VER
//#pragma warning (pop)
#endif
