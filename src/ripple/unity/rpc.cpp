//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <BeastConfig.h>

// This has to be included early to prevent an obscure MSVC compile error
#include <boost/asio/deadline_timer.hpp>

#include <ripple/protocol/JsonFields.h>

#include <ripple/rpc/RPCHandler.h>

#include <ripple/rpc/impl/Coroutine.cpp>
#include <ripple/rpc/impl/RPCHandler.cpp>
#include <ripple/rpc/impl/Status.cpp>
#include <ripple/rpc/impl/Yield.cpp>
#include <ripple/rpc/impl/Utilities.cpp>

#include <ripple/rpc/handlers/Handlers.h>
#include <ripple/rpc/handlers/AccountCurrenciesHandler.cpp>
#include <ripple/rpc/handlers/AccountInfo.cpp>
#include <ripple/rpc/handlers/AccountLines.cpp>
#include <ripple/rpc/handlers/AccountObjects.cpp>
#include <ripple/rpc/handlers/AccountOffers.cpp>
#include <ripple/rpc/handlers/AccountTx.cpp>
#include <ripple/rpc/handlers/AccountTxOld.cpp>
#include <ripple/rpc/handlers/AccountTxSwitch.cpp>
#include <ripple/rpc/handlers/BlackList.cpp>
#include <ripple/rpc/handlers/BookOffers.cpp>
#include <ripple/rpc/handlers/CanDelete.cpp>
#include <ripple/rpc/handlers/Connect.cpp>
#include <ripple/rpc/handlers/ConsensusInfo.cpp>
#include <ripple/rpc/handlers/Feature1.cpp>
#include <ripple/rpc/handlers/FetchInfo.cpp>
#include <ripple/rpc/handlers/GatewayBalances.cpp>
#include <ripple/rpc/handlers/GetCounts.cpp>
#include <ripple/rpc/handlers/Internal.cpp>
#include <ripple/rpc/handlers/LedgerHandler.cpp>
#include <ripple/rpc/handlers/LedgerAccept.cpp>
#include <ripple/rpc/handlers/LedgerCleanerHandler.cpp>
#include <ripple/rpc/handlers/LedgerClosed.cpp>
#include <ripple/rpc/handlers/LedgerCurrent.cpp>
#include <ripple/rpc/handlers/LedgerData.cpp>
#include <ripple/rpc/handlers/LedgerEntry.cpp>
#include <ripple/rpc/handlers/LedgerHeader.cpp>
#include <ripple/rpc/handlers/LedgerRequest.cpp>
#include <ripple/rpc/handlers/LogLevel.cpp>
#include <ripple/rpc/handlers/LogRotate.cpp>
#include <ripple/rpc/handlers/NoRippleCheck.cpp>
#include <ripple/rpc/handlers/OwnerInfo.cpp>
#include <ripple/rpc/handlers/PathFind.cpp>
#include <ripple/rpc/handlers/Peers.cpp>
#include <ripple/rpc/handlers/Ping.cpp>
#include <ripple/rpc/handlers/Print.cpp>
#include <ripple/rpc/handlers/Random.cpp>
#include <ripple/rpc/handlers/RipplePathFind.cpp>
#include <ripple/rpc/handlers/ServerInfo.cpp>
#include <ripple/rpc/handlers/ServerState.cpp>
#include <ripple/rpc/handlers/SignHandler.cpp>
#include <ripple/rpc/handlers/SignFor.cpp>
#include <ripple/rpc/handlers/Stop.cpp>
#include <ripple/rpc/handlers/Submit.cpp>
#include <ripple/rpc/handlers/SubmitMultiSigned.cpp>
#include <ripple/rpc/handlers/Subscribe.cpp>
#include <ripple/rpc/handlers/TransactionEntry.cpp>
#include <ripple/rpc/handlers/Tx.cpp>
#include <ripple/rpc/handlers/TxHistory.cpp>
#include <ripple/rpc/handlers/UnlAdd.cpp>
#include <ripple/rpc/handlers/UnlDelete.cpp>
#include <ripple/rpc/handlers/UnlList.cpp>
#include <ripple/rpc/handlers/UnlLoad.cpp>
#include <ripple/rpc/handlers/UnlNetwork.cpp>
#include <ripple/rpc/handlers/UnlReset.cpp>
#include <ripple/rpc/handlers/UnlScore.cpp>
#include <ripple/rpc/handlers/Unsubscribe.cpp>
#include <ripple/rpc/handlers/ValidationCreate.cpp>
#include <ripple/rpc/handlers/ValidationSeed.cpp>
#include <ripple/rpc/handlers/WalletPropose.cpp>
#include <ripple/rpc/handlers/WalletSeed.cpp>

#include <ripple/rpc/impl/AccountFromString.cpp>
#include <ripple/rpc/impl/Accounts.cpp>
#include <ripple/rpc/impl/GetAccountObjects.cpp>
#include <ripple/rpc/impl/Handler.cpp>
#include <ripple/rpc/impl/KeypairForSignature.cpp>
#include <ripple/rpc/impl/LegacyPathFind.cpp>
#include <ripple/rpc/impl/LookupLedger.cpp>
#include <ripple/rpc/impl/ParseAccountIds.cpp>
#include <ripple/rpc/impl/TransactionSign.cpp>
#include <ripple/rpc/impl/RPCVersion.cpp>

#include <ripple/rpc/tests/Coroutine.test.cpp>
#include <ripple/rpc/tests/JSONRPC.test.cpp>
#include <ripple/rpc/tests/KeyGeneration.test.cpp>
#include <ripple/rpc/tests/Status.test.cpp>
#include <ripple/rpc/tests/Yield.test.cpp>
