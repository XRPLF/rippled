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

#ifndef RIPPLE_PROTOCOL_ERRORCODES_H_INCLUDED
#define RIPPLE_PROTOCOL_ERRORCODES_H_INCLUDED

#include <ripple/json/json_value.h>
#include <ripple/protocol/jss.h>

namespace ripple {

// VFALCO NOTE These are outside the RPC namespace

// NOTE: Although the precise numeric values of these codes were never
// intended to be stable, several API endpoints include the numeric values.
// Some users came to rely on the values, meaning that renumbering would be
// a breaking change for those users.
//
// We therefore treat the range of values as stable although they are not
// and are subject to change.
//
// Please only append to this table. Do not "fill-in" gaps and do not re-use
// or repurpose error code values.
enum error_code_i
{
    // -1 represents codes not listed in this enumeration
    rpcUNKNOWN               = -1,

    rpcSUCCESS               = 0,

    rpcBAD_SYNTAX            = 1,
    rpcJSON_RPC              = 2,
    rpcFORBIDDEN             = 3,

    // Misc failure
    // unused                  4,
    // unused                  5,
    rpcNO_PERMISSION         = 6,
    rpcNO_EVENTS             = 7,
    // unused                  8,
    rpcTOO_BUSY              = 9,
    rpcSLOW_DOWN             = 10,
    rpcHIGH_FEE              = 11,
    rpcNOT_ENABLED           = 12,
    rpcNOT_READY             = 13,
    rpcAMENDMENT_BLOCKED     = 14,

    // Networking
    rpcNO_CLOSED             = 15,
    rpcNO_CURRENT            = 16,
    rpcNO_NETWORK            = 17,

    // Ledger state
    // unused                  18,
    rpcACT_NOT_FOUND         = 19,
    // unused                  20,
    rpcLGR_NOT_FOUND         = 21,
    rpcLGR_NOT_VALIDATED     = 22,
    rpcMASTER_DISABLED       = 23,
    // unused                  24,
    // unused                  25,
    // unused                  26,
    // unused                  27,
    // unused                  28,
    rpcTXN_NOT_FOUND         = 29,
    // unused                  30,

    // Malformed command
    rpcINVALID_PARAMS        = 31,
    rpcUNKNOWN_COMMAND       = 32,
    rpcNO_PF_REQUEST         = 33,

    // Bad parameter
    rpcACT_BITCOIN           = 34,
    rpcACT_MALFORMED         = 35,
    rpcALREADY_MULTISIG      = 36,
    rpcALREADY_SINGLE_SIG    = 37,
    // unused                  38,
    // unused                  39,
    rpcBAD_FEATURE           = 40,
    rpcBAD_ISSUER            = 41,
    rpcBAD_MARKET            = 42,
    rpcBAD_SECRET            = 43,
    rpcBAD_SEED              = 44,
    rpcCHANNEL_MALFORMED     = 45,
    rpcCHANNEL_AMT_MALFORMED = 46,
    rpcCOMMAND_MISSING       = 47,
    rpcDST_ACT_MALFORMED     = 48,
    rpcDST_ACT_MISSING       = 49,
    rpcDST_ACT_NOT_FOUND     = 50,
    rpcDST_AMT_MALFORMED     = 51,
    rpcDST_AMT_MISSING       = 52,
    rpcDST_ISR_MALFORMED     = 53,
    // unused                  54,
    // unused                  55,
    // unused                  56,
    rpcLGR_IDXS_INVALID      = 57,
    rpcLGR_IDX_MALFORMED     = 58,
    // unused                  59,
    // unused                  60,
    // unused                  61,
    rpcPUBLIC_MALFORMED      = 62,
    rpcSIGNING_MALFORMED     = 63,
    rpcSENDMAX_MALFORMED     = 64,
    rpcSRC_ACT_MALFORMED     = 65,
    rpcSRC_ACT_MISSING       = 66,
    rpcSRC_ACT_NOT_FOUND     = 67,
    // unused                  68,
    rpcSRC_CUR_MALFORMED     = 69,
    rpcSRC_ISR_MALFORMED     = 70,
    rpcSTREAM_MALFORMED      = 71,
    rpcATX_DEPRECATED        = 72,

