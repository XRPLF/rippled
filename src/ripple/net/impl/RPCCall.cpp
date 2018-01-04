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
#include <ripple/app/main/Application.h>
#include <ripple/net/RPCCall.h>
#include <ripple/net/RPCErr.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/Log.h>
#include <ripple/core/Config.h>
#include <ripple/json/json_reader.h>
#include <ripple/json/to_string.h>
#include <ripple/json/Object.h>
#include <ripple/net/HTTPClient.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/SystemParameters.h>
#include <ripple/protocol/types.h>
#include <ripple/rpc/ServerHandler.h>
#include <ripple/beast/core/LexicalCast.h>
#include <beast/core/string.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/regex.hpp>
#include <array>
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
    beast::Journal j_;

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
    Json::Value
    parseAccountTransactions (Json::Value const& jvParams)
    {
        Json::Value     jvRequest (Json::objectValue);
        unsigned int    iParams = jvParams.size ();

        auto const account =
            parseBase58<AccountID>(jvParams[0u].asString());
        if (! account)
            return rpcError (rpcACT_MALFORMED);

        jvRequest[jss::account]= toBase58(*account);

        bool            bDone   = false;

        while (!bDone && iParams >= 2)
        {
            // VFALCO Why is Json::StaticString appearing on the right side?
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
        unsigned int    iParams = jvParams.size ();

        auto const account =
            parseBase58<AccountID>(jvParams[0u].asString());
        if (! account)
            return rpcError (rpcACT_MALFORMED);

        jvRequest[jss::account]    = toBase58(*account);

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

    // feature [<feature>] [accept|reject]
    Json::Value parseFeature (Json::Value const& jvParams)
    {
        Json::Value     jvRequest (Json::objectValue);

        if (jvParams.size () > 0)
            jvRequest[jss::feature] = jvParams[0u].asString ();

        if (jvParams.size () > 1)
        {
            auto const action = jvParams[1u].asString ();

            // This may look reversed, but it's intentional: jss::vetoed
            // determines whether an amendment is vetoed - so "reject" means
            // that jss::vetoed is true.
            if (beast::detail::iequals(action, "reject"))
                jvRequest[jss::vetoed] = Json::Value (true);
            else if (beast::detail::iequals(action, "accept"))
                jvRequest[jss::vetoed] = Json::Value (false);
            else
                return rpcError (rpcINVALID_PARAMS);
        }

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

    // sign_for <account> <secret> <json> offline
    // sign_for <account> <secret> <json>
    Json::Value parseSignFor (Json::Value const& jvParams)
    {
        bool const bOffline = 4 == jvParams.size () && jvParams[3u].asString () == "offline";

        if (3 == jvParams.size () || bOffline)
        {
            Json::Value txJSON;
            Json::Reader reader;
            if (reader.parse (jvParams[2u].asString (), txJSON))
            {
                // sign_for txJSON.
                Json::Value jvRequest{Json::objectValue};

                jvRequest[jss::account] = jvParams[0u].asString ();
                jvRequest[jss::secret]  = jvParams[1u].asString ();
                jvRequest[jss::tx_json] = txJSON;

                if (bOffline)
                    jvRequest[jss::offline] = true;

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

        JLOG (j_.trace()) << "RPC method: " << jvParams[0u];
        JLOG (j_.trace()) << "RPC json: " << jvParams[1u];

        if (reader.parse (jvParams[1u].asString (), jvRequest))
        {
            if (!jvRequest.isObject ())
                return rpcError (rpcINVALID_PARAMS);

            jvRequest[jss::method] = jvParams[0u];

            return jvRequest;
        }

        return rpcError (rpcINVALID_PARAMS);
    }

    bool isValidJson2(Json::Value const& jv)
    {
        if (jv.isArray())
        {
            if (jv.size() == 0)
                return false;
            for (auto const& j : jv)
            {
                if (!isValidJson2(j))
                    return false;
            }
            return true;
        }
        if (jv.isObject())
        {
            if (jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0" &&
                jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0" &&
                jv.isMember(jss::id) && jv.isMember(jss::method))
            {
                if (jv.isMember(jss::params) &&
                      !(jv[jss::params].isArray() || jv[jss::params].isObject()))
                    return false;
                return true;
            }
        }
        return false;
    }

    Json::Value parseJson2(Json::Value const& jvParams)
    {
        Json::Reader reader;
        Json::Value jv;
        bool valid_parse = reader.parse(jvParams[0u].asString(), jv);
        if (valid_parse && isValidJson2(jv))
        {
            if (jv.isObject())
            {
                Json::Value jv1{Json::objectValue};
                if (jv.isMember(jss::params))
                {
                    auto const& params = jv[jss::params];
                    for (auto i = params.begin(); i != params.end(); ++i)
                        jv1[i.key().asString()] = *i;
                }
                jv1[jss::jsonrpc] = jv[jss::jsonrpc];
                jv1[jss::ripplerpc] = jv[jss::ripplerpc];
                jv1[jss::id] = jv[jss::id];
                jv1[jss::method] = jv[jss::method];
                return jv1;
            }
            // else jv.isArray()
            Json::Value jv1{Json::arrayValue};
            for (Json::UInt j = 0; j < jv.size(); ++j)
            {
                if (jv[j].isMember(jss::params))
                {
                    auto const& params = jv[j][jss::params];
                    for (auto i = params.begin(); i != params.end(); ++i)
                        jv1[j][i.key().asString()] = *i;
                }
                jv1[j][jss::jsonrpc] = jv[j][jss::jsonrpc];
                jv1[j][jss::ripplerpc] = jv[j][jss::ripplerpc];
                jv1[j][jss::id] = jv[j][jss::id];
                jv1[j][jss::method] = jv[j][jss::method];
            }
            return jv1;
        }
        auto jv_error = rpcError(rpcINVALID_PARAMS);
        if (jv.isMember(jss::jsonrpc))
            jv_error[jss::jsonrpc] = jv[jss::jsonrpc];
        if (jv.isMember(jss::ripplerpc))
            jv_error[jss::ripplerpc] = jv[jss::ripplerpc];
        if (jv.isMember(jss::id))
            jv_error[jss::id] = jv[jss::id];
        return jv_error;
    }

    // ledger [id|index|current|closed|validated] [full|tx]
    Json::Value parseLedger (Json::Value const& jvParams)
    {
        Json::Value     jvRequest (Json::objectValue);

        if (!jvParams.size ())
        {
            return jvRequest;
        }

        jvParseLedger (jvRequest, jvParams[0u].asString ());

        if (2 == jvParams.size ())
        {
            if (jvParams[1u].asString () == "full")
            {
                jvRequest[jss::full]   = true;
            }
            else if (jvParams[1u].asString () == "tx")
            {
                jvRequest[jss::transactions] = true;
                jvRequest[jss::expand] = true;
            }
        }

        return jvRequest;
    }

    // ledger_header <id>|<index>
    Json::Value parseLedgerId (Json::Value const& jvParams)
    {
        Json::Value     jvRequest (Json::objectValue);

        std::string     strLedger   = jvParams[0u].asString ();

        if (strLedger.length () == 64)
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
        return parseAccountRaw1 (jvParams);
    }

    Json::Value parseAccountCurrencies (Json::Value const& jvParams)
    {
        return parseAccountRaw1 (jvParams);
    }

    // account_lines <account> <account>|"" [<ledger>]
    Json::Value parseAccountLines (Json::Value const& jvParams)
    {
        return parseAccountRaw2 (jvParams, jss::peer);
    }

    // account_channels <account> <account>|"" [<ledger>]
    Json::Value parseAccountChannels (Json::Value const& jvParams)
    {
        return parseAccountRaw2 (jvParams, jss::destination_account);
    }

    // channel_authorize <private_key> <channel_id> <drops>
    Json::Value parseChannelAuthorize (Json::Value const& jvParams)
    {
        Json::Value jvRequest (Json::objectValue);

        jvRequest[jss::secret] = jvParams[0u];
        {
            // verify the channel id is a valid 256 bit number
            uint256 channelId;
            if (!channelId.SetHexExact (jvParams[1u].asString ()))
                return rpcError (rpcCHANNEL_MALFORMED);
        }
        jvRequest[jss::channel_id] = jvParams[1u].asString ();

        try
        {
            auto const drops = std::stoul (jvParams[2u].asString ());
            (void)drops;  // just used for error checking
            jvRequest[jss::amount] = jvParams[2u];
        }
        catch (std::exception const&)
        {
            return rpcError (rpcCHANNEL_AMT_MALFORMED);
        }

        return jvRequest;
    }

    // channel_verify <public_key> <channel_id> <drops> <signature>
    Json::Value parseChannelVerify (Json::Value const& jvParams)
    {
        std::string const strPk = jvParams[0u].asString ();

        if (!parseBase58<PublicKey> (TokenType::TOKEN_ACCOUNT_PUBLIC, strPk))
            return rpcError (rpcPUBLIC_MALFORMED);

        Json::Value jvRequest (Json::objectValue);

        jvRequest[jss::public_key] = strPk;
        {
            // verify the channel id is a valid 256 bit number
            uint256 channelId;
            if (!channelId.SetHexExact (jvParams[1u].asString ()))
                return rpcError (rpcCHANNEL_MALFORMED);
        }
        jvRequest[jss::channel_id] = jvParams[1u].asString ();
        try
        {
            auto const drops = std::stoul (jvParams[2u].asString ());
            (void)drops;  // just used for error checking
            jvRequest[jss::amount] = jvParams[2u];
        }
        catch (std::exception const&)
        {
            return rpcError (rpcCHANNEL_AMT_MALFORMED);
        }
        jvRequest[jss::signature] = jvParams[3u].asString ();

        return jvRequest;
    }

    Json::Value parseAccountRaw2 (Json::Value const& jvParams,
                                  char const * const acc2Field)
    {
        std::array<char const* const, 2> accFields{{jss::account, acc2Field}};
        auto const nParams = jvParams.size ();
        Json::Value jvRequest (Json::objectValue);
        for (auto i = 0; i < nParams; ++i)
        {
            std::string strParam = jvParams[i].asString ();

            if (i==1 && strParam.empty())
                continue;

            // Parameters 0 and 1 are accounts
            if (i < 2)
            {
                if (parseBase58<PublicKey> (
                        TokenType::TOKEN_ACCOUNT_PUBLIC, strParam) ||
                    parseBase58<AccountID> (strParam) ||
                    parseGenericSeed (strParam))
                {
                    jvRequest[accFields[i]] = std::move (strParam);
                }
                else
                {
                    return rpcError (rpcACT_MALFORMED);
                }
            }
            else
            {
                if (jvParseLedger (jvRequest, strParam))
                    return jvRequest;
                return rpcError (rpcLGR_IDX_MALFORMED);
            }
        }

        return jvRequest;
    }

    // TODO: Get index from an alternate syntax: rXYZ:<index>
    Json::Value parseAccountRaw1 (Json::Value const& jvParams)
    {
        std::string     strIdent    = jvParams[0u].asString ();
        unsigned int    iCursor     = jvParams.size ();
        bool            bStrict     = false;

        if (iCursor >= 2 && jvParams[iCursor - 1] == jss::strict)
        {
            bStrict = true;
            --iCursor;
        }

        if (! parseBase58<PublicKey>(TokenType::TOKEN_ACCOUNT_PUBLIC, strIdent) &&
            ! parseBase58<AccountID>(strIdent) &&
            ! parseGenericSeed(strIdent))
            return rpcError (rpcACT_MALFORMED);

        // Get info on account.
        Json::Value jvRequest (Json::objectValue);

        jvRequest[jss::account]    = strIdent;

        if (bStrict)
            jvRequest[jss::strict]     = 1;

        if (iCursor == 2 && !jvParseLedger (jvRequest, jvParams[1u].asString ()))
            return rpcError (rpcLGR_IDX_MALFORMED);

        return jvRequest;
    }

    // ripple_path_find <json> [<ledger>]
    Json::Value parseRipplePathFind (Json::Value const& jvParams)
    {
        Json::Reader    reader;
        Json::Value     jvRequest{Json::objectValue};
        bool            bLedger     = 2 == jvParams.size ();

        JLOG (j_.trace()) << "RPC json: " << jvParams[0u];

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

            Json::Value jvRequest{Json::objectValue};

            jvRequest[jss::tx_blob]    = jvParams[0u].asString ();

            return jvRequest;
        }
        else if ((2 == jvParams.size () || bOffline)
                 && reader.parse (jvParams[1u].asString (), txJSON))
        {
            // Signing or submitting tx_json.
            Json::Value jvRequest{Json::objectValue};

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
        if (1 == jvParams.size ())
        {
            Json::Value     txJSON;
            Json::Reader    reader;
            if (reader.parse (jvParams[0u].asString (), txJSON))
            {
                Json::Value jvRequest{Json::objectValue};
                jvRequest[jss::tx_json] = txJSON;
                return jvRequest;
            }
        }

        return rpcError (rpcINVALID_PARAMS);
    }

    // transaction_entry <tx_hash> <ledger_hash/ledger_index>
    Json::Value parseTransactionEntry (Json::Value const& jvParams)
    {
        // Parameter count should have already been verified.
        assert (jvParams.size() == 2);

        std::string const txHash = jvParams[0u].asString();
        if (txHash.length() != 64)
            return rpcError (rpcINVALID_PARAMS);

        Json::Value jvRequest{Json::objectValue};
        jvRequest[jss::tx_hash] = txHash;

        jvParseLedger (jvRequest, jvParams[1u].asString());

        // jvParseLedger inserts a "ledger_index" of 0 if it doesn't
        // find a match.
        if (jvRequest.isMember(jss::ledger_index) &&
            jvRequest[jss::ledger_index] == 0)
                return rpcError (rpcINVALID_PARAMS);

        return jvRequest;
    }


    // tx <transaction_id>
    Json::Value parseTx (Json::Value const& jvParams)
    {
        Json::Value jvRequest{Json::objectValue};

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
        Json::Value jvRequest{Json::objectValue};

        jvRequest[jss::start]  = jvParams[0u].asUInt ();

        return jvRequest;
    }

    // validation_create [<pass_phrase>|<seed>|<seed_key>]
    //
    // NOTE: It is poor security to specify secret information on the command line.  This information might be saved in the command
    // shell history file (e.g. .bash_history) and it may be leaked via the process status command (i.e. ps).
    Json::Value parseValidationCreate (Json::Value const& jvParams)
    {
        Json::Value jvRequest{Json::objectValue};

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
        Json::Value jvRequest{Json::objectValue};

        if (jvParams.size ())
            jvRequest[jss::secret]     = jvParams[0u].asString ();

        return jvRequest;
    }

    // wallet_propose [<passphrase>]
    // <passphrase> is only for testing. Master seeds should only be generated randomly.
    Json::Value parseWalletPropose (Json::Value const& jvParams)
    {
        Json::Value jvRequest{Json::objectValue};

        if (jvParams.size ())
            jvRequest[jss::passphrase]     = jvParams[0u].asString ();

        return jvRequest;
    }

    // wallet_seed [<seed>|<passphrase>|<passkey>]
    Json::Value parseWalletSeed (Json::Value const& jvParams)
    {
        Json::Value jvRequest{Json::objectValue};

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

        Json::Value jvRequest{Json::objectValue};

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

    explicit
    RPCParser (beast::Journal j)
            :j_ (j){}

    //--------------------------------------------------------------------------

    // Convert a rpc method and params to a request.
    // <-- { method: xyz, params: [... ] } or { error: ..., ... }
    Json::Value parseCommand (std::string strMethod, Json::Value jvParams, bool allowAnyCommand)
    {
        if (auto stream = j_.trace())
        {
            stream << "Method: '" << strMethod << "'";
            stream << "Params: " << jvParams;
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
            {   "account_channels",     &RPCParser::parseAccountChannels,       1,  3   },
            {   "account_objects",      &RPCParser::parseAccountItems,          1,  5   },
            {   "account_offers",       &RPCParser::parseAccountItems,          1,  4   },
            {   "account_tx",           &RPCParser::parseAccountTransactions,   1,  8   },
            {   "book_offers",          &RPCParser::parseBookOffers,            2,  7   },
            {   "can_delete",           &RPCParser::parseCanDelete,             0,  1   },
            {   "channel_authorize",    &RPCParser::parseChannelAuthorize,      3,  3   },
            {   "channel_verify",       &RPCParser::parseChannelVerify,         4,  4   },
            {   "connect",              &RPCParser::parseConnect,               1,  2   },
            {   "consensus_info",       &RPCParser::parseAsIs,                  0,  0   },
            {   "feature",              &RPCParser::parseFeature,               0,  2   },
            {   "fetch_info",           &RPCParser::parseFetchInfo,             0,  1   },
            {   "gateway_balances",     &RPCParser::parseGatewayBalances  ,     1,  -1  },
            {   "get_counts",           &RPCParser::parseGetCounts,             0,  1   },
            {   "json",                 &RPCParser::parseJson,                  2,  2   },
            {   "json2",                &RPCParser::parseJson2,                 1,  1   },
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
            {   "sign_for",             &RPCParser::parseSignFor,               3,  4   },
            {   "submit",               &RPCParser::parseSignSubmit,            1,  3   },
            {   "submit_multisigned",   &RPCParser::parseSubmitMultiSigned,     1,  1   },
            {   "server_info",          &RPCParser::parseAsIs,                  0,  0   },
            {   "server_state",         &RPCParser::parseAsIs,                  0,  0   },
            {   "stop",                 &RPCParser::parseAsIs,                  0,  0   },
            {   "transaction_entry",    &RPCParser::parseTransactionEntry,      2,  2   },
            {   "tx",                   &RPCParser::parseTx,                    1,  2   },
            {   "tx_account",           &RPCParser::parseTxAccount,             1,  7   },
            {   "tx_history",           &RPCParser::parseTxHistory,             1,  1   },
            {   "unl_list",             &RPCParser::parseAsIs,                  0,  0   },
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
                    JLOG (j_.debug()) <<
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
                std::string const& strData, beast::Journal j)
    {
        if (callbackFuncP)
        {
            // Only care about the result, if we care to deliver it callbackFuncP.

            // Receive reply
            if (iStatus == 401)
                Throw<std::runtime_error> (
                    "incorrect rpcuser or rpcpassword (authorization failed)");
            else if ((iStatus >= 400) && (iStatus != 400) && (iStatus != 404) && (iStatus != 500)) // ?
                Throw<std::runtime_error> (
                    std::string ("server returned HTTP error ") +
                        std::to_string (iStatus));
            else if (strData.empty ())
                Throw<std::runtime_error> ("no response from server");

            // Parse reply
            JLOG (j.debug()) << "RPC reply: " << strData << std::endl;

            Json::Reader    reader;
            Json::Value     jvReply;
            if (!reader.parse (strData, jvReply))
                Throw<std::runtime_error> ("couldn't parse reply from server");

            if (!jvReply)
                Throw<std::runtime_error> ("expected reply to have result, error and id properties");

            Json::Value     jvResult (Json::objectValue);

            jvResult["result"] = jvReply;

            (callbackFuncP) (jvResult);
        }

        return false;
    }

    // Build the request.
    static void onRequest (std::string const& strMethod, Json::Value const& jvParams,
        const std::map<std::string, std::string>& mHeaders, std::string const& strPath,
            boost::asio::streambuf& sb, std::string const& strHost, beast::Journal j)
    {
        JLOG (j.debug()) << "requestRPC: strPath='" << strPath << "'";

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

// Used internally by rpcClient.
static Json::Value
rpcCmdLineToJson (std::vector<std::string> const& args,
    Json::Value& retParams, beast::Journal j)
{
    Json::Value jvRequest (Json::objectValue);

    RPCParser   rpParser (j);
    Json::Value jvRpcParams (Json::arrayValue);

    for (int i = 1; i != args.size (); i++)
        jvRpcParams.append (args[i]);

    retParams = Json::Value (Json::objectValue);

    retParams[jss::method] = args[0];
    retParams[jss::params] = jvRpcParams;

    jvRequest   = rpParser.parseCommand (args[0], jvRpcParams, true);

    JLOG (j.trace()) << "RPC Request: " << jvRequest << std::endl;
    return jvRequest;
}

Json::Value
cmdLineToJSONRPC (std::vector<std::string> const& args, beast::Journal j)
{
    Json::Value jv = Json::Value (Json::objectValue);
    auto const paramsObj = rpcCmdLineToJson (args, jv, j);

    // Re-use jv to return our formatted result.
    jv.clear();

    // Allow parser to rewrite method.
    jv[jss::method] = paramsObj.isMember (jss::method) ?
        paramsObj[jss::method].asString() : args[0];

    // If paramsObj is not empty, put it in a [params] array.
    if (paramsObj.begin() != paramsObj.end())
    {
        auto& paramsArray = Json::setArray (jv, jss::params);
        paramsArray.append (paramsObj);
    }
    if (paramsObj.isMember(jss::jsonrpc))
        jv[jss::jsonrpc] = paramsObj[jss::jsonrpc];
    if (paramsObj.isMember(jss::ripplerpc))
        jv[jss::ripplerpc] = paramsObj[jss::ripplerpc];
    if (paramsObj.isMember(jss::id))
        jv[jss::id] = paramsObj[jss::id];
    return jv;
}

//------------------------------------------------------------------------------

std::pair<int, Json::Value>
rpcClient(std::vector<std::string> const& args,
    Config const& config, Logs& logs)
{
    static_assert(rpcBAD_SYNTAX == 1 && rpcSUCCESS == 0,
        "Expect specific rpc enum values.");
    if (args.empty ())
        return { rpcBAD_SYNTAX, {} }; // rpcBAD_SYNTAX = print usage

    int         nRet = rpcSUCCESS;
    Json::Value jvOutput;
    Json::Value jvRequest (Json::objectValue);

    try
    {
        Json::Value jvRpc   = Json::Value (Json::objectValue);
        jvRequest = rpcCmdLineToJson (args, jvRpc, logs.journal ("RPCParser"));

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
                setup = setup_ServerHandler(
                    config,
                    beast::logstream { logs.journal ("HTTPClient").warn() });
            }
            catch (std::exception const&)
            {
                // ignore any exceptions, so the command
                // line client works without a config file
            }

            if (config.rpc_ip)
                setup.client.ip = config.rpc_ip->to_string();
            if (config.rpc_port)
                setup.client.port = *config.rpc_port;

            Json::Value jvParams (Json::arrayValue);

            if (!setup.client.admin_user.empty ())
                jvRequest["admin_user"] = setup.client.admin_user;

            if (!setup.client.admin_password.empty ())
                jvRequest["admin_password"] = setup.client.admin_password;

            if (jvRequest.isObject())
                jvParams.append (jvRequest);
            else if (jvRequest.isArray())
            {
                for (Json::UInt i = 0; i < jvRequest.size(); ++i)
                    jvParams.append(jvRequest[i]);
            }

            {
                boost::asio::io_service isService;
                RPCCall::fromNetwork (
                    isService,
                    setup.client.ip,
                    setup.client.port,
                    setup.client.user,
                    setup.client.password,
                    "",
                    jvRequest.isMember (jss::method)           // Allow parser to rewrite method.
                        ? jvRequest[jss::method].asString ()
                        : jvRequest.isArray()
                           ? "batch"
                           : args[0],
                    jvParams,                               // Parsed, execute.
                    setup.client.secure != 0,                // Use SSL
                    config.quiet(),
                    logs,
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

            // If had an error, supply invocation in result.
            if (jvOutput.isMember (jss::error))
            {
                jvOutput["rpc"]             = jvRpc;            // How the command was seen as method + params.
                jvOutput["request_sent"]    = jvRequest;        // How the command was translated.
            }
        }

        if (jvOutput.isMember (jss::error))
        {
            jvOutput[jss::status]  = "error";
            if (jvOutput.isMember(jss::error_code))
                nRet = std::stoi(jvOutput[jss::error_code].asString());
            else if (jvOutput[jss::error].isMember(jss::error_code))
                nRet = std::stoi(jvOutput[jss::error][jss::error_code].asString());
            else
                nRet = rpcBAD_SYNTAX;
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

    return { nRet, std::move(jvOutput) };
}

//------------------------------------------------------------------------------

namespace RPCCall {

int fromCommandLine (
    Config const& config,
    const std::vector<std::string>& vCmd,
    Logs& logs)
{
    auto const result = rpcClient(vCmd, config, logs);

    if (result.first != rpcBAD_SYNTAX)
        std::cout << result.second.toStyledString ();

    return result.first;
}

//------------------------------------------------------------------------------

void fromNetwork (
    boost::asio::io_service& io_service,
    std::string const& strIp, const std::uint16_t iPort,
    std::string const& strUsername, std::string const& strPassword,
    std::string const& strPath, std::string const& strMethod,
    Json::Value const& jvParams, const bool bSSL, const bool quiet,
    Logs& logs,
    std::function<void (Json::Value const& jvInput)> callbackFuncP)
{
    auto j = logs.journal ("HTTPClient");

    // Connect to localhost
    if (!quiet)
    {
        JLOG(j.info()) << (bSSL ? "Securely connecting to " : "Connecting to ") <<
            strIp << ":" << iPort << std::endl;
    }

    // HTTP basic authentication
    auto const auth = beast::detail::base64_encode(strUsername + ":" + strPassword);

    std::map<std::string, std::string> mapRequestHeaders;

    mapRequestHeaders["Authorization"] = std::string ("Basic ") + auth;

    // Send request

    // Number of bytes to try to receive if no
    // Content-Length header received
    const int RPC_REPLY_MAX_BYTES (256*1024*1024);

    using namespace std::chrono_literals;
    auto constexpr RPC_NOTIFY = 10min;

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
            strPath, std::placeholders::_1, std::placeholders::_2, j),
        RPC_REPLY_MAX_BYTES,
        RPC_NOTIFY,
        std::bind (&RPCCallImp::onResponse, callbackFuncP,
                   std::placeholders::_1, std::placeholders::_2,
                   std::placeholders::_3, j),
        j);
}

} // RPCCall

} // ripple
