#pragma once

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>

namespace xrpl {

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
// Protocol-wide, 50+ files
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum ErrorCodeI {
    // -1 represents codes not listed in this enumeration
    RpcUnknown = -1,

    RpcSuccess = 0,

    RpcBadSyntax = 1,
    RpcJsonRpc = 2,
    RpcForbidden = 3,

    RpcWrongNetwork = 4,
    // Misc failure
    // unused                  5,
    RpcNoPermission = 6,
    RpcNoEvents = 7,
    // unused                  8,
    RpcTooBusy = 9,
    RpcSlowDown = 10,
    RpcHighFee = 11,
    RpcNotEnabled = 12,
    RpcNotReady = 13,
    RpcAmendmentBlocked = 14,

    // Networking
    RpcNoClosed = 15,
    RpcNoCurrent = 16,
    RpcNoNetwork = 17,
    RpcNotSynced = 18,

    // Ledger state
    RpcActNotFound = 19,
    // unused                  20,
    RpcLgrNotFound = 21,
    RpcLgrNotValidated = 22,
    RpcMasterDisabled = 23,
    // unused                  24,
    // unused                  25,
    // unused                  26,
    // unused                  27,
    // unused                  28,
    RpcTxnNotFound = 29,
    RpcInvalidHotwallet = 30,

    // Malformed command
    RpcInvalidParams = 31,
    RpcUnknownCommand = 32,
    RpcNoPfRequest = 33,

    // Bad parameter
    // NOT USED DO NOT USE AGAIN rpcACT_BITCOIN = 34,
    RpcActMalformed = 35,
    RpcAlreadyMultisig = 36,
    RpcAlreadySingleSig = 37,
    // unused                  38,
    // unused                  39,
    RpcBadFeature = 40,
    RpcBadIssuer = 41,
    RpcBadMarket = 42,
    RpcBadSecret = 43,
    RpcBadSeed = 44,
    RpcChannelMalformed = 45,
    RpcChannelAmtMalformed = 46,
    RpcCommandMissing = 47,
    RpcDstActMalformed = 48,
    RpcDstActMissing = 49,
    RpcDstActNotFound = 50,
    RpcDstAmtMalformed = 51,
    RpcDstAmtMissing = 52,
    RpcDstIsrMalformed = 53,
    // unused                  54,
    // unused                  55,
    // unused                  56,
    RpcLgrIdxsInvalid = 57,
    RpcLgrIdxMalformed = 58,
    // unused                  59,
    // unused                  60,
    // unused                  61,
    RpcPublicMalformed = 62,
    RpcSigningMalformed = 63,
    RpcSendmaxMalformed = 64,
    RpcSrcActMalformed = 65,
    RpcSrcActMissing = 66,
    RpcSrcActNotFound = 67,
    RpcDelegateActNotFound = 68,
    RpcSrcCurMalformed = 69,
    RpcSrcIsrMalformed = 70,
    RpcStreamMalformed = 71,
    RpcAtxDeprecated = 72,

    // Internal error (should never happen)
    RpcInternal = 73,  // Generic internal error.
    RpcNotImpl = 74,
    RpcNotSupported = 75,
    RpcBadKeyType = 76,
    RpcDbDeserialization = 77,
    RpcExcessiveLgrRange = 78,
    RpcInvalidLgrRange = 79,
    RpcExpiredValidatorList = 80,

    // unused = 90,
    // DEPRECATED. New code must not use this value.
    RpcReportingUnsupported = 91,

    RpcObjectNotFound = 92,

    // AMM
    RpcIssueMalformed = 93,

    // Oracle
    RpcOracleMalformed = 94,

    // deposit_authorized + credentials
    RpcBadCredentials = 95,

    // Simulate
    RpcTxSigned = 96,

    // Pathfinding
    RpcDomainMalformed = 97,

    // ledger_entry
    RpcEntryNotFound = 98,
    RpcUnexpectedLedgerType = 99,

    RpcLast = RpcUnexpectedLedgerType  // rpcLAST should always equal the last code.
};

/** Codes returned in the `warnings` array of certain RPC commands.

    These values need to remain stable.
*/
// Protocol-wide, 50+ files
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum WarningCodeI {
    WarnRpcUnsupportedMajority = 1001,
    WarnRpcAmendmentBlocked = 1002,
    WarnRpcExpiredValidatorList = 1003,
    // unused = 1004
    WarnRpcFieldsDeprecated = 2004,  // xrpld needs to maintain
                                     // compatibility with Clio on this code.
};

//------------------------------------------------------------------------------

// VFALCO NOTE these should probably not be in the RPC namespace.

namespace RPC {

/** Maps an rpc error code to its token, default message, and HTTP status. */
struct ErrorInfo
{
    // Default ctor needed to produce an empty std::array during constexpr eval.
    constexpr ErrorInfo()
        : code(RpcUnknown), token("unknown"), message("An unknown error code."), httpStatus(200)
    {
    }

