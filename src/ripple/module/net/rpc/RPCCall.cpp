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

#include <boost/regex.hpp>

namespace ripple {

class RPCParser;

SETUP_LOG (RPCParser)

static inline bool isSwitchChar (char c)
{
#ifdef __WXMSW__
    return c == '-' || c == '/';
#else
    return c == '-';
#endif
}

class RPCParser
{
private:
    // TODO New routine for parsing ledger parameters, other routines should standardize on this.
    static bool jvParseLedger (Json::Value& jvRequest, const std::string& strLedger)
    {
        if (strLedger == "current" || strLedger == "closed" || strLedger == "validated")
        {
            jvRequest["ledger_index"]   = strLedger;
        }
        else if (strLedger.length () == 64)
        {
            // YYY Could confirm this is a uint256.
            jvRequest["ledger_hash"]    = strLedger;
        }
        else
        {
            jvRequest["ledger_index"]   = beast::lexicalCast <std::uint32_t> (strLedger);
        }

        return true;
    }

    // Build a object { "currency" : "XYZ", "issuer" : "rXYX" }
    static Json::Value jvParseCurrencyIssuer (const std::string& strCurrencyIssuer)
    {
        static boost::regex reCurIss ("\\`([[:alpha:]]{3})(?:/(.+))?\\'");

        boost::smatch   smMatch;

        if (boost::regex_match (strCurrencyIssuer, smMatch, reCurIss))
        {
            Json::Value jvResult (Json::objectValue);
            std::string strCurrency = smMatch[1];
            std::string strIssuer   = smMatch[2];

            jvResult["currency"]    = strCurrency;

            if (strIssuer.length ())
            {
                // Could confirm issuer is a valid Ripple address.
                jvResult["issuer"]      = strIssuer;
            }

            return jvResult;
        }
        else
        {
            return RPC::make_param_error (std::string ("Invalid currency/issuer '") +
                    strCurrencyIssuer + "'");
        }
    }

private:
    typedef Json::Value (RPCParser::*parseFuncPtr) (const Json::Value& jvParams);

    Json::Value parseAsIs (const Json::Value& jvParams)
    {
        Json::Value v (Json::objectValue);

        if (jvParams.isArray () && (jvParams.size () > 0))
            v["params"] = jvParams;

        return v;
    }

    Json::Value parseInternal (const Json::Value& jvParams)
    {
        Json::Value v (Json::objectValue);
        v["internal_command"] = jvParams[0u];

        Json::Value params (Json::arrayValue);

        for (unsigned i = 1; i < jvParams.size (); ++i)
            params.append (jvParams[i]);

        v["params"] = params;

        return v;
    }

    // fetch_info [clear]
    Json::Value parseFetchInfo (const Json::Value& jvParams)
    {
        Json::Value     jvRequest (Json::objectValue);
        unsigned int    iParams = jvParams.size ();

        if (iParams != 0)
            jvRequest[jvParams[0u].asString()] = true;

        return jvRequest;
    }

    // account_tx accountID [ledger_min [ledger_max [limit [offset]]]] [binary] [count] [descending]
    Json::Value parseAccountTransactions (const Json::Value& jvParams)
    {
        Json::Value     jvRequest (Json::objectValue);
        RippleAddress   raAccount;
        unsigned int    iParams = jvParams.size ();

        if (!raAccount.setAccountID (jvParams[0u].asString ()))
            return rpcError (rpcACT_MALFORMED);

        jvRequest["account"]    = raAccount.humanAccountID ();

        bool            bDone   = false;

        while (!bDone && iParams >= 2)
        {
            if (jvParams[iParams - 1].asString () == "binary")
            {
                jvRequest["binary"]     = true;
                --iParams;
            }
            else if (jvParams[iParams - 1].asString () == "count")
            {
                jvRequest["count"]      = true;
                --iParams;
            }
            else if (jvParams[iParams - 1].asString () == "descending")
            {
                jvRequest["descending"] = true;
                --iParams;
            }
            else
            {
                bDone   = true;
            }
        }

        if (1 == iParams)
        {
        }
        else if (2 == iParams)
        {
            if (!jvParseLedger (jvRequest, jvParams[1u].asString ()))
                return jvRequest;
        }
        else
        {
            std::int64_t   uLedgerMin  = jvParams[1u].asInt ();
            std::int64_t   uLedgerMax  = jvParams[2u].asInt ();

            if (uLedgerMax != -1 && uLedgerMax < uLedgerMin)
            {
                return rpcError (rpcLGR_IDXS_INVALID);
            }

            jvRequest["ledger_index_min"]   = jvParams[1u].asInt ();
            jvRequest["ledger_index_max"]   = jvParams[2u].asInt ();

            if (iParams >= 4)
                jvRequest["limit"]  = jvParams[3u].asInt ();

            if (iParams >= 5)
                jvRequest["offset"] = jvParams[4u].asInt ();
        }

        return jvRequest;
    }

