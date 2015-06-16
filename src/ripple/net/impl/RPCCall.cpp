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
#include <ripple/net/RPCCall.h>
#include <ripple/net/RPCErr.h>
#include <ripple/basics/Log.h>
#include <ripple/core/Config.h>
#include <ripple/json/json_reader.h>
#include <ripple/json/to_string.h>
#include <ripple/net/HTTPClient.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/SystemParameters.h>
#include <ripple/server/ServerHandler.h>
#include <beast/module/core/text/LexicalCast.h>
#include <boost/asio/streambuf.hpp>
#include <boost/regex.hpp>
#include <iostream>
#include <type_traits>

namespace ripple {

class RPCParser;

//
// HTTP protocol
//
// This ain't Apache.  We're just using HTTP header for the length field
// and to be compatible with other JSON-RPC implementations.
//

std::string createHTTPPost (
    std::string const& strHost,
    std::string const& strPath,
    std::string const& strMsg,
    std::map<std::string, std::string> const& mapRequestHeaders)
{
    std::ostringstream s;

    // CHECKME this uses a different version than the replies below use. Is
    //         this by design or an accident or should it be using
    //         BuildInfo::getFullVersionString () as well?

    s << "POST "
      << (strPath.empty () ? "/" : strPath)
      << " HTTP/1.0\r\n"
      << "User-Agent: " << systemName () << "-json-rpc/v1\r\n"
      << "Host: " << strHost << "\r\n"
      << "Content-Type: application/json\r\n"
      << "Content-Length: " << strMsg.size () << "\r\n"
      << "Accept: application/json\r\n";

    for (auto const& item : mapRequestHeaders)
        s << item.first << ": " << item.second << "\r\n";

    s << "\r\n" << strMsg;

    return s.str ();
}

class RPCParser
{
private:
    // TODO New routine for parsing ledger parameters, other routines should standardize on this.
    static bool jvParseLedger (Json::Value& jvRequest, std::string const& strLedger)
    {
        if (strLedger == "current" || strLedger == "closed" || strLedger == "validated")
        {
            jvRequest[jss::ledger_index]   = strLedger;
        }
        else if (strLedger.length () == 64)
        {
            // YYY Could confirm this is a uint256.
            jvRequest[jss::ledger_hash]    = strLedger;
        }
        else
        {
            jvRequest[jss::ledger_index]   = beast::lexicalCast <std::uint32_t> (strLedger);
        }

        return true;
    }

