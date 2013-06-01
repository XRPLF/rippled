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

/**	Include this to get the @ref ripple_ledger module.

    @file ripple_ledger.h
    @ingroup ripple_ledger
*/

/**	Ledger classes.

	This module exposes functionality for accessing and processing the ledger.

	@defgroup ripple_ledger
*/

#ifndef RIPPLE_LEDGER_H
#define RIPPLE_LEDGER_H

#include "modules/ripple_basics/ripple_basics.h"

#include "../ripple_data/ripple_data.h"

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


// New abstract interfaces
#include "src/cpp/ripple/ripple_IFeatures.h"
#include "src/cpp/ripple/ripple_IFeeVote.h"
#include "src/cpp/ripple/ripple_ILoadFeeTrack.h"
#include "src/cpp/ripple/ripple_IValidations.h"
#include "src/cpp/ripple/FeatureTable.h"

#endif