    // tx_account accountID [ledger_min [ledger_max [limit]]]] [binary] [count] [forward]
    Json::Value parseTxAccount (const Json::Value& jvParams)
    {
        Json::Value     jvRequest (Json::objectValue);
        RippleAddress   raAccount;
        unsigned int    iParams = jvParams.size ();

        if (!raAccount.setAccountID (jvParams[0u].asString ()))
            return rpcError (rpcACT_MALFORMED);

        jvRequest["account"]    = raAccount.humanAccountID ();

        bool            bDone   = false;

        while (!bDone && iParams >= 2)
        {
            if (jvParams[iParams - 1].asString () == "binary")
            {
                jvRequest["binary"]     = true;
                --iParams;
            }
            else if (jvParams[iParams - 1].asString () == "count")
            {
                jvRequest["count"]      = true;
                --iParams;
            }
            else if (jvParams[iParams - 1].asString () == "forward")
            {
                jvRequest["forward"] = true;
                --iParams;
            }
            else
            {
                bDone   = true;
            }
        }

        if (1 == iParams)
        {
        }
        else if (2 == iParams)
        {
            if (!jvParseLedger (jvRequest, jvParams[1u].asString ()))
                return jvRequest;
        }
        else
        {
            std::int64_t   uLedgerMin  = jvParams[1u].asInt ();
            std::int64_t   uLedgerMax  = jvParams[2u].asInt ();

            if (uLedgerMax != -1 && uLedgerMax < uLedgerMin)
            {
                return rpcError (rpcLGR_IDXS_INVALID);
            }

            jvRequest["ledger_index_min"]   = jvParams[1u].asInt ();
            jvRequest["ledger_index_max"]   = jvParams[2u].asInt ();

            if (iParams >= 4)
                jvRequest["limit"]  = jvParams[3u].asInt ();
        }

        return jvRequest;
    }

    // book_offers <taker_pays> <taker_gets> [<taker> [<ledger> [<limit> [<proof> [<marker>]]]]]
    // limit: 0 = no limit
    // proof: 0 or 1
    //
    // Mnemonic: taker pays --> offer --> taker gets
    Json::Value parseBookOffers (const Json::Value& jvParams)
    {
        Json::Value     jvRequest (Json::objectValue);

        Json::Value     jvTakerPays = jvParseCurrencyIssuer (jvParams[0u].asString ());
        Json::Value     jvTakerGets = jvParseCurrencyIssuer (jvParams[1u].asString ());

        if (isRpcError (jvTakerPays))
        {
            return jvTakerPays;
        }
        else
        {
            jvRequest["taker_pays"] = jvTakerPays;
        }

        if (isRpcError (jvTakerGets))
        {
            return jvTakerGets;
        }
        else
        {
            jvRequest["taker_gets"] = jvTakerGets;
        }

        if (jvParams.size () >= 3)
        {
            jvRequest["issuer"] = jvParams[2u].asString ();
        }

        if (jvParams.size () >= 4 && !jvParseLedger (jvRequest, jvParams[3u].asString ()))
            return jvRequest;

        if (jvParams.size () >= 5)
        {
            int     iLimit  = jvParams[5u].asInt ();

            if (iLimit > 0)
                jvRequest["limit"]  = iLimit;
        }

        if (jvParams.size () >= 6 && jvParams[5u].asInt ())
        {
            jvRequest["proof"]  = true;
        }

        if (jvParams.size () == 7)
            jvRequest["marker"] = jvParams[6u];

        return jvRequest;
    }

    // connect <ip> [port]
    Json::Value parseConnect (const Json::Value& jvParams)
    {
        Json::Value     jvRequest (Json::objectValue);

        jvRequest["ip"] = jvParams[0u].asString ();

        if (jvParams.size () == 2)
            jvRequest["port"]   = jvParams[1u].asUInt ();

        return jvRequest;
    }

    // Return an error for attemping to subscribe/unsubscribe via RPC.
    Json::Value parseEvented (const Json::Value& jvParams)
    {
        return rpcError (rpcNO_EVENTS);
    }

