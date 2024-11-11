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

#include <xrpl/protocol/ErrorCodes.h>
#include <array>
#include <cassert>
#include <stdexcept>

namespace ripple {
namespace RPC {

namespace detail {

// Unordered array of ErrorInfos, so we don't have to maintain the list
// ordering by hand.
//
// This array will be omitted from the object file; only the sorted version
// will remain in the object file.  But the string literals will remain.
//
// There's a certain amount of tension in determining the correct HTTP
// status to associate with a given RPC error.  Initially all RPC errors
// returned 200 (OK).  And that's the default behavior if no HTTP status code
// is specified below.
//
// The codes currently selected target the load balancer fail-over use case.
// If a query fails on one node but is likely to have a positive outcome
// on a different node, then the failure should return a 4xx/5xx range
// status code.

// clang-format off
constexpr static ErrorInfo unorderedErrorInfos[]{
    {rpcACT_MALFORMED,          "actMalformed",         "Account malformed."},
    {rpcACT_NOT_FOUND,          "actNotFound",          "Account not found."},
    {rpcALREADY_MULTISIG,       "alreadyMultisig",      "Already multisigned."},
    {rpcALREADY_SINGLE_SIG,     "alreadySingleSig",     "Already single-signed."},
    {rpcAMENDMENT_BLOCKED,      "amendmentBlocked",     "Amendment blocked, need upgrade.", 503},
    {rpcEXPIRED_VALIDATOR_LIST, "unlBlocked",           "Validator list expired.", 503},
    {rpcATX_DEPRECATED,         "deprecated",           "Use the new API or specify a ledger range.", 400},
    {rpcBAD_KEY_TYPE,           "badKeyType",           "Bad key type.", 400},
    {rpcBAD_FEATURE,            "badFeature",           "Feature unknown or invalid.", 500},
    {rpcBAD_ISSUER,             "badIssuer",            "Issuer account malformed.", 400},
    {rpcBAD_MARKET,             "badMarket",            "No such market.", 404},
    {rpcBAD_SECRET,             "badSecret",            "Secret does not match account.", 403},
    {rpcBAD_SEED,               "badSeed",              "Disallowed seed.", 403},
    {rpcBAD_SYNTAX,             "badSyntax",            "Syntax error.", 400},
    {rpcCHANNEL_MALFORMED,      "channelMalformed",     "Payment channel is malformed.", 400},
    {rpcCHANNEL_AMT_MALFORMED,  "channelAmtMalformed",  "Payment channel amount is malformed.", 400},
    {rpcCOMMAND_MISSING,        "commandMissing",       "Missing command entry.", 400},
    {rpcDB_DESERIALIZATION,     "dbDeserialization",    "Database deserialization error.", 502},
    {rpcDST_ACT_MALFORMED,      "dstActMalformed",      "Destination account is malformed.", 400},
    {rpcDST_ACT_MISSING,        "dstActMissing",        "Destination account not provided.", 400},
    {rpcDST_ACT_NOT_FOUND,      "dstActNotFound",       "Destination account not found.", 404},
    {rpcDST_AMT_MALFORMED,      "dstAmtMalformed",      "Destination amount/currency/issuer is malformed.", 400},
    {rpcDST_AMT_MISSING,        "dstAmtMissing",        "Destination amount/currency/issuer is missing.", 400},
    {rpcDST_ISR_MALFORMED,      "dstIsrMalformed",      "Destination issuer is malformed.", 400},
    {rpcEXCESSIVE_LGR_RANGE,    "excessiveLgrRange",    "Ledger range exceeds 1000.", 400},
    {rpcFORBIDDEN,              "forbidden",            "Bad credentials.", 403},
    {rpcHIGH_FEE,               "highFee",              "Current transaction fee exceeds your limit.", 402},
    {rpcINTERNAL,               "internal",             "Internal error.", 500},
    {rpcINVALID_LGR_RANGE,      "invalidLgrRange",      "Ledger range is invalid.", 400},
    {rpcINVALID_PARAMS,         "invalidParams",        "Invalid parameters.", 400},
    {rpcINVALID_HOTWALLET,      "invalidHotWallet",     "Invalid hotwallet.", 400},
    {rpcISSUE_MALFORMED,        "issueMalformed",       "Issue is malformed.", 400},
    {rpcJSON_RPC,               "json_rpc",             "JSON-RPC transport error.", 500},
    {rpcLGR_IDXS_INVALID,       "lgrIdxsInvalid",       "Ledger indexes invalid.", 400},
    {rpcLGR_IDX_MALFORMED,      "lgrIdxMalformed",      "Ledger index malformed.", 400},
    {rpcLGR_NOT_FOUND,          "lgrNotFound",          "Ledger not found.", 404},
    {rpcLGR_NOT_VALIDATED,      "lgrNotValidated",      "Ledger not validated.", 202},
    {rpcMASTER_DISABLED,        "masterDisabled",       "Master key is disabled.", 403},
    {rpcNOT_ENABLED,            "notEnabled",           "Not enabled in configuration.", 501},
    {rpcNOT_IMPL,               "notImpl",              "Not implemented.", 501},
    {rpcNOT_READY,              "notReady",             "Not ready to handle this request.", 503},
    {rpcNOT_SUPPORTED,          "notSupported",         "Operation not supported.", 501},
    {rpcNO_CLOSED,              "noClosed",             "Closed ledger is unavailable.", 503},
    {rpcNO_CURRENT,             "noCurrent",            "Current ledger is unavailable.", 503},
    {rpcNOT_SYNCED,             "notSynced",            "Not synced to the network.", 503},
    {rpcNO_EVENTS,              "noEvents",             "Current transport does not support events.", 405},
    {rpcNO_NETWORK,             "noNetwork",            "Not synced to the network.", 503},
    {rpcNO_PERMISSION,          "noPermission",         "You don't have permission for this command.", 401},
    {rpcNO_PF_REQUEST,          "noPathRequest",        "No pathfinding request in progress.", 404},
    {rpcOBJECT_NOT_FOUND,       "objectNotFound",       "The requested object was not found.", 404},
    {rpcPUBLIC_MALFORMED,       "publicMalformed",      "Public key is malformed.", 400},
    {rpcSENDMAX_MALFORMED,      "sendMaxMalformed",     "SendMax amount malformed.", 400},
    {rpcSIGNING_MALFORMED,      "signingMalformed",     "Signing of transaction is malformed.", 400},
    {rpcSLOW_DOWN,              "slowDown",             "You are placing too much load on the server.", 429},
    {rpcSRC_ACT_MALFORMED,      "srcActMalformed",      "Source account is malformed.", 400},
    {rpcSRC_ACT_MISSING,        "srcActMissing",        "Source account not provided.", 400},
    {rpcSRC_ACT_NOT_FOUND,      "srcActNotFound",       "Source account not found.", 404},
    {rpcSRC_CUR_MALFORMED,      "srcCurMalformed",      "Source currency is malformed.", 400},
    {rpcSRC_ISR_MALFORMED,      "srcIsrMalformed",      "Source issuer is malformed.", 400},
    {rpcSTREAM_MALFORMED,       "malformedStream",      "Stream malformed.", 400},
    {rpcTOO_BUSY,               "tooBusy",              "The server is too busy to help you now.", 503},
    {rpcTXN_NOT_FOUND,          "txnNotFound",          "Transaction not found.", 404},
    {rpcUNKNOWN_COMMAND,        "unknownCmd",           "Unknown method.", 405},
    {rpcORACLE_MALFORMED,       "oracleMalformed",      "Oracle request is malformed.", 400},
    {rpcBAD_CREDENTIALS,        "badCredentials",       "Credentials do not exist, are not accepted, or have expired.", 400}};
// clang-format on

// Sort and validate unorderedErrorInfos at compile time.  Should be
// converted to consteval when get to C++20.
template <int M, int N>
constexpr auto
sortErrorInfos(ErrorInfo const (&unordered)[N]) -> std::array<ErrorInfo, M>
{
    std::array<ErrorInfo, M> ret = {};

    for (ErrorInfo const& info : unordered)
    {
        if (info.code <= rpcSUCCESS || info.code > rpcLAST)
            throw(std::out_of_range("Invalid error_code_i"));

        // The first valid code follows rpcSUCCESS immediately.
        static_assert(rpcSUCCESS == 0, "Unexpected error_code_i layout.");
        int const index{info.code - 1};

        if (ret[index].code != rpcUNKNOWN)
            throw(std::invalid_argument("Duplicate error_code_i in list"));

        ret[index] = info;
    }

    // Verify that all entries are filled in starting with 1 and proceeding
    // to rpcLAST.
    //
    // It's okay for there to be missing entries; they will contain the code
    // rpcUNKNOWN.  But other than that all entries should match their index.
    int codeCount{0};
    int expect{rpcBAD_SYNTAX - 1};
    for (ErrorInfo const& info : ret)
    {
        ++expect;
        if (info.code == rpcUNKNOWN)
            continue;

        if (info.code != expect)
            throw(std::invalid_argument("Empty error_code_i in list"));
        ++codeCount;
    }
    if (expect != rpcLAST)
        throw(std::invalid_argument("Insufficient list entries"));
    if (codeCount != N)
        throw(std::invalid_argument("Bad handling of unorderedErrorInfos"));

    return ret;
}

constexpr auto sortedErrorInfos{sortErrorInfos<rpcLAST>(unorderedErrorInfos)};

constexpr ErrorInfo unknownError;

}  // namespace detail

//------------------------------------------------------------------------------

ErrorInfo const&
get_error_info(error_code_i code)
{
    if (code <= rpcSUCCESS || code > rpcLAST)
        return detail::unknownError;
    return detail::sortedErrorInfos[code - 1];
}

Json::Value
make_error(error_code_i code)
{
    Json::Value json;
    inject_error(code, json);
    return json;
}

Json::Value
make_error(error_code_i code, std::string const& message)
{
    Json::Value json;
    inject_error(code, message, json);
    return json;
}

bool
contains_error(Json::Value const& json)
{
    if (json.isObject() && json.isMember(jss::error))
        return true;
    return false;
}

int
error_code_http_status(error_code_i code)
{
    return get_error_info(code).http_status;
}

}  // namespace RPC

std::string
rpcErrorString(Json::Value const& jv)
{
    assert(RPC::contains_error(jv));
    return jv[jss::error].asString() + jv[jss::error_message].asString();
}

}  // namespace ripple
