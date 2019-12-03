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

Json::Value doAccountCurrencies     (RPC::JsonContext&);
Json::Value doAccountInfo           (RPC::JsonContext&);
Json::Value doAccountLines          (RPC::JsonContext&);
Json::Value doAccountChannels       (RPC::JsonContext&);
Json::Value doAccountObjects        (RPC::JsonContext&);
Json::Value doAccountOffers         (RPC::JsonContext&);
Json::Value doAccountTxSwitch       (RPC::JsonContext&);
Json::Value doAccountTxOld          (RPC::JsonContext&);
Json::Value doAccountTxJson         (RPC::JsonContext&);
Json::Value doBookOffers            (RPC::JsonContext&);
Json::Value doBlackList             (RPC::JsonContext&);
Json::Value doCanDelete             (RPC::JsonContext&);
Json::Value doChannelAuthorize      (RPC::JsonContext&);
Json::Value doChannelVerify         (RPC::JsonContext&);
Json::Value doConnect               (RPC::JsonContext&);
Json::Value doConsensusInfo         (RPC::JsonContext&);
Json::Value doDepositAuthorized     (RPC::JsonContext&);
Json::Value doDownloadShard         (RPC::JsonContext&);
Json::Value doFeature               (RPC::JsonContext&);
Json::Value doFee                   (RPC::JsonContext&);
Json::Value doFetchInfo             (RPC::JsonContext&);
Json::Value doGatewayBalances       (RPC::JsonContext&);
Json::Value doGetCounts             (RPC::JsonContext&);
Json::Value doLedgerAccept          (RPC::JsonContext&);
Json::Value doLedgerCleaner         (RPC::JsonContext&);
Json::Value doLedgerClosed          (RPC::JsonContext&);
Json::Value doLedgerCurrent         (RPC::JsonContext&);
Json::Value doLedgerData            (RPC::JsonContext&);
Json::Value doLedgerEntry           (RPC::JsonContext&);
Json::Value doLedgerHeader          (RPC::JsonContext&);
Json::Value doLedgerRequest         (RPC::JsonContext&);
Json::Value doLogLevel              (RPC::JsonContext&);
Json::Value doLogRotate             (RPC::JsonContext&);
Json::Value doManifest              (RPC::JsonContext&);
Json::Value doNoRippleCheck         (RPC::JsonContext&);
Json::Value doOwnerInfo             (RPC::JsonContext&);
Json::Value doPathFind              (RPC::JsonContext&);
Json::Value doPause                 (RPC::JsonContext&);
Json::Value doPeers                 (RPC::JsonContext&);
Json::Value doPing                  (RPC::JsonContext&);
Json::Value doPrint                 (RPC::JsonContext&);
Json::Value doRandom                (RPC::JsonContext&);
Json::Value doResume                (RPC::JsonContext&);
Json::Value doPeerReservationsAdd   (RPC::JsonContext&);
Json::Value doPeerReservationsDel   (RPC::JsonContext&);
Json::Value doPeerReservationsList  (RPC::JsonContext&);
Json::Value doRipplePathFind        (RPC::JsonContext&);
Json::Value doServerInfo            (RPC::JsonContext&); // for humans
Json::Value doServerState           (RPC::JsonContext&); // for machines
Json::Value doSign                  (RPC::JsonContext&);
Json::Value doSignFor               (RPC::JsonContext&);
Json::Value doCrawlShards           (RPC::JsonContext&);
Json::Value doStop                  (RPC::JsonContext&);
Json::Value doSubmit                (RPC::JsonContext&);
Json::Value doSubmitMultiSigned     (RPC::JsonContext&);
Json::Value doSubscribe             (RPC::JsonContext&);
Json::Value doTransactionEntry      (RPC::JsonContext&);
Json::Value doTxJson                (RPC::JsonContext&);
Json::Value doTxHistory             (RPC::JsonContext&);
Json::Value doUnlList               (RPC::JsonContext&);
Json::Value doUnsubscribe           (RPC::JsonContext&);
Json::Value doValidationCreate      (RPC::JsonContext&);
Json::Value doWalletPropose         (RPC::JsonContext&);
Json::Value doValidators            (RPC::JsonContext&);
Json::Value doValidatorListSites    (RPC::JsonContext&);
Json::Value doValidatorInfo         (RPC::JsonContext&);
} // ripple

#endif