    // feature [<feature>] [true|false]
    Json::Value parseFeature (const Json::Value& jvParams)
    {
        Json::Value     jvRequest (Json::objectValue);

        if (jvParams.size () > 0)
            jvRequest["feature"]    = jvParams[0u].asString ();

        if (jvParams.size () > 1)
            jvRequest["vote"]       = beast::lexicalCastThrow <bool> (jvParams[1u].asString ());

        return jvRequest;
    }

    // get_counts [<min_count>]
    Json::Value parseGetCounts (const Json::Value& jvParams)
    {
        Json::Value     jvRequest (Json::objectValue);

        if (jvParams.size ())
            jvRequest["min_count"]  = jvParams[0u].asUInt ();

        return jvRequest;
    }

    // json <command> <json>
    Json::Value parseJson (const Json::Value& jvParams)
    {
        Json::Reader    reader;
        Json::Value     jvRequest;

        WriteLog (lsTRACE, RPCParser) << "RPC method: " << jvParams[0u];
        WriteLog (lsTRACE, RPCParser) << "RPC json: " << jvParams[1u];

        if (reader.parse (jvParams[1u].asString (), jvRequest))
        {
            if (!jvRequest.isObject ())
                return rpcError (rpcINVALID_PARAMS);

            jvRequest["method"] = jvParams[0u];

            return jvRequest;
        }

        return rpcError (rpcINVALID_PARAMS);
    }

    // ledger [id|index|current|closed|validated] [full]
    Json::Value parseLedger (const Json::Value& jvParams)
    {
        Json::Value     jvRequest (Json::objectValue);

        if (!jvParams.size ())
        {
            return jvRequest;
        }

        jvParseLedger (jvRequest, jvParams[0u].asString ());

        if (2 == jvParams.size () && jvParams[1u].asString () == "full")
        {
            jvRequest["full"]   = bool (1);
        }

        return jvRequest;
    }

    // ledger_header <id>|<index>
    Json::Value parseLedgerId (const Json::Value& jvParams)
    {
        Json::Value     jvRequest (Json::objectValue);

        std::string     strLedger   = jvParams[0u].asString ();

        if (strLedger.length () == 32)
        {
            jvRequest["ledger_hash"]    = strLedger;
        }
        else
        {
            jvRequest["ledger_index"]   = beast::lexicalCast <std::uint32_t> (strLedger);
        }

        return jvRequest;
    }

    // log_level:                           Get log levels
    // log_level <severity>:                Set master log level to the specified severity
    // log_level <partition> <severity>:    Set specified partition to specified severity
    Json::Value parseLogLevel (const Json::Value& jvParams)
    {
        Json::Value     jvRequest (Json::objectValue);

        if (jvParams.size () == 1)
        {
            jvRequest["severity"] = jvParams[0u].asString ();
        }
        else if (jvParams.size () == 2)
        {
            jvRequest["partition"] = jvParams[0u].asString ();
            jvRequest["severity"] = jvParams[1u].asString ();
        }

        return jvRequest;
    }

    // owner_info <account>|<nickname>|<account_public_key>
    // owner_info <seed>|<pass_phrase>|<key> [<ledfer>]
    // account_info <account>|<nickname>|<account_public_key>
    // account_info <seed>|<pass_phrase>|<key> [<ledger>]
    // account_offers <account>|<nickname>|<account_public_key> [<ledger>]
    Json::Value parseAccountItems (const Json::Value& jvParams)
    {
        return parseAccountRaw (jvParams, false);
    }

    Json::Value parseAccountCurrencies (const Json::Value& jvParams)
    {
        return parseAccountRaw (jvParams, false);
    }

    // account_lines <account> <account>|"" [<ledger>]
    Json::Value parseAccountLines (const Json::Value& jvParams)
    {
        return parseAccountRaw (jvParams, true);
    }

