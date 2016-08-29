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

#ifndef RIPPLE_RPC_HANDLERS_HANDLERS_H_INCLUDED
#define RIPPLE_RPC_HANDLERS_HANDLERS_H_INCLUDED

#include <ripple/rpc/handlers/LedgerHandler.h>

namespace ripple {

Json::Value doAccountCurrencies     (RPC::Context&);
Json::Value doAccountInfo           (RPC::Context&);
Json::Value doAccountLines          (RPC::Context&);
Json::Value doAccountChannels       (RPC::Context&);
Json::Value doAccountObjects        (RPC::Context&);
Json::Value doAccountOffers         (RPC::Context&);
Json::Value doAccountTx             (RPC::Context&);
Json::Value doAccountTxSwitch       (RPC::Context&);
Json::Value doAccountTxOld          (RPC::Context&);
Json::Value doBookOffers            (RPC::Context&);
Json::Value doBlackList             (RPC::Context&);
Json::Value doCanDelete             (RPC::Context&);
Json::Value doChannelAuthorize      (RPC::Context&);
Json::Value doChannelVerify         (RPC::Context&);
Json::Value doConnect               (RPC::Context&);
Json::Value doConsensusInfo         (RPC::Context&);
Json::Value doFeature               (RPC::Context&);
Json::Value doFee                   (RPC::Context&);
Json::Value doFetchInfo             (RPC::Context&);
Json::Value doGatewayBalances       (RPC::Context&);
Json::Value doGetCounts             (RPC::Context&);
Json::Value doLedgerAccept          (RPC::Context&);
Json::Value doLedgerCleaner         (RPC::Context&);
Json::Value doLedgerClosed          (RPC::Context&);
Json::Value doLedgerCurrent         (RPC::Context&);
Json::Value doLedgerData            (RPC::Context&);
Json::Value doLedgerEntry           (RPC::Context&);
Json::Value doLedgerHeader          (RPC::Context&);
Json::Value doLedgerRequest         (RPC::Context&);
Json::Value doLogLevel              (RPC::Context&);
Json::Value doLogRotate             (RPC::Context&);
Json::Value doNoRippleCheck         (RPC::Context&);
Json::Value doOwnerInfo             (RPC::Context&);
Json::Value doPathFind              (RPC::Context&);
Json::Value doPeers                 (RPC::Context&);
Json::Value doPing                  (RPC::Context&);
Json::Value doPrint                 (RPC::Context&);
Json::Value doRandom                (RPC::Context&);
Json::Value doRipplePathFind        (RPC::Context&);
Json::Value doServerInfo            (RPC::Context&); // for humans
Json::Value doServerState           (RPC::Context&); // for machines
Json::Value doSessionClose          (RPC::Context&);
Json::Value doSessionOpen           (RPC::Context&);
Json::Value doSign                  (RPC::Context&);
Json::Value doSignFor               (RPC::Context&);
Json::Value doStop                  (RPC::Context&);
Json::Value doSubmit                (RPC::Context&);
Json::Value doSubmitMultiSigned     (RPC::Context&);
Json::Value doSubscribe             (RPC::Context&);
Json::Value doTransactionEntry      (RPC::Context&);
Json::Value doTx                    (RPC::Context&);
Json::Value doTxHistory             (RPC::Context&);
Json::Value doUnlList               (RPC::Context&);
Json::Value doUnsubscribe           (RPC::Context&);
Json::Value doValidationCreate      (RPC::Context&);
Json::Value doValidationSeed        (RPC::Context&);
Json::Value doWalletLock            (RPC::Context&);
Json::Value doWalletPropose         (RPC::Context&);
Json::Value doWalletSeed            (RPC::Context&);
Json::Value doWalletUnlock          (RPC::Context&);
Json::Value doWalletVerify          (RPC::Context&);

} // ripple

#endif
