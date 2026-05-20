#include <xrpld/rpc/RPCCall.h>

#include <xrpld/core/Config.h>
#include <xrpld/rpc/ServerHandler.h>

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base64.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/net/HTTPClient.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/tokens.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/regex/v5/regex.hpp>
#include <boost/regex/v5/regex_match.hpp>
#include <boost/system/detail/error_code.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace xrpl {

class RPCParser;

//
// HTTP protocol
//
// This ain't Apache.  We're just using HTTP header for the length field
// and to be compatible with other JSON-RPC implementations.
//

std::string
createHTTPPost(
    std::string const& strHost,
    std::string const& strPath,
    std::string const& strMsg,
    std::unordered_map<std::string, std::string> const& mapRequestHeaders)
{
    std::ostringstream s;

    // CHECKME this uses a different version than the replies below use. Is
    //         this by design or an accident or should it be using
    //         BuildInfo::getFullVersionString () as well?

    s << "POST " << (strPath.empty() ? "/" : strPath) << " HTTP/1.0\r\n"
      << "User-Agent: " << systemName() << "-json-rpc/v1\r\n"
      << "Host: " << strHost << "\r\n"
      << "Content-Type: application/json\r\n"
      << "Content-Length: " << strMsg.size() << "\r\n"
      << "Accept: application/json\r\n";

    for (auto const& [k, v] : mapRequestHeaders)
        s << k << ": " << v << "\r\n";

    s << "\r\n" << strMsg;

    return s.str();
}

class RPCParser
{
private:
    unsigned const apiVersion_;
    beast::Journal const j_;

    // TODO New routine for parsing ledger parameters, other routines should
    // standardize on this.
    static bool
    jvParseLedger(json::Value& jvRequest, std::string const& strLedger)
    {
        if (strLedger == "current" || strLedger == "closed" || strLedger == "validated")
        {
            jvRequest[jss::ledger_index] = strLedger;
        }
        else if (strLedger.length() == 64)
        {
            // YYY Could confirm this is a uint256.
            jvRequest[jss::ledger_hash] = strLedger;
        }
        else
        {
            jvRequest[jss::ledger_index] = beast::lexicalCast<std::uint32_t>(strLedger);
        }

        return true;
    }

    // Build a object { "currency" : "XYZ", "issuer" : "rXYX" }
    static json::Value
    jvParseCurrencyIssuer(std::string const& strCurrencyIssuer)
    {
        // Matches a sequence of 3 characters from
        // `xrpl::detail::isoCharSet` (the currency),
        // optionally followed by a forward slash and some other characters
        // (the issuer).
        // https://www.boost.org/doc/libs/1_82_0/libs/regex/doc/html/boost_regex/syntax/perl_syntax.html
        static boost::regex const kReCurIss("\\`([][:alnum:]<>(){}[|?!@#$%^&*]{3})(?:/(.+))?\\'");

        boost::smatch smMatch;

        if (boost::regex_match(strCurrencyIssuer, smMatch, kReCurIss))
        {
            json::Value jvResult(json::ValueType::Object);
            std::string const strCurrency = smMatch[1];
            std::string const strIssuer = smMatch[2];

            jvResult[jss::currency] = strCurrency;

            if (!strIssuer.empty())
            {
                // Could confirm issuer is a valid XRPL address.
                jvResult[jss::issuer] = strIssuer;
            }

            return jvResult;
        }

        return RPC::makeParamError(
            std::string("Invalid currency/issuer '") + strCurrencyIssuer + "'");
    }

    static bool
    validPublicKey(std::string const& strPk, TokenType type = TokenType::AccountPublic)
    {
        if (parseBase58<xrpl::PublicKey>(type, strPk))
            return true;

        auto pkHex = strUnHex(strPk);
        if (!pkHex)
            return false;

        if (!publicKeyType(makeSlice(*pkHex)))
            return false;

        return true;
    }

private:
    using parseFuncPtr = json::Value (RPCParser::*)(json::Value const& jvParams);

    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseAsIs(json::Value const& jvParams)
    {
        json::Value v(json::ValueType::Object);

        if (jvParams.isArray() && (jvParams.size() > 0))
            v[jss::params] = jvParams;

        return v;
    }

    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseInternal(json::Value const& jvParams)
    {
        json::Value v(json::ValueType::Object);
        v[jss::internal_command] = jvParams[0u];

        json::Value params(json::ValueType::Array);

        for (unsigned i = 1; i < jvParams.size(); ++i)
            params.append(jvParams[i]);

        v[jss::params] = params;

        return v;
    }

    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseManifest(json::Value const& jvParams)
    {
        if (jvParams.size() == 1)
        {
            json::Value jvRequest(json::ValueType::Object);

            std::string const strPk = jvParams[0u].asString();
            if (!validPublicKey(strPk, TokenType::NodePublic))
                return rpcError(RpcPublicMalformed);

            jvRequest[jss::public_key] = strPk;

            return jvRequest;
        }

        return rpcError(RpcInvalidParams);
    }

    // fetch_info [clear]
    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseFetchInfo(json::Value const& jvParams)
    {
        json::Value jvRequest(json::ValueType::Object);
        unsigned int const iParams = jvParams.size();

        if (iParams != 0)
            jvRequest[jvParams[0u].asString()] = true;

        return jvRequest;
    }

    // account_tx accountID [ledger_min [ledger_max [limit [offset]]]] [binary]
    // [count] [descending]
    json::Value
    // NOLINTNEXTLINE(readability-make-member-function-const)
    parseAccountTransactions(json::Value const& jvParams)
    {
        json::Value jvRequest(json::ValueType::Object);
        unsigned int iParams = jvParams.size();

        auto const account = parseBase58<AccountID>(jvParams[0u].asString());
        if (!account)
            return rpcError(RpcActMalformed);

        jvRequest[jss::account] = toBase58(*account);

        bool bDone = false;

        while (!bDone && iParams >= 2)
        {
            // VFALCO Why is json::StaticString appearing on the right side?
            if (jvParams[iParams - 1].asString() == jss::binary)
            {
                jvRequest[jss::binary] = true;
                --iParams;
            }
            else if (jvParams[iParams - 1].asString() == jss::count)
            {
                jvRequest[jss::count] = true;
                --iParams;
            }
            else if (jvParams[iParams - 1].asString() == jss::descending)
            {
                jvRequest[jss::descending] = true;
                --iParams;
            }
            else
            {
                bDone = true;
            }
        }

        if (1 == iParams)
        {
        }
        else if (2 == iParams)
        {
            if (!jvParseLedger(jvRequest, jvParams[1u].asString()))
                return jvRequest;
        }
        else
        {
            std::int64_t const uLedgerMin = jvParams[1u].asInt();
            std::int64_t const uLedgerMax = jvParams[2u].asInt();

            if (uLedgerMax != -1 && uLedgerMax < uLedgerMin)
            {
                if (apiVersion_ == 1)
                    return rpcError(RpcLgrIdxsInvalid);
                return rpcError(RpcNotSynced);
            }

            jvRequest[jss::ledger_index_min] = jvParams[1u].asInt();
            jvRequest[jss::ledger_index_max] = jvParams[2u].asInt();

            if (iParams >= 4)
                jvRequest[jss::limit] = jvParams[3u].asInt();

            if (iParams >= 5)
                jvRequest[jss::offset] = jvParams[4u].asInt();
        }

        return jvRequest;
    }

