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

#include <tuple>

#include <BeastConfig.h>

// Unfortunate but necessary since RPC handlers can literally do anything
#include <ripple/unity/app.h>
#include <ripple/unity/json.h>
#include <ripple/common/jsonrpc_fields.h>

#include <ripple/unity/rpcx.h>

#include <ripple/module/rpc/RPCHandler.h>
#include <ripple/overlay/Overlay.h>
#include <tuple>

#include <ripple/module/rpc/RPCHandler.h>
#include <ripple/module/rpc/impl/ErrorCodes.cpp>
#include <ripple/module/rpc/impl/Manager.cpp>
#include <ripple/module/rpc/impl/RPCServerHandler.cpp>
#include <ripple/module/rpc/impl/RPCHandler.cpp>

#include <ripple/module/rpc/handlers/Handlers.h>
#include <ripple/module/rpc/handlers/AccountCurrencies.cpp>
#include <ripple/module/rpc/handlers/AccountInfo.cpp>
#include <ripple/module/rpc/handlers/AccountLines.cpp>
#include <ripple/module/rpc/handlers/AccountOffers.cpp>
#include <ripple/module/rpc/handlers/AccountTx.cpp>
#include <ripple/module/rpc/handlers/AccountTxOld.cpp>
#include <ripple/module/rpc/handlers/AccountTxSwitch.cpp>
#include <ripple/module/rpc/handlers/BlackList.cpp>
#include <ripple/module/rpc/handlers/BookOffers.cpp>
#include <ripple/module/rpc/handlers/Connect.cpp>
#include <ripple/module/rpc/handlers/ConsensusInfo.cpp>
#include <ripple/module/rpc/handlers/Feature.cpp>
#include <ripple/module/rpc/handlers/FetchInfo.cpp>
#include <ripple/module/rpc/handlers/GetCounts.cpp>
#include <ripple/module/rpc/handlers/Ledger.cpp>
#include <ripple/module/rpc/handlers/LedgerAccept.cpp>
#include <ripple/module/rpc/handlers/LedgerCleaner.cpp>
#include <ripple/module/rpc/handlers/LedgerClosed.cpp>
#include <ripple/module/rpc/handlers/LedgerCurrent.cpp>
#include <ripple/module/rpc/handlers/LedgerData.cpp>
#include <ripple/module/rpc/handlers/LedgerEntry.cpp>
#include <ripple/module/rpc/handlers/LedgerHeader.cpp>
#include <ripple/module/rpc/handlers/LedgerRequest.cpp>
#include <ripple/module/rpc/handlers/LogLevel.cpp>
#include <ripple/module/rpc/handlers/LogRotate.cpp>
#include <ripple/module/rpc/handlers/NicknameInfo.cpp>
#include <ripple/module/rpc/handlers/OwnerInfo.cpp>
#include <ripple/module/rpc/handlers/PathFind.cpp>
#include <ripple/module/rpc/handlers/Peers.cpp>
#include <ripple/module/rpc/handlers/Ping.cpp>
#include <ripple/module/rpc/handlers/Print.cpp>
#include <ripple/module/rpc/handlers/Profile.cpp>
#include <ripple/module/rpc/handlers/ProofCreate.cpp>
#include <ripple/module/rpc/handlers/ProofSolve.cpp>
#include <ripple/module/rpc/handlers/ProofVerify.cpp>
#include <ripple/module/rpc/handlers/Random.cpp>
#include <ripple/module/rpc/handlers/RipplePathFind.cpp>
#include <ripple/module/rpc/handlers/SMS.cpp>
#include <ripple/module/rpc/handlers/ServerInfo.cpp>
#include <ripple/module/rpc/handlers/ServerState.cpp>
#include <ripple/module/rpc/handlers/Sign.cpp>
#include <ripple/module/rpc/handlers/Stop.cpp>
#include <ripple/module/rpc/handlers/Submit.cpp>
#include <ripple/module/rpc/handlers/Subscribe.cpp>
#include <ripple/module/rpc/handlers/TransactionEntry.cpp>
#include <ripple/module/rpc/handlers/Tx.cpp>
#include <ripple/module/rpc/handlers/TxHistory.cpp>
#include <ripple/module/rpc/handlers/UnlAdd.cpp>
#include <ripple/module/rpc/handlers/UnlDelete.cpp>
#include <ripple/module/rpc/handlers/UnlList.cpp>
#include <ripple/module/rpc/handlers/UnlLoad.cpp>
#include <ripple/module/rpc/handlers/UnlNetwork.cpp>
#include <ripple/module/rpc/handlers/UnlReset.cpp>
#include <ripple/module/rpc/handlers/UnlScore.cpp>
#include <ripple/module/rpc/handlers/Unsubscribe.cpp>
#include <ripple/module/rpc/handlers/ValidationCreate.cpp>
#include <ripple/module/rpc/handlers/ValidationSeed.cpp>
#include <ripple/module/rpc/handlers/WalletAccounts.cpp>
#include <ripple/module/rpc/handlers/WalletPropose.cpp>
#include <ripple/module/rpc/handlers/WalletSeed.cpp>

#include <ripple/module/rpc/impl/AccountFromString.cpp>
#include <ripple/module/rpc/impl/Accounts.cpp>
#include <ripple/module/rpc/impl/Authorize.cpp>
#include <ripple/module/rpc/impl/GetMasterGenerator.cpp>
#include <ripple/module/rpc/impl/Handler.cpp>
#include <ripple/module/rpc/impl/LegacyPathFind.cpp>
#include <ripple/module/rpc/impl/LookupLedger.cpp>
#include <ripple/module/rpc/impl/ParseAccountIds.cpp>
#include <ripple/module/rpc/impl/TransactionSign.cpp>