    // Internal error (should never happen)
    rpcINTERNAL              = 73,  // Generic internal error.
    rpcNOT_IMPL              = 74,
    rpcNOT_SUPPORTED         = 75,
    rpcBAD_KEY_TYPE          = 76,
    rpcDB_DESERIALIZATION    = 77,
    rpcLAST = rpcDB_DESERIALIZATION   // rpcLAST should always equal the last code.
};

//------------------------------------------------------------------------------

// VFALCO NOTE these should probably not be in the RPC namespace.

namespace RPC {

/** Maps an rpc error code to its token and default message. */
struct ErrorInfo
{
    // Default ctor needed to produce an empty std::array during constexpr eval.
    constexpr ErrorInfo ()
    : code (rpcUNKNOWN)
    , token ("unknown")
    , message ("An unknown error code.")
    { }

    constexpr ErrorInfo (error_code_i code_, char const* token_,
        char const* message_)
        : code (code_)
        , token (token_)
        , message (message_)
    { }

    error_code_i code;
    Json::StaticString token;
    Json::StaticString message;
};

/** Returns an ErrorInfo that reflects the error code. */
ErrorInfo const& get_error_info (error_code_i code);

/** Add or update the json update to reflect the error code. */
/** @{ */
template <class JsonValue>
void inject_error (error_code_i code, JsonValue& json)
{
    ErrorInfo const& info (get_error_info (code));
    json [jss::error] = info.token;
    json [jss::error_code] = info.code;
    json [jss::error_message] = info.message;
}

template <class JsonValue>
void inject_error (int code, JsonValue& json)
{
    inject_error (error_code_i (code), json);
}

template <class JsonValue>
void inject_error (
    error_code_i code, std::string const& message, JsonValue& json)
{
    ErrorInfo const& info (get_error_info (code));
    json [jss::error] = info.token;
    json [jss::error_code] = info.code;
    json [jss::error_message] = message;
}

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

inline Json::Value missing_field_error (Json::StaticString name)
{
    return missing_field_error (std::string (name));
}

inline std::string object_field_message (std::string const& name)
{
    return "Invalid field '" + name + "', not object.";
}

inline Json::Value object_field_error (std::string const& name)
{
    return make_param_error (object_field_message (name));
}

inline Json::Value object_field_error (Json::StaticString name)
{
    return object_field_error (std::string (name));
}

inline std::string invalid_field_message (std::string const& name)
{
    return "Invalid field '" + name + "'.";
}

inline std::string invalid_field_message (Json::StaticString name)
{
    return invalid_field_message (std::string(name));
}

inline Json::Value invalid_field_error (std::string const& name)
{
    return make_param_error (invalid_field_message (name));
}

inline Json::Value invalid_field_error (Json::StaticString name)
{
    return invalid_field_error (std::string (name));
}

inline std::string expected_field_message (
    std::string const& name, std::string const& type)
{
    return "Invalid field '" + name + "', not " + type + ".";
}

inline std::string expected_field_message (
    Json::StaticString name, std::string const& type)
{
    return expected_field_message (std::string (name), type);
}

inline Json::Value expected_field_error (
    std::string const& name, std::string const& type)
{
    return make_param_error (expected_field_message (name, type));
}

inline Json::Value expected_field_error (
    Json::StaticString name, std::string const& type)
{
    return expected_field_error (std::string (name), type);
}

/** @} */

/** Returns `true` if the json contains an rpc error specification. */
bool contains_error (Json::Value const& json);

} // RPC

/** Returns a single string with the contents of an RPC error. */
std::string rpcErrorString(Json::Value const& jv);

}

#endif