    // book_offers <taker_pays> <taker_gets> [<taker> [<ledger> [<limit>
    // [<proof> [<marker>]]]]] limit: 0 = no limit proof: 0 or 1
    //
    // Mnemonic: taker pays --> offer --> taker gets
    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseBookOffers(json::Value const& jvParams)
    {
        json::Value jvRequest(json::ValueType::Object);

        json::Value jvTakerPays = jvParseCurrencyIssuer(jvParams[0u].asString());
        json::Value jvTakerGets = jvParseCurrencyIssuer(jvParams[1u].asString());

        if (isRpcError(jvTakerPays))
        {
            return jvTakerPays;
        }

        jvRequest[jss::taker_pays] = jvTakerPays;

        if (isRpcError(jvTakerGets))
        {
            return jvTakerGets;
        }

        jvRequest[jss::taker_gets] = jvTakerGets;

        if (jvParams.size() >= 3)
        {
            jvRequest[jss::issuer] = jvParams[2u].asString();
        }

        if (jvParams.size() >= 4 && !jvParseLedger(jvRequest, jvParams[3u].asString()))
            return jvRequest;

        if (jvParams.size() >= 5)
        {
            try
            {
                int const iLimit = jvParams[4u].asInt();

                if (iLimit > 0)
                    jvRequest[jss::limit] = iLimit;
            }
            catch (std::exception const&)
            {
                return RPC::invalidFieldError(jss::limit);
            }
        }

        if (jvParams.size() >= 6)
        {
            try
            {
                int const bProof = jvParams[5u].asInt();
                if (bProof != 0)
                    jvRequest[jss::proof] = true;
            }
            catch (std::exception const&)
            {
                return RPC::invalidFieldError(jss::proof);
            }
        }

        if (jvParams.size() == 7)
            jvRequest[jss::marker] = jvParams[6u];

        return jvRequest;
    }

    // can_delete [<ledgerid>|<ledgerhash>|now|always|never]
    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseCanDelete(json::Value const& jvParams)
    {
        json::Value jvRequest(json::ValueType::Object);

        if (jvParams.size() == 0u)
            return jvRequest;

        std::string const input = jvParams[0u].asString();
        if (input.find_first_not_of("0123456789") == std::string::npos)
        {
            jvRequest["can_delete"] = jvParams[0u].asUInt();
        }
        else
        {
            jvRequest["can_delete"] = input;
        }

        return jvRequest;
    }

    // connect <ip[:port]> [port]
    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseConnect(json::Value const& jvParams)
    {
        json::Value jvRequest(json::ValueType::Object);
        std::string ip = jvParams[0u].asString();
        if (jvParams.size() == 2)
        {
            jvRequest[jss::ip] = ip;
            jvRequest[jss::port] = jvParams[1u].asUInt();
            return jvRequest;
        }

        // handle case where there is one argument of the form ip:port
        if (std::count(ip.begin(), ip.end(), ':') == 1)
        {
            std::size_t const colon = ip.find_last_of(':');
            jvRequest[jss::ip] = std::string{ip, 0, colon};
            jvRequest[jss::port] = json::Value{std::string{ip, colon + 1}}.asUInt();
            return jvRequest;
        }

        // default case, no port
        jvRequest[jss::ip] = ip;
        return jvRequest;
    }

    // deposit_authorized <source_account> <destination_account>
    // [<ledger> [<credentials>, ...]]
    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseDepositAuthorized(json::Value const& jvParams)
    {
        json::Value jvRequest(json::ValueType::Object);
        jvRequest[jss::source_account] = jvParams[0u].asString();
        jvRequest[jss::destination_account] = jvParams[1u].asString();

        if (jvParams.size() >= 3)
            jvParseLedger(jvRequest, jvParams[2u].asString());

        // 8 credentials max
        if ((jvParams.size() >= 4) && (jvParams.size() <= 11))
        {
            jvRequest[jss::credentials] = json::Value(json::ValueType::Array);
            for (uint32_t i = 3; i < jvParams.size(); ++i)
                jvRequest[jss::credentials].append(jvParams[i].asString());
        }

        return jvRequest;
    }

    // Return an error for attempting to subscribe/unsubscribe via RPC.
    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseEvented(json::Value const& jvParams)
    {
        return rpcError(RpcNoEvents);
    }

    // feature [<feature>] [accept|reject]
    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseFeature(json::Value const& jvParams)
    {
        json::Value jvRequest(json::ValueType::Object);

        if (jvParams.size() > 0)
            jvRequest[jss::feature] = jvParams[0u].asString();

        if (jvParams.size() > 1)
        {
            auto const action = jvParams[1u].asString();

            // This may look reversed, but it's intentional: jss::vetoed
            // determines whether an amendment is vetoed - so "reject" means
            // that jss::vetoed is true.
            if (boost::iequals(action, "reject"))
            {
                jvRequest[jss::vetoed] = json::Value(true);
            }
            else if (boost::iequals(action, "accept"))
            {
                jvRequest[jss::vetoed] = json::Value(false);
            }
            else
            {
                return rpcError(RpcInvalidParams);
            }
        }

        return jvRequest;
    }

    // get_counts [<min_count>]
    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseGetCounts(json::Value const& jvParams)
    {
        json::Value jvRequest(json::ValueType::Object);

        if (jvParams.size() != 0u)
            jvRequest[jss::min_count] = jvParams[0u].asUInt();

        return jvRequest;
    }