    // TODO: Get index from an alternate syntax: rXYZ:<index>
    Json::Value parseAccountRaw (const Json::Value& jvParams, bool bPeer)
    {
        std::string     strIdent    = jvParams[0u].asString ();
        unsigned int    iCursor     = jvParams.size ();
        bool            bStrict     = false;
        std::string     strPeer;

        if (!bPeer && iCursor >= 2 && jvParams[iCursor - 1] == "strict")
        {
            bStrict = true;
            --iCursor;
        }

        if (bPeer && iCursor >= 2)
            strPeer = jvParams[iCursor].asString ();

        int             iIndex      = 0;
        //  int             iIndex      = jvParams.size() >= 2 ? beast::lexicalCast <int>(jvParams[1u].asString()) : 0;

        RippleAddress   raAddress;

        if (!raAddress.setAccountPublic (strIdent) && !raAddress.setAccountID (strIdent) && !raAddress.setSeedGeneric (strIdent))
            return rpcError (rpcACT_MALFORMED);

        // Get info on account.
        Json::Value jvRequest (Json::objectValue);

        jvRequest["account"]    = strIdent;

        if (bStrict)
            jvRequest["strict"]     = 1;

        if (iIndex)
            jvRequest["account_index"]  = iIndex;

        if (!strPeer.empty ())
        {
            RippleAddress   raPeer;

            if (!raPeer.setAccountPublic (strPeer) && !raPeer.setAccountID (strPeer) && !raPeer.setSeedGeneric (strPeer))
                return rpcError (rpcACT_MALFORMED);

            jvRequest["peer"]   = strPeer;
        }

        if (iCursor == (2 + bPeer) && !jvParseLedger (jvRequest, jvParams[1u + bPeer].asString ()))
            return rpcError (rpcLGR_IDX_MALFORMED);

        return jvRequest;
    }

    // proof_create [<difficulty>] [<secret>]
    Json::Value parseProofCreate (const Json::Value& jvParams)
    {
        Json::Value     jvRequest;

        if (jvParams.size () >= 1)
            jvRequest["difficulty"] = jvParams[0u].asInt ();

        if (jvParams.size () >= 2)
            jvRequest["secret"] = jvParams[1u].asString ();

        return jvRequest;
    }

    // proof_solve <token>
    Json::Value parseProofSolve (const Json::Value& jvParams)
    {
        Json::Value     jvRequest;

        jvRequest["token"] = jvParams[0u].asString ();

        return jvRequest;
    }

    // proof_verify <token> <solution> [<difficulty>] [<secret>]
    Json::Value parseProofVerify (const Json::Value& jvParams)
    {
        Json::Value     jvRequest;

        jvRequest["token"] = jvParams[0u].asString ();
        jvRequest["solution"] = jvParams[1u].asString ();

        if (jvParams.size () >= 3)
            jvRequest["difficulty"] = jvParams[2u].asInt ();

        if (jvParams.size () >= 4)
            jvRequest["secret"] = jvParams[3u].asString ();

        return jvRequest;
    }

    // ripple_path_find <json> [<ledger>]
    Json::Value parseRipplePathFind (const Json::Value& jvParams)
    {
        Json::Reader    reader;
        Json::Value     jvRequest;
        bool            bLedger     = 2 == jvParams.size ();

        WriteLog (lsTRACE, RPCParser) << "RPC json: " << jvParams[0u];

        if (reader.parse (jvParams[0u].asString (), jvRequest))
        {
            if (bLedger)
            {
                jvParseLedger (jvRequest, jvParams[1u].asString ());
            }

            return jvRequest;
        }

        return rpcError (rpcINVALID_PARAMS);
    }

    // sign/submit any transaction to the network
    //
    // sign <private_key> <json> offline
    // submit <private_key> <json>
    // submit <tx_blob>
    Json::Value parseSignSubmit (const Json::Value& jvParams)
    {
        Json::Value     txJSON;
        Json::Reader    reader;
        bool            bOffline    = 3 == jvParams.size () && jvParams[2u].asString () == "offline";

        if (1 == jvParams.size ())
        {
            // Submitting tx_blob

            Json::Value jvRequest;

            jvRequest["tx_blob"]    = jvParams[0u].asString ();

            return jvRequest;
        }
        else if ((2 == jvParams.size () || bOffline)
                 && reader.parse (jvParams[1u].asString (), txJSON))
        {
            // Signing or submitting tx_json.
            Json::Value jvRequest;

            jvRequest["secret"]     = jvParams[0u].asString ();
            jvRequest["tx_json"]    = txJSON;

            if (bOffline)
                jvRequest["offline"]    = true;

            return jvRequest;
        }

        return rpcError (rpcINVALID_PARAMS);
    }

    // sms <text>
    Json::Value parseSMS (const Json::Value& jvParams)
    {
        Json::Value     jvRequest;

        jvRequest["text"]   = jvParams[0u].asString ();

        return jvRequest;
    }

    // tx <transaction_id>
    Json::Value parseTx (const Json::Value& jvParams)
    {
        Json::Value jvRequest;

        if (jvParams.size () > 1)
        {
            if (jvParams[1u].asString () == "binary")
                jvRequest["binary"] = true;
        }

        jvRequest["transaction"]    = jvParams[0u].asString ();
        return jvRequest;
    }