    // Build a object { "currency" : "XYZ", "issuer" : "rXYX" }
    static Json::Value jvParseCurrencyIssuer (std::string const& strCurrencyIssuer)
    {
        static boost::regex reCurIss ("\\`([[:alpha:]]{3})(?:/(.+))?\\'");

        boost::smatch   smMatch;

        if (boost::regex_match (strCurrencyIssuer, smMatch, reCurIss))
        {
            Json::Value jvResult (Json::objectValue);
            std::string strCurrency = smMatch[1];
            std::string strIssuer   = smMatch[2];

            jvResult[jss::currency]    = strCurrency;

            if (strIssuer.length ())
            {
                // Could confirm issuer is a valid Ripple address.
                jvResult[jss::issuer]      = strIssuer;
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
    using parseFuncPtr = Json::Value (RPCParser::*) (Json::Value const& jvParams);

    Json::Value parseAsIs (Json::Value const& jvParams)
    {
        Json::Value v (Json::objectValue);

        if (jvParams.isArray () && (jvParams.size () > 0))
            v[jss::params] = jvParams;

        return v;
    }

    Json::Value parseInternal (Json::Value const& jvParams)
    {
        Json::Value v (Json::objectValue);
        v[jss::internal_command] = jvParams[0u];

        Json::Value params (Json::arrayValue);

        for (unsigned i = 1; i < jvParams.size (); ++i)
            params.append (jvParams[i]);

        v[jss::params] = params;

        return v;
    }

    // fetch_info [clear]
    Json::Value parseFetchInfo (Json::Value const& jvParams)
    {
        Json::Value     jvRequest (Json::objectValue);
        unsigned int    iParams = jvParams.size ();

        if (iParams != 0)
            jvRequest[jvParams[0u].asString()] = true;

        return jvRequest;
    }

    // account_tx accountID [ledger_min [ledger_max [limit [offset]]]] [binary] [count] [descending]
    Json::Value parseAccountTransactions (Json::Value const& jvParams)
    {
        Json::Value     jvRequest (Json::objectValue);
        RippleAddress   raAccount;
        unsigned int    iParams = jvParams.size ();

        if (!raAccount.setAccountID (jvParams[0u].asString ()))
            return rpcError (rpcACT_MALFORMED);

        jvRequest[jss::account]    = raAccount.humanAccountID ();

        bool            bDone   = false;

        while (!bDone && iParams >= 2)
        {
            if (jvParams[iParams - 1].asString () == jss::binary)
            {
                jvRequest[jss::binary]     = true;
                --iParams;
            }
            else if (jvParams[iParams - 1].asString () == jss::count)
            {
                jvRequest[jss::count]      = true;
                --iParams;
            }
            else if (jvParams[iParams - 1].asString () == jss::descending)
            {
                jvRequest[jss::descending] = true;
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

            jvRequest[jss::ledger_index_min]   = jvParams[1u].asInt ();
            jvRequest[jss::ledger_index_max]   = jvParams[2u].asInt ();

            if (iParams >= 4)
                jvRequest[jss::limit]  = jvParams[3u].asInt ();

            if (iParams >= 5)
                jvRequest[jss::offset] = jvParams[4u].asInt ();
        }

        return jvRequest;
    }

    // tx_account accountID [ledger_min [ledger_max [limit]]]] [binary] [count] [forward]
    Json::Value parseTxAccount (Json::Value const& jvParams)
    {
        Json::Value     jvRequest (Json::objectValue);
        RippleAddress   raAccount;
        unsigned int    iParams = jvParams.size ();

        if (!raAccount.setAccountID (jvParams[0u].asString ()))
            return rpcError (rpcACT_MALFORMED);

        jvRequest[jss::account]    = raAccount.humanAccountID ();

        bool            bDone   = false;

        while (!bDone && iParams >= 2)
        {
            if (jvParams[iParams - 1].asString () == jss::binary)
            {
                jvRequest[jss::binary]     = true;
                --iParams;
            }
            else if (jvParams[iParams - 1].asString () == jss::count)
            {
                jvRequest[jss::count]      = true;
                --iParams;
            }
            else if (jvParams[iParams - 1].asString () == jss::forward)
            {
                jvRequest[jss::forward] = true;
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

            jvRequest[jss::ledger_index_min]   = jvParams[1u].asInt ();
            jvRequest[jss::ledger_index_max]   = jvParams[2u].asInt ();

            if (iParams >= 4)
                jvRequest[jss::limit]  = jvParams[3u].asInt ();
        }

        return jvRequest;
    }

    // book_offers <taker_pays> <taker_gets> [<taker> [<ledger> [<limit> [<proof> [<marker>]]]]]
    // limit: 0 = no limit
    // proof: 0 or 1
    //
    // Mnemonic: taker pays --> offer --> taker gets
    Json::Value parseBookOffers (Json::Value const& jvParams)
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
            jvRequest[jss::taker_pays] = jvTakerPays;
        }

        if (isRpcError (jvTakerGets))
        {
            return jvTakerGets;
        }
        else
        {
            jvRequest[jss::taker_gets] = jvTakerGets;
        }

        if (jvParams.size () >= 3)
        {
            jvRequest[jss::issuer] = jvParams[2u].asString ();
        }

        if (jvParams.size () >= 4 && !jvParseLedger (jvRequest, jvParams[3u].asString ()))
            return jvRequest;

        if (jvParams.size () >= 5)
        {
            int     iLimit  = jvParams[5u].asInt ();

            if (iLimit > 0)
                jvRequest[jss::limit]  = iLimit;
        }

        if (jvParams.size () >= 6 && jvParams[5u].asInt ())
        {
            jvRequest[jss::proof]  = true;
        }

        if (jvParams.size () == 7)
            jvRequest[jss::marker] = jvParams[6u];

        return jvRequest;
    }

    // can_delete [<ledgerid>|<ledgerhash>|now|always|never]
    Json::Value parseCanDelete (Json::Value const& jvParams)
    {
        Json::Value     jvRequest (Json::objectValue);

        if (!jvParams.size ())
            return jvRequest;

        std::string input = jvParams[0u].asString();
        if (input.find_first_not_of("0123456789") ==
                std::string::npos)
            jvRequest["can_delete"] = jvParams[0u].asUInt();
        else
            jvRequest["can_delete"] = input;

        return jvRequest;
    }

    // connect <ip> [port]
    Json::Value parseConnect (Json::Value const& jvParams)
    {
        Json::Value     jvRequest (Json::objectValue);

        jvRequest[jss::ip] = jvParams[0u].asString ();

        if (jvParams.size () == 2)
            jvRequest[jss::port]   = jvParams[1u].asUInt ();

        return jvRequest;
    }

    // Return an error for attemping to subscribe/unsubscribe via RPC.
    Json::Value parseEvented (Json::Value const& jvParams)
    {
        return rpcError (rpcNO_EVENTS);
    }

    // feature [<feature>] [true|false]
    Json::Value parseFeature (Json::Value const& jvParams)
    {
        Json::Value     jvRequest (Json::objectValue);

        if (jvParams.size () > 0)
            jvRequest[jss::feature]    = jvParams[0u].asString ();

        if (jvParams.size () > 1)
            jvRequest[jss::vote]       = beast::lexicalCastThrow <bool> (jvParams[1u].asString ());

        return jvRequest;
    }

    // get_counts [<min_count>]
    Json::Value parseGetCounts (Json::Value const& jvParams)
    {
        Json::Value     jvRequest (Json::objectValue);

        if (jvParams.size ())
            jvRequest[jss::min_count]  = jvParams[0u].asUInt ();

        return jvRequest;
    }

    // sign_for
    Json::Value parseSignFor (Json::Value const& jvParams)
    {
        Json::Value     txJSON;
        Json::Reader    reader;

        if ((4 == jvParams.size ())
            && reader.parse (jvParams[3u].asString (), txJSON))
        {
            if (txJSON.type () == Json::objectValue)
            {
                // Return SigningFor object for the submitted transaction.
                Json::Value jvRequest;
                jvRequest["signing_for"] = jvParams[0u].asString ();
                jvRequest["account"] = jvParams[1u].asString ();
                jvRequest["secret"]  = jvParams[2u].asString ();
                jvRequest["tx_json"] = txJSON;

                return jvRequest;
            }
        }
        return rpcError (rpcINVALID_PARAMS);
    }

    // json <command> <json>
    Json::Value parseJson (Json::Value const& jvParams)
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
    Json::Value parseLedger (Json::Value const& jvParams)
    {
        Json::Value     jvRequest (Json::objectValue);

        if (!jvParams.size ())
        {
            return jvRequest;
        }

        jvParseLedger (jvRequest, jvParams[0u].asString ());

        if (2 == jvParams.size () && jvParams[1u].asString () == "full")
        {
            jvRequest[jss::full]   = bool (1);
        }

        return jvRequest;
    }

    // ledger_header <id>|<index>
    Json::Value parseLedgerId (Json::Value const& jvParams)
    {
        Json::Value     jvRequest (Json::objectValue);

        std::string     strLedger   = jvParams[0u].asString ();

        if (strLedger.length () == 32)
        {
            jvRequest[jss::ledger_hash]    = strLedger;
        }
        else
        {
            jvRequest[jss::ledger_index]   = beast::lexicalCast <std::uint32_t> (strLedger);
        }

        return jvRequest;
    }

    // log_level:                           Get log levels
    // log_level <severity>:                Set master log level to the specified severity
    // log_level <partition> <severity>:    Set specified partition to specified severity
    Json::Value parseLogLevel (Json::Value const& jvParams)
    {
        Json::Value     jvRequest (Json::objectValue);

        if (jvParams.size () == 1)
        {
            jvRequest[jss::severity] = jvParams[0u].asString ();
        }
        else if (jvParams.size () == 2)
        {
            jvRequest[jss::partition] = jvParams[0u].asString ();
            jvRequest[jss::severity] = jvParams[1u].asString ();
        }

        return jvRequest;
    }

    // owner_info <account>|<account_public_key>
    // owner_info <seed>|<pass_phrase>|<key> [<ledfer>]
    // account_info <account>|<account_public_key>
    // account_info <seed>|<pass_phrase>|<key> [<ledger>]
    // account_offers <account>|<account_public_key> [<ledger>]
    Json::Value parseAccountItems (Json::Value const& jvParams)
    {
        return parseAccountRaw (jvParams, false);
    }

    Json::Value parseAccountCurrencies (Json::Value const& jvParams)
    {
        return parseAccountRaw (jvParams, false);
    }

    // account_lines <account> <account>|"" [<ledger>]
    Json::Value parseAccountLines (Json::Value const& jvParams)
    {
        return parseAccountRaw (jvParams, true);
    }

    // TODO: Get index from an alternate syntax: rXYZ:<index>
    Json::Value parseAccountRaw (Json::Value const& jvParams, bool bPeer)
    {
        std::string     strIdent    = jvParams[0u].asString ();
        unsigned int    iCursor     = jvParams.size ();
        bool            bStrict     = false;
        std::string     strPeer;

        if (!bPeer && iCursor >= 2 && jvParams[iCursor - 1] == jss::strict)
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

        jvRequest[jss::account]    = strIdent;

        if (bStrict)
            jvRequest[jss::strict]     = 1;

        if (iIndex)
            jvRequest[jss::account_index]  = iIndex;

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

    // ripple_path_find <json> [<ledger>]
    Json::Value parseRipplePathFind (Json::Value const& jvParams)
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
    Json::Value parseSignSubmit (Json::Value const& jvParams)
    {
        Json::Value     txJSON;
        Json::Reader    reader;
        bool const      bOffline    = 3 == jvParams.size () && jvParams[2u].asString () == "offline";

        if (1 == jvParams.size ())
        {
            // Submitting tx_blob

            Json::Value jvRequest;

            jvRequest[jss::tx_blob]    = jvParams[0u].asString ();

            return jvRequest;
        }
        else if ((2 == jvParams.size () || bOffline)
                 && reader.parse (jvParams[1u].asString (), txJSON))
        {
            // Signing or submitting tx_json.
            Json::Value jvRequest;

            jvRequest[jss::secret]     = jvParams[0u].asString ();
            jvRequest[jss::tx_json]    = txJSON;

            if (bOffline)
                jvRequest[jss::offline]    = true;

            return jvRequest;
        }

        return rpcError (rpcINVALID_PARAMS);
    }

    // submit any multisigned transaction to the network
    //
    // submit_multisigned <json>
    Json::Value parseSubmitMultiSigned (Json::Value const& jvParams)
    {
        Json::Value     jvRequest;
        Json::Reader    reader;
        bool const      bOffline    = 2 == jvParams.size () && jvParams[1u].asString () == "offline";

        if ((1 == jvParams.size () || bOffline)
            && reader.parse (jvParams[0u].asString (), jvRequest))
        {
            // Multisigned.
            if (bOffline)
                jvRequest["offline"]    = true;

            return jvRequest;
        }

        return rpcError (rpcINVALID_PARAMS);
    }

    // tx <transaction_id>
    Json::Value parseTx (Json::Value const& jvParams)
    {
        Json::Value jvRequest;

        if (jvParams.size () > 1)
        {
            if (jvParams[1u].asString () == jss::binary)
                jvRequest[jss::binary] = true;
        }

        jvRequest["transaction"]    = jvParams[0u].asString ();
        return jvRequest;
    }

    // tx_history <index>
    Json::Value parseTxHistory (Json::Value const& jvParams)
    {
        Json::Value jvRequest;

        jvRequest[jss::start]  = jvParams[0u].asUInt ();

        return jvRequest;
    }

    // unl_add <domain>|<node_public> [<comment>]
    Json::Value parseUnlAdd (Json::Value const& jvParams)
    {
        std::string strNode     = jvParams[0u].asString ();
        std::string strComment  = (jvParams.size () == 2) ? jvParams[1u].asString () : "";

        RippleAddress   naNodePublic;

        if (strNode.length ())
        {
            Json::Value jvRequest;

            jvRequest[jss::node]       = strNode;

            if (strComment.length ())
                jvRequest[jss::comment]    = strComment;

            return jvRequest;
        }

        return rpcError (rpcINVALID_PARAMS);
    }

    // unl_delete <domain>|<public_key>
    Json::Value parseUnlDelete (Json::Value const& jvParams)
    {
        Json::Value jvRequest;

        jvRequest[jss::node]       = jvParams[0u].asString ();

        return jvRequest;
    }

    // validation_create [<pass_phrase>|<seed>|<seed_key>]
    //
    // NOTE: It is poor security to specify secret information on the command line.  This information might be saved in the command
    // shell history file (e.g. .bash_history) and it may be leaked via the process status command (i.e. ps).
    Json::Value parseValidationCreate (Json::Value const& jvParams)
    {
        Json::Value jvRequest;

        if (jvParams.size ())
            jvRequest[jss::secret]     = jvParams[0u].asString ();

        return jvRequest;
    }

    // validation_seed [<pass_phrase>|<seed>|<seed_key>]
    //
    // NOTE: It is poor security to specify secret information on the command line.  This information might be saved in the command
    // shell history file (e.g. .bash_history) and it may be leaked via the process status command (i.e. ps).
    Json::Value parseValidationSeed (Json::Value const& jvParams)
    {
        Json::Value jvRequest;

        if (jvParams.size ())
            jvRequest[jss::secret]     = jvParams[0u].asString ();

        return jvRequest;
    }

    // wallet_propose [<passphrase>]
    // <passphrase> is only for testing. Master seeds should only be generated randomly.
    Json::Value parseWalletPropose (Json::Value const& jvParams)
    {
        Json::Value jvRequest;

        if (jvParams.size ())
            jvRequest[jss::passphrase]     = jvParams[0u].asString ();

        return jvRequest;
    }

    // wallet_seed [<seed>|<passphrase>|<passkey>]
    Json::Value parseWalletSeed (Json::Value const& jvParams)
    {
        Json::Value jvRequest;

        if (jvParams.size ())
            jvRequest[jss::secret]     = jvParams[0u].asString ();

        return jvRequest;
    }

    // parse gateway balances
    // gateway_balances [<ledger>] <issuer_account> [ <hotwallet> [ <hotwallet> ]]

    Json::Value parseGatewayBalances (Json::Value const& jvParams)
    {
        unsigned int index = 0;
        const unsigned int size = jvParams.size ();

        Json::Value jvRequest;

        std::string param = jvParams[index++].asString ();
        if (param.empty ())
            return RPC::make_param_error ("Invalid first parameter");

        if (param[0] != 'r')
        {
            if (param.size() == 64)
                jvRequest[jss::ledger_hash] = param;
            else
                jvRequest[jss::ledger_index] = param;

            if (size <= index)
                return RPC::make_param_error ("Invalid hotwallet");

            param = jvParams[index++].asString ();
        }

        jvRequest[jss::account] = param;

        if (index < size)
        {
            Json::Value& hotWallets =
                (jvRequest["hotwallet"] = Json::arrayValue);
            while (index < size)
                hotWallets.append (jvParams[index++].asString ());
        }

        return jvRequest;
    }

public:
    //--------------------------------------------------------------------------

    static std::string EncodeBase64 (std::string const& s)
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
        if (ShouldLog (lsTRACE, RPCParser))
        {
            WriteLog (lsTRACE, RPCParser) << "RPC method:" << strMethod;
            WriteLog (lsTRACE, RPCParser) << "RPC params:" << jvParams;
        }

        struct Command
        {
            const char*     name;
            parseFuncPtr    parse;
            int             minParams;
            int             maxParams;
        };

        // FIXME: replace this with a function-static std::map and the lookup
        // code with std::map::find when the problem with magic statics on
        // Visual Studio is fixed.
        static
        Command const commands[] =
        {
            // Request-response methods
            // - Returns an error, or the request.
            // - To modify the method, provide a new method in the request.
            {   "account_currencies",   &RPCParser::parseAccountCurrencies,     1,  2   },
            {   "account_info",         &RPCParser::parseAccountItems,          1,  2   },
            {   "account_lines",        &RPCParser::parseAccountLines,          1,  5   },
            {   "account_objects",      &RPCParser::parseAccountItems,          1,  5   },
            {   "account_offers",       &RPCParser::parseAccountItems,          1,  4   },
            {   "account_tx",           &RPCParser::parseAccountTransactions,   1,  8   },
            {   "book_offers",          &RPCParser::parseBookOffers,            2,  7   },
            {   "can_delete",           &RPCParser::parseCanDelete,             0,  1   },
            {   "connect",              &RPCParser::parseConnect,               1,  2   },
            {   "consensus_info",       &RPCParser::parseAsIs,                  0,  0   },
            {   "feature",              &RPCParser::parseFeature,               0,  2   },
            {   "fetch_info",           &RPCParser::parseFetchInfo,             0,  1   },
            {   "gateway_balances",     &RPCParser::parseGatewayBalances  ,     1,  -1  },
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
            {   "owner_info",           &RPCParser::parseAccountItems,          1,  2   },
            {   "peers",                &RPCParser::parseAsIs,                  0,  0   },
            {   "ping",                 &RPCParser::parseAsIs,                  0,  0   },
            {   "print",                &RPCParser::parseAsIs,                  0,  1   },
    //      {   "profile",              &RPCParser::parseProfile,               1,  9   },
            {   "random",               &RPCParser::parseAsIs,                  0,  0   },
            {   "ripple_path_find",     &RPCParser::parseRipplePathFind,        1,  2   },
            {   "sign",                 &RPCParser::parseSignSubmit,            2,  3   },
#if RIPPLE_ENABLE_MULTI_SIGN
            {   "sign_for",             &RPCParser::parseSignFor,               4,  4   },
#endif // RIPPLE_ENABLE_MULTI_SIGN
            {   "submit",               &RPCParser::parseSignSubmit,            1,  3   },
#if RIPPLE_ENABLE_MULTI_SIGN
            {   "submit_multisigned",   &RPCParser::parseSubmitMultiSigned,     1,  1   },
#endif // RIPPLE_ENABLE_MULTI_SIGN
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
            {   "version",              &RPCParser::parseAsIs,                  0,  0   },
            {   "wallet_propose",       &RPCParser::parseWalletPropose,         0,  1   },
            {   "wallet_seed",          &RPCParser::parseWalletSeed,            0,  1   },
            {   "internal",             &RPCParser::parseInternal,              1,  -1  },

            // Evented methods
            {   "path_find",            &RPCParser::parseEvented,               -1, -1  },
            {   "subscribe",            &RPCParser::parseEvented,               -1, -1  },
            {   "unsubscribe",          &RPCParser::parseEvented,               -1, -1  },
        };

        auto const count = jvParams.size ();

        for (auto const& command : commands)
        {
            if (strMethod == command.name)
            {
                if ((command.minParams >= 0 && count < command.minParams) ||
                    (command.maxParams >= 0 && count > command.maxParams))
                {
                    WriteLog (lsDEBUG, RPCParser) <<
                        "Wrong number of parameters for " << command.name <<
                        " minimum=" << command.minParams <<
                        " maximum=" << command.maxParams <<
                        " actual=" << count;

                    return rpcError (rpcBAD_SYNTAX);
                }

                return (this->* (command.parse)) (jvParams);
            }
        }

        // The command could not be found
        if (!allowAnyCommand)
            return rpcError (rpcUNKNOWN_COMMAND);

        return parseAsIs (jvParams);
    }
};

//------------------------------------------------------------------------------

//
// JSON-RPC protocol.  Bitcoin speaks version 1.0 for maximum compatibility,
// but uses JSON-RPC 1.1/2.0 standards for parts of the 1.0 standard that were
// unspecified (HTTP errors and contents of 'error').
//
// 1.0 spec: http://json-rpc.org/wiki/specification
// 1.2 spec: http://groups.google.com/group/json-rpc/web/json-rpc-over-http
//

std::string JSONRPCRequest (std::string const& strMethod, Json::Value const& params, Json::Value const& id)
{
    Json::Value request;
    request[jss::method] = strMethod;
    request[jss::params] = params;
    request[jss::id] = id;
    return to_string (request) + "\n";
}

struct RPCCallImp
{
    // VFALCO NOTE Is this a to-do comment or a doc comment?
    // Place the async result somewhere useful.
    static void callRPCHandler (Json::Value* jvOutput, Json::Value const& jvInput)
    {
        (*jvOutput) = jvInput;
    }

    static bool onResponse (
        std::function<void (Json::Value const& jvInput)> callbackFuncP,
            const boost::system::error_code& ecResult, int iStatus,
                std::string const& strData)
    {
        if (callbackFuncP)
        {
            // Only care about the result, if we care to deliver it callbackFuncP.

            // Receive reply
            if (iStatus == 401)
                throw std::runtime_error ("incorrect rpcuser or rpcpassword (authorization failed)");
            else if ((iStatus >= 400) && (iStatus != 400) && (iStatus != 404) && (iStatus != 500)) // ?
                throw std::runtime_error (std::string ("server returned HTTP error ") + std::to_string (iStatus));
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
    static void onRequest (std::string const& strMethod, Json::Value const& jvParams,
        const std::map<std::string, std::string>& mHeaders, std::string const& strPath,
            boost::asio::streambuf& sb, std::string const& strHost)
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
        jvRpc[jss::params] = jvRpcParams;

        jvRequest   = rpParser.parseCommand (vCmd[0], jvRpcParams, true);

        WriteLog (lsTRACE, RPCParser) << "RPC Request: " << jvRequest << std::endl;

        if (jvRequest.isMember (jss::error))
        {
            jvOutput            = jvRequest;
            jvOutput["rpc"]     = jvRpc;
        }
        else
        {
            ServerHandler::Setup setup;
            try
            {
                std::stringstream ss;
                setup = setup_ServerHandler(getConfig(), ss);
            }
            catch(...)
            {
                // ignore any exceptions, so the command
                // line client works without a config file
            }

            if (getConfig().rpc_ip)
                setup.client.ip = getConfig().rpc_ip->to_string();
            if (getConfig().rpc_port)
                setup.client.port = *getConfig().rpc_port;

            Json::Value jvParams (Json::arrayValue);

            if (!setup.client.admin_user.empty ())
                jvRequest["admin_user"] = setup.client.admin_user;

            if (!setup.client.admin_password.empty ())
                jvRequest["admin_password"] = setup.client.admin_password;

            jvParams.append (jvRequest);

            {
                boost::asio::io_service isService;
                fromNetwork (
                    isService,
                    setup.client.ip,
                    setup.client.port,
                    setup.client.user,
                    setup.client.password,
                    "",
                    jvRequest.isMember ("method")           // Allow parser to rewrite method.
                        ? jvRequest["method"].asString () : vCmd[0],
                    jvParams,                               // Parsed, execute.
                    setup.client.secure != 0,                // Use SSL
                    std::bind (RPCCallImp::callRPCHandler, &jvOutput,
                               std::placeholders::_1));
                isService.run(); // This blocks until there is no more outstanding async calls.
            }

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
            if (jvOutput.isMember (jss::error))
            {
                jvOutput["rpc"]             = jvRpc;            // How the command was seen as method + params.
                jvOutput["request_sent"]    = jvRequest;        // How the command was translated.
            }
        }

        if (jvOutput.isMember (jss::error))
        {
            jvOutput[jss::status]  = "error";

            nRet    = jvOutput.isMember (jss::error_code)
                      ? beast::lexicalCast <int> (jvOutput[jss::error_code].asString ())
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
    std::string const& strIp, const int iPort,
    std::string const& strUsername, std::string const& strPassword,
    std::string const& strPath, std::string const& strMethod,
    Json::Value const& jvParams, const bool bSSL,
    std::function<void (Json::Value const& jvInput)> callbackFuncP)
{
    // Connect to localhost
    if (!getConfig ().QUIET)
    {
        std::cerr << (bSSL ? "Securely connecting to " : "Connecting to ") <<
            strIp << ":" << iPort << std::endl;
    }

    // HTTP basic authentication
    auto const auth = RPCParser::EncodeBase64 (strUsername + ":" + strPassword);

    std::map<std::string, std::string> mapRequestHeaders;

    mapRequestHeaders["Authorization"] = std::string ("Basic ") + auth;

    // Send request

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