    // sign_for <account> <secret> <json> offline
    // sign_for <account> <secret> <json>
    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseSignFor(json::Value const& jvParams)
    {
        bool const bOffline = 4 == jvParams.size() && jvParams[3u].asString() == "offline";

        if (3 == jvParams.size() || bOffline)
        {
            json::Value txJSON;
            json::Reader reader;
            if (reader.parse(jvParams[2u].asString(), txJSON))
            {
                // sign_for txJSON.
                json::Value jvRequest{json::ValueType::Object};

                jvRequest[jss::account] = jvParams[0u].asString();
                jvRequest[jss::secret] = jvParams[1u].asString();
                jvRequest[jss::tx_json] = txJSON;

                if (bOffline)
                    jvRequest[jss::offline] = true;

                return jvRequest;
            }
        }
        return rpcError(RpcInvalidParams);
    }

    // json <command> <json>
    json::Value
    parseJson(json::Value const& jvParams)
    {
        json::Reader reader;
        json::Value jvRequest;

        JLOG(j_.trace()) << "RPC method: " << jvParams[0u];
        JLOG(j_.trace()) << "RPC json: " << jvParams[1u];

        if (reader.parse(jvParams[1u].asString(), jvRequest))
        {
            if (!jvRequest.isObjectOrNull())
                return rpcError(RpcInvalidParams);

            jvRequest[jss::method] = jvParams[0u];

            return jvRequest;
        }

        return rpcError(RpcInvalidParams);
    }

