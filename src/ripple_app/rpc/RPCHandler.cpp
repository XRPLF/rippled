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

#include <ripple_rpc/impl/Handlers.cpp>

namespace ripple {

//
// Carries out the RPC.
//

SETUP_LOG (RPCHandler)

RPCHandler::RPCHandler (NetworkOPs* netOps)
    : mNetOps (netOps)
    , mRole (Config::FORBID)
{
}

RPCHandler::RPCHandler (NetworkOPs* netOps, InfoSub::pointer infoSub)
    : mNetOps (netOps)
    , mInfoSub (infoSub)
    , mRole (Config::FORBID)
{
}

// Provide the JSON-RPC "result" value.
//
// JSON-RPC provides a method and an array of params. JSON-RPC is used as a transport for a command and a request object. The
// command is the method. The request object is supplied as the first element of the params.
Json::Value RPCHandler::doRpcCommand (const std::string& strMethod, Json::Value const& jvParams, int iRole, Resource::Charge& loadType)
{
    WriteLog (lsTRACE, RPCHandler) << "doRpcCommand:" << strMethod << ":" << jvParams;

    if (!jvParams.isArray () || jvParams.size () > 1)
        return logRPCError (rpcError (rpcINVALID_PARAMS));

    Json::Value params   = jvParams.size () ? jvParams[0u] : Json::Value (Json::objectValue);

    if (!params.isObject ())
        return logRPCError (rpcError (rpcINVALID_PARAMS));

    // Provide the JSON-RPC method as the field "command" in the request.
    params[jss::command]    = strMethod;

    Json::Value jvResult = doCommand (params, iRole, loadType);

    // Always report "status".  On an error report the request as received.
    if (jvResult.isMember ("error"))
    {
        jvResult[jss::status]  = jss::error;
        jvResult[jss::request] = params;

    }
    else
    {
        jvResult[jss::status]  = jss::success;
    }

    return logRPCError (jvResult);
}

Json::Value RPCHandler::doInternal (Json::Value params, Resource::Charge& loadType, Application::ScopedLockType& masterLockHolder)
{
    // Used for debug or special-purpose RPC commands
    if (!params.isMember ("internal_command"))
        return rpcError (rpcINVALID_PARAMS);

    return RPCInternalHandler::runHandler (params["internal_command"].asString (), params["params"]);
}

Json::Value RPCHandler::doCommand (const Json::Value& params, int iRole, Resource::Charge& loadType)
{
    if (iRole != Config::ADMIN)
    {
        // VFALCO NOTE Should we also add up the jtRPC jobs?
        //
        int jc = getApp().getJobQueue ().getJobCountGE (jtCLIENT);

        if (jc > 500)
        {
            WriteLog (lsDEBUG, RPCHandler) << "Too busy for command: " << jc;
            return rpcError (rpcTOO_BUSY);
        }
    }

    if (!params.isMember ("command"))
        return rpcError (rpcCOMMAND_MISSING);

    std::string     strCommand  = params[jss::command].asString ();

    WriteLog (lsTRACE, RPCHandler) << "COMMAND:" << strCommand;
    WriteLog (lsTRACE, RPCHandler) << "REQUEST:" << params;

    mRole   = iRole;

    static struct
    {
        const char*     pCommand;
        doFuncPtr       dfpFunc;
        bool            bAdminRequired;
        unsigned int    iOptions;
    } commandsA[] =
    {
        // Request-response methods
        {   "account_info",         &RPCHandler::doAccountInfo,         false,  optCurrent  },
        {   "account_currencies",   &RPCHandler::doAccountCurrencies,   false,  optCurrent  },
        {   "account_lines",        &RPCHandler::doAccountLines,        false,  optCurrent  },
        {   "account_offers",       &RPCHandler::doAccountOffers,       false,  optCurrent  },
        {   "account_tx",           &RPCHandler::doAccountTxSwitch,     false,  optNetwork  },
        {   "blacklist",            &RPCHandler::doBlackList,           true,   optNone     },
        {   "book_offers",          &RPCHandler::doBookOffers,          false,  optCurrent  },
        {   "connect",              &RPCHandler::doConnect,             true,   optNone     },
        {   "consensus_info",       &RPCHandler::doConsensusInfo,       true,   optNone     },
        {   "get_counts",           &RPCHandler::doGetCounts,           true,   optNone     },
        {   "internal",             &RPCHandler::doInternal,            true,   optNone     },
        {   "feature",              &RPCHandler::doFeature,             true,   optNone     },
        {   "fetch_info",           &RPCHandler::doFetchInfo,           true,   optNone     },
        {   "ledger",               &RPCHandler::doLedger,              false,  optNetwork  },
        {   "ledger_accept",        &RPCHandler::doLedgerAccept,        true,   optCurrent  },
        {   "ledger_cleaner",       &RPCHandler::doLedgerCleaner,       true,   optNetwork  },
        {   "ledger_closed",        &RPCHandler::doLedgerClosed,        false,  optClosed   },
        {   "ledger_current",       &RPCHandler::doLedgerCurrent,       false,  optCurrent  },
        {   "ledger_data",          &RPCHandler::doLedgerData,          false,  optCurrent  },
        {   "ledger_entry",         &RPCHandler::doLedgerEntry,         false,  optCurrent  },
        {   "ledger_header",        &RPCHandler::doLedgerHeader,        false,  optCurrent  },
        {   "log_level",            &RPCHandler::doLogLevel,            true,   optNone     },
        {   "logrotate",            &RPCHandler::doLogRotate,           true,   optNone     },
//      {   "nickname_info",        &RPCHandler::doNicknameInfo,        false,  optCurrent  },
        {   "owner_info",           &RPCHandler::doOwnerInfo,           false,  optCurrent  },
        {   "peers",                &RPCHandler::doPeers,               true,   optNone     },
        {   "path_find",            &RPCHandler::doPathFind,            false,  optCurrent  },
        {   "ping",                 &RPCHandler::doPing,                false,  optNone     },
        {   "print",                &RPCHandler::doPrint,               true,   optNone     },
//      {   "profile",              &RPCHandler::doProfile,             false,  optCurrent  },
        {   "proof_create",         &RPCHandler::doProofCreate,         true,   optNone     },
        {   "proof_solve",          &RPCHandler::doProofSolve,          true,   optNone     },
        {   "proof_verify",         &RPCHandler::doProofVerify,         true,   optNone     },
        {   "random",               &RPCHandler::doRandom,              false,  optNone     },
        {   "ripple_path_find",     &RPCHandler::doRipplePathFind,      false,  optCurrent  },
        {   "sign",                 &RPCHandler::doSign,                false,  optNone     },
        {   "submit",               &RPCHandler::doSubmit,              false,  optCurrent  },
        {   "server_info",          &RPCHandler::doServerInfo,          false,  optNone     },
        {   "server_state",         &RPCHandler::doServerState,         false,  optNone     },
        {   "sms",                  &RPCHandler::doSMS,                 true,   optNone     },
        {   "stop",                 &RPCHandler::doStop,                true,   optNone     },
        {   "transaction_entry",    &RPCHandler::doTransactionEntry,    false,  optCurrent  },
        {   "tx",                   &RPCHandler::doTx,                  false,  optNetwork  },
        {   "tx_history",           &RPCHandler::doTxHistory,           false,  optNone     },
        {   "unl_add",              &RPCHandler::doUnlAdd,              true,   optNone     },
        {   "unl_delete",           &RPCHandler::doUnlDelete,           true,   optNone     },
        {   "unl_list",             &RPCHandler::doUnlList,             true,   optNone     },
        {   "unl_load",             &RPCHandler::doUnlLoad,             true,   optNone     },
        {   "unl_network",          &RPCHandler::doUnlNetwork,          true,   optNone     },
        {   "unl_reset",            &RPCHandler::doUnlReset,            true,   optNone     },
        {   "unl_score",            &RPCHandler::doUnlScore,            true,   optNone     },
        {   "validation_create",    &RPCHandler::doValidationCreate,    true,   optNone     },
        {   "validation_seed",      &RPCHandler::doValidationSeed,      true,   optNone     },
        {   "wallet_accounts",      &RPCHandler::doWalletAccounts,      false,  optCurrent  },
        {   "wallet_propose",       &RPCHandler::doWalletPropose,       true,   optNone     },
        {   "wallet_seed",          &RPCHandler::doWalletSeed,          true,   optNone     },

        // Evented methods
        {   "subscribe",            &RPCHandler::doSubscribe,           false,  optNone     },
        {   "unsubscribe",          &RPCHandler::doUnsubscribe,         false,  optNone     },
    };

    int i = RIPPLE_ARRAYSIZE (commandsA);

    while (i-- && strCommand != commandsA[i].pCommand)
        ;

    if (i < 0)
    {
        return rpcError (rpcUNKNOWN_COMMAND);
    }
    else if (commandsA[i].bAdminRequired && mRole != Config::ADMIN)
    {
        return rpcError (rpcNO_PERMISSION);
    }

    {
        Application::ScopedLockType lock (getApp().getMasterLock ());

        if ((commandsA[i].iOptions & optNetwork) && (mNetOps->getOperatingMode () < NetworkOPs::omSYNCING))
        {
            WriteLog (lsINFO, RPCHandler) << "Insufficient network mode for RPC: " << mNetOps->strOperatingMode ();

            return rpcError (rpcNO_NETWORK);
        }

        if (!getConfig ().RUN_STANDALONE && (commandsA[i].iOptions & optCurrent) && (getApp().getLedgerMaster().getValidatedLedgerAge() > 120))
        {
            return rpcError (rpcNO_CURRENT);
        }
        else if ((commandsA[i].iOptions & optClosed) && !mNetOps->getClosedLedger ())
        {
            return rpcError (rpcNO_CLOSED);
        }
        else
        {
            try
            {
                LoadEvent::autoptr ev   = getApp().getJobQueue().getLoadEventAP(
                    jtGENERIC, std::string("cmd:") + strCommand);
                Json::Value jvRaw       = (this->* (commandsA[i].dfpFunc)) (params, loadType, lock);

                // Regularize result.
                if (jvRaw.isObject ())
                {
                    // Got an object.
                    return jvRaw;
                }
                else
                {
                    // Probably got a string.
                    Json::Value jvResult (Json::objectValue);

                    jvResult[jss::message] = jvRaw;

                    return jvResult;
                }
            }
            catch (std::exception& e)
            {
                WriteLog (lsINFO, RPCHandler) << "Caught throw: " << e.what ();

                if (loadType == Resource::feeReferenceRPC)
                    loadType = Resource::feeExceptionRPC;

                return rpcError (rpcINTERNAL);
            }
        }
    }
}

RPCInternalHandler* RPCInternalHandler::sHeadHandler = nullptr;

RPCInternalHandler::RPCInternalHandler (const std::string& name, handler_t Handler) : mName (name), mHandler (Handler)
{
    mNextHandler = sHeadHandler;
    sHeadHandler = this;
}

Json::Value RPCInternalHandler::runHandler (const std::string& name, const Json::Value& params)
{
    RPCInternalHandler* h = sHeadHandler;

    while (h != nullptr)
    {
        if (name == h->mName)
        {
            WriteLog (lsWARNING, RPCHandler) << "Internal command " << name << ": " << params;
            Json::Value ret = h->mHandler (params);
            WriteLog (lsWARNING, RPCHandler) << "Internal command returns: " << ret;
            return ret;
        }

        h = h->mNextHandler;
    }

    return rpcError (rpcBAD_SYNTAX);
}

} // ripple