    constexpr ErrorInfo(ErrorCodeI code, char const* token, char const* message)
        : code(code), token(token), message(message), httpStatus(200)
    {
    }

    constexpr ErrorInfo(ErrorCodeI code, char const* token, char const* message, int httpStatus)
        : code(code), token(token), message(message), httpStatus(httpStatus)
    {
    }

    ErrorCodeI code;
    json::StaticString token;
    json::StaticString message;
    int httpStatus;
};

/** Returns an ErrorInfo that reflects the error code. */
ErrorInfo const&
getErrorInfo(ErrorCodeI code);

/** Add or update the json update to reflect the error code. */
/** @{ */
void
injectError(ErrorCodeI code, json::Value& json);

void
injectError(ErrorCodeI code, std::string const& message, json::Value& json);
/** @} */

/** Returns a new json object that reflects the error code. */
/** @{ */
json::Value
makeError(ErrorCodeI code);
json::Value
makeError(ErrorCodeI code, std::string const& message);
/** @} */

/** Returns a new json object that indicates invalid parameters. */
/** @{ */
inline json::Value
makeParamError(std::string const& message)
{
    return makeError(RpcInvalidParams, message);
}

inline std::string
missingFieldMessage(std::string const& name)
{
    return "Missing field '" + name + "'.";
}

inline json::Value
missingFieldError(std::string const& name)
{
    return makeParamError(missingFieldMessage(name));
}

inline json::Value
missingFieldError(json::StaticString name)
{
    return missingFieldError(std::string(name));
}

inline std::string
objectFieldMessage(std::string const& name)
{
    return "Invalid field '" + name + "', not object.";
}

inline json::Value
objectFieldError(std::string const& name)
{
    return makeParamError(objectFieldMessage(name));
}

inline json::Value
objectFieldError(json::StaticString name)
{
    return objectFieldError(std::string(name));
}

inline std::string
invalidFieldMessage(std::string const& name)
{
    return "Invalid field '" + name + "'.";
}

inline std::string
invalidFieldMessage(json::StaticString name)
{
    return invalidFieldMessage(std::string(name));
}

inline json::Value
invalidFieldError(std::string const& name)
{
    return makeParamError(invalidFieldMessage(name));
}

inline json::Value
invalidFieldError(json::StaticString name)
{
    return invalidFieldError(std::string(name));
}

inline std::string
expectedFieldMessage(std::string const& name, std::string const& type)
{
    return "Invalid field '" + name + "', not " + type + ".";
}

inline std::string
expectedFieldMessage(json::StaticString name, std::string const& type)
{
    return expectedFieldMessage(std::string(name), type);
}

inline json::Value
expectedFieldError(std::string const& name, std::string const& type)
{
    return makeParamError(expectedFieldMessage(name, type));
}

inline json::Value
expectedFieldError(json::StaticString name, std::string const& type)
{
    return expectedFieldError(std::string(name), type);
}

inline json::Value
notValidatorError()
{
    return makeParamError("not a validator");
}

/** @} */

/** Returns `true` if the json contains an rpc error specification. */
bool
containsError(json::Value const& json);

/** Returns http status that corresponds to the error code. */
int
errorCodeHttpStatus(ErrorCodeI code);

}  // namespace RPC

/** Returns a single string with the contents of an RPC error. */
std::string
rpcErrorString(json::Value const& jv);

}  // namespace xrpl