    bool
    isValidJson2(json::Value const& jv)
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
                return !jv.isMember(jss::params) ||
                    (jv[jss::params].isNull() || jv[jss::params].isArray() ||
                     jv[jss::params].isObject());
            }
        }
        return false;
    }

    json::Value
    parseJson2(json::Value const& jvParams)
    {
        json::Reader reader;
        json::Value jv;
        bool const validParse = reader.parse(jvParams[0u].asString(), jv);
        if (validParse && isValidJson2(jv))
        {
            if (jv.isObject())
            {
                json::Value jv1{json::ValueType::Object};
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
            json::Value jv1{json::ValueType::Array};
            for (json::UInt j = 0; j < jv.size(); ++j)
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
        auto jvError = rpcError(RpcInvalidParams);
        if (jv.isMember(jss::jsonrpc))
            jvError[jss::jsonrpc] = jv[jss::jsonrpc];
        if (jv.isMember(jss::ripplerpc))
            jvError[jss::ripplerpc] = jv[jss::ripplerpc];
        if (jv.isMember(jss::id))
            jvError[jss::id] = jv[jss::id];
        return jvError;
    }

    // ledger [id|index|current|closed|validated] [full|tx]
    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseLedger(json::Value const& jvParams)
    {
        json::Value jvRequest(json::ValueType::Object);

        if (jvParams.size() == 0u)
        {
            return jvRequest;
        }

        jvParseLedger(jvRequest, jvParams[0u].asString());

        if (2 == jvParams.size())
        {
            if (jvParams[1u].asString() == "full")
            {
                jvRequest[jss::full] = true;
            }
            else if (jvParams[1u].asString() == "tx")
            {
                jvRequest[jss::transactions] = true;
                jvRequest[jss::expand] = true;
            }
        }

        return jvRequest;
    }

    // ledger_header <id>|<index>
    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseLedgerId(json::Value const& jvParams)
    {
        json::Value jvRequest(json::ValueType::Object);

        std::string const strLedger = jvParams[0u].asString();

        if (strLedger.length() == 64)
        {
            jvRequest[jss::ledger_hash] = strLedger;
        }
        else
        {
            jvRequest[jss::ledger_index] = beast::lexicalCast<std::uint32_t>(strLedger);
        }

        return jvRequest;
    }

    // ledger_entry [id] [<index>]
    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseLedgerEntry(json::Value const& jvParams)
    {
        json::Value jvRequest{json::ValueType::Object};

        jvRequest[jss::index] = jvParams[0u].asString();

        if (jvParams.size() == 2 && !jvParseLedger(jvRequest, jvParams[1u].asString()))
            return rpcError(RpcLgrIdxMalformed);

        return jvRequest;
    }

    // log_level:                           Get log levels
    // log_level <severity>:                Set master log level to the
    // specified severity log_level <partition> <severity>:    Set specified
    // partition to specified severity
    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseLogLevel(json::Value const& jvParams)
    {
        json::Value jvRequest(json::ValueType::Object);

        if (jvParams.size() == 1)
        {
            jvRequest[jss::severity] = jvParams[0u].asString();
        }
        else if (jvParams.size() == 2)
        {
            jvRequest[jss::partition] = jvParams[0u].asString();
            jvRequest[jss::severity] = jvParams[1u].asString();
        }

        return jvRequest;
    }

    // owner_info <account>
    // account_info <account> [<ledger>]
    // account_offers <account> [<ledger>]
    json::Value
    parseAccountItems(json::Value const& jvParams)
    {
        return parseAccountRaw1(jvParams);
    }

    json::Value
    parseAccountCurrencies(json::Value const& jvParams)
    {
        return parseAccountRaw1(jvParams);
    }

    // account_lines <account> <account>|"" [<ledger>]
    json::Value
    parseAccountLines(json::Value const& jvParams)
    {
        return parseAccountRaw2(jvParams, jss::peer);
    }

    // account_channels <account> <account>|"" [<ledger>]
    json::Value
    parseAccountChannels(json::Value const& jvParams)
    {
        return parseAccountRaw2(jvParams, jss::destination_account);
    }

    // channel_authorize: <private_key> [<key_type>] <channel_id> <drops>
    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseChannelAuthorize(json::Value const& jvParams)
    {
        json::Value jvRequest(json::ValueType::Object);

        unsigned int index = 0;

        if (jvParams.size() == 4)
        {
            jvRequest[jss::passphrase] = jvParams[index];
            index++;

            if (!keyTypeFromString(jvParams[index].asString()))
                return rpcError(RpcBadKeyType);
            jvRequest[jss::key_type] = jvParams[index];
            index++;
        }
        else
        {
            jvRequest[jss::secret] = jvParams[index];
            index++;
        }

        {
            // verify the channel id is a valid 256 bit number
            uint256 channelId;
            if (!channelId.parseHex(jvParams[index].asString()))
                return rpcError(RpcChannelMalformed);
            jvRequest[jss::channel_id] = to_string(channelId);
            index++;
        }

        if (!jvParams[index].isString() || !toUInt64(jvParams[index].asString()))
            return rpcError(RpcChannelAmtMalformed);
        jvRequest[jss::amount] = jvParams[index];

        // If additional parameters are appended, be sure to increment index
        // here

        return jvRequest;
    }

    // channel_verify <public_key> <channel_id> <drops> <signature>
    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseChannelVerify(json::Value const& jvParams)
    {
        std::string const strPk = jvParams[0u].asString();

        if (!validPublicKey(strPk))
            return rpcError(RpcPublicMalformed);

        json::Value jvRequest(json::ValueType::Object);

        jvRequest[jss::public_key] = strPk;
        {
            // verify the channel id is a valid 256 bit number
            uint256 channelId;
            if (!channelId.parseHex(jvParams[1u].asString()))
                return rpcError(RpcChannelMalformed);
        }
        jvRequest[jss::channel_id] = jvParams[1u].asString();

        if (!jvParams[2u].isString() || !toUInt64(jvParams[2u].asString()))
            return rpcError(RpcChannelAmtMalformed);
        jvRequest[jss::amount] = jvParams[2u];

        jvRequest[jss::signature] = jvParams[3u].asString();

        return jvRequest;
    }

    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseAccountRaw2(json::Value const& jvParams, char const* const acc2Field)
    {
        std::array<char const* const, 2> accFields{{jss::account, acc2Field}};
        auto const nParams = jvParams.size();
        json::Value jvRequest(json::ValueType::Object);
        for (auto i = 0; i < nParams; ++i)
        {
            // This was non-const. see comment below
            std::string const strParam = jvParams[i].asString();

            if (i == 1 && strParam.empty())
                continue;

            // Parameters 0 and 1 are accounts
            if (i < 2)
            {
                if (parseBase58<AccountID>(strParam))
                {
                    // TODO: this was std::move'd before but it does not work in practice.
                    // We would need a Value(std::string&&) for it to work.
                    // See https://github.com/XRPLF/rippled/issues/6677
                    jvRequest[accFields[i]] = strParam;
                }
                else
                {
                    return rpcError(RpcActMalformed);
                }
            }
            else
            {
                if (jvParseLedger(jvRequest, strParam))
                    return jvRequest;
                return rpcError(RpcLgrIdxMalformed);
            }
        }

        return jvRequest;
    }

    // TODO: Get index from an alternate syntax: rXYZ:<index>
    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseAccountRaw1(json::Value const& jvParams)
    {
        std::string const strIdent = jvParams[0u].asString();
        unsigned int const iCursor = jvParams.size();

        if (!parseBase58<AccountID>(strIdent))
            return rpcError(RpcActMalformed);

        // Get info on account.
        json::Value jvRequest(json::ValueType::Object);

        jvRequest[jss::account] = strIdent;

        if (iCursor == 2 && !jvParseLedger(jvRequest, jvParams[1u].asString()))
            return rpcError(RpcLgrIdxMalformed);

        return jvRequest;
    }

    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseVault(json::Value const& jvParams)
    {
        std::string const strVaultID = jvParams[0u].asString();
        uint256 id = beast::kZero;
        if (!id.parseHex(strVaultID))
            return rpcError(RpcInvalidParams);

        json::Value jvRequest(json::ValueType::Object);
        jvRequest[jss::vault_id] = strVaultID;

        if (jvParams.size() > 1)
            jvParseLedger(jvRequest, jvParams[1u].asString());

        return jvRequest;
    }

    // peer_reservations_add <public_key> [<name>]
    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parsePeerReservationsAdd(json::Value const& jvParams)
    {
        json::Value jvRequest;
        jvRequest[jss::public_key] = jvParams[0u].asString();
        if (jvParams.size() > 1)
        {
            jvRequest[jss::description] = jvParams[1u].asString();
        }
        return jvRequest;
    }

    // peer_reservations_del <public_key>
    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parsePeerReservationsDel(json::Value const& jvParams)
    {
        json::Value jvRequest;
        jvRequest[jss::public_key] = jvParams[0u].asString();
        return jvRequest;
    }

    // ripple_path_find <json> [<ledger>]
    json::Value
    parseRipplePathFind(json::Value const& jvParams)
    {
        json::Reader reader;
        json::Value jvRequest{json::ValueType::Object};
        bool const bLedger = 2 == jvParams.size();

        JLOG(j_.trace()) << "RPC json: " << jvParams[0u];

        if (reader.parse(jvParams[0u].asString(), jvRequest))
        {
            if (bLedger)
            {
                jvParseLedger(jvRequest, jvParams[1u].asString());
            }

            return jvRequest;
        }

        return rpcError(RpcInvalidParams);
    }

    // simulate any transaction on the network
    //
    // simulate <tx_blob> [binary]
    // simulate <tx_json> [binary]
    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseSimulate(json::Value const& jvParams)
    {
        json::Value txJSON;
        json::Reader reader;
        json::Value jvRequest{json::ValueType::Object};

        if (reader.parse(jvParams[0u].asString(), txJSON))
        {
            jvRequest[jss::tx_json] = txJSON;
        }
        else
        {
            jvRequest[jss::tx_blob] = jvParams[0u].asString();
        }

        if (jvParams.size() == 2)
        {
            if (!jvParams[1u].isString() || jvParams[1u].asString() != "binary")
                return rpcError(RpcInvalidParams);
            jvRequest[jss::binary] = true;
        }

        return jvRequest;
    }

    // sign/submit any transaction to the network
    //
    // sign <private_key> <json> offline
    // submit <private_key> <json>
    // submit <tx_blob>
    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseSignSubmit(json::Value const& jvParams)
    {
        json::Value txJSON;
        json::Reader reader;
        bool const bOffline = jvParams.size() >= 3 && jvParams[2u].asString() == "offline";
        std::optional<std::string> const field = [&jvParams,
                                                  bOffline]() -> std::optional<std::string> {
            if (jvParams.size() < 3)
                return std::nullopt;
            if (jvParams.size() < 4 && bOffline)
                return std::nullopt;
            json::UInt const index = bOffline ? 3u : 2u;
            return jvParams[index].asString();
        }();

        if (1 == jvParams.size())
        {
            // Submitting tx_blob

            json::Value jvRequest{json::ValueType::Object};

            jvRequest[jss::tx_blob] = jvParams[0u].asString();

            return jvRequest;
        }
        if ((jvParams.size() >= 2 || bOffline) && reader.parse(jvParams[1u].asString(), txJSON))
        {
            // Signing or submitting tx_json.
            json::Value jvRequest{json::ValueType::Object};

            jvRequest[jss::secret] = jvParams[0u].asString();
            jvRequest[jss::tx_json] = txJSON;

            if (bOffline)
                jvRequest[jss::offline] = true;

            if (field)
                jvRequest[jss::signature_target] = *field;

            return jvRequest;
        }

        return rpcError(RpcInvalidParams);
    }

    // submit any multisigned transaction to the network
    //
    // submit_multisigned <json>
    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseSubmitMultiSigned(json::Value const& jvParams)
    {
        if (1 == jvParams.size())
        {
            json::Value txJSON;
            json::Reader reader;
            if (reader.parse(jvParams[0u].asString(), txJSON))
            {
                json::Value jvRequest{json::ValueType::Object};
                jvRequest[jss::tx_json] = txJSON;
                return jvRequest;
            }
        }

        return rpcError(RpcInvalidParams);
    }

    // transaction_entry <tx_hash> <ledger_hash/ledger_index>
    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseTransactionEntry(json::Value const& jvParams)
    {
        // Parameter count should have already been verified.
        XRPL_ASSERT(
            jvParams.size() == 2, "xrpl::RPCParser::parseTransactionEntry : valid parameter count");

        std::string const txHash = jvParams[0u].asString();
        if (txHash.length() != 64)
            return rpcError(RpcInvalidParams);

        json::Value jvRequest{json::ValueType::Object};
        jvRequest[jss::tx_hash] = txHash;

        jvParseLedger(jvRequest, jvParams[1u].asString());

        // jvParseLedger inserts a "ledger_index" of 0 if it doesn't
        // find a match.
        if (jvRequest.isMember(jss::ledger_index) && jvRequest[jss::ledger_index] == 0)
            return rpcError(RpcInvalidParams);

        return jvRequest;
    }

    // tx <transaction_id>
    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseTx(json::Value const& jvParams)
    {
        json::Value jvRequest{json::ValueType::Object};

        if (jvParams.size() == 2 || jvParams.size() == 4)
        {
            if (jvParams[1u].asString() == jss::binary)
                jvRequest[jss::binary] = true;
        }

        if (jvParams.size() >= 3)
        {
            auto const offset = jvParams.size() == 3 ? 0 : 1;

            jvRequest[jss::min_ledger] = jvParams[1u + offset].asString();
            jvRequest[jss::max_ledger] = jvParams[2u + offset].asString();
        }

        if (jvParams[0u].asString().length() == 16)
        {
            jvRequest[jss::ctid] = jvParams[0u].asString();
        }
        else
        {
            jvRequest[jss::transaction] = jvParams[0u].asString();
        }

        return jvRequest;
    }

    // tx_history <index>
    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseTxHistory(json::Value const& jvParams)
    {
        json::Value jvRequest{json::ValueType::Object};

        jvRequest[jss::start] = jvParams[0u].asUInt();

        return jvRequest;
    }

    // validation_create [<pass_phrase>|<seed>|<seed_key>]
    //
    // NOTE: It is poor security to specify secret information on the command
    // line.  This information might be saved in the command shell history file
    // (e.g. .bash_history) and it may be leaked via the process status command
    // (i.e. ps).
    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseValidationCreate(json::Value const& jvParams)
    {
        json::Value jvRequest{json::ValueType::Object};

        if (jvParams.size() != 0u)
            jvRequest[jss::secret] = jvParams[0u].asString();

        return jvRequest;
    }

    // wallet_propose [<passphrase>]
    // <passphrase> is only for testing. Master seeds should only be generated
    // randomly.
    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseWalletPropose(json::Value const& jvParams)
    {
        json::Value jvRequest{json::ValueType::Object};

        if (jvParams.size() != 0u)
            jvRequest[jss::passphrase] = jvParams[0u].asString();

        return jvRequest;
    }

    // parse gateway balances
    // gateway_balances [<ledger>] <issuer_account> [ <hotwallet> [ <hotwallet>
    // ]]

    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseGatewayBalances(json::Value const& jvParams)
    {
        unsigned int index = 0;
        unsigned int const size = jvParams.size();

        json::Value jvRequest{json::ValueType::Object};

        std::string param = jvParams[index++].asString();
        if (param.empty())
            return RPC::makeParamError("Invalid first parameter");

        if (param[0] != 'r')
        {
            if (param.size() == 64)
            {
                jvRequest[jss::ledger_hash] = param;
            }
            else
            {
                jvRequest[jss::ledger_index] = param;
            }

            if (size <= index)
                return RPC::makeParamError("Invalid hotwallet");

            param = jvParams[index++].asString();
        }

        jvRequest[jss::account] = param;

        if (index < size)
        {
            json::Value& hotWallets = (jvRequest["hotwallet"] = json::ValueType::Array);
            while (index < size)
                hotWallets.append(jvParams[index++].asString());
        }

        return jvRequest;
    }

    // server_definitions [hash]
    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseServerDefinitions(json::Value const& jvParams)
    {
        json::Value jvRequest{json::ValueType::Object};

        if (jvParams.size() == 1)
        {
            jvRequest[jss::hash] = jvParams[0u].asString();
        }

        return jvRequest;
    }

    // server_info [counters]
    json::Value
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    parseServerInfo(json::Value const& jvParams)
    {
        json::Value jvRequest(json::ValueType::Object);
        if (jvParams.size() == 1 && jvParams[0u].asString() == "counters")
            jvRequest[jss::counters] = true;
        return jvRequest;
    }

public:
    //--------------------------------------------------------------------------

    explicit RPCParser(unsigned apiVersion, beast::Journal j) : apiVersion_(apiVersion), j_(j)
    {
    }

    //--------------------------------------------------------------------------

    // Convert a rpc method and params to a request.
    // <-- { method: xyz, params: [... ] } or { error: ..., ... }
    json::Value
    parseCommand(std::string strMethod, json::Value jvParams, bool allowAnyCommand)
    {
        if (auto stream = j_.trace())
        {
            stream << "Method: '" << strMethod << "'";
            stream << "Params: " << jvParams;
        }

        struct Command
        {
            char const* name;
            parseFuncPtr parse;
            int minParams;
            int maxParams;
        };

        static constexpr Command kCommands[] = {
            // Request-response methods
            // - Returns an error, or the request.
            // - To modify the method, provide a new method in the request.
            {.name = "account_currencies",
             .parse = &RPCParser::parseAccountCurrencies,
             .minParams = 1,
             .maxParams = 3},
            {.name = "account_info",
             .parse = &RPCParser::parseAccountItems,
             .minParams = 1,
             .maxParams = 3},
            {.name = "account_lines",
             .parse = &RPCParser::parseAccountLines,
             .minParams = 1,
             .maxParams = 5},
            {.name = "account_channels",
             .parse = &RPCParser::parseAccountChannels,
             .minParams = 1,
             .maxParams = 3},
            {.name = "account_nfts",
             .parse = &RPCParser::parseAccountItems,
             .minParams = 1,
             .maxParams = 5},
            {.name = "account_objects",
             .parse = &RPCParser::parseAccountItems,
             .minParams = 1,
             .maxParams = 5},
            {.name = "account_offers",
             .parse = &RPCParser::parseAccountItems,
             .minParams = 1,
             .maxParams = 4},
            {.name = "account_tx",
             .parse = &RPCParser::parseAccountTransactions,
             .minParams = 1,
             .maxParams = 8},
            {.name = "amm_info", .parse = &RPCParser::parseAsIs, .minParams = 1, .maxParams = 2},
            {.name = "vault_info", .parse = &RPCParser::parseVault, .minParams = 1, .maxParams = 2},
            {.name = "book_changes",
             .parse = &RPCParser::parseLedgerId,
             .minParams = 1,
             .maxParams = 1},
            {.name = "book_offers",
             .parse = &RPCParser::parseBookOffers,
             .minParams = 2,
             .maxParams = 7},
            {.name = "can_delete",
             .parse = &RPCParser::parseCanDelete,
             .minParams = 0,
             .maxParams = 1},
            {.name = "channel_authorize",
             .parse = &RPCParser::parseChannelAuthorize,
             .minParams = 3,
             .maxParams = 4},
            {.name = "channel_verify",
             .parse = &RPCParser::parseChannelVerify,
             .minParams = 4,
             .maxParams = 4},
            {.name = "connect", .parse = &RPCParser::parseConnect, .minParams = 1, .maxParams = 2},
            {.name = "consensus_info",
             .parse = &RPCParser::parseAsIs,
             .minParams = 0,
             .maxParams = 0},
            {.name = "deposit_authorized",
             .parse = &RPCParser::parseDepositAuthorized,
             .minParams = 2,
             .maxParams = 11},
            {.name = "feature", .parse = &RPCParser::parseFeature, .minParams = 0, .maxParams = 2},
            {.name = "fetch_info",
             .parse = &RPCParser::parseFetchInfo,
             .minParams = 0,
             .maxParams = 1},
            {.name = "gateway_balances",
             .parse = &RPCParser::parseGatewayBalances,
             .minParams = 1,
             .maxParams = -1},
            {.name = "get_counts",
             .parse = &RPCParser::parseGetCounts,
             .minParams = 0,
             .maxParams = 1},
            {.name = "json", .parse = &RPCParser::parseJson, .minParams = 2, .maxParams = 2},
            {.name = "json2", .parse = &RPCParser::parseJson2, .minParams = 1, .maxParams = 1},
            {.name = "ledger", .parse = &RPCParser::parseLedger, .minParams = 0, .maxParams = 2},
            {.name = "ledger_accept",
             .parse = &RPCParser::parseAsIs,
             .minParams = 0,
             .maxParams = 0},
            {.name = "ledger_closed",
             .parse = &RPCParser::parseAsIs,
             .minParams = 0,
             .maxParams = 0},
            {.name = "ledger_current",
             .parse = &RPCParser::parseAsIs,
             .minParams = 0,
             .maxParams = 0},
            {.name = "ledger_entry",
             .parse = &RPCParser::parseLedgerEntry,
             .minParams = 1,
             .maxParams = 2},
            {.name = "ledger_header",
             .parse = &RPCParser::parseLedgerId,
             .minParams = 1,
             .maxParams = 1},
            {.name = "ledger_request",
             .parse = &RPCParser::parseLedgerId,
             .minParams = 1,
             .maxParams = 1},
            {.name = "log_level",
             .parse = &RPCParser::parseLogLevel,
             .minParams = 0,
             .maxParams = 2},
            {.name = "logrotate", .parse = &RPCParser::parseAsIs, .minParams = 0, .maxParams = 0},
            {.name = "manifest",
             .parse = &RPCParser::parseManifest,
             .minParams = 1,
             .maxParams = 1},
            {.name = "owner_info",
             .parse = &RPCParser::parseAccountItems,
             .minParams = 1,
             .maxParams = 3},
            {.name = "peers", .parse = &RPCParser::parseAsIs, .minParams = 0, .maxParams = 0},
            {.name = "ping", .parse = &RPCParser::parseAsIs, .minParams = 0, .maxParams = 0},
            {.name = "print", .parse = &RPCParser::parseAsIs, .minParams = 0, .maxParams = 1},
            //      {   "profile",              &RPCParser::parseProfile, 1,  9
            //      },
            {.name = "random", .parse = &RPCParser::parseAsIs, .minParams = 0, .maxParams = 0},
            {.name = "peer_reservations_add",
             .parse = &RPCParser::parsePeerReservationsAdd,
             .minParams = 1,
             .maxParams = 2},
            {.name = "peer_reservations_del",
             .parse = &RPCParser::parsePeerReservationsDel,
             .minParams = 1,
             .maxParams = 1},
            {.name = "peer_reservations_list",
             .parse = &RPCParser::parseAsIs,
             .minParams = 0,
             .maxParams = 0},
            {.name = "ripple_path_find",
             .parse = &RPCParser::parseRipplePathFind,
             .minParams = 1,
             .maxParams = 2},
            {.name = "server_definitions",
             .parse = &RPCParser::parseServerDefinitions,
             .minParams = 0,
             .maxParams = 1},
            {.name = "server_info",
             .parse = &RPCParser::parseServerInfo,
             .minParams = 0,
             .maxParams = 1},
            {.name = "server_state",
             .parse = &RPCParser::parseServerInfo,
             .minParams = 0,
             .maxParams = 1},
            {.name = "sign", .parse = &RPCParser::parseSignSubmit, .minParams = 2, .maxParams = 4},
            {.name = "sign_for", .parse = &RPCParser::parseSignFor, .minParams = 3, .maxParams = 4},
            {.name = "stop", .parse = &RPCParser::parseAsIs, .minParams = 0, .maxParams = 0},
            {.name = "simulate",
             .parse = &RPCParser::parseSimulate,
             .minParams = 1,
             .maxParams = 2},
            {.name = "submit",
             .parse = &RPCParser::parseSignSubmit,
             .minParams = 1,
             .maxParams = 4},
            {.name = "submit_multisigned",
             .parse = &RPCParser::parseSubmitMultiSigned,
             .minParams = 1,
             .maxParams = 1},
            {.name = "transaction_entry",
             .parse = &RPCParser::parseTransactionEntry,
             .minParams = 2,
             .maxParams = 2},
            {.name = "tx", .parse = &RPCParser::parseTx, .minParams = 1, .maxParams = 4},
            {.name = "tx_history",
             .parse = &RPCParser::parseTxHistory,
             .minParams = 1,
             .maxParams = 1},
            {.name = "unl_list", .parse = &RPCParser::parseAsIs, .minParams = 0, .maxParams = 0},
            {.name = "validation_create",
             .parse = &RPCParser::parseValidationCreate,
             .minParams = 0,
             .maxParams = 1},
            {.name = "validator_info",
             .parse = &RPCParser::parseAsIs,
             .minParams = 0,
             .maxParams = 0},
            {.name = "version", .parse = &RPCParser::parseAsIs, .minParams = 0, .maxParams = 0},
            {.name = "wallet_propose",
             .parse = &RPCParser::parseWalletPropose,
             .minParams = 0,
             .maxParams = 1},
            {.name = "internal",
             .parse = &RPCParser::parseInternal,
             .minParams = 1,
             .maxParams = -1},

            // Event methods
            {.name = "path_find",
             .parse = &RPCParser::parseEvented,
             .minParams = -1,
             .maxParams = -1},
            {.name = "subscribe",
             .parse = &RPCParser::parseEvented,
             .minParams = -1,
             .maxParams = -1},
            {.name = "unsubscribe",
             .parse = &RPCParser::parseEvented,
             .minParams = -1,
             .maxParams = -1},
        };

        auto const count = jvParams.size();

        for (auto const& command : kCommands)
        {
            if (strMethod == command.name)
            {
                if ((command.minParams >= 0 && count < command.minParams) ||
                    (command.maxParams >= 0 && count > command.maxParams))
                {
                    JLOG(j_.debug()) << "Wrong number of parameters for " << command.name
                                     << " minimum=" << command.minParams
                                     << " maximum=" << command.maxParams << " actual=" << count;

                    return rpcError(RpcBadSyntax);
                }

                return (this->*(command.parse))(jvParams);
            }
        }

        // The command could not be found
        if (!allowAnyCommand)
            return rpcError(RpcUnknownCommand);

        return parseAsIs(jvParams);
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

std::string
jsonrpcRequest(std::string const& strMethod, json::Value const& params, json::Value const& id)
{
    json::Value request;
    request[jss::method] = strMethod;
    request[jss::params] = params;
    request[jss::id] = id;
    return to_string(request) + "\n";
}

namespace {
// Special local exception type thrown when request can't be parsed.
class RequestNotParsable : public std::runtime_error
{
    using std::runtime_error::runtime_error;  // Inherit constructors
};
};  // namespace

struct RPCCallImp
{
    explicit RPCCallImp() = default;

    // VFALCO NOTE Is this a to-do comment or a doc comment?
    // Place the async result somewhere useful.
    static void
    callRPCHandler(json::Value* jvOutput, json::Value const& jvInput)
    {
        (*jvOutput) = jvInput;
    }

    static bool
    onResponse(
        std::function<void(json::Value const& jvInput)> callbackFuncP,
        boost::system::error_code const& ecResult,
        int iStatus,
        std::string const& strData,
        beast::Journal j)
    {
        if (callbackFuncP)
        {
            // Only care about the result, if we care to deliver it
            // callbackFuncP.

            // Receive reply
            if (strData.empty())
            {
                Throw<std::runtime_error>(
                    "no response from server. Please "
                    "ensure that the xrpld server is running in another "
                    "process.");
            }

            // Parse reply
            JLOG(j.debug()) << "RPC reply: " << strData << std::endl;
            if (strData.starts_with("Unable to parse request") ||
                strData.starts_with(jss::invalid_API_version.cStr()))
                Throw<RequestNotParsable>(strData);
            json::Reader reader;
            json::Value jvReply;
            if (!reader.parse(strData, jvReply))
                Throw<std::runtime_error>("couldn't parse reply from server");

            if (!jvReply)
                Throw<std::runtime_error>("expected reply to have result, error and id properties");

            json::Value jvResult(json::ValueType::Object);

            jvResult["result"] = jvReply;

            (callbackFuncP)(jvResult);
        }

        return false;
    }

    // Build the request.
    static void
    onRequest(
        std::string const& strMethod,
        json::Value const& jvParams,
        std::unordered_map<std::string, std::string> const& headers,
        std::string const& strPath,
        boost::asio::streambuf& sb,
        std::string const& strHost,
        beast::Journal j)
    {
        JLOG(j.debug()) << "requestRPC: strPath='" << strPath << "'";

        std::ostream osRequest(&sb);
        osRequest << createHTTPPost(
            strHost, strPath, jsonrpcRequest(strMethod, jvParams, json::Value(1)), headers);
    }
};

//------------------------------------------------------------------------------

// Used internally by rpcClient.
json::Value
rpcCmdToJson(
    std::vector<std::string> const& args,
    json::Value& retParams,
    unsigned int apiVersion,
    beast::Journal j)
{
    json::Value jvRequest(json::ValueType::Object);

    RPCParser rpParser(apiVersion, j);
    json::Value jvRpcParams(json::ValueType::Array);

    for (int i = 1; i != args.size(); i++)
        jvRpcParams.append(args[i]);

    retParams = json::Value(json::ValueType::Object);

    retParams[jss::method] = args[0];
    retParams[jss::params] = jvRpcParams;

    jvRequest = rpParser.parseCommand(args[0], jvRpcParams, true);

    auto insertApiVersion = [apiVersion](json::Value& jr) {
        if (jr.isObject() && !jr.isMember(jss::error) && !jr.isMember(jss::api_version))
        {
            jr[jss::api_version] = apiVersion;
        }
    };

    if (jvRequest.isObject())
    {
        insertApiVersion(jvRequest);
    }
    else if (jvRequest.isArray())
    {
        // NOLINTNEXTLINE(modernize-use-ranges)
        std::for_each(jvRequest.begin(), jvRequest.end(), insertApiVersion);
    }

    JLOG(j.trace()) << "RPC Request: " << jvRequest << std::endl;
    return jvRequest;
}

//------------------------------------------------------------------------------

std::pair<int, json::Value>
rpcClient(
    std::vector<std::string> const& args,
    Config const& config,
    Logs& logs,
    unsigned int apiVersion,
    std::unordered_map<std::string, std::string> const& headers)
{
    static_assert(RpcBadSyntax == 1 && RpcSuccess == 0, "Expect specific rpc enum values.");
    if (args.empty())
        return {RpcBadSyntax, {}};  // rpcBAD_SYNTAX = print usage

    int nRet = RpcSuccess;
    json::Value jvOutput;
    json::Value jvRequest(json::ValueType::Object);

    try
    {
        json::Value jvRpc = json::Value(json::ValueType::Object);
        jvRequest = rpcCmdToJson(args, jvRpc, apiVersion, logs.journal("RPCParser"));

        if (jvRequest.isMember(jss::error))
        {
            jvOutput = jvRequest;
            jvOutput["rpc"] = jvRpc;
        }
        else
        {
            xrpl::ServerHandler::Setup setup;
            try
            {
                beast::logstream rpcCallLog{logs.journal("HTTPClient").warn()};
                setup = setupServerHandler(config, rpcCallLog);
            }
            catch (std::exception const&)  // NOLINT(bugprone-empty-catch)
            {
                // ignore any exceptions, so the command
                // line client works without a config file
            }

            if (config.rpc_ip)
            {
                setup.client.ip = config.rpc_ip->address().to_string();
                setup.client.port = config.rpc_ip->port();
            }

            json::Value jvParams(json::ValueType::Array);

            if (!setup.client.admin_user.empty())
                jvRequest["admin_user"] = setup.client.admin_user;

            if (!setup.client.admin_password.empty())
                jvRequest["admin_password"] = setup.client.admin_password;

            if (jvRequest.isObject())
            {
                jvParams.append(jvRequest);
            }
            else if (jvRequest.isArray())
            {
                for (json::UInt i = 0; i < jvRequest.size(); ++i)
                    jvParams.append(jvRequest[i]);
            }

            {
                boost::asio::io_context isService;
                RPCCall::fromNetwork(
                    isService,
                    setup.client.ip,
                    setup.client.port,
                    setup.client.user,
                    setup.client.password,
                    "",
                    // Allow parser to rewrite method.
                    [&]() -> std::string {
                        if (jvRequest.isMember(jss::method))
                            return jvRequest[jss::method].asString();
                        return jvRequest.isArray() ? "batch" : args[0];
                    }(),
                    jvParams,                                    // Parsed, execute.
                    static_cast<int>(setup.client.secure) != 0,  // Use SSL
                    config.quiet(),
                    logs,
                    std::bind(RPCCallImp::callRPCHandler, &jvOutput, std::placeholders::_1),
                    headers);
                isService.run();  // This blocks until there are no more
                                  // outstanding async calls.
            }
            if (jvOutput.isMember("result"))
            {
                // Had a successful JSON-RPC 2.0 call.
                jvOutput = jvOutput["result"];

                // jvOutput may report a server side error.
                // It should report "status".
            }
            else
            {
                // Transport error.
                json::Value const jvRpcError = jvOutput;

                jvOutput = rpcError(RpcJsonRpc);
                jvOutput["result"] = jvRpcError;
            }

            // If had an error, supply invocation in result.
            if (jvOutput.isMember(jss::error))
            {
                jvOutput["rpc"] = jvRpc;  // How the command was seen as method + params.
                jvOutput["request_sent"] = jvRequest;  // How the command was translated.
            }
        }

        if (jvOutput.isMember(jss::error))
        {
            jvOutput[jss::status] = "error";
            if (jvOutput.isMember(jss::error_code))
            {
                nRet = std::stoi(jvOutput[jss::error_code].asString());
            }
            else if (jvOutput[jss::error].isMember(jss::error_code))
            {
                nRet = std::stoi(jvOutput[jss::error][jss::error_code].asString());
            }
            else
            {
                nRet = RpcBadSyntax;
            }
        }

        // YYY We could have a command line flag for single line output for
        // scripts. YYY We would intercept output here and simplify it.
    }
    catch (RequestNotParsable const& e)
    {
        jvOutput = rpcError(RpcInvalidParams);
        jvOutput["error_what"] = e.what();
        nRet = RpcInvalidParams;
    }
    catch (std::exception& e)
    {
        jvOutput = rpcError(RpcInternal);
        jvOutput["error_what"] = e.what();
        nRet = RpcInternal;
    }

    return {nRet, std::move(jvOutput)};
}

//------------------------------------------------------------------------------

namespace RPCCall {

int
fromCommandLine(Config const& config, std::vector<std::string> const& vCmd, Logs& logs)
{
    auto const result = rpcClient(vCmd, config, logs, RPC::kApiCommandLineVersion);

    std::cout << result.second.toStyledString();

    return result.first;
}

//------------------------------------------------------------------------------

void
fromNetwork(
    boost::asio::io_context& ioContext,
    std::string const& strIp,
    std::uint16_t const iPort,
    std::string const& strUsername,
    std::string const& strPassword,
    std::string const& strPath,
    std::string const& strMethod,
    json::Value const& jvParams,
    bool const bSSL,
    bool const quiet,
    Logs& logs,
    std::function<void(json::Value const& jvInput)> callbackFuncP,
    std::unordered_map<std::string, std::string> headers)
{
    auto j = logs.journal("HTTPClient");

    // Connect to localhost
    if (!quiet)
    {
        JLOG(j.info()) << (bSSL ? "Securely connecting to " : "Connecting to ") << strIp << ":"
                       << iPort << std::endl;
    }

    // HTTP basic authentication
    headers["Authorization"] =
        std::string("Basic ") + base64Encode(strUsername + ":" + strPassword);

    // Send request

    // Number of bytes to try to receive if no
    // Content-Length header received
    constexpr auto kRpcReplyMaxBytes = megabytes(256);

    using namespace std::chrono_literals;
    static constexpr auto kRpcWebhookTimeout = 30s;

    HTTPClient::request(
        bSSL,
        ioContext,
        strIp,
        iPort,
        std::bind(
            &RPCCallImp::onRequest,
            strMethod,
            jvParams,
            headers,
            strPath,
            std::placeholders::_1,
            std::placeholders::_2,
            j),
        kRpcReplyMaxBytes,
        kRpcWebhookTimeout,
        std::bind(
            &RPCCallImp::onResponse,
            callbackFuncP,
            std::placeholders::_1,
            std::placeholders::_2,
            std::placeholders::_3,
            j),
        j);
}

}  // namespace RPCCall

}  // namespace xrpl