    // tx_history <index>
    Json::Value parseTxHistory (const Json::Value& jvParams)
    {
        Json::Value jvRequest;

        jvRequest["start"]  = jvParams[0u].asUInt ();

        return jvRequest;
    }

    // unl_add <domain>|<node_public> [<comment>]
    Json::Value parseUnlAdd (const Json::Value& jvParams)
    {
        std::string strNode     = jvParams[0u].asString ();
        std::string strComment  = (jvParams.size () == 2) ? jvParams[1u].asString () : "";

        RippleAddress   naNodePublic;

        if (strNode.length ())
        {
            Json::Value jvRequest;

            jvRequest["node"]       = strNode;

            if (strComment.length ())
                jvRequest["comment"]    = strComment;

            return jvRequest;
        }

        return rpcError (rpcINVALID_PARAMS);
    }

    // unl_delete <domain>|<public_key>
    Json::Value parseUnlDelete (const Json::Value& jvParams)
    {
        Json::Value jvRequest;

        jvRequest["node"]       = jvParams[0u].asString ();

        return jvRequest;
    }

    // validation_create [<pass_phrase>|<seed>|<seed_key>]
    //
    // NOTE: It is poor security to specify secret information on the command line.  This information might be saved in the command
    // shell history file (e.g. .bash_history) and it may be leaked via the process status command (i.e. ps).
    Json::Value parseValidationCreate (const Json::Value& jvParams)
    {
        Json::Value jvRequest;

        if (jvParams.size ())
            jvRequest["secret"]     = jvParams[0u].asString ();

        return jvRequest;
    }

    // validation_seed [<pass_phrase>|<seed>|<seed_key>]
    //
    // NOTE: It is poor security to specify secret information on the command line.  This information might be saved in the command
    // shell history file (e.g. .bash_history) and it may be leaked via the process status command (i.e. ps).
    Json::Value parseValidationSeed (const Json::Value& jvParams)
    {
        Json::Value jvRequest;

        if (jvParams.size ())
            jvRequest["secret"]     = jvParams[0u].asString ();

        return jvRequest;
    }

    // wallet_accounts <seed>
    Json::Value parseWalletAccounts (const Json::Value& jvParams)
    {
        Json::Value jvRequest;

        jvRequest["seed"]       = jvParams[0u].asString ();

        return jvRequest;
    }

    // wallet_propose [<passphrase>]
    // <passphrase> is only for testing. Master seeds should only be generated randomly.
    Json::Value parseWalletPropose (const Json::Value& jvParams)
    {
        Json::Value jvRequest;

        if (jvParams.size ())
            jvRequest["passphrase"]     = jvParams[0u].asString ();

        return jvRequest;
    }

    // wallet_seed [<seed>|<passphrase>|<passkey>]
    Json::Value parseWalletSeed (const Json::Value& jvParams)
    {
        Json::Value jvRequest;

        if (jvParams.size ())
            jvRequest["secret"]     = jvParams[0u].asString ();

        return jvRequest;
    }

public:
    //--------------------------------------------------------------------------

    static std::string EncodeBase64 (const std::string& s)
    {
        // FIXME: This performs terribly
        BIO* b64, *bmem;
        BUF_MEM* bptr;

        // VFALCO TODO What the heck is BIO and BUF_MEM!?
        //             Surely we don't need OpenSSL or dynamic allocations
        //             to perform a base64 encoding...
        //
        b64 = BIO_new (BIO_f_base64 ());
        BIO_set_flags (b64, BIO_FLAGS_BASE64_NO_NL);
        bmem = BIO_new (BIO_s_mem ());
        b64 = BIO_push (b64, bmem);
        BIO_write (b64, s.data (), s.size ());
        (void) BIO_flush (b64);
        BIO_get_mem_ptr (b64, &bptr);

        std::string result (bptr->data, bptr->length);
        BIO_free_all (b64);

        return result;
    }

    //--------------------------------------------------------------------------

