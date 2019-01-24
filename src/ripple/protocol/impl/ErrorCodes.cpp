//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012 - 2019 Ripple Labs Inc.

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

#include <ripple/basics/contract.h>
#include <ripple/basics/safe_cast.h>
#include <ripple/protocol/ErrorCodes.h>
#include <cassert>
#include <unordered_map>
#include <utility>

namespace std {

template <>
struct hash <ripple::error_code_i>
{
    explicit hash() = default;

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
    using Map = std::unordered_map <error_code_i, ErrorInfo>;

    ErrorCategory ()
        : m_unknown (rpcUNKNOWN, "unknown", "An unknown error code.")
    {
        add (rpcACT_BITCOIN,           "actBitcoin",          "Account is bitcoin address.");
        add (rpcACT_MALFORMED,         "actMalformed",        "Account malformed.");
        add (rpcACT_NOT_FOUND,         "actNotFound",         "Account not found.");
        add (rpcALREADY_MULTISIG,      "alreadyMultisig",     "Already multisigned.");
        add (rpcALREADY_SINGLE_SIG,    "alreadySingleSig",    "Already single-signed.");
        add (rpcAMENDMENT_BLOCKED,     "amendmentBlocked",    "Amendment blocked, need upgrade.");
        add (rpcATX_DEPRECATED,        "deprecated",          "Use the new API or specify a ledger range.");
        add (rpcBAD_FEATURE,           "badFeature",          "Feature unknown or invalid.");
        add (rpcBAD_ISSUER,            "badIssuer",           "Issuer account malformed.");
        add (rpcBAD_MARKET,            "badMarket",           "No such market.");
        add (rpcBAD_SECRET,            "badSecret",           "Secret does not match account.");
        add (rpcBAD_SEED,              "badSeed",             "Disallowed seed.");
        add (rpcBAD_SYNTAX,            "badSyntax",           "Syntax error.");
        add (rpcCHANNEL_MALFORMED,     "channelMalformed",    "Payment channel is malformed.");
        add (rpcCHANNEL_AMT_MALFORMED, "channelAmtMalformed", "Payment channel amount is malformed.");
        add (rpcCOMMAND_MISSING,       "commandMissing",      "Missing command entry.");
        add (rpcDST_ACT_MALFORMED,     "dstActMalformed",     "Destination account is malformed.");
        add (rpcDST_ACT_MISSING,       "dstActMissing",       "Destination account not provided.");
        add (rpcDST_ACT_NOT_FOUND,     "dstActNotFound",      "Destination account not found.");
        add (rpcDST_AMT_MALFORMED,     "dstAmtMalformed",     "Destination amount/currency/issuer is malformed.");
        add (rpcDST_AMT_MISSING,       "dstAmtMissing",       "Destination amount/currency/issuer is missing.");
        add (rpcDST_ISR_MALFORMED,     "dstIsrMalformed",     "Destination issuer is malformed.");
        add (rpcFORBIDDEN,             "forbidden",           "Bad credentials.");
        add (rpcHIGH_FEE,              "highFee",             "Current transaction fee exceeds your limit.");
        add (rpcINTERNAL,              "internal",            "Internal error.");
        add (rpcINVALID_PARAMS,        "invalidParams",       "Invalid parameters.");
        add (rpcJSON_RPC,              "json_rpc",            "JSON-RPC transport error.");
        add (rpcLGR_IDXS_INVALID,      "lgrIdxsInvalid",      "Ledger indexes invalid.");
        add (rpcLGR_IDX_MALFORMED,     "lgrIdxMalformed",     "Ledger index malformed.");
        add (rpcLGR_NOT_FOUND,         "lgrNotFound",         "Ledger not found.");
        add (rpcLGR_NOT_VALIDATED,     "lgrNotValidated",     "Ledger not validated.");
        add (rpcMASTER_DISABLED,       "masterDisabled",      "Master key is disabled.");
        add (rpcNOT_ENABLED,           "notEnabled",          "Not enabled in configuration.");
        add (rpcNOT_IMPL,              "notImpl",             "Not implemented.");
        add (rpcNOT_READY,             "notReady",            "Not ready to handle this request.");
        add (rpcNOT_SUPPORTED,         "notSupported",        "Operation not supported.");
        add (rpcNO_CLOSED,             "noClosed",            "Closed ledger is unavailable.");
        add (rpcNO_CURRENT,            "noCurrent",           "Current ledger is unavailable.");
        add (rpcNO_EVENTS,             "noEvents",            "Current transport does not support events.");
        add (rpcNO_NETWORK,            "noNetwork",           "Not synced to Ripple network.");
        add (rpcNO_PERMISSION,         "noPermission",        "You don't have permission for this command.");
        add (rpcNO_PF_REQUEST,         "noPathRequest",       "No pathfinding request in progress.");
        add (rpcPUBLIC_MALFORMED,      "publicMalformed",     "Public key is malformed.");
        add (rpcSIGNING_MALFORMED,     "signingMalformed",    "Signing of transaction is malformed.");
        add (rpcSLOW_DOWN,             "slowDown",            "You are placing too much load on the server.");
        add (rpcSRC_ACT_MALFORMED,     "srcActMalformed",     "Source account is malformed.");
        add (rpcSRC_ACT_MISSING,       "srcActMissing",       "Source account not provided.");
        add (rpcSRC_ACT_NOT_FOUND,     "srcActNotFound",      "Source account not found.");
        add (rpcSRC_CUR_MALFORMED,     "srcCurMalformed",     "Source currency is malformed.");
        add (rpcSRC_ISR_MALFORMED,     "srcIsrMalformed",     "Source issuer is malformed.");
        add (rpcSTREAM_MALFORMED,      "malformedStream",     "Stream malformed.");
        add (rpcTOO_BUSY,              "tooBusy",             "The server is too busy to help you now.");
        add (rpcTXN_NOT_FOUND,         "txnNotFound",         "Transaction not found.");
        add (rpcUNKNOWN_COMMAND,       "unknownCmd",          "Unknown method.");
        add (rpcSENDMAX_MALFORMED,     "sendMaxMalformed",    "SendMax amount malformed.");

        // Verify that the number of entries in m_map equals the number of
        // enums that have descriptions.  That skips rpcUNKNOWN and rpcSUCCESS,
        // which are -1 and 0 respectively.
        assert (safe_cast<int>(rpcLAST) == m_map.size());
    }

    ErrorInfo const& get (error_code_i code) const
    {
        Map::const_iterator const iter {m_map.find (code)};
        assert (iter != m_map.end());
        if (iter != m_map.end())
            return iter->second;
        return m_unknown;
    }

private:
    void add (error_code_i code, std::string const& token,
        std::string const& message)
    {
        std::pair <Map::iterator, bool> const result {
            m_map.emplace (std::piecewise_construct,
                std::forward_as_tuple (code), std::forward_as_tuple (
                    code, token, message))};

        if (! result.second)
            Throw<std::invalid_argument> ("duplicate error code");
    }

private:
    Map m_map;
    ErrorInfo const m_unknown;
};

}

//------------------------------------------------------------------------------

ErrorInfo const& get_error_info (error_code_i code)
{
    static detail::ErrorCategory const category;
    return category.get (code);
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
    if (json.isObject() && json.isMember (jss::error))
        return true;
    return false;
}

} // RPC

std::string rpcErrorString(Json::Value const& jv)
{
    assert(RPC::contains_error(jv));
    return jv[jss::error].asString() +
        jv[jss::error_message].asString();
}

} // ripple

