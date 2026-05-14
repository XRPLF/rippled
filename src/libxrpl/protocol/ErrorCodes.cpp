/** @file
 *  RPC error code registry and JSON serialization for the XRPL RPC layer.
 *
 *  Defines the complete set of named error conditions that can be returned to
 *  API clients, a compile-time-validated lookup table sorted by error code,
 *  O(1) metadata retrieval, JSON stamping helpers, and the HTTP-status mapping
 *  used by the transport layer for load-balancer failover decisions.
 */

#include <xrpl/protocol/ErrorCodes.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>

#include <array>
#include <stdexcept>
#include <string>

namespace xrpl {
namespace RPC {

namespace detail {

/** Authoring table of every recognized RPC error condition.
 *
 *  Entries may appear in any order and are grouped thematically for
 *  readability. `sortErrorInfos` re-indexes them into `kSORTED_ERROR_INFOS`
 *  at compile time, so numeric ordering is never a maintenance concern here.
 *
 *  HTTP status assignments target the load-balancer failover use case.
 *  Errors that might resolve on a different node (amendment-blocked,
 *  not-synced, too-busy) use 5xx/429 so upstream proxies can retry
 *  elsewhere. Permanent or client-side errors (bad syntax, bad credentials)
 *  use 4xx. Everything else defaults to 200 (OK), preserving the original
 *  JSON-RPC semantics where the HTTP transaction always succeeded.
 *
 *  @note This array is elided from the object file; only the sorted copy
 *      `kSORTED_ERROR_INFOS` is retained. The string literals do remain.
 */
// clang-format off
constexpr static ErrorInfo kUNORDERED_ERROR_INFOS[]{
    {RpcActMalformed,          "actMalformed",         "Account malformed."},
    {RpcActNotFound,          "actNotFound",          "Account not found."},
    {RpcAlreadyMultisig,       "alreadyMultisig",      "Already multisigned."},
    {RpcAlreadySingleSig,     "alreadySingleSig",     "Already single-signed."},
    {RpcAmendmentBlocked,      "amendmentBlocked",     "Amendment blocked, need upgrade.", 503},
    {RpcExpiredValidatorList, "unlBlocked",           "Validator list expired.", 503},
    {RpcAtxDeprecated,         "deprecated",           "Use the new API or specify a ledger range.", 400},
    {RpcBadKeyType,           "badKeyType",           "Bad key type.", 400},
    {RpcBadFeature,            "badFeature",           "Feature unknown or invalid.", 500},
    {RpcBadIssuer,             "badIssuer",            "Issuer account malformed.", 400},
    {RpcBadMarket,             "badMarket",            "No such market.", 404},
    {RpcBadSecret,             "badSecret",            "Secret does not match account.", 403},
    {RpcBadSeed,               "badSeed",              "Disallowed seed.", 403},
    {RpcBadSyntax,             "badSyntax",            "Syntax error.", 400},
    {RpcChannelMalformed,      "channelMalformed",     "Payment channel is malformed.", 400},
    {RpcChannelAmtMalformed,  "channelAmtMalformed",  "Payment channel amount is malformed.", 400},
    {RpcCommandMissing,        "commandMissing",       "Missing command entry.", 400},
    {RpcDbDeserialization,     "dbDeserialization",    "Database deserialization error.", 502},
    {RpcDstActMalformed,      "dstActMalformed",      "Destination account is malformed.", 400},
    {RpcDstActMissing,        "dstActMissing",        "Destination account not provided.", 400},
    {RpcDstActNotFound,      "dstActNotFound",       "Destination account not found.", 404},
    {RpcDstAmtMalformed,      "dstAmtMalformed",      "Destination amount/currency/issuer is malformed.", 400},
    {RpcDstAmtMissing,        "dstAmtMissing",        "Destination amount/currency/issuer is missing.", 400},
    {RpcDstIsrMalformed,      "dstIsrMalformed",      "Destination issuer is malformed.", 400},
    {RpcExcessiveLgrRange,    "excessiveLgrRange",    "Ledger range exceeds 1000.", 400},
    {RpcForbidden,              "forbidden",            "Bad credentials.", 403},
    {RpcHighFee,               "highFee",              "Current transaction fee exceeds your limit.", 402},
    {RpcInternal,               "internal",             "Internal error.", 500},
    {RpcInvalidLgrRange,      "invalidLgrRange",      "Ledger range is invalid.", 400},
    {RpcInvalidParams,         "invalidParams",        "Invalid parameters.", 400},
    {RpcInvalidHotwallet,      "invalidHotWallet",     "Invalid hotwallet.", 400},
    {RpcIssueMalformed,        "issueMalformed",       "Issue is malformed.", 400},
    {RpcJsonRpc,               "json_rpc",             "JSON-RPC transport error.", 500},
    {RpcLgrIdxsInvalid,       "lgrIdxsInvalid",       "Ledger indexes invalid.", 400},
    {RpcLgrIdxMalformed,      "lgrIdxMalformed",      "Ledger index malformed.", 400},
    {RpcLgrNotFound,          "lgrNotFound",          "Ledger not found.", 404},
    {RpcLgrNotValidated,      "lgrNotValidated",      "Ledger not validated.", 202},
    {RpcMasterDisabled,        "masterDisabled",       "Master key is disabled.", 403},
    {RpcNotEnabled,            "notEnabled",           "Not enabled in configuration.", 501},
    {RpcNotImpl,               "notImpl",              "Not implemented.", 501},
    {RpcNotReady,              "notReady",             "Not ready to handle this request.", 503},
    {RpcNotSupported,          "notSupported",         "Operation not supported.", 501},
    {RpcNoClosed,              "noClosed",             "Closed ledger is unavailable.", 503},
    {RpcNoCurrent,             "noCurrent",            "Current ledger is unavailable.", 503},
    {RpcNotSynced,             "notSynced",            "Not synced to the network.", 503},
    {RpcNoEvents,              "noEvents",             "Current transport does not support events.", 405},
    {RpcNoNetwork,             "noNetwork",            "Not synced to the network.", 503},
    {RpcWrongNetwork,          "wrongNetwork",         "Wrong network.", 503},
    {RpcNoPermission,          "noPermission",         "You don't have permission for this command.", 401},
    {RpcNoPfRequest,          "noPathRequest",        "No pathfinding request in progress.", 404},
    {RpcObjectNotFound,       "objectNotFound",       "The requested object was not found.", 404},
    {RpcPublicMalformed,       "publicMalformed",      "Public key is malformed.", 400},
    {RpcSendmaxMalformed,      "sendMaxMalformed",     "SendMax amount malformed.", 400},
    {RpcSigningMalformed,      "signingMalformed",     "Signing of transaction is malformed.", 400},
    {RpcSlowDown,              "slowDown",             "You are placing too much load on the server.", 429},
    {RpcSrcActMalformed,      "srcActMalformed",      "Source account is malformed.", 400},
    {RpcSrcActMissing,        "srcActMissing",        "Source account not provided.", 400},
    {RpcSrcActNotFound,      "srcActNotFound",       "Source account not found.", 404},
    {RpcDelegateActNotFound, "delegateActNotFound",  "Delegate account not found.", 404},
    {RpcSrcCurMalformed,      "srcCurMalformed",      "Source currency is malformed.", 400},
    {RpcSrcIsrMalformed,      "srcIsrMalformed",      "Source issuer is malformed.", 400},
    {RpcStreamMalformed,       "malformedStream",      "Stream malformed.", 400},
    {RpcTooBusy,               "tooBusy",              "The server is too busy to help you now.", 503},
    {RpcTxnNotFound,          "txnNotFound",          "Transaction not found.", 404},
    {RpcUnknownCommand,        "unknownCmd",           "Unknown method.", 405},
    {RpcOracleMalformed,       "oracleMalformed",      "Oracle request is malformed.", 400},
    {RpcBadCredentials,        "badCredentials",       "Credentials do not exist, are not accepted, or have expired.", 400},
    {RpcTxSigned,              "transactionSigned",    "Transaction should not be signed.", 400},
    {RpcDomainMalformed,       "domainMalformed",      "Domain is malformed.", 400},
    {RpcEntryNotFound,        "entryNotFound",        "Entry not found.", 400},
    {RpcUnexpectedLedgerType, "unexpectedLedgerType", "Unexpected ledger type.", 400},
};
// clang-format on

/** Sort and integrity-check an unordered `ErrorInfo` array at compile time.
 *
 *  Produces a `std::array<ErrorInfo, M>` indexed by `code - 1`, enabling
 *  O(1) lookup in `getErrorInfo()`. Three invariants are enforced:
 *
 *  1. **Range** — every code must satisfy `rpcSUCCESS < code <= rpcLAST`;
 *     codes outside that window throw `std::out_of_range`.
 *  2. **Uniqueness** — if two entries claim the same slot, the second
 *     throws `std::invalid_argument` (duplicate code).
 *  3. **Contiguity** — after placement, each occupied slot must carry the
 *     code value that matches its index position, and the count of occupied
 *     slots must equal `N` (the input size). This rejects off-by-one typos
 *     that would silently land an entry at the wrong index.
 *
 *  @tparam M  Output array size; must equal `rpcLAST`.
 *  @tparam N  Input array size; deduced from the argument.
 *  @param  unordered  C-array of `ErrorInfo` entries in any order.
 *  @return A `std::array<ErrorInfo, M>` in canonical index order.
 *  @throw std::out_of_range       If any code is outside `(rpcSUCCESS, rpcLAST]`.
 *  @throw std::invalid_argument   If any code is duplicated or the contiguity
 *      check fails.
 *  @note Should become `consteval` when the codebase moves to C++20.
 */
template <int M, int N>
constexpr auto
sortErrorInfos(ErrorInfo const (&unordered)[N]) -> std::array<ErrorInfo, M>
{
    std::array<ErrorInfo, M> ret = {};

    for (ErrorInfo const& info : unordered)
    {
        if (info.code <= RpcSuccess || info.code > RpcLast)
            throw(std::out_of_range("Invalid error_code_i"));

        // rpcSUCCESS == 0, so the first valid code maps to index 0.
        static_assert(RpcSuccess == 0, "Unexpected error_code_i layout.");
        int const index{info.code - 1};

        if (ret[index].code != RpcUnknown)
            throw(std::invalid_argument("Duplicate error_code_i in list"));

        ret[index] = info;
    }

    // Slots left at rpcUNKNOWN represent intentional gaps in the enum
    // (retired or reserved codes). All other slots must match their index.
    int codeCount{0};
    int expect{RpcBadSyntax - 1};
    for (ErrorInfo const& info : ret)
    {
        ++expect;
        if (info.code == RpcUnknown)
            continue;

        if (info.code != expect)
            throw(std::invalid_argument("Empty error_code_i in list"));
        ++codeCount;
    }
    if (expect != RpcLast)
        throw(std::invalid_argument("Insufficient list entries"));
    if (codeCount != N)
        throw(std::invalid_argument("Bad handling of unorderedErrorInfos"));

    return ret;
}

/** Compile-time lookup table indexed by `code - 1`.
 *
 *  Built from `kUNORDERED_ERROR_INFOS` by `sortErrorInfos`, which validates
 *  range, uniqueness, and contiguity. Gaps in `ErrorCodeI` remain as
 *  default-constructed `ErrorInfo` entries whose `code` field equals
 *  `rpcUNKNOWN`. `getErrorInfo()` accesses this array via direct subscript.
 */
constexpr auto kSORTED_ERROR_INFOS{sortErrorInfos<RpcLast>(kUNORDERED_ERROR_INFOS)};

/** Fallback returned by `getErrorInfo()` for out-of-range or unknown codes. */
constexpr ErrorInfo kUNKNOWN_ERROR;

}  // namespace detail

//------------------------------------------------------------------------------

/** Stamp `error`, `error_code`, and `error_message` fields onto @p json using
 *  the default message for @p code.
 *
 *  @param code  The RPC error code whose metadata to inject.
 *  @param json  The JSON object to mutate; existing fields are overwritten.
 */
void
injectError(ErrorCodeI code, json::Value& json)
{
    ErrorInfo const& info(getErrorInfo(code));
    json[jss::error] = info.token;
    json[jss::error_code] = info.code;
    json[jss::error_message] = info.message;
}

/** Stamp `error`, `error_code`, and `error_message` fields onto @p json,
 *  replacing the default message with a caller-supplied diagnostic string.
 *
 *  The `error` token and numeric `error_code` are taken from the registry;
 *  only `error_message` is overridden, enabling context-specific diagnostics
 *  while keeping the stable machine-readable fields intact.
 *
 *  @param code     The RPC error code whose token and numeric code to inject.
 *  @param message  Context-specific human-readable message to use instead of
 *      the registry default.
 *  @param json     The JSON object to mutate; existing fields are overwritten.
 */
void
injectError(ErrorCodeI code, std::string const& message, json::Value& json)
{
    ErrorInfo const& info(getErrorInfo(code));
    json[jss::error] = info.token;
    json[jss::error_code] = info.code;
    json[jss::error_message] = message;
}

/** O(1) lookup of the `ErrorInfo` for @p code via direct array subscript.
 *
 *  @param code  The error code to look up.
 *  @return Reference to the matching `ErrorInfo`, or `detail::kUNKNOWN_ERROR`
 *      if @p code is out of the valid range `(rpcSUCCESS, rpcLAST]`.
 */
ErrorInfo const&
getErrorInfo(ErrorCodeI code)
{
    if (code <= RpcSuccess || code > RpcLast)
        return detail::kUNKNOWN_ERROR;
    return detail::kSORTED_ERROR_INFOS[code - 1];
}

/** Construct and return a new JSON error object for @p code.
 *
 *  @param code  The RPC error code.
 *  @return A fresh `Json::Value` object with `error`, `error_code`, and
 *      `error_message` fields populated from the registry.
 */
json::Value
makeError(ErrorCodeI code)
{
    json::Value json;
    injectError(code, json);
    return json;
}

/** Construct and return a new JSON error object with a custom message.
 *
 *  @param code     The RPC error code.
 *  @param message  Context-specific message to use for `error_message`.
 *  @return A fresh `Json::Value` object with `error` and `error_code` from
 *      the registry and `error_message` set to @p message.
 */
json::Value
makeError(ErrorCodeI code, std::string const& message)
{
    json::Value json;
    injectError(code, message, json);
    return json;
}

/** Return `true` if @p json is an object containing an `"error"` member.
 *
 *  @param json  The value to probe.
 *  @return `true` if @p json carries an RPC error; `false` otherwise.
 *  @note Only the presence of the `"error"` key is checked; the specific
 *      code is not inspected. Use `getErrorInfo()` for code-level branching.
 */
bool
containsError(json::Value const& json)
{
    return json.isObject() && json.isMember(jss::error);
}

/** Return the HTTP status integer associated with @p code.
 *
 *  Used by the HTTP transport layer when constructing the response header.
 *  Codes not given an explicit status in the registry default to 200.
 *
 *  @param code  The RPC error code.
 *  @return HTTP status integer (e.g., 200, 400, 403, 503).
 */
int
errorCodeHttpStatus(ErrorCodeI code)
{
    return getErrorInfo(code).http_status;
}

}  // namespace RPC

/** Concatenate the `error` token and `error_message` from a JSON error value.
 *
 *  Convenience helper for logging and diagnostic strings. Lives in the `xrpl`
 *  namespace rather than `xrpl::RPC` for broader accessibility.
 *
 *  @param jv  A `Json::Value` that must already contain an RPC error (i.e.,
 *      `containsError(jv)` is true).
 *  @return Concatenation of the `error` token string and `error_message`
 *      string with no separator.
 *  @note An `XRPL_ASSERT` fires in debug builds if @p jv does not contain
 *      an error, making misuse diagnosable early.
 */
std::string
rpcErrorString(json::Value const& jv)
{
    XRPL_ASSERT(RPC::containsError(jv), "xrpl::RPC::rpcErrorString : input contains an error");
    return jv[jss::error].asString() + jv[jss::error_message].asString();
}

}  // namespace xrpl