    // Convert a rpc method and params to a request.
    // <-- { method: xyz, params: [... ] } or { error: ..., ... }
    Json::Value parseCommand (std::string strMethod, Json::Value jvParams, bool allowAnyCommand)
    {
        WriteLog (lsTRACE, RPCParser) << "RPC method:" << strMethod;
        WriteLog (lsTRACE, RPCParser) << "RPC params:" << jvParams;

        struct Command
        {
            const char*     pCommand;
            parseFuncPtr    pfpFunc;
            int             iMinParams;
            int             iMaxParams;
        };
        static Command commandsA[] =
        {
            // Request-response methods
            // - Returns an error, or the request.
            // - To modify the method, provide a new method in the request.
            {   "account_currencies",   &RPCParser::parseAccountCurrencies,     1,  2   },
            {   "account_info",         &RPCParser::parseAccountItems,          1,  2   },
            {   "account_lines",        &RPCParser::parseAccountLines,          1,  3   },
            {   "account_offers",       &RPCParser::parseAccountItems,          1,  2   },
            {   "account_tx",           &RPCParser::parseAccountTransactions,   1,  8   },
            {   "book_offers",          &RPCParser::parseBookOffers,            2,  7   },
            {   "connect",              &RPCParser::parseConnect,               1,  2   },
            {   "consensus_info",       &RPCParser::parseAsIs,                  0,  0   },
            {   "feature",              &RPCParser::parseFeature,               0,  2   },
            {   "fetch_info",           &RPCParser::parseFetchInfo,             0,  1   },
            {   "get_counts",           &RPCParser::parseGetCounts,             0,  1   },
            {   "json",                 &RPCParser::parseJson,                  2,  2   },
            {   "ledger",               &RPCParser::parseLedger,                0,  2   },
            {   "ledger_accept",        &RPCParser::parseAsIs,                  0,  0   },
            {   "ledger_closed",        &RPCParser::parseAsIs,                  0,  0   },
            {   "ledger_current",       &RPCParser::parseAsIs,                  0,  0   },
    //      {   "ledger_entry",         &RPCParser::parseLedgerEntry,          -1, -1   },
            {   "ledger_header",        &RPCParser::parseLedgerId,              1,  1   },
            {   "ledger_request",       &RPCParser::parseLedgerId,              1,  1   },
            {   "log_level",            &RPCParser::parseLogLevel,              0,  2   },
            {   "logrotate",            &RPCParser::parseAsIs,                  0,  0   },
    //      {   "nickname_info",        &RPCParser::parseNicknameInfo,          1,  1   },
            {   "owner_info",           &RPCParser::parseAccountItems,          1,  2   },
            {   "peers",                &RPCParser::parseAsIs,                  0,  0   },
            {   "ping",                 &RPCParser::parseAsIs,                  0,  0   },
            {   "print",                &RPCParser::parseAsIs,                  0,  1   },
    //      {   "profile",              &RPCParser::parseProfile,               1,  9   },
            {   "proof_create",         &RPCParser::parseProofCreate,           0,  2   },
            {   "proof_solve",          &RPCParser::parseProofSolve,            1,  1   },
            {   "proof_verify",         &RPCParser::parseProofVerify,           2,  4   },
            {   "random",               &RPCParser::parseAsIs,                  0,  0   },
            {   "ripple_path_find",     &RPCParser::parseRipplePathFind,        1,  2   },
            {   "sign",                 &RPCParser::parseSignSubmit,            2,  3   },
            {   "sms",                  &RPCParser::parseSMS,                   1,  1   },
            {   "submit",               &RPCParser::parseSignSubmit,            1,  3   },
            {   "server_info",          &RPCParser::parseAsIs,                  0,  0   },
            {   "server_state",         &RPCParser::parseAsIs,                  0,  0   },
            {   "stop",                 &RPCParser::parseAsIs,                  0,  0   },
    //      {   "transaction_entry",    &RPCParser::parseTransactionEntry,     -1,  -1  },
            {   "tx",                   &RPCParser::parseTx,                    1,  2   },
            {   "tx_account",           &RPCParser::parseTxAccount,             1,  7   },
            {   "tx_history",           &RPCParser::parseTxHistory,             1,  1   },
            {   "unl_add",              &RPCParser::parseUnlAdd,                1,  2   },
            {   "unl_delete",           &RPCParser::parseUnlDelete,             1,  1   },
            {   "unl_list",             &RPCParser::parseAsIs,                  0,  0   },
            {   "unl_load",             &RPCParser::parseAsIs,                  0,  0   },
            {   "unl_network",          &RPCParser::parseAsIs,                  0,  0   },
            {   "unl_reset",            &RPCParser::parseAsIs,                  0,  0   },
            {   "unl_score",            &RPCParser::parseAsIs,                  0,  0   },
            {   "validation_create",    &RPCParser::parseValidationCreate,      0,  1   },
            {   "validation_seed",      &RPCParser::parseValidationSeed,        0,  1   },
            {   "wallet_accounts",      &RPCParser::parseWalletAccounts,        1,  1   },
            {   "wallet_propose",       &RPCParser::parseWalletPropose,         0,  1   },
            {   "wallet_seed",          &RPCParser::parseWalletSeed,            0,  1   },
            {   "internal",             &RPCParser::parseInternal,              1,  -1  },

            // Evented methods
            {   "path_find",            &RPCParser::parseEvented,               -1, -1  },
            {   "subscribe",            &RPCParser::parseEvented,               -1, -1  },
            {   "unsubscribe",          &RPCParser::parseEvented,               -1, -1  },
        };

        int i = RIPPLE_ARRAYSIZE (commandsA);

        while (i-- && strMethod != commandsA[i].pCommand)
            ;

        if (i < 0)
        {
            if (!allowAnyCommand)
                return rpcError (rpcUNKNOWN_COMMAND);

            return parseAsIs (jvParams);
        }
        else if ((commandsA[i].iMinParams >= 0 && jvParams.size () < commandsA[i].iMinParams)
                 || (commandsA[i].iMaxParams >= 0 && jvParams.size () > commandsA[i].iMaxParams))
        {
            WriteLog (lsWARNING, RPCParser) << "Wrong number of parameters: minimum=" << commandsA[i].iMinParams
                                            << " maximum=" << commandsA[i].iMaxParams
                                            << " actual=" << jvParams.size ();

            return rpcError (rpcBAD_SYNTAX);
        }

        return (this->* (commandsA[i].pfpFunc)) (jvParams);
    }
};

//------------------------------------------------------------------------------

struct RPCCallImp
{
    // VFALCO NOTE Is this a to-do comment or a doc comment?
    // Place the async result somewhere useful.
    static void callRPCHandler (Json::Value* jvOutput, const Json::Value& jvInput)
    {
        (*jvOutput) = jvInput;
    }

