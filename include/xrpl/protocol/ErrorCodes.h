#pragma once

/** @file
 *  Single source of truth for every RPC error the XRPL node can emit.
 *
 *  Defines the stable numeric code space (`ErrorCodeI`), a parallel warning
 *  code space (`WarningCodeI`), the `ErrorInfo` struct that binds each code
 *  to a token and HTTP status, and the complete vocabulary of JSON helpers
 *  used by RPC handlers to produce well-formed error responses. Every
 *  component that rejects an RPC call — from malformed-parameter checks to
 *  ledger-not-found conditions — funnels through this file.
 */

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>

namespace xrpl {

// VFALCO NOTE These are outside the RPC namespace

/** Numeric codes for every named RPC error the XRPL node can return.
 *
 *  Values are used as machine-readable error identifiers in RPC responses
 *  (the `error_code` field). They were never formally promised to be stable,
 *  but real API consumers depend on them, so the range is now treated as
 *  **append-only**: new codes go at the end, gaps are never filled, and
 *  retired values are commented out rather than reassigned.
 *
 *  Codes are grouped thematically (general failures, networking, ledger
 *  state, malformed commands, bad parameters, internal errors) to guide
 *  maintainers when choosing where a new code belongs.
 *
 *  `RpcLast` must always equal the highest assigned code; the compile-time
 *  validation in `ErrorCodes.cpp` enforces this and will fail to compile if
 *  it is not updated when a new code is added.
 *
 *  @note `RpcUnknown` (-1) is returned by `getErrorInfo()` for any code
 *      that falls outside the range `(RpcSuccess, RpcLast]`.
 */
// Protocol-wide, 50+ files
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum ErrorCodeI {
    // -1 represents codes not listed in this enumeration
    RpcUnknown = -1,  /**< Sentinel for out-of-range or unrecognised codes. */

    RpcSuccess = 0,  /**< No error. */

    // General failures
    RpcBadSyntax = 1,   /**< Request could not be parsed as valid JSON-RPC. */
    RpcJsonRpc = 2,     /**< JSON-RPC transport-level error. */
    RpcForbidden = 3,   /**< Credentials rejected. */

    RpcWrongNetwork = 4,     /**< Request arrived on the wrong network. */
    // Misc failure
    // unused                  5,
    RpcNoPermission = 6,      /**< Caller lacks permission for this command. */
    RpcNoEvents = 7,          /**< Transport does not support event subscriptions. */
    // unused                  8,
    RpcTooBusy = 9,           /**< Server load too high to serve the request now. */
    RpcSlowDown = 10,         /**< Caller is sending requests too rapidly. */
    RpcHighFee = 11,          /**< Current fee exceeds the caller's stated limit. */
    RpcNotEnabled = 12,       /**< Feature not enabled in the server's configuration. */
    RpcNotReady = 13,         /**< Server is not yet ready to handle this request. */
    RpcAmendmentBlocked = 14, /**< Node needs an upgrade; amendment-blocked. */

    // Networking
    RpcNoClosed = 15,   /**< Closed ledger is unavailable. */
    RpcNoCurrent = 16,  /**< Current ledger is unavailable. */
    RpcNoNetwork = 17,  /**< Not synced to the network. */
    RpcNotSynced = 18,  /**< Not synced to the network. */

    // Ledger state
    RpcActNotFound = 19,      /**< Specified account does not exist in the ledger. */
    // unused                  20,
    RpcLgrNotFound = 21,      /**< Requested ledger does not exist. */
    RpcLgrNotValidated = 22,  /**< Requested ledger exists but has not yet been validated. */
    RpcMasterDisabled = 23,   /**< Master key is disabled on this account. */
    // unused                  24,
    // unused                  25,
    // unused                  26,
    // unused                  27,
    // unused                  28,
    RpcTxnNotFound = 29,        /**< Transaction not found. */
    RpcInvalidHotwallet = 30,   /**< Specified hotwallet address is invalid. */

    // Malformed command
    RpcInvalidParams = 31,    /**< One or more request parameters are invalid. */
    RpcUnknownCommand = 32,   /**< The requested command is not recognised. */
    RpcNoPfRequest = 33,      /**< No pathfinding request is currently in progress. */

    // Bad parameter
    // NOT USED DO NOT USE AGAIN rpcACT_BITCOIN = 34,
    RpcActMalformed = 35,         /**< Account address is malformed. */
    RpcAlreadyMultisig = 36,      /**< Account is already set up for multi-signing. */
    RpcAlreadySingleSig = 37,     /**< Account is already single-signed. */
    // unused                  38,
    // unused                  39,
    RpcBadFeature = 40,           /**< Unknown or invalid amendment feature. */
    RpcBadIssuer = 41,            /**< Issuer account address is malformed. */
    RpcBadMarket = 42,            /**< Requested order-book does not exist. */
    RpcBadSecret = 43,            /**< Secret key does not match the specified account. */
    RpcBadSeed = 44,              /**< Seed value is disallowed. */
    RpcChannelMalformed = 45,     /**< Payment channel identifier is malformed. */
    RpcChannelAmtMalformed = 46,  /**< Payment channel amount is malformed. */
    RpcCommandMissing = 47,       /**< Request object is missing the command field. */
    RpcDstActMalformed = 48,      /**< Destination account address is malformed. */
    RpcDstActMissing = 49,        /**< Destination account was not provided. */
    RpcDstActNotFound = 50,       /**< Destination account does not exist in the ledger. */
    RpcDstAmtMalformed = 51,      /**< Destination amount, currency, or issuer is malformed. */
    RpcDstAmtMissing = 52,        /**< Destination amount, currency, or issuer was not provided. */
    RpcDstIsrMalformed = 53,      /**< Destination issuer is malformed. */
    // unused                  54,
    // unused                  55,
    // unused                  56,
    RpcLgrIdxsInvalid = 57,       /**< Ledger index range is invalid. */
    RpcLgrIdxMalformed = 58,      /**< Individual ledger index is malformed. */
    // unused                  59,
    // unused                  60,
    // unused                  61,
    RpcPublicMalformed = 62,      /**< Public key is malformed. */
    RpcSigningMalformed = 63,     /**< Transaction signing data is malformed. */
    RpcSendmaxMalformed = 64,     /**< SendMax amount is malformed. */
    RpcSrcActMalformed = 65,      /**< Source account address is malformed. */
    RpcSrcActMissing = 66,        /**< Source account was not provided. */
    RpcSrcActNotFound = 67,       /**< Source account does not exist in the ledger. */
    RpcDelegateActNotFound = 68,  /**< Delegate account does not exist in the ledger. */
    RpcSrcCurMalformed = 69,      /**< Source currency is malformed. */
    RpcSrcIsrMalformed = 70,      /**< Source issuer is malformed. */
    RpcStreamMalformed = 71,      /**< Subscription stream specification is malformed. */
    RpcAtxDeprecated = 72,        /**< Deprecated API endpoint; use the current API. */

    // Internal error (should never happen)
    RpcInternal = 73,               /**< Generic internal server error. */
    RpcNotImpl = 74,                /**< Feature not yet implemented. */
    RpcNotSupported = 75,           /**< Operation not supported by this server. */
    RpcBadKeyType = 76,             /**< Key type is not supported. */
    RpcDbDeserialization = 77,      /**< Failed to deserialize an object from the database. */
    RpcExcessiveLgrRange = 78,      /**< Requested ledger range exceeds the 1000-ledger limit. */
    RpcInvalidLgrRange = 79,        /**< Requested ledger range bounds are logically invalid. */
    RpcExpiredValidatorList = 80,   /**< Validator list has expired; node needs an updated UNL. */

    // unused = 90,
    // DEPRECATED. New code must not use this value.
    RpcReportingUnsupported = 91,   /**< @deprecated Reporting-mode-only command sent to a non-reporting node. */

    RpcObjectNotFound = 92,         /**< Requested ledger object was not found. */

    // AMM
    RpcIssueMalformed = 93,         /**< AMM asset issue specification is malformed. */

    // Oracle
    RpcOracleMalformed = 94,        /**< Oracle request is malformed. */

    // deposit_authorized + credentials
    RpcBadCredentials = 95,         /**< Credentials do not exist, are not accepted, or have expired. */

    // Simulate
    RpcTxSigned = 96,               /**< Simulate rejected a pre-signed transaction. */

    // Pathfinding
    RpcDomainMalformed = 97,        /**< Domain field is malformed. */

    // ledger_entry
    RpcEntryNotFound = 98,          /**< Requested ledger entry was not found. */
    RpcUnexpectedLedgerType = 99,   /**< Ledger entry type does not match the request. */

    RpcLast = RpcUnexpectedLedgerType  /**< Sentinel: always equal to the highest assigned code. */
};

/** Numeric codes returned in the `warnings` array of certain RPC responses.
 *
 *  Warning codes appear alongside a successful result (not in the top-level
 *  `error` field) and inform the caller of advisory conditions such as
 *  imminent amendment blocking or a deprecated field being used.
 *
 *  Values start at 1001 to be clearly distinct from `ErrorCodeI` values and
 *  must remain **stable** — external implementations such as Clio hardcode
 *  specific values (notably `WarnRpcFieldsDeprecated = 2004`).
 */
// Protocol-wide, 50+ files
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum WarningCodeI {
    WarnRpcUnsupportedMajority = 1001,  /**< A non-default amendment has gained majority support. */
    WarnRpcAmendmentBlocked = 1002,     /**< Node is amendment-blocked and needs an upgrade. */
    WarnRpcExpiredValidatorList = 1003, /**< Validator list has expired. */
    // unused = 1004
    WarnRpcFieldsDeprecated = 2004,  /**< Request used one or more deprecated fields.
                                      *   @note Value must stay fixed at 2004; Clio hardcodes it. */
};

//------------------------------------------------------------------------------

// VFALCO NOTE these should probably not be in the RPC namespace.

namespace RPC {

/** Binds an `ErrorCodeI` to its human-readable token, default message, and HTTP status.
 *
 *  Instances are stored in a compile-time-validated array in `ErrorCodes.cpp`
 *  and returned by reference from `getErrorInfo()`. `Json::StaticString` fields
 *  hold raw `const char*` pointers to string literals, avoiding heap allocation
 *  when the token is written into a JSON response.
 *
 *  The `http_status` field drives load-balancer failover semantics: errors that
 *  indicate a node is temporarily unable to serve (e.g., amendment-blocked,
 *  too-busy) use 5xx/429 so a proxy can redirect to a healthy peer; client-fault
 *  errors use 4xx; everything else defaults to 200 for backward compatibility.
 */
struct ErrorInfo
{
    /** Default-constructs an unknown-error entry.
     *
     *  Required so that `std::array<ErrorInfo, N>` can be value-initialised
     *  during `constexpr` evaluation of the lookup table.
     */
    constexpr ErrorInfo()
        : code(RpcUnknown), token("unknown"), message("An unknown error code."), http_status(200)
    {
    }

