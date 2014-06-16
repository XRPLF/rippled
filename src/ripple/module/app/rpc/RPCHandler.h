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

#ifndef RIPPLE_APP_RPC_HANDLER
#define RIPPLE_APP_RPC_HANDLER

#include <ripple/module/rpc/impl/AccountFromString.h>
#include <ripple/module/rpc/impl/Accounts.h>
#include <ripple/module/rpc/impl/Authorize.h>
#include <ripple/module/rpc/impl/GetMasterGenerator.h>
#include <ripple/module/rpc/impl/LookupLedger.h>
#include <ripple/module/rpc/impl/ParseAccountIds.h>
#include <ripple/module/rpc/impl/TransactionSign.h>

namespace ripple {

// used by the RPCServer or WSDoor to carry out these RPC commands
class NetworkOPs;
class InfoSub;

// VFALCO TODO Refactor to abstract interface IRPCHandler
//

class RPCHandler
{
public:
    explicit RPCHandler (NetworkOPs* netOps);

    RPCHandler (NetworkOPs* netOps, InfoSub::pointer infoSub);

    Json::Value doCommand       (const Json::Value& jvRequest, int role, Resource::Charge& loadType);

    Json::Value doRpcCommand    (const std::string& strCommand, Json::Value const& jvParams, int iRole, Resource::Charge& loadType);

private:
    typedef Json::Value (RPCHandler::*doFuncPtr) (
        Json::Value params,
        Resource::Charge& loadType,
        Application::ScopedLockType& MasterLockHolder);

    // VFALCO TODO Document these and give the enumeration a label.
    enum
    {
        optNone     = 0,
        optNetwork  = 1,                // Need network
        optCurrent  = 2 + optNetwork,   // Need current ledger
        optClosed   = 4 + optNetwork,   // Need closed ledger
    };

    // Utilities

    Json::Value doAccountCurrencies     (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doAccountInfo           (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doAccountLines          (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doAccountOffers         (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doAccountTx             (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doAccountTxSwitch       (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doAccountTxOld          (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doBookOffers            (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doBlackList             (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doConnect               (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doConsensusInfo         (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doFeature               (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doFetchInfo             (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doGetCounts             (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doInternal              (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doLedger                (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doLedgerAccept          (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doLedgerCleaner         (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doLedgerClosed          (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doLedgerCurrent         (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doLedgerData            (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doLedgerEntry           (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doLedgerHeader          (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doLedgerRequest         (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doLogLevel              (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doLogRotate             (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doNicknameInfo          (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doOwnerInfo             (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doPathFind              (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doPeers                 (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doPing                  (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doPrint                 (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doProfile               (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doProofCreate           (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doProofSolve            (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doProofVerify           (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doRandom                (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doRipplePathFind        (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doSMS                   (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doServerInfo            (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh); // for humans
    Json::Value doServerState           (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh); // for machines
    Json::Value doSessionClose          (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doSessionOpen           (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doSign                  (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doStop                  (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doSubmit                (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doSubscribe             (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doTransactionEntry      (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doTx                    (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doTxHistory             (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doUnlAdd                (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doUnlDelete             (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doUnlFetch              (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doUnlList               (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doUnlLoad               (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doUnlNetwork            (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doUnlReset              (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doUnlScore              (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doUnsubscribe           (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doValidationCreate      (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doValidationSeed        (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doWalletAccounts        (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doWalletLock            (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doWalletPropose         (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doWalletSeed            (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doWalletUnlock          (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);
    Json::Value doWalletVerify          (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& mlh);

private:
    NetworkOPs*         mNetOps;
    InfoSub::pointer    mInfoSub;

    // VFALCO TODO Create an enumeration for this.
    int                 mRole;
};

class RPCInternalHandler
{
public:
    typedef Json::Value (*handler_t) (const Json::Value&);

public:
    RPCInternalHandler (const std::string& name, handler_t handler);
    static Json::Value runHandler (const std::string& name, const Json::Value& params);

private:
    // VFALCO TODO Replace with a singleton with a well defined interface and
    //             a lock free stack (if necessary).
    //
    static RPCInternalHandler*  sHeadHandler;

    RPCInternalHandler*         mNextHandler;
    std::string                 mName;
    handler_t                   mHandler;
};

} // ripple

#endif
