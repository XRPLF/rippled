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

#ifndef RIPPLE_RPC_ERRORCODES_H_INCLUDED
#define RIPPLE_RPC_ERRORCODES_H_INCLUDED

#include <ripple/unity/json.h>

namespace ripple {

// VFALCO NOTE These are outside the RPC namespace

enum error_code_i
{
    rpcUNKNOWN = -1,     // Represents codes not listed in this enumeration

    rpcSUCCESS = 0,

    rpcBAD_SYNTAX,  // Must be 1 to print usage to command line.
    rpcJSON_RPC,
    rpcFORBIDDEN,

    // Error numbers beyond this line are not stable between versions.
    // Programs should use error tokens.

    // Misc failure
    rpcLOAD_FAILED,
    rpcNO_PERMISSION,
    rpcNO_EVENTS,
    rpcNOT_STANDALONE,
    rpcTOO_BUSY,
    rpcSLOW_DOWN,
    rpcHIGH_FEE,

    // Networking
    rpcNO_CLOSED,
    rpcNO_CURRENT,
    rpcNO_NETWORK,

    // Ledger state
    rpcACT_EXISTS,
    rpcACT_NOT_FOUND,
    rpcINSUF_FUNDS,
    rpcLGR_NOT_FOUND,
    rpcMASTER_DISABLED,
    rpcNICKNAME_MISSING,
    rpcNO_ACCOUNT,
    rpcNO_PATH,
    rpcPASSWD_CHANGED,
    rpcSRC_MISSING,
    rpcSRC_UNCLAIMED,
    rpcTXN_NOT_FOUND,
    rpcWRONG_SEED,

    // Malformed command
    rpcINVALID_PARAMS,
    rpcUNKNOWN_COMMAND,
    rpcNO_PF_REQUEST,

    // Bad parameter
    rpcACT_BITCOIN,
    rpcACT_MALFORMED,
    rpcQUALITY_MALFORMED,
    rpcBAD_BLOB,
    rpcBAD_FEATURE,
    rpcBAD_ISSUER,
    rpcBAD_MARKET,
    rpcBAD_SECRET,
    rpcBAD_SEED,
    rpcCOMMAND_MISSING,
    rpcDST_ACT_MALFORMED,
    rpcDST_ACT_MISSING,
    rpcDST_AMT_MALFORMED,
    rpcDST_ISR_MALFORMED,
    rpcGETS_ACT_MALFORMED,
    rpcGETS_AMT_MALFORMED,
    rpcHOST_IP_MALFORMED,
    rpcLGR_IDXS_INVALID,
    rpcLGR_IDX_MALFORMED,
    rpcNICKNAME_MALFORMED,
    rpcNICKNAME_PERM,
    rpcPAYS_ACT_MALFORMED,
    rpcPAYS_AMT_MALFORMED,
    rpcPORT_MALFORMED,
    rpcPUBLIC_MALFORMED,
    rpcSRC_ACT_MALFORMED,
    rpcSRC_ACT_MISSING,
    rpcSRC_ACT_NOT_FOUND,
    rpcSRC_AMT_MALFORMED,
    rpcSRC_CUR_MALFORMED,
    rpcSRC_ISR_MALFORMED,
    rpcATX_DEPRECATED,

    // Internal error (should never happen)
    rpcINTERNAL,        // Generic internal error.
    rpcFAIL_GEN_DECRYPT,
    rpcNOT_IMPL,
    rpcNOT_SUPPORTED,
    rpcNO_GEN_DECRYPT,
};

//------------------------------------------------------------------------------

namespace RPC {

/** Maps an rpc error code to its token and default message. */
struct ErrorInfo
{
    ErrorInfo (error_code_i code_, std::string const& token_,
        std::string const& message_)
        : code (code_)
        , token (token_)
        , message (message_)
    { }

    error_code_i code;
    std::string token;
    std::string message;
};

/** Returns an ErrorInfo that reflects the error code. */
ErrorInfo const& get_error_info (error_code_i code);

/** Add or update the json update to reflect the error code. */
/** @{ */
void inject_error (error_code_i code, Json::Value& json);
inline void inject_error (int code, Json::Value& json)
    { inject_error (error_code_i (code), json); }
void inject_error (error_code_i code, std::string const& message, Json::Value& json);
/** @} */

/** Returns a new json object that reflects the error code. */
/** @{ */
Json::Value make_error (error_code_i code);
Json::Value make_error (error_code_i code, std::string const& message);
/** @} */

/** Returns a new json object that indicates invalid parameters. */
/** @{ */
inline Json::Value make_param_error (std::string const& message)
{
    return make_error (rpcINVALID_PARAMS, message);
}

inline std::string missing_field_message (std::string const& name)
{
    return "Missing field '" + name + "'.";
}

inline Json::Value missing_field_error (std::string const& name)
{
    return make_param_error (missing_field_message (name));
}

inline std::string object_field_message (std::string const& name)
{
    return "Invalid field '" + name + "', not object.";
}

inline Json::Value object_field_error (std::string const& name)
{
    return make_param_error (object_field_message (name));
}

inline std::string invalid_field_message (std::string const& name)
{
    return "Invalid field '" + name + "'.";
}

inline Json::Value invalid_field_error (std::string const& name)
{
    return make_param_error (object_field_message (name));
}

inline std::string expected_field_message (
    std::string const& name, std::string const& type)
{
    return "Invalid field '" + name + "', not " + type + ".";
}

inline Json::Value expected_field_error (
    std::string const& name, std::string const& type)
{
    return make_param_error (expected_field_message (name, type));
}

/** @} */

/** Returns `true` if the json contains an rpc error specification. */
bool contains_error (Json::Value const& json);

}

}

#endif
