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

#include <unordered_map>
#include <utility>

#include <ripple/module/rpc/ErrorCodes.h>

namespace std {

template <>
struct hash <ripple::error_code_i>
{
    std::size_t operator() (ripple::error_code_i value) const
    {
        return value;
    }
};

}
namespace ripple {
namespace RPC {

namespace detail {

class ErrorCategory
{
public:
    typedef ripple::unordered_map <error_code_i, ErrorInfo> Map;

    ErrorCategory ()
        : m_unknown (rpcUNKNOWN, "unknown", "An unknown error code.")
    {
        add (rpcACT_BITCOIN,           "actBitcoin",        "Account is bitcoin address.");
        add (rpcACT_EXISTS,            "actExists",         "Account already exists.");
        add (rpcACT_MALFORMED,         "actMalformed",      "Account malformed.");
        add (rpcACT_NOT_FOUND,         "actNotFound",       "Account not found.");
        add (rpcBAD_BLOB,              "badBlob",           "Blob must be a non-empty hex string.");
        add (rpcBAD_FEATURE,           "badFeature",        "Feature unknown or invalid.");
        add (rpcBAD_ISSUER,            "badIssuer",         "Issuer account malformed.");
        add (rpcBAD_MARKET,            "badMarket",         "No such market.");
        add (rpcBAD_SECRET,            "badSecret",         "Secret does not match account.");
        add (rpcBAD_SEED,              "badSeed",           "Disallowed seed.");
        add (rpcBAD_SYNTAX,            "badSyntax",         "Syntax error.");
        add (rpcCOMMAND_MISSING,       "commandMissing",    "Missing command entry.");
        add (rpcDST_ACT_MALFORMED,     "dstActMalformed",   "Destination account is malformed.");
        add (rpcDST_ACT_MISSING,       "dstActMissing",     "Destination account does not exist.");
        add (rpcDST_AMT_MALFORMED,     "dstAmtMalformed",   "Destination amount/currency/issuer is malformed.");
        add (rpcDST_ISR_MALFORMED,     "dstIsrMalformed",   "Destination issuer is malformed.");
        add (rpcFAIL_GEN_DECRYPT,      "failGenDecrypt",    "Failed to decrypt generator.");
        add (rpcFORBIDDEN,             "forbidden",         "Bad credentials.");
        add (rpcGETS_ACT_MALFORMED,    "getsActMalformed",  "Gets account malformed.");
        add (rpcGETS_AMT_MALFORMED,    "getsAmtMalformed",  "Gets amount malformed.");
        add (rpcHIGH_FEE,              "highFee",           "Current transaction fee exceeds your limit.");
        add (rpcHOST_IP_MALFORMED,     "hostIpMalformed",   "Host IP is malformed.");
        add (rpcINSUF_FUNDS,           "insufFunds",        "Insufficient funds.");
        add (rpcINTERNAL,              "internal",          "Internal error.");
        add (rpcINVALID_PARAMS,        "invalidParams",     "Invalid parameters.");
        add (rpcJSON_RPC,              "json_rpc",          "JSON-RPC transport error.");
        add (rpcLGR_IDXS_INVALID,      "lgrIdxsInvalid",    "Ledger indexes invalid.");
        add (rpcLGR_IDX_MALFORMED,     "lgrIdxMalformed",   "Ledger index malformed.");
        add (rpcLGR_NOT_FOUND,         "lgrNotFound",       "Ledger not found.");
        add (rpcMASTER_DISABLED,       "masterDisabled",    "Master key is disabled.");
        add (rpcNICKNAME_MALFORMED,    "nicknameMalformed", "Nickname is malformed.");
        add (rpcNICKNAME_MISSING,      "nicknameMissing",   "Nickname does not exist.");
        add (rpcNICKNAME_PERM,         "nicknamePerm",      "Account does not control nickname.");
        add (rpcNOT_IMPL,              "notImpl",           "Not implemented.");
        add (rpcNO_ACCOUNT,            "noAccount",         "No such account.");
        add (rpcNO_CLOSED,             "noClosed",          "Closed ledger is unavailable.");
        add (rpcNO_CURRENT,            "noCurrent",         "Current ledger is unavailable.");
        add (rpcNO_EVENTS,             "noEvents",          "Current transport does not support events.");
        add (rpcNO_GEN_DECRYPT,        "noGenDecrypt",      "Password failed to decrypt master public generator.");
        add (rpcNO_NETWORK,            "noNetwork",         "Not synced to Ripple network.");
        add (rpcNO_PATH,               "noPath",            "Unable to find a ripple path.");
        add (rpcNO_PERMISSION,         "noPermission",      "You don't have permission for this command.");
        add (rpcNO_PF_REQUEST,         "noPathRequest",     "No pathfinding request in progress.");
        add (rpcNOT_STANDALONE,        "notStandAlone",     "Operation valid in debug mode only.");
        add (rpcNOT_SUPPORTED,         "notSupported",      "Operation not supported.");
        add (rpcPASSWD_CHANGED,        "passwdChanged",     "Wrong key, password changed.");
        add (rpcPAYS_ACT_MALFORMED,    "paysActMalformed",  "Pays account malformed.");
        add (rpcPAYS_AMT_MALFORMED,    "paysAmtMalformed",  "Pays amount malformed.");
        add (rpcPORT_MALFORMED,        "portMalformed",     "Port is malformed.");
        add (rpcPUBLIC_MALFORMED,      "publicMalformed",   "Public key is malformed.");
        add (rpcQUALITY_MALFORMED,     "qualityMalformed",  "Quality malformed.");
        add (rpcSRC_ACT_MALFORMED,     "srcActMalformed",   "Source account is malformed.");
        add (rpcSRC_ACT_MISSING,       "srcActMissing",     "Source account not provided.");
        add (rpcSRC_ACT_NOT_FOUND,     "srcActNotFound",    "Source account not found.");
        add (rpcSRC_AMT_MALFORMED,     "srcAmtMalformed",   "Source amount/currency/issuer is malformed.");
        add (rpcSRC_CUR_MALFORMED,     "srcCurMalformed",   "Source currency is malformed.");
        add (rpcSRC_ISR_MALFORMED,     "srcIsrMalformed",   "Source issuer is malformed.");
        add (rpcSRC_UNCLAIMED,         "srcUnclaimed",      "Source account is not claimed.");
        add (rpcTXN_NOT_FOUND,         "txnNotFound",       "Transaction not found.");
        add (rpcUNKNOWN_COMMAND,       "unknownCmd",        "Unknown method.");
        add (rpcWRONG_SEED,            "wrongSeed",         "The regular key does not point as the master key.");
        add (rpcTOO_BUSY,              "tooBusy",           "The server is too busy to help you now.");
        add (rpcSLOW_DOWN,             "slowDown",          "You are placing too much load on the server.");
        add (rpcATX_DEPRECATED,        "deprecated",        "Use the new API or specify a ledger range.");
    }

