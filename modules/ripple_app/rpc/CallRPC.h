//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef __CALLRPC__
#define __CALLRPC__

class RPCParser
{
public:
    Json::Value parseCommand (std::string strMethod, Json::Value jvParams);

private:
    typedef Json::Value (RPCParser::*parseFuncPtr) (const Json::Value& jvParams);

    Json::Value parseAccountRaw (const Json::Value& jvParams, bool bPeer);

    Json::Value parseAccountItems (const Json::Value& jvParams);
    Json::Value parseAccountLines (const Json::Value& jvParams);
    Json::Value parseAccountTransactions (const Json::Value& jvParams);
    Json::Value parseAsIs (const Json::Value& jvParams);
    Json::Value parseBookOffers (const Json::Value& jvParams);
    Json::Value parseConnect (const Json::Value& jvParams);
#if ENABLE_INSECURE
    Json::Value parseDataDelete (const Json::Value& jvParams);
    Json::Value parseDataFetch (const Json::Value& jvParams);
    Json::Value parseDataStore (const Json::Value& jvParams);
#endif
    Json::Value parseEvented (const Json::Value& jvParams);
    Json::Value parseFeature (const Json::Value& jvParams);
    Json::Value parseFetchInfo (const Json::Value& jvParams);
    Json::Value parseGetCounts (const Json::Value& jvParams);
    Json::Value parseInternal (const Json::Value& jvParams);
    Json::Value parseJson (const Json::Value& jvParams);
    Json::Value parseLedger (const Json::Value& jvParams);
    Json::Value parseLedgerId (const Json::Value& jvParams);
#if ENABLE_INSECURE
    Json::Value parseLogin (const Json::Value& jvParams);
#endif
    Json::Value parseLogLevel (const Json::Value& jvParams);
    Json::Value parseOwnerInfo (const Json::Value& jvParams);
    Json::Value parseProofCreate (const Json::Value& jvParams);
    Json::Value parseProofSolve (const Json::Value& jvParams);
    Json::Value parseProofVerify (const Json::Value& jvParams);
    Json::Value parseRandom (const Json::Value& jvParams);
    Json::Value parseRipplePathFind (const Json::Value& jvParams);
    Json::Value parseSMS (const Json::Value& jvParams);
    Json::Value parseSignSubmit (const Json::Value& jvParams);
    Json::Value parseTx (const Json::Value& jvParams);
    Json::Value parseTxHistory (const Json::Value& jvParams);
    Json::Value parseTxAccount (const Json::Value& jvParams);
    Json::Value parseUnlAdd (const Json::Value& jvParams);
    Json::Value parseUnlDelete (const Json::Value& jvParams);
    Json::Value parseValidationCreate (const Json::Value& jvParams);
    Json::Value parseValidationSeed (const Json::Value& jvParams);
    Json::Value parseWalletAccounts (const Json::Value& jvParams);
    Json::Value parseWalletPropose (const Json::Value& jvParams);
    Json::Value parseWalletSeed (const Json::Value& jvParams);
};

extern int commandLineRPC (const std::vector<std::string>& vCmd);

extern void callRPC (
    boost::asio::io_service& io_service,
    const std::string& strIp, const int iPort,
    const std::string& strUsername, const std::string& strPassword,
    const std::string& strPath, const std::string& strMethod,
    const Json::Value& jvParams, const bool bSSL,
    FUNCTION_TYPE<void (const Json::Value& jvInput)> callbackFuncP = FUNCTION_TYPE<void (const Json::Value& jvInput)> ());
#endif

// vim:ts=4
