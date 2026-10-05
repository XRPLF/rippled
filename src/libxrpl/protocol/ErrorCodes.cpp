#include <xrpl/protocol/ErrorCodes.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace xrpl {
namespace rpc {

namespace detail {

// Unordered array of ErrorInfos, so we don't have to maintain the list
// ordering by hand.
//
// This array will be omitted from the object file; only the sorted version
// will remain in the object file.  But the string literals will remain.
//
// There's a certain amount of tension in determining the correct HTTP
// status to associate with a given RPC error.  Every row below names its
// HTTP status, and a row that omits it does not compile, since ErrorInfo has
// no constructor that leaves the status out.
//
// The codes currently selected target the load balancer fail-over use case.
// If a query fails on one node but is likely to have a positive outcome
// on a different node, then the failure should return a 4xx/5xx range
// status code.

// The rows below are aligned by hand into four columns, so that a wrong status or a mistyped token
// is visible by scanning one column. That cannot be had inside the 100-column limit once the
// longest enumerator and the longest message sit in one row. So this region is exempt from the
// limit by design rather than by oversight.
// clang-format off
static constexpr ErrorInfo kUnorderedErrorInfos[]{
    {RpcActMalformed,                             "actMalformed",                             "Account malformed.",                                                  400},
    {RpcActNotFound,                              "actNotFound",                              "Account not found.",                                                  404},
    {RpcAlreadyMultisig,                          "alreadyMultisig",                          "Already multisigned.",                                                400},
    {RpcAlreadySingleSig,                         "alreadySingleSig",                         "Already single-signed.",                                              400},
    {RpcAmendmentBlocked,                         "amendmentBlocked",                         "Amendment blocked, need upgrade.",                                    503},
    {RpcExpiredValidatorList,                     "unlBlocked",                               "Validator list expired.",                                             503},
    {RpcAtxDeprecated,                            "deprecated",                               "Use the new API or specify a ledger range.",                          400},
    {RpcBadKeyType,                               "badKeyType",                               "Bad key type.",                                                       400},
    {RpcBadFeature,                               "badFeature",                               "Feature unknown or invalid.",                                         500},
    {RpcBadIssuer,                                "badIssuer",                                "Issuer account malformed.",                                           400},
    {RpcBadMarket,                                "badMarket",                                "No such market.",                                                     404},
    {RpcBadSecret,                                "badSecret",                                "Secret does not match account.",                                      403},
    {RpcBadSeed,                                  "badSeed",                                  "Disallowed seed.",                                                    403},
    {RpcBadSyntax,                                "badSyntax",                                "Syntax error.",                                                       400},
    {RpcChannelMalformed,                         "channelMalformed",                         "Payment channel is malformed.",                                       400},
    {RpcChannelAmtMalformed,                      "channelAmtMalformed",                      "Payment channel amount is malformed.",                                400},
    {RpcCommandMissing,                           "commandMissing",                           "Missing command entry.",                                              400},
    {RpcDbDeserialization,                        "dbDeserialization",                        "Database deserialization error.",                                     502},
    {RpcDstActMalformed,                          "dstActMalformed",                          "Destination account is malformed.",                                   400},
    {RpcDstActMissing,                            "dstActMissing",                            "Destination account not provided.",                                   400},
    {RpcDstActNotFound,                           "dstActNotFound",                           "Destination account not found.",                                      404},
    {RpcDstAmtMalformed,                          "dstAmtMalformed",                          "Destination amount/currency/issuer is malformed.",                    400},
    {RpcDstAmtMissing,                            "dstAmtMissing",                            "Destination amount/currency/issuer is missing.",                      400},
    {RpcDstIsrMalformed,                          "dstIsrMalformed",                          "Destination issuer is malformed.",                                    400},
    {RpcExcessiveLgrRange,                        "excessiveLgrRange",                        "Ledger range exceeds 1000.",                                          400},
    {RpcForbidden,                                "forbidden",                                "Bad credentials.",                                                    403},
    {RpcHighFee,                                  "highFee",                                  "Current transaction fee exceeds your limit.",                         402},
    {RpcInternal,                                 "internal",                                 "Internal error.",                                                     500},
    {RpcInvalidLgrRange,                          "invalidLgrRange",                          "Ledger range is invalid.",                                            400},
    {RpcInvalidParams,                            "invalidParams",                            "Invalid parameters.",                                                 400},
    {RpcInvalidHotwallet,                         "invalidHotWallet",                         "Invalid hotwallet.",                                                  400},
    {RpcIssueMalformed,                           "issueMalformed",                           "Issue is malformed.",                                                 400},
    {RpcJsonRpc,                                  "json_rpc",                                 "JSON-RPC transport error.",                                           500},
    {RpcLgrIdxsInvalid,                           "lgrIdxsInvalid",                           "Ledger indexes invalid.",                                             400},
    {RpcLgrIdxMalformed,                          "lgrIdxMalformed",                          "Ledger index malformed.",                                             400},
    {RpcLgrNotFound,                              "lgrNotFound",                              "Ledger not found.",                                                   404},
    {RpcLgrNotValidated,                          "lgrNotValidated",                          "Ledger not validated.",                                               202},
    {RpcMasterDisabled,                           "masterDisabled",                           "Master key is disabled.",                                             403},
    {RpcNotEnabled,                               "notEnabled",                               "Not enabled in configuration.",                                       501},
    {RpcNotImpl,                                  "notImpl",                                  "Not implemented.",                                                    501},
    {RpcNotReady,                                 "notReady",                                 "Not ready to handle this request.",                                   503},
    {RpcNotSupported,                             "notSupported",                             "Operation not supported.",                                            501},
    {RpcNoClosed,                                 "noClosed",                                 "Closed ledger is unavailable.",                                       503},
    {RpcNoCurrent,                                "noCurrent",                                "Current ledger is unavailable.",                                      503},
    {RpcNotSynced,                                "notSynced",                                "Not synced to the network.",                                          503},
    {RpcNoEvents,                                 "noEvents",                                 "Current transport does not support events.",                          405},
    {RpcNoNetwork,                                "noNetwork",                                "Not synced to the network.",                                          503},
    {RpcWrongNetwork,                             "wrongNetwork",                             "Wrong network.",                                                      503},
    {RpcNoPermission,                             "noPermission",                             "You don't have permission for this command.",                         401},
    {RpcNoPfRequest,                              "noPathRequest",                            "No pathfinding request in progress.",                                 404},
    {RpcObjectNotFound,                           "objectNotFound",                           "The requested object was not found.",                                 404},
    {RpcPublicMalformed,                          "publicMalformed",                          "Public key is malformed.",                                            400},
    {RpcSendmaxMalformed,                         "sendMaxMalformed",                         "SendMax amount malformed.",                                           400},
    {RpcSigningMalformed,                         "signingMalformed",                         "Signing of transaction is malformed.",                                400},
    {RpcSlowDown,                                 "slowDown",                                 "You are placing too much load on the server.",                        429},
    {RpcSrcActMalformed,                          "srcActMalformed",                          "Source account is malformed.",                                        400},
    {RpcSrcActMissing,                            "srcActMissing",                            "Source account not provided.",                                        400},
    {RpcSrcActNotFound,                           "srcActNotFound",                           "Source account not found.",                                           404},
    {RpcDelegateActNotFound,                      "delegateActNotFound",                      "Delegate account not found.",                                         404},
    {RpcSrcCurMalformed,                          "srcCurMalformed",                          "Source currency is malformed.",                                       400},
    {RpcSrcIsrMalformed,                          "srcIsrMalformed",                          "Source issuer is malformed.",                                         400},
    {RpcStreamMalformed,                          "malformedStream",                          "Stream malformed.",                                                   400},
    {RpcTooBusy,                                  "tooBusy",                                  "The server is too busy to help you now.",                             503},
    {RpcTxnNotFound,                              "txnNotFound",                              "Transaction not found.",                                              404},
    {RpcUnknownCommand,                           "unknownCmd",                               "Unknown method.",                                                     405},
    {RpcOracleMalformed,                          "oracleMalformed",                          "Oracle request is malformed.",                                        400},
    {RpcBadCredentials,                           "badCredentials",                           "Credentials do not exist, are not accepted, or have expired.",        400},
    {RpcTxSigned,                                 "transactionSigned",                        "Transaction should not be signed.",                                   400},
    {RpcDomainMalformed,                          "domainMalformed",                          "Domain is malformed.",                                                400},
    {RpcEntryNotFound,                            "entryNotFound",                            "Entry not found.",                                                    400},
    {RpcUnexpectedLedgerType,                     "unexpectedLedgerType",                     "Unexpected ledger type.",                                             400},
    {RpcInvalidTransaction,                       "invalidTransaction",                       "Transaction is invalid.",                                             400},
    {RpcInternalSubmit,                           "internalSubmit",                           "Internal error during submit.",                                       500},
    {RpcInternalJson,                             "internalJson",                             "Internal error during JSON handling.",                                500},
    {RpcInternalSimulate,                         "internalSimulate",                         "Internal error during simulate.",                                     500},
    {RpcFieldNotFoundTransaction,                 "fieldNotFoundTransaction",                 "Missing required field.",                                             400},
    {RpcNotYetImplemented,                        "notYetImplemented",                        "Not yet implemented.",                                                501},
    {RpcTransactionNotFound,                      "transactionNotFound",                      "Transaction not found.",                                              404},
    {RpcMalformedRequest,                         "malformedRequest",                         "Request is malformed.",                                               400},
    {RpcNotStandAlone,                            "notStandAlone",                            "Server is not running stand-alone.",                                  501},
    {RpcUnknownOption,                            "unknownOption",                            "Unknown option.",                                                     400},
    {RpcMalformedAccount,                         "malformedAccount",                         "Account is malformed.",                                               400},
    {RpcMalformedAddress,                         "malformedAddress",                         "Address is malformed.",                                               400},
    {RpcMalformedAuthorized,                      "malformedAuthorized",                      "Authorized is malformed.",                                            400},
    {RpcMalformedAuthorizedCredentials,           "malformedAuthorizedCredentials",           "AuthorizedCredentials is malformed.",                                 400},
    {RpcMalformedBridgeAccount,                   "malformedBridgeAccount",                   "BridgeAccount is malformed.",                                         400},
    {RpcMalformedBroker,                          "malformedBroker",                          "Broker is malformed.",                                                400},
    {RpcMalformedCurrency,                        "malformedCurrency",                        "Currency is malformed.",                                              400},
    {RpcMalformedDirRoot,                         "malformedDirRoot",                         "DirRoot is malformed.",                                               400},
    {RpcMalformedDocumentID,                      "malformedDocumentID",                      "DocumentID is malformed.",                                            400},
    {RpcMalformedIssue,                           "malformedIssue",                           "Issue is malformed.",                                                 400},
    {RpcMalformedIssuingChainDoor,                "malformedIssuingChainDoor",                "IssuingChainDoor is malformed.",                                      400},
    {RpcMalformedLockingChainDoor,                "malformedLockingChainDoor",                "LockingChainDoor is malformed.",                                      400},
    {RpcMalformedMPTIssuanceID,                   "malformedMPTIssuanceID",                   "MPTIssuanceID is malformed.",                                         400},
    {RpcMalformedMPTokenIssuance,                 "malformedMPTokenIssuance",                 "MPTokenIssuance is malformed.",                                       400},
    {RpcMalformedOwner,                           "malformedOwner",                           "Owner is malformed.",                                                 400},
    {RpcMalformedSeq,                             "malformedSeq",                             "Seq is malformed.",                                                   400},
    {RpcMalformedSponsee,                         "malformedSponsee",                         "Sponsee is malformed.",                                               400},
    {RpcMalformedSponsor,                         "malformedSponsor",                         "Sponsor is malformed.",                                               400},
    {RpcMalformedXChainOwnedClaimID,              "malformedXChainOwnedClaimID",              "XChainOwnedClaimID is malformed.",                                    400},
    {RpcMalformedXChainOwnedCreateAccountClaimID, "malformedXChainOwnedCreateAccountClaimID", "XChainOwnedCreateAccountClaimID is malformed.",                       400},
    {RpcApiVersionConflict,                       "apiVersionConflict",                       "Subscriptions on one connection must all name the same api_version.", 400},
};
// clang-format on

// Sort and validate unorderedErrorInfos at compile time.
template <int M, int N>
consteval auto
sortErrorInfos(ErrorInfo const (&unordered)[N]) -> std::array<ErrorInfo, M>
{
    std::array<ErrorInfo, M> ret = {};

    for (ErrorInfo const& info : unordered)
    {
        if (info.code <= RpcSuccess || info.code > RpcLast)
            throw(std::out_of_range("Invalid error_code_i"));

        // The first valid code follows RpcSuccess immediately.
        static_assert(RpcSuccess == 0, "Unexpected error_code_i layout.");
        int const index{info.code - 1};

        if (ret[index].code != RpcUnknown)
            throw(std::invalid_argument("Duplicate error_code_i in list"));

        ret[index] = info;
    }

    // Verify that all entries are filled in starting with 1 and proceeding
    // to RpcLast.
    //
    // It's okay for there to be missing entries; they will contain the code
    // RpcUnknown.  But other than that all entries should match their index.
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

constexpr auto kSortedErrorInfos{sortErrorInfos<RpcLast>(kUnorderedErrorInfos)};

constexpr ErrorInfo kUnknownError;

/**
 * Asserts that no two rows of @p sorted name the same token.
 *
 * A duplicate is then a build error rather than a reply reporting the wrong
 * `error_code`. `consteval` admits no run-time call, so the two `throw`
 * statements are how a constant evaluation fails and are otherwise unreachable.
 *
 * @param sorted The table, ordered by code, with a gap for every unused code.
 * @return True. It reports failure by failing to be a constant expression.
 */
template <std::size_t N>
consteval bool
tokensAreUnique(std::array<ErrorInfo, N> const& sorted)
{
    for (std::size_t i{0}; i < N; ++i)
    {
        // Gaps in the table name no error, so they name no token either.
        if (sorted[i].code == RpcUnknown)
            continue;

        std::string_view const token{sorted[i].token.cStr()};
        if (token.empty())
            throw(std::invalid_argument("Empty token in list"));

        for (std::size_t j{i + 1}; j < N; ++j)
        {
            if (sorted[j].code != RpcUnknown && std::string_view{sorted[j].token.cStr()} == token)
                throw(std::invalid_argument("Duplicate token in list"));
        }
    }
    return true;
}

static_assert(tokensAreUnique(kSortedErrorInfos), "Duplicate token in list");

/**
 * The tokens of @p sorted, measured at compile time.
 *
 * A lookup compares a caller's token against these rather than against the
 * table's own `char const*`, which it would have to measure first: comparing a
 * view with a pointer calls strlen on every row the scan passes. Built from the
 * same table, so the two cannot name different tokens.
 *
 * @param sorted The table, ordered by code, with a gap for every unused code.
 * @return One view per row, the empty view for a gap. No token a caller can
 *         pass is equal to the empty view.
 */
template <std::size_t N>
consteval std::array<std::string_view, N>
tokenViews(std::array<ErrorInfo, N> const& sorted)
{
    std::array<std::string_view, N> ret{};
    for (std::size_t i{0}; i < N; ++i)
    {
        if (sorted[i].code != RpcUnknown)
            ret[i] = std::string_view{sorted[i].token.cStr()};
    }
    return ret;
}

constexpr auto kSortedTokens{tokenViews(kSortedErrorInfos)};

}  // namespace detail

//------------------------------------------------------------------------------

void
injectError(ErrorCodeI code, json::Value& json)
{
    ErrorInfo const& info(getErrorInfo(code));
    json[jss::error] = info.token;
    json[jss::error_code] = info.code;
    json[jss::error_message] = info.message;
}

void
injectError(ErrorCodeI code, std::string const& message, json::Value& json)
{
    ErrorInfo const& info(getErrorInfo(code));
    json[jss::error] = info.token;
    json[jss::error_code] = info.code;
    json[jss::error_message] = message;
}

ErrorInfo const&
getErrorInfo(ErrorCodeI code)
{
    if (code <= RpcSuccess || code > RpcLast)
        return detail::kUnknownError;
    return detail::kSortedErrorInfos[code - 1];
}

ErrorCodeI
codeForToken(std::string_view token)
{
    if (token.empty())
        return RpcUnknown;

    for (std::size_t i{0}; i < detail::kSortedTokens.size(); ++i)
    {
        // A gap holds the empty view, which the token is not, so a gap names no code here either.
        if (detail::kSortedTokens[i] == token)
            return detail::kSortedErrorInfos[i].code;
    }
    return RpcUnknown;
}

json::Value
makeError(ErrorCodeI code)
{
    json::Value json;
    injectError(code, json);
    return json;
}

json::Value
makeError(ErrorCodeI code, std::string const& message)
{
    json::Value json;
    injectError(code, message, json);
    return json;
}

bool
containsError(json::Value const& json)
{
    return json.isObject() && json.isMember(jss::error);
}

int
errorCodeHttpStatus(ErrorCodeI code)
{
    return getErrorInfo(code).httpStatus;
}

}  // namespace rpc

std::string
rpcErrorString(json::Value const& jv)
{
    XRPL_ASSERT(rpc::containsError(jv), "xrpl::rpc::rpcErrorString : input contains an error");
    return jv[jss::error].asString() + jv[jss::error_message].asString();
}

}  // namespace xrpl