    ErrorInfo const& get (error_code_i code) const
    {
        Map::const_iterator const iter (m_map.find (code));
        if (iter != m_map.end())
            return iter->second;
        return m_unknown;
    }

private:
    void add (error_code_i code, std::string const& token,
        std::string const& message)
    {
        std::pair <Map::iterator, bool> result (
            m_map.emplace (std::piecewise_construct,
                std::forward_as_tuple (code), std::forward_as_tuple (
                    code, token, message)));
        if (! result.second)
            throw std::invalid_argument ("duplicate error code");
    }

private:
    Map m_map;
    ErrorInfo m_unknown;
};

}

//------------------------------------------------------------------------------

ErrorInfo const& get_error_info (error_code_i code)
{
    static detail::ErrorCategory category;
    return category.get (code);
}

void inject_error (error_code_i code, Json::Value& json)
{
    ErrorInfo const& info (get_error_info (code));
    json ["error"] = info.token;
    json ["error_code"] = info.code;
    json ["error_message"] = info.message;
}

void inject_error (error_code_i code, std::string const& message, Json::Value& json)
{
    ErrorInfo const& info (get_error_info (code));
    json ["error"] = info.token;
    json ["error_code"] = info.code;
    json ["error_message"] = message;
}

Json::Value make_error (error_code_i code)
{
    Json::Value json;
    inject_error (code, json);
    return json;
}

Json::Value make_error (error_code_i code, std::string const& message)
{
    Json::Value json;
    inject_error (code, message, json);
    return json;
}

bool contains_error (Json::Value const& json)
{
    if (json.isObject() && json.isMember ("error"))
        return true;
    return false;
}

}
}
