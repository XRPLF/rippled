//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef __RPCHANDLER__
#define __RPCHANDLER__

#define LEDGER_CURRENT      -1
#define LEDGER_CLOSED       -2
#define LEDGER_VALIDATED    -3

// used by the RPCServer or WSDoor to carry out these RPC commands
class NetworkOPs;
class InfoSub;

// VFALCO TODO Refactor to abstract interface IRPCHandler
//
class RPCHandler
{
public:
    enum
    {
        GUEST,
        USER,
        ADMIN,
        FORBID
    };

    explicit RPCHandler (NetworkOPs* netOps);

    RPCHandler (NetworkOPs* netOps, InfoSub::pointer infoSub);

    Json::Value doCommand       (const Json::Value& jvRequest, int role, LoadType* loadType);

    Json::Value doRpcCommand    (const std::string& strCommand, Json::Value const& jvParams, int iRole, LoadType* loadType);

private:
    typedef Json::Value (RPCHandler::*doFuncPtr) (
        Json::Value params,
        LoadType* loadType,
        ScopedLock& MasterLockHolder);

    // VFALCO TODO Document these and give the enumeration a label.
    enum
    {
        optNone     = 0,
        optNetwork  = 1,                // Need network
        optCurrent  = 2 + optNetwork,   // Need current ledger
        optClosed   = 4 + optNetwork,   // Need closed ledger
    };

    // Utilities

    void addSubmitPath (Json::Value& txJSON);

    boost::unordered_set <RippleAddress> parseAccountIds (const Json::Value& jvArray);

    Json::Value transactionSign (Json::Value jvRequest, bool bSubmit, bool bFailHard, ScopedLock& mlh);

    Json::Value lookupLedger (Json::Value jvRequest, Ledger::pointer& lpLedger);

    Json::Value getMasterGenerator (
        Ledger::ref lrLedger,
        const RippleAddress& naRegularSeed,
        RippleAddress& naMasterGenerator);

    Json::Value authorize (
        Ledger::ref lrLedger,
        const RippleAddress& naRegularSeed,
        const RippleAddress& naSrcAccountID,
        RippleAddress& naAccountPublic,
        RippleAddress& naAccountPrivate,
        STAmount& saSrcBalance,
        const STAmount& saFee,
        AccountState::pointer& asSrc,
        const RippleAddress& naVerifyGenerator);

    Json::Value accounts (
        Ledger::ref lrLedger,
        const RippleAddress& naMasterGenerator);

    Json::Value accountFromString (
        Ledger::ref lrLedger,
        RippleAddress& naAccount,
        bool& bIndex,
        const std::string& strIdent,
        const int iIndex,
        const bool bStrict);

    Json::Value doAccountInfo           (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doAccountLines          (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doAccountOffers         (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doAccountTransactions   (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doBookOffers            (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doConnect               (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doConsensusInfo         (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doFeature               (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doFetchInfo             (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doGetCounts             (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doInternal              (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doLedger                (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doLedgerAccept          (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doLedgerClosed          (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doLedgerCurrent         (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doLedgerEntry           (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doLedgerHeader          (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doLogLevel              (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doLogRotate             (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doNicknameInfo          (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doOwnerInfo             (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doPathFind              (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doPeers                 (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doPing                  (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doProfile               (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doProofCreate           (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doProofSolve            (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doProofVerify           (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doRandom                (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doRipplePathFind        (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doSMS                   (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doServerInfo            (Json::Value params, LoadType* loadType, ScopedLock& mlh); // for humans
    Json::Value doServerState           (Json::Value params, LoadType* loadType, ScopedLock& mlh); // for machines
    Json::Value doSessionClose          (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doSessionOpen           (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doSign                  (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doStop                  (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doSubmit                (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doSubscribe             (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doTransactionEntry      (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doTx                    (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doTxHistory             (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doUnlAdd                (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doUnlDelete             (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doUnlFetch              (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doUnlList               (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doUnlLoad               (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doUnlNetwork            (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doUnlReset              (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doUnlScore              (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doUnsubscribe           (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doValidationCreate      (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doValidationSeed        (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doWalletAccounts        (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doWalletLock            (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doWalletPropose         (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doWalletSeed            (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doWalletUnlock          (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doWalletVerify          (Json::Value params, LoadType* loadType, ScopedLock& mlh);

#if ENABLE_INSECURE
    Json::Value doDataDelete            (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doDataFetch             (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doDataStore             (Json::Value params, LoadType* loadType, ScopedLock& mlh);
    Json::Value doLogin                 (Json::Value params, LoadType* loadType, ScopedLock& mlh);
#endif

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

// VFALCO TODO tidy up this loose function
int iAdminGet (const Json::Value& jvRequest, const std::string& strRemoteIp);

#endif
// vim:ts=4