    /** Constructs an `ErrorInfo` with HTTP status defaulting to 200.
     *
     *  @param code     The `ErrorCodeI` value this entry represents.
     *  @param token    Short machine-readable string token (e.g., `"invalidParams"`).
     *  @param message  Default human-readable error message.
     */
    constexpr ErrorInfo(ErrorCodeI code, char const* token, char const* message)
        : code(code), token(token), message(message), http_status(200)
    {
    }

    /** Constructs an `ErrorInfo` with an explicit HTTP status.
     *
     *  @param code       The `ErrorCodeI` value this entry represents.
     *  @param token      Short machine-readable string token.
     *  @param message    Default human-readable error message.
     *  @param httpStatus HTTP status code returned to clients and load balancers.
     */
    constexpr ErrorInfo(ErrorCodeI code, char const* token, char const* message, int httpStatus)
        : code(code), token(token), message(message), http_status(httpStatus)
    {
    }

    ErrorCodeI code;           /**< Numeric error code. */
    json::StaticString token;  /**< Short machine-readable string token (e.g., `"invalidParams"`). */
    json::StaticString message; /**< Default human-readable error message. */
    int http_status;           /**< HTTP status for this error; 200 unless overridden. */
};

/** Look up the `ErrorInfo` for a given error code.
 *
 *  Performs a single bounds check followed by a direct array subscript —
 *  O(1) with no hash table or binary search.
 *
 *  @param code  The error code to look up.
 *  @return A `const` reference to the matching `ErrorInfo`, or to an
 *      internal unknown-error sentinel if @p code is outside the range
 *      `(RpcSuccess, RpcLast]`.
 */
ErrorInfo const&
getErrorInfo(ErrorCodeI code);

/** Stamp `error`, `error_code`, and `error_message` fields onto a JSON object.
 *
 *  Uses the default message registered for @p code. Any existing values for
 *  those three fields are overwritten.
 *
 *  @param code  The RPC error code whose metadata to inject.
 *  @param json  The JSON object to mutate.
 */
/** @{ */
void
injectError(ErrorCodeI code, json::Value& json);

/** Stamp `error`, `error_code`, and `error_message` fields onto a JSON object,
 *  replacing the default message with a caller-supplied string.
 *
 *  The machine-readable `error` token and numeric `error_code` are taken from
 *  the registry; only `error_message` is overridden, enabling context-specific
 *  diagnostics (e.g., naming the exact malformed field) while keeping the
 *  stable fields intact.
 *
 *  @param code     The RPC error code whose token and numeric code to inject.
 *  @param message  Context-specific human-readable message.
 *  @param json     The JSON object to mutate.
 */
void
injectError(ErrorCodeI code, std::string const& message, json::Value& json);
/** @} */

/** Construct a fresh JSON error object for the given code.
 *
 *  Convenience wrapper around `injectError` for handlers that build a response
 *  from scratch rather than annotating an existing object.
 *
 *  @param code  The RPC error code.
 *  @return A new `Json::Value` object with `error`, `error_code`, and
 *      `error_message` populated from the registry.
 */
/** @{ */
json::Value
makeError(ErrorCodeI code);

/** Construct a fresh JSON error object with a caller-supplied message.
 *
 *  @param code     The RPC error code.
 *  @param message  Context-specific message written to `error_message`.
 *  @return A new `Json::Value` object with `error` and `error_code` from
 *      the registry and `error_message` set to @p message.
 */
json::Value
makeError(ErrorCodeI code, std::string const& message);
/** @} */

/** Construct an `rpcINVALID_PARAMS` error object with a caller-supplied message.
 *
 *  Thin wrapper around `makeError(RpcInvalidParams, message)` used by the
 *  field-error helper family below to avoid repetitive code at every
 *  parameter-validation site.
 *
 *  @param message  Human-readable description of the parameter problem.
 *  @return A new `Json::Value` error object for `RpcInvalidParams`.
 */
inline json::Value
makeParamError(std::string const& message)
{
    return makeError(RpcInvalidParams, message);
}

/** Format a "missing field" diagnostic string.
 *
 *  @param name  The field name that was absent.
 *  @return The string `"Missing field '<name>'."`.
 */
inline std::string
missingFieldMessage(std::string const& name)
{
    return "Missing field '" + name + "'.";
}

/** Return an `rpcINVALID_PARAMS` error for a missing required field.
 *
 *  @param name  The name of the missing field.
 *  @return A new JSON error object with a "Missing field" message.
 */
inline json::Value
missingFieldError(std::string const& name)
{
    return makeParamError(missingFieldMessage(name));
}

/** @copydoc missingFieldError(std::string const&)
 *
 *  @param name  The name of the missing field as a `Json::StaticString`.
 */
inline json::Value
missingFieldError(json::StaticString name)
{
    return missingFieldError(std::string(name));
}

/** Format a "field is not an object" diagnostic string.
 *
 *  @param name  The field name that was expected to be an object.
 *  @return The string `"Invalid field '<name>', not object."`.
 */
inline std::string
objectFieldMessage(std::string const& name)
{
    return "Invalid field '" + name + "', not object.";
}

/** Return an `rpcINVALID_PARAMS` error for a field that must be an object.
 *
 *  @param name  The name of the field with the wrong type.
 *  @return A new JSON error object with a "not object" message.
 */
inline json::Value
objectFieldError(std::string const& name)
{
    return makeParamError(objectFieldMessage(name));
}

/** @copydoc objectFieldError(std::string const&)
 *
 *  @param name  The field name as a `Json::StaticString`.
 */
inline json::Value
objectFieldError(json::StaticString name)
{
    return objectFieldError(std::string(name));
}

/** Format a generic "invalid field" diagnostic string.
 *
 *  @param name  The field name that was invalid.
 *  @return The string `"Invalid field '<name>'."`.
 */
inline std::string
invalidFieldMessage(std::string const& name)
{
    return "Invalid field '" + name + "'.";
}

/** @copydoc invalidFieldMessage(std::string const&)
 *
 *  @param name  The field name as a `Json::StaticString`.
 */
inline std::string
invalidFieldMessage(json::StaticString name)
{
    return invalidFieldMessage(std::string(name));
}

/** Return an `rpcINVALID_PARAMS` error for a field that failed generic validation.
 *
 *  @param name  The name of the invalid field.
 *  @return A new JSON error object with an "Invalid field" message.
 */
inline json::Value
invalidFieldError(std::string const& name)
{
    return makeParamError(invalidFieldMessage(name));
}

/** @copydoc invalidFieldError(std::string const&)
 *
 *  @param name  The field name as a `Json::StaticString`.
 */
inline json::Value
invalidFieldError(json::StaticString name)
{
    return invalidFieldError(std::string(name));
}

/** Format a "field has wrong type" diagnostic string.
 *
 *  @param name  The field name.
 *  @param type  The expected type description (e.g., `"unsigned integer"`).
 *  @return The string `"Invalid field '<name>', not <type>."`.
 */
inline std::string
expectedFieldMessage(std::string const& name, std::string const& type)
{
    return "Invalid field '" + name + "', not " + type + ".";
}

/** @copydoc expectedFieldMessage(std::string const&, std::string const&)
 *
 *  @param name  The field name as a `Json::StaticString`.
 *  @param type  The expected type description.
 */
inline std::string
expectedFieldMessage(json::StaticString name, std::string const& type)
{
    return expectedFieldMessage(std::string(name), type);
}

/** Return an `rpcINVALID_PARAMS` error for a field whose value has the wrong type.
 *
 *  @param name  The name of the field with the wrong type.
 *  @param type  The expected type description (e.g., `"unsigned integer"`).
 *  @return A new JSON error object with a "not <type>" message.
 */
inline json::Value
expectedFieldError(std::string const& name, std::string const& type)
{
    return makeParamError(expectedFieldMessage(name, type));
}

/** @copydoc expectedFieldError(std::string const&, std::string const&)
 *
 *  @param name  The field name as a `Json::StaticString`.
 *  @param type  The expected type description.
 */
inline json::Value
expectedFieldError(json::StaticString name, std::string const& type)
{
    return expectedFieldError(std::string(name), type);
}

/** Return an `rpcINVALID_PARAMS` error for commands that require a validator node.
 *
 *  Used by the handful of commands (e.g., `validator_info`) that are only
 *  meaningful when the local node is a validator.
 *
 *  @return A new JSON error object with the message `"not a validator"`.
 */
inline json::Value
notValidatorError()
{
    return makeParamError("not a validator");
}

/** @} */

/** Return `true` if @p json represents an RPC error response.
 *
 *  The canonical test used throughout the RPC layer to distinguish error
 *  responses from successful ones. Only the presence of the `"error"` key
 *  is checked; the specific code is not inspected.
 *
 *  @param json  The JSON value to probe.
 *  @return `true` if @p json is an object containing an `"error"` member.
 *  @see getErrorInfo() for code-level branching on a known error.
 */
bool
containsError(json::Value const& json);

/** Return the HTTP status integer associated with an error code.
 *
 *  Used by the HTTP transport layer when constructing response headers.
 *  HTTP status assignments follow load-balancer failover semantics: transient
 *  server-side conditions (amendment-blocked, too-busy, not-synced) use 5xx
 *  or 429 so proxies can retry on a healthy peer; client-fault errors use
 *  4xx; codes with no explicit assignment default to 200.
 *
 *  @param code  The RPC error code.
 *  @return HTTP status integer (e.g., 200, 400, 403, 503).
 */
int
errorCodeHttpStatus(ErrorCodeI code);

}  // namespace RPC

/** Concatenate the `error` token and `error_message` from a JSON error value.
 *
 *  Convenience helper for producing logging and diagnostic strings from an
 *  already-constructed RPC error object.
 *
 *  @param jv  A `Json::Value` that must contain an RPC error
 *      (i.e., `RPC::containsError(jv)` is `true`).
 *  @return The `error` token string concatenated with the `error_message`
 *      string, with no separator.
 *  @note An `XRPL_ASSERT` fires in debug builds if @p jv does not contain
 *      an error, making misuse diagnosable early.
 */
std::string
rpcErrorString(json::Value const& jv);

}  // namespace xrpl