    static bool onResponse (
        std::function<void (const Json::Value& jvInput)> callbackFuncP,
            const boost::system::error_code& ecResult, int iStatus,
                const std::string& strData)
    {
        if (callbackFuncP)
        {
            // Only care about the result, if we care to deliver it callbackFuncP.

            // Receive reply
            if (iStatus == 401)
                throw std::runtime_error ("incorrect rpcuser or rpcpassword (authorization failed)");
            else if ((iStatus >= 400) && (iStatus != 400) && (iStatus != 404) && (iStatus != 500)) // ?
                throw std::runtime_error (strprintf ("server returned HTTP error %d", iStatus));
            else if (strData.empty ())
                throw std::runtime_error ("no response from server");

            // Parse reply
            WriteLog (lsDEBUG, RPCParser) << "RPC reply: " << strData << std::endl;

            Json::Reader    reader;
            Json::Value     jvReply;

            if (!reader.parse (strData, jvReply))
                throw std::runtime_error ("couldn't parse reply from server");

            if (jvReply.isNull ())
                throw std::runtime_error ("expected reply to have result, error and id properties");

            Json::Value     jvResult (Json::objectValue);

            jvResult["result"] = jvReply;

            (callbackFuncP) (jvResult);
        }

        return false;
    }

    // Build the request.
    static void onRequest (const std::string& strMethod, const Json::Value& jvParams,
        const std::map<std::string, std::string>& mHeaders, const std::string& strPath,
            boost::asio::streambuf& sb, const std::string& strHost)
    {
        WriteLog (lsDEBUG, RPCParser) << "requestRPC: strPath='" << strPath << "'";

        std::ostream    osRequest (&sb);

        osRequest <<
                  createHTTPPost (
                      strHost,
                      strPath,
                      JSONRPCRequest (strMethod, jvParams, Json::Value (1)),
                      mHeaders);
    }
};

//------------------------------------------------------------------------------

int RPCCall::fromCommandLine (const std::vector<std::string>& vCmd)
{
    Json::Value jvOutput;
    int         nRet = 0;
    Json::Value jvRequest (Json::objectValue);

    try
    {
        RPCParser   rpParser;
        Json::Value jvRpcParams (Json::arrayValue);

        if (vCmd.empty ()) return 1;                                            // 1 = print usage.

        for (int i = 1; i != vCmd.size (); i++)
            jvRpcParams.append (vCmd[i]);

        Json::Value jvRpc   = Json::Value (Json::objectValue);

        jvRpc["method"] = vCmd[0];
        jvRpc["params"] = jvRpcParams;

        jvRequest   = rpParser.parseCommand (vCmd[0], jvRpcParams, true);

        WriteLog (lsTRACE, RPCParser) << "RPC Request: " << jvRequest << std::endl;

        if (jvRequest.isMember ("error"))
        {
            jvOutput            = jvRequest;
            jvOutput["rpc"]     = jvRpc;
        }
        else
        {
            Json::Value jvParams (Json::arrayValue);

            jvParams.append (jvRequest);

            if (!getConfig ().RPC_ADMIN_USER.empty ())
                jvRequest["admin_user"]     = getConfig ().RPC_ADMIN_USER;

            if (!getConfig ().RPC_ADMIN_PASSWORD.empty ())
                jvRequest["admin_password"] = getConfig ().RPC_ADMIN_PASSWORD;

            boost::asio::io_service         isService;

            fromNetwork (
                isService,
                getConfig ().getRpcIP (),
                getConfig ().getRpcPort (),
                getConfig ().RPC_USER,
                getConfig ().RPC_PASSWORD,
                "",
                jvRequest.isMember ("method")           // Allow parser to rewrite method.
                    ? jvRequest["method"].asString () : vCmd[0],
                jvParams,                               // Parsed, execute.
                false,
                std::bind (RPCCallImp::callRPCHandler, &jvOutput,
                           std::placeholders::_1));

            isService.run (); // This blocks until there is no more outstanding async calls.

            if (jvOutput.isMember ("result"))
            {
                // Had a successful JSON-RPC 2.0 call.
                jvOutput    = jvOutput["result"];

                // jvOutput may report a server side error.
                // It should report "status".
            }
            else
            {
                // Transport error.
                Json::Value jvRpcError  = jvOutput;

                jvOutput            = rpcError (rpcJSON_RPC);
                jvOutput["result"]  = jvRpcError;
            }

            // If had an error, supply invokation in result.
            if (jvOutput.isMember ("error"))
            {
                jvOutput["rpc"]             = jvRpc;            // How the command was seen as method + params.
                jvOutput["request_sent"]    = jvRequest;        // How the command was translated.
            }
        }

        if (jvOutput.isMember ("error"))
        {
            jvOutput["status"]  = "error";

            nRet    = jvOutput.isMember ("error_code")
                      ? beast::lexicalCast <int> (jvOutput["error_code"].asString ())
                      : 1;
        }

        // YYY We could have a command line flag for single line output for scripts.
        // YYY We would intercept output here and simplify it.
    }
    catch (std::exception& e)
    {
        jvOutput                = rpcError (rpcINTERNAL);
        jvOutput["error_what"]  = e.what ();
        nRet                    = rpcINTERNAL;
    }
    catch (...)
    {
        jvOutput                = rpcError (rpcINTERNAL);
        jvOutput["error_what"]  = "exception";
        nRet                    = rpcINTERNAL;
    }

    std::cout << jvOutput.toStyledString ();

    return nRet;
}

//------------------------------------------------------------------------------

void RPCCall::fromNetwork (
    boost::asio::io_service& io_service,
    const std::string& strIp, const int iPort,
    const std::string& strUsername, const std::string& strPassword,
    const std::string& strPath, const std::string& strMethod,
    const Json::Value& jvParams, const bool bSSL,
    std::function<void (const Json::Value& jvInput)> callbackFuncP)
{
    // Connect to localhost
    if (!getConfig ().QUIET)
    {
        Log::out() << "Connecting to: " << strIp << ":" << iPort;
        //  Log::out() << "Username: " << strUsername << ":" << strPassword;
        //  Log::out() << "Path: " << strPath;
        //  Log::out() << "Method: " << strMethod;
    }

    // HTTP basic authentication
    std::string strUserPass64 = RPCParser::EncodeBase64 (strUsername + ":" + strPassword);

    std::map<std::string, std::string> mapRequestHeaders;

    mapRequestHeaders["Authorization"] = std::string ("Basic ") + strUserPass64;

    // Send request
    // Log(lsDEBUG) << "requesting" << std::endl;
    // WriteLog (lsDEBUG, RPCParser) << "send request " << strMethod << " : " << strRequest << std::endl;

    const int RPC_REPLY_MAX_BYTES (256*1024*1024);
    const int RPC_NOTIFY_SECONDS (600);

    HTTPClient::request (
        bSSL,
        io_service,
        strIp,
        iPort,
        std::bind (
            &RPCCallImp::onRequest,
            strMethod,
            jvParams,
            mapRequestHeaders,
            strPath, std::placeholders::_1, std::placeholders::_2),
        RPC_REPLY_MAX_BYTES,
        boost::posix_time::seconds (RPC_NOTIFY_SECONDS),
        std::bind (&RPCCallImp::onResponse, callbackFuncP,
                   std::placeholders::_1, std::placeholders::_2,
                   std::placeholders::_3));
}

} // ripple
