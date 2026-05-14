/** @file
 *  Implements the `ledger_entry` RPC handler, which fetches a single ledger
 *  state object (SLE) from a specified ledger by computing its `uint256` index
 *  key from a semantic descriptor and returning it as JSON or raw binary.
 *
 *  Each supported entry type has a dedicated parser function that converts
 *  the client-supplied JSON field into a `uint256` key via the appropriate
 *  `keylet::*` constructor.  A static dispatch table built from the
 *  `ledger_entries.macro` X-macro drives the central `doLedgerEntry` handler.
 *
 *  The parallel `doLedgerEntryGrpc` function performs the same lookup over
 *  gRPC, accepting a raw 32-byte key and always returning serialized bytes.
 */
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/GRPCHandlers.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/handlers/ledger/LedgerEntryHelpers.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/json/json_errors.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <grpcpp/support/status.h>
#include <org/xrpl/rpc/v1/get_ledger_entry.pb.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace xrpl {

/** Signature shared by all per-entry-type parser functions.
 *
 *  @param params     The JSON value for the entry descriptor field (or the
 *      entire `context.params` object for the `bridge` special case).
 *  @param fieldName  The `jss` name of the descriptor field, used in error
 *      messages.
 *  @param apiVersion The negotiated API version; controls error format and
 *      accepted input shapes.
 *  @return The computed `uint256` ledger-state key on success, or a
 *      fully-formed error `Json::Value` on failure.
 */
using FunctionType = std::function<Expected<uint256, json::Value>(
    json::Value const&,
    json::StaticString const,
    unsigned const apiVersion)>;

static Expected<uint256, json::Value>
parseFixed(
    Keylet const& keylet,
    json::Value const& params,
    json::StaticString const& fieldName,
    unsigned const apiVersion);

/** Create a `FunctionType` parser for a singleton ledger object at a fixed key.
 *
 *  Singletons (Amendments, Fee Settings, Negative UNL) have well-known keys
 *  that require no parameter derivation.  The returned lambda accepts either
 *  `true` (fetch the singleton) or a raw hex string (direct key lookup) and
 *  delegates to `parseFixed`.
 *
 *  @param keylet  The fixed `Keylet` whose key is captured into the lambda.
 *  @return A `FunctionType` callable that resolves the singleton's key.
 */
static FunctionType
fixed(Keylet const& keylet)
{
    return [keylet](
               json::Value const& params,
               json::StaticString const fieldName,
               unsigned const apiVersion) -> Expected<uint256, json::Value> {
        return parseFixed(keylet, params, fieldName, apiVersion);
    };
}

/** Parse a `uint256` ledger-state key directly from a hex string.
 *
 *  Used as the fallback path in dual-form parsers when the caller supplies
 *  a raw hex key instead of a semantic descriptor object.
 *
 *  @param params        The JSON value expected to be a 64-character hex string.
 *  @param fieldName     The field name used in error messages.
 *  @param expectedType  Human-readable description of the accepted value type,
 *      used in error messages.
 *  @return The parsed key on success, or an `rpcINVALID_PARAMS` error on
 *      failure.
 */
static Expected<uint256, json::Value>
parseObjectID(
    json::Value const& params,
    json::StaticString const fieldName,
    std::string const& expectedType = "hex string or object")
{
    if (auto const uNodeIndex = LedgerEntryHelpers::parse<uint256>(params))
    {
        return *uNodeIndex;
    }
    return LedgerEntryHelpers::invalidFieldError("malformedRequest", fieldName, expectedType);
}

/** Parse the `index` field, supporting direct hex keys and (API v3+) singleton aliases.
 *
 *  In API version 3 and later, the following plain string aliases are
 *  recognised without knowing the underlying hash:
 *  - `"amendments"` — the Amendments singleton.
 *  - `"fee"` — the Fee Settings singleton.
 *  - `"nunl"` — the Negative UNL singleton.
 *  - `"hashes"` — the short skip list (hashes since the last flag ledger).
 *
 *  For all other values, or for API version ≤ 2, falls through to raw hex
 *  parsing via `parseObjectID`.
 *
 *  @param params      The value of the `index` field in the request.
 *  @param fieldName   Always `jss::index`; passed through to error messages.
 *  @param apiVersion  Determines whether string aliases are accepted.
 *  @return The resolved `uint256` key, or an `rpcINVALID_PARAMS` error.
 *  @note The `"hashes"` alias resolves to the *short* skip list.  To fetch
 *      the long (epoch-indexed) skip list, supply its raw hex key directly.
 */
static Expected<uint256, json::Value>
parseIndex(json::Value const& params, json::StaticString const fieldName, unsigned const apiVersion)
{
    if (apiVersion > 2u && params.isString())
    {
        std::string const index = params.asString();
        if (index == jss::amendments.cStr())
            return keylet::amendments().key;
        if (index == jss::fee.cStr())
            return keylet::fees().key;
        if (index == jss::nunl)
            return keylet::negativeUNL().key;
        if (index == jss::hashes)
        {
            // Note this only finds the "short" skip list. Use "hashes":index to
            // get the long list.
            return keylet::skip().key;
        }
    }
    return parseObjectID(params, fieldName, "hex string");
}

/** Compute the `AccountRoot` key for a given account address.
 *
 *  Accepts a Base58-encoded `AccountID` string and returns
 *  `keylet::account(*account).key`.  Zero-value accounts are rejected.
 *
 *  @param params     The JSON string containing the Base58Check account address.
 *  @param fieldName  Used in error messages; typically `jss::account_root`.
 *  @param apiVersion Unused; present for uniform `FunctionType` signature.
 *  @return The `uint256` key on success, or a `malformedAddress` error.
 */
static Expected<uint256, json::Value>
parseAccountRoot(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (auto const account = LedgerEntryHelpers::parse<AccountID>(params))
    {
        return keylet::account(*account).key;
    }

    return LedgerEntryHelpers::invalidFieldError("malformedAddress", fieldName, "AccountID");
}

/** Parser for the Amendments singleton; delegates to `fixed(keylet::amendments())`. */
auto const parseAmendments = fixed(keylet::amendments());

/** Compute the AMM ledger-entry key from an asset pair or a raw hex key.
 *
 *  Accepts either a 64-character hex string (direct lookup) or a JSON object
 *  with `asset` and `asset2` fields describing the two assets in the pool.
 *  The pair is passed to `keylet::amm(*asset, *asset2)`.
 *
 *  @param params     Hex string or object with `asset` and `asset2`.
 *  @param fieldName  Used in error messages.
 *  @param apiVersion Unused; present for uniform `FunctionType` signature.
 *  @return The `uint256` key, or an `rpcINVALID_PARAMS` error.
 */
static Expected<uint256, json::Value>
parseAMM(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName);
    }

    if (auto const value = LedgerEntryHelpers::hasRequired(params, {jss::asset, jss::asset2});
        !value)
    {
        return Unexpected(value.error());
    }

    auto const asset = LedgerEntryHelpers::requiredAsset(params, jss::asset, "malformedRequest");
    if (!asset)
        return Unexpected(asset.error());

    auto const asset2 = LedgerEntryHelpers::requiredAsset(params, jss::asset2, "malformedRequest");
    if (!asset2)
        return Unexpected(asset2.error());

    return keylet::amm(*asset, *asset2).key;
}

/** Compute the Bridge ledger-entry key from bridge specification fields.
 *
 *  Unlike every other parser, this function receives the **entire**
 *  `context.params` object rather than a single sub-field, because it must
 *  read two sibling fields: `bridge` (the bridge spec object) and
 *  `bridge_account` (the door account used to select locking vs. issuing
 *  chain side).
 *
 *  Accepts either:
 *  - A hex string under `bridge` for a direct key lookup.
 *  - A bridge specification object under `bridge` together with a
 *    `bridge_account`; validates that the account matches one of the bridge's
 *    door accounts, then computes `keylet::bridge(*bridge, chainType).key`.
 *
 *  @param params     The full `context.params` JSON object.
 *  @param fieldName  Always `jss::bridge`; used in error messages.
 *  @param apiVersion Unused; present for uniform `FunctionType` signature.
 *  @return The `uint256` key, or an `rpcINVALID_PARAMS` error.
 *  @note This parser is the only one in `doLedgerEntry`'s dispatch loop that
 *      receives `context.params` rather than `context.params[fieldName]`.
 */
static Expected<uint256, json::Value>
parseBridge(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!params.isMember(jss::bridge))
    {
        return Unexpected(LedgerEntryHelpers::missingFieldError(jss::bridge));
    }

    if (params[jss::bridge].isString())
    {
        return parseObjectID(params, fieldName);
    }

    auto const bridge = LedgerEntryHelpers::parseBridgeFields(params[jss::bridge]);
    if (!bridge)
        return Unexpected(bridge.error());

    auto const account = LedgerEntryHelpers::requiredAccountID(
        params, jss::bridge_account, "malformedBridgeAccount");
    if (!account)
        return Unexpected(account.error());

    STXChainBridge::ChainType const chainType =
        STXChainBridge::srcChain(account.value() == bridge->lockingChainDoor());
    if (account.value() != bridge->door(chainType))
        return LedgerEntryHelpers::malformedError("malformedRequest", "");

    return keylet::bridge(*bridge, chainType).key;
}

/** Compute the Check ledger-entry key from a raw hex string.
 *
 *  Check objects have no semantic constructor — clients must supply their
 *  256-bit key directly.  Delegates unconditionally to `parseObjectID`.
 *
 *  @param params     A 64-character hex string.
 *  @param fieldName  Used in error messages; typically `jss::check`.
 *  @param apiVersion Unused; present for uniform `FunctionType` signature.
 *  @return The `uint256` key, or an `rpcINVALID_PARAMS` error.
 */
static Expected<uint256, json::Value>
parseCheck(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    return parseObjectID(params, fieldName, "hex string");
}

/** Compute the Credential ledger-entry key from subject/issuer/type or a raw hex key.
 *
 *  Accepts either a 64-character hex string (direct lookup) or a JSON object
 *  with `subject` (AccountID), `issuer` (AccountID), and `credential_type`
 *  (hex blob, max `kMAX_CREDENTIAL_TYPE_LENGTH` bytes).  The semantic form
 *  calls `keylet::credential(*subject, *issuer, credTypeSlice).key`.
 *
 *  @param cred       Hex string or object with `subject`, `issuer`, and
 *      `credential_type` fields.
 *  @param fieldName  Used in error messages.
 *  @param apiVersion Unused; present for uniform `FunctionType` signature.
 *  @return The `uint256` key, or an `rpcINVALID_PARAMS` error.
 */
static Expected<uint256, json::Value>
parseCredential(
    json::Value const& cred,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!cred.isObject())
    {
        return parseObjectID(cred, fieldName);
    }

    auto const subject =
        LedgerEntryHelpers::requiredAccountID(cred, jss::subject, "malformedRequest");
    if (!subject)
        return Unexpected(subject.error());

    auto const issuer =
        LedgerEntryHelpers::requiredAccountID(cred, jss::issuer, "malformedRequest");
    if (!issuer)
        return Unexpected(issuer.error());

    auto const credType = LedgerEntryHelpers::requiredHexBlob(
        cred, jss::credential_type, kMAX_CREDENTIAL_TYPE_LENGTH, "malformedRequest");
    if (!credType)
        return Unexpected(credType.error());

    return keylet::credential(*subject, *issuer, Slice(credType->data(), credType->size())).key;
}

/** Compute the Delegate ledger-entry key from account/authorize pair or a raw hex key.
 *
 *  Accepts either a 64-character hex string (direct lookup) or a JSON object
 *  with `account` and `authorize` (both AccountIDs).  The semantic form calls
 *  `keylet::delegate(*account, *authorize).key`.
 *
 *  @param params     Hex string or object with `account` and `authorize`.
 *  @param fieldName  Used in error messages.
 *  @param apiVersion Unused; present for uniform `FunctionType` signature.
 *  @return The `uint256` key, or an `rpcINVALID_PARAMS` error.
 */
static Expected<uint256, json::Value>
parseDelegate(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName);
    }

    auto const account =
        LedgerEntryHelpers::requiredAccountID(params, jss::account, "malformedAddress");
    if (!account)
        return Unexpected(account.error());

    auto const authorize =
        LedgerEntryHelpers::requiredAccountID(params, jss::authorize, "malformedAddress");
    if (!authorize)
        return Unexpected(authorize.error());

    return keylet::delegate(*account, *authorize).key;
}

/** Validate and deserialise an `authorized_credentials` JSON array into an `STArray`.
 *
 *  Each element must be a JSON object containing `issuer` (AccountID) and
 *  `credential_type` (hex blob ≤ `kMAX_CREDENTIAL_TYPE_LENGTH` bytes).
 *  The array must be non-empty and must not exceed `kMAX_CREDENTIALS_ARRAY_SIZE`
 *  elements.
 *
 *  @param jv  The raw JSON array value from the request.
 *  @return An `STArray` of `sfCredential` inner objects on success, or an
 *      `rpcINVALID_PARAMS` error with a `malformedAuthorizedCredentials`
 *      error code on failure.
 *  @note The returned `STArray` is **unsorted**.  Callers must apply
 *      `credentials::makeSorted` before computing the deposit-preauth keylet.
 */
static Expected<STArray, json::Value>
parseAuthorizeCredentials(json::Value const& jv)
{
    if (!jv.isArray())
    {
        return LedgerEntryHelpers::invalidFieldError(
            "malformedAuthorizedCredentials", jss::authorized_credentials, "array");
    }

    std::uint32_t const n = jv.size();
    if (n > kMAX_CREDENTIALS_ARRAY_SIZE)
    {
        return Unexpected(
            LedgerEntryHelpers::malformedError(
                "malformedAuthorizedCredentials",
                "Invalid field '" + std::string(jss::authorized_credentials) +
                    "', array too long."));
    }

    if (n == 0)
    {
        return Unexpected(
            LedgerEntryHelpers::malformedError(
                "malformedAuthorizedCredentials",
                "Invalid field '" + std::string(jss::authorized_credentials) + "', array empty."));
    }

    STArray arr(sfAuthorizeCredentials, n);
    for (auto const& jo : jv)
    {
        if (!jo.isObject())
        {
            return LedgerEntryHelpers::invalidFieldError(
                "malformedAuthorizedCredentials", jss::authorized_credentials, "array of objects");
        }

        if (auto const value = LedgerEntryHelpers::hasRequired(
                jo, {jss::issuer, jss::credential_type}, "malformedAuthorizedCredentials");
            !value)
        {
            return Unexpected(value.error());
        }

        auto const issuer = LedgerEntryHelpers::requiredAccountID(
            jo, jss::issuer, "malformedAuthorizedCredentials");
        if (!issuer)
            return Unexpected(issuer.error());

        auto const credentialType = LedgerEntryHelpers::requiredHexBlob(
            jo,
            jss::credential_type,
            kMAX_CREDENTIAL_TYPE_LENGTH,
            "malformedAuthorizedCredentials");
        if (!credentialType)
            return Unexpected(credentialType.error());

        auto credential = STObject::makeInnerObject(sfCredential);
        credential.setAccountID(sfIssuer, *issuer);
        credential.setFieldVL(sfCredentialType, *credentialType);
        arr.pushBack(std::move(credential));
    }

    return arr;
}

/** Compute the DepositPreauth ledger-entry key from owner/authorized pair or a raw hex key.
 *
 *  Accepts either a 64-character hex string (direct lookup) or a JSON object.
 *  The object form requires `owner` (AccountID) plus **exactly one of**:
 *  - `authorized` — a single authorized AccountID; uses
 *    `keylet::depositPreauth(*owner, *authorized).key`.
 *  - `authorized_credentials` — a JSON array processed by
 *    `parseAuthorizeCredentials`; the resulting `STArray` is sorted via
 *    `credentials::makeSorted` before the keylet is computed, so credentials
 *    supplied in any order resolve to the same object.
 *
 *  @param dp         Hex string or object with `owner` and either `authorized`
 *      or `authorized_credentials`.
 *  @param fieldName  Used in error messages.
 *  @param apiVersion Unused; present for uniform `FunctionType` signature.
 *  @return The `uint256` key, or an `rpcINVALID_PARAMS` error.
 *  @note An empty array after sorting is treated as a malformed input; this
 *      guards against degenerate credential sets that produce a zero-length
 *      canonical form.
 */
static Expected<uint256, json::Value>
parseDepositPreauth(
    json::Value const& dp,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!dp.isObject())
    {
        return parseObjectID(dp, fieldName);
    }

    if ((dp.isMember(jss::authorized) == dp.isMember(jss::authorized_credentials)))
    {
        return LedgerEntryHelpers::malformedError(
            "malformedRequest",
            "Must have exactly one of `authorized` and "
            "`authorized_credentials`.");
    }

    auto const owner = LedgerEntryHelpers::requiredAccountID(dp, jss::owner, "malformedOwner");
    if (!owner)
    {
        return Unexpected(owner.error());
    }

    if (dp.isMember(jss::authorized))
    {
        if (auto const authorized = LedgerEntryHelpers::parse<AccountID>(dp[jss::authorized]))
        {
            return keylet::depositPreauth(*owner, *authorized).key;
        }
        return LedgerEntryHelpers::invalidFieldError(
            "malformedAuthorized", jss::authorized, "AccountID");
    }

    auto const& ac(dp[jss::authorized_credentials]);
    auto const arr = parseAuthorizeCredentials(ac);
    if (!arr.has_value())
        return Unexpected(arr.error());

    auto const& sorted = credentials::makeSorted(arr.value());
    if (sorted.empty())
    {
        // TODO: this error message is bad/inaccurate
        return LedgerEntryHelpers::invalidFieldError(
            "malformedAuthorizedCredentials", jss::authorized_credentials, "array");
    }

    return keylet::depositPreauth(*owner, sorted).key;
}

/** Compute the DID ledger-entry key from an account address.
 *
 *  Accepts a Base58-encoded AccountID and calls `keylet::did(*account).key`.
 *  Unlike `parseAccountRoot`, this parser accepts only the account string form
 *  (no hex fallback).
 *
 *  @param params     A JSON string containing the Base58Check account address.
 *  @param fieldName  Used in error messages.
 *  @param apiVersion Unused; present for uniform `FunctionType` signature.
 *  @return The `uint256` key, or a `malformedAddress` error.
 */
static Expected<uint256, json::Value>
parseDID(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    auto const account = LedgerEntryHelpers::parse<AccountID>(params);
    if (!account)
    {
        return LedgerEntryHelpers::invalidFieldError("malformedAddress", fieldName, "AccountID");
    }

    return keylet::did(*account).key;
}

/** Compute a DirectoryNode ledger-entry key from an owner or root hash plus sub-index.
 *
 *  Accepts either a 64-character hex string (direct lookup) or a JSON object
 *  with **exactly one of** `dir_root` or `owner`, plus an optional `sub_index`
 *  (defaults to 0):
 *  - `dir_root` (uint256 hex) — calls `keylet::page(*uDirRoot, uSubIndex).key`.
 *  - `owner` (AccountID) — calls
 *    `keylet::page(keylet::ownerDir(*ownerID), uSubIndex).key`.
 *
 *  @param params     Hex string, or object with `dir_root` or `owner` and
 *      optional `sub_index`.
 *  @param fieldName  Used in error messages.
 *  @param apiVersion Unused; present for uniform `FunctionType` signature.
 *  @return The `uint256` key, or an `rpcINVALID_PARAMS` error.
 *  @note Supplying both `dir_root` and `owner`, or neither, is an error.
 */
static Expected<uint256, json::Value>
parseDirectoryNode(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName);
    }

    if (params.isMember(jss::sub_index) &&
        (!params[jss::sub_index].isConvertibleTo(json::ValueType::UInt) ||
         params[jss::sub_index].isBool()))
    {
        return LedgerEntryHelpers::invalidFieldError("malformedRequest", jss::sub_index, "number");
    }

    if (params.isMember(jss::owner) == params.isMember(jss::dir_root))
    {
        return LedgerEntryHelpers::malformedError(
            "malformedRequest", "Must have exactly one of `owner` and `dir_root` fields.");
    }

    std::uint64_t const uSubIndex = params.get(jss::sub_index, 0).asUInt();

    if (params.isMember(jss::dir_root))
    {
        if (auto const uDirRoot = LedgerEntryHelpers::parse<uint256>(params[jss::dir_root]))
        {
            return keylet::page(*uDirRoot, uSubIndex).key;
        }

        return LedgerEntryHelpers::invalidFieldError("malformedDirRoot", jss::dir_root, "hash");
    }

    if (params.isMember(jss::owner))
    {
        auto const ownerID = LedgerEntryHelpers::parse<AccountID>(params[jss::owner]);
        if (!ownerID)
        {
            return LedgerEntryHelpers::invalidFieldError(
                "malformedAddress", jss::owner, "AccountID");
        }

        return keylet::page(keylet::ownerDir(*ownerID), uSubIndex).key;
    }

    return LedgerEntryHelpers::malformedError("malformedRequest", "");
}

/** Compute the Escrow ledger-entry key from owner/sequence or a raw hex key.
 *
 *  Accepts either a 64-character hex string (direct lookup) or a JSON object
 *  with `owner` (AccountID) and `seq` (uint32) corresponding to the sequence
 *  number of the EscrowCreate transaction that created the escrow.  The
 *  semantic form calls `keylet::escrow(*owner, *seq).key`.
 *
 *  @param params     Hex string or object with `owner` and `seq`.
 *  @param fieldName  Used in error messages.
 *  @param apiVersion Unused; present for uniform `FunctionType` signature.
 *  @return The `uint256` key, or an `rpcINVALID_PARAMS` error.
 */
static Expected<uint256, json::Value>
parseEscrow(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName);
    }

    auto const id = LedgerEntryHelpers::requiredAccountID(params, jss::owner, "malformedOwner");
    if (!id)
        return Unexpected(id.error());
    auto const seq = LedgerEntryHelpers::requiredUInt32(params, jss::seq, "malformedSeq");
    if (!seq)
        return Unexpected(seq.error());

    return keylet::escrow(*id, *seq).key;
}

/** Parser for the Fee Settings singleton; delegates to `fixed(keylet::fees())`. */
auto const parseFeeSettings = fixed(keylet::fees());

/** Resolve the key for a singleton ledger object.
 *
 *  Accepts either `true` (boolean) to fetch the well-known singleton, or a
 *  hex string for a direct key override.  A boolean `false` is rejected.
 *  This function is not invoked directly — it is called from lambdas produced
 *  by `fixed()`.
 *
 *  @param keylet     The singleton's fixed `Keylet`.
 *  @param params     `true`, or a 64-character hex string.
 *  @param fieldName  Used in error messages.
 *  @param apiVersion Unused; present for consistency.
 *  @return The singleton's `uint256` key, or an `rpcINVALID_PARAMS` error.
 */
static Expected<uint256, json::Value>
parseFixed(
    Keylet const& keylet,
    json::Value const& params,
    json::StaticString const& fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!params.isBool())
    {
        return parseObjectID(params, fieldName, "hex string");
    }
    if (!params.asBool())
    {
        return LedgerEntryHelpers::invalidFieldError("invalidParams", fieldName, "true");
    }

    return keylet.key;
}

/** Compute the LedgerHashes (skip list) key from a ledger index or the short-list sentinel.
 *
 *  Accepts:
 *  - An integer — returns `keylet::skip(index).key` for the long skip list
 *    covering that ledger sequence range.
 *  - A boolean `true` or hex string — delegates to `parseFixed(keylet::skip(), ...)`,
 *    returning the key for the short skip list (hashes since the last flag ledger).
 *
 *  @param params      Integer ledger index, boolean `true`, or hex string.
 *  @param fieldName   Used in error messages; typically `jss::hashes`.
 *  @param apiVersion  Forwarded to `parseFixed` for the non-integer path.
 *  @return The `uint256` key, or an `rpcINVALID_PARAMS` error.
 */
static Expected<uint256, json::Value>
parseLedgerHashes(
    json::Value const& params,
    json::StaticString const fieldName,
    unsigned const apiVersion)
{
    if (params.isUInt() || params.isInt())
    {
        // If the index doesn't parse as a UInt, throw
        auto const index = params.asUInt();

        // Return the "long" skip list for the given ledger index.
        auto const keylet = keylet::skip(index);
        return keylet.key;
    }
    // Return the key in `params` or the "short" skip list, which contains
    // hashes since the last flag ledger.
    return parseFixed(keylet::skip(), params, fieldName, apiVersion);
}

/** Compute the LoanBroker ledger-entry key from owner/sequence or a raw hex key.
 *
 *  Accepts either a 64-character hex string (direct lookup) or a JSON object
 *  with `owner` (AccountID) and `seq` (uint32).  The semantic form calls
 *  `keylet::loanbroker(*owner, *seq).key`.
 *
 *  @param params     Hex string or object with `owner` and `seq`.
 *  @param fieldName  Used in error messages.
 *  @param apiVersion Unused; present for uniform `FunctionType` signature.
 *  @return The `uint256` key, or an `rpcINVALID_PARAMS` error.
 */
static Expected<uint256, json::Value>
parseLoanBroker(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName, "hex string");
    }

    auto const id = LedgerEntryHelpers::requiredAccountID(params, jss::owner, "malformedOwner");
    if (!id)
        return Unexpected(id.error());
    auto const seq = LedgerEntryHelpers::requiredUInt32(params, jss::seq, "malformedSeq");
    if (!seq)
        return Unexpected(seq.error());

    return keylet::loanbroker(*id, *seq).key;
}

/** Compute the Loan ledger-entry key from broker ID/sequence or a raw hex key.
 *
 *  Accepts either a 64-character hex string (direct lookup) or a JSON object
 *  with `loan_broker_id` (uint256) and `loan_seq` (uint32).  The semantic
 *  form calls `keylet::loan(*loanBrokerID, *loanSeq).key`.
 *
 *  @param params     Hex string or object with `loan_broker_id` and `loan_seq`.
 *  @param fieldName  Used in error messages.
 *  @param apiVersion Unused; present for uniform `FunctionType` signature.
 *  @return The `uint256` key, or an `rpcINVALID_PARAMS` error.
 */
static Expected<uint256, json::Value>
parseLoan(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName, "hex string");
    }

    auto const id =
        LedgerEntryHelpers::requiredUInt256(params, jss::loan_broker_id, "malformedBroker");
    if (!id)
        return Unexpected(id.error());
    auto const seq = LedgerEntryHelpers::requiredUInt32(params, jss::loan_seq, "malformedSeq");
    if (!seq)
        return Unexpected(seq.error());

    return keylet::loan(*id, *seq).key;
}

/** Compute the MPToken ledger-entry key from issuance ID/account or a raw hex key.
 *
 *  Accepts either a 64-character hex string (direct lookup) or a JSON object
 *  with `mpt_issuance_id` (uint192 hex) and `account` (AccountID).  The
 *  semantic form calls `keylet::mptoken(*mptIssuanceID, *account).key`.
 *
 *  @param params     Hex string or object with `mpt_issuance_id` and `account`.
 *  @param fieldName  Used in error messages.
 *  @param apiVersion Unused; present for uniform `FunctionType` signature.
 *  @return The `uint256` key, or an `rpcINVALID_PARAMS` error.
 */
static Expected<uint256, json::Value>
parseMPToken(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName);
    }

    auto const mptIssuanceID =
        LedgerEntryHelpers::requiredUInt192(params, jss::mpt_issuance_id, "malformedMPTIssuanceID");
    if (!mptIssuanceID)
        return Unexpected(mptIssuanceID.error());

    auto const account =
        LedgerEntryHelpers::requiredAccountID(params, jss::account, "malformedAccount");
    if (!account)
        return Unexpected(account.error());

    return keylet::mptoken(*mptIssuanceID, *account).key;
}

/** Compute the MPTokenIssuance ledger-entry key from a uint192 issuance ID.
 *
 *  Accepts only a 48-character hex string encoding the 192-bit MPT issuance
 *  ID; there is no structured-object alternative.  Calls
 *  `keylet::mptIssuance(*mptIssuanceID).key`.
 *
 *  @param params     A 48-character hex string (192-bit issuance ID).
 *  @param fieldName  Used in error messages.
 *  @param apiVersion Unused; present for uniform `FunctionType` signature.
 *  @return The `uint256` key, or a `malformedMPTokenIssuance` error.
 */
static Expected<uint256, json::Value>
parseMPTokenIssuance(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    auto const mptIssuanceID = LedgerEntryHelpers::parse<uint192>(params);
    if (!mptIssuanceID)
    {
        return LedgerEntryHelpers::invalidFieldError(
            "malformedMPTokenIssuance", fieldName, "Hash192");
    }

    return keylet::mptIssuance(*mptIssuanceID).key;
}

/** Compute the NFTokenOffer ledger-entry key from a raw hex key.
 *
 *  NFTokenOffer objects have no semantic constructor — clients must supply
 *  the 256-bit key directly.  Delegates unconditionally to `parseObjectID`.
 *
 *  @param params     A 64-character hex string.
 *  @param fieldName  Used in error messages.
 *  @param apiVersion Unused; present for uniform `FunctionType` signature.
 *  @return The `uint256` key, or an `rpcINVALID_PARAMS` error.
 */
static Expected<uint256, json::Value>
parseNFTokenOffer(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    return parseObjectID(params, fieldName, "hex string");
}

/** Compute the NFTokenPage ledger-entry key from a raw hex key.
 *
 *  NFTokenPage objects have no semantic constructor — clients must supply
 *  the 256-bit key directly.  Delegates unconditionally to `parseObjectID`.
 *
 *  @param params     A 64-character hex string.
 *  @param fieldName  Used in error messages.
 *  @param apiVersion Unused; present for uniform `FunctionType` signature.
 *  @return The `uint256` key, or an `rpcINVALID_PARAMS` error.
 */
static Expected<uint256, json::Value>
parseNFTokenPage(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    return parseObjectID(params, fieldName, "hex string");
}

/** Parser for the Negative UNL singleton; delegates to `fixed(keylet::negativeUNL())`. */
auto const parseNegativeUNL = fixed(keylet::negativeUNL());

/** Compute the Offer ledger-entry key from account/sequence or a raw hex key.
 *
 *  Accepts either a 64-character hex string (direct lookup) or a JSON object
 *  with `account` (AccountID) and `seq` (uint32) — the sequence number of the
 *  OfferCreate transaction.  The semantic form calls
 *  `keylet::offer(*account, *seq).key`.
 *
 *  @param params     Hex string or object with `account` and `seq`.
 *  @param fieldName  Used in error messages.
 *  @param apiVersion Unused; present for uniform `FunctionType` signature.
 *  @return The `uint256` key, or an `rpcINVALID_PARAMS` error.
 */
static Expected<uint256, json::Value>
parseOffer(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName);
    }

    auto const id = LedgerEntryHelpers::requiredAccountID(params, jss::account, "malformedAddress");
    if (!id)
        return Unexpected(id.error());

    auto const seq = LedgerEntryHelpers::requiredUInt32(params, jss::seq, "malformedRequest");
    if (!seq)
        return Unexpected(seq.error());

    return keylet::offer(*id, *seq).key;
}

/** Compute the Oracle ledger-entry key from account/document-ID or a raw hex key.
 *
 *  Accepts either a 64-character hex string (direct lookup) or a JSON object
 *  with `account` (AccountID) and `oracle_document_id` (uint32).  The semantic
 *  form calls `keylet::oracle(*account, *oracleDocumentID).key`.
 *
 *  @param params     Hex string or object with `account` and `oracle_document_id`.
 *  @param fieldName  Used in error messages.
 *  @param apiVersion Unused; present for uniform `FunctionType` signature.
 *  @return The `uint256` key, or an `rpcINVALID_PARAMS` error.
 */
static Expected<uint256, json::Value>
parseOracle(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName);
    }

    auto const id = LedgerEntryHelpers::requiredAccountID(params, jss::account, "malformedAccount");
    if (!id)
        return Unexpected(id.error());

    auto const seq =
        LedgerEntryHelpers::requiredUInt32(params, jss::oracle_document_id, "malformedDocumentID");
    if (!seq)
        return Unexpected(seq.error());

    return keylet::oracle(*id, *seq).key;
}

/** Compute the PayChannel ledger-entry key from a raw hex key.
 *
 *  PayChannel objects have no semantic constructor — clients must supply the
 *  256-bit key directly.  Delegates unconditionally to `parseObjectID`.
 *
 *  @param params     A 64-character hex string.
 *  @param fieldName  Used in error messages.
 *  @param apiVersion Unused; present for uniform `FunctionType` signature.
 *  @return The `uint256` key, or an `rpcINVALID_PARAMS` error.
 */
static Expected<uint256, json::Value>
parsePayChannel(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    return parseObjectID(params, fieldName, "hex string");
}

/** Compute the PermissionedDomain ledger-entry key from account/sequence or a raw hex key.
 *
 *  Accepts either a 64-character hex string (direct lookup) or a JSON object
 *  with `account` (AccountID) and `seq` (uint32).  The semantic form calls
 *  `keylet::permissionedDomain(*account, *seq).key`.
 *
 *  @param pd         Hex string or object with `account` and `seq`.
 *  @param fieldName  Used in error messages.
 *  @param apiVersion Unused; present for uniform `FunctionType` signature.
 *  @return The `uint256` key, or an `rpcINVALID_PARAMS` error.
 */
static Expected<uint256, json::Value>
parsePermissionedDomain(
    json::Value const& pd,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (pd.isString())
    {
        return parseObjectID(pd, fieldName);
    }

    if (!pd.isObject())
    {
        return LedgerEntryHelpers::invalidFieldError(
            "malformedRequest", fieldName, "hex string or object");
    }

    auto const account =
        LedgerEntryHelpers::requiredAccountID(pd, jss::account, "malformedAddress");
    if (!account)
        return Unexpected(account.error());

    auto const seq = LedgerEntryHelpers::requiredUInt32(pd, jss::seq, "malformedRequest");
    if (!seq)
        return Unexpected(seq.error());

    return keylet::permissionedDomain(*account, pd[jss::seq].asUInt()).key;
}

/** Compute the RippleState (trust-line) ledger-entry key from accounts/currency or a raw hex key.
 *
 *  Accepts either a 64-character hex string (direct lookup) or a JSON object
 *  with `accounts` (a 2-element array of AccountIDs) and `currency` (a currency
 *  code string).  The semantic form calls
 *  `keylet::line(*id1, *id2, uCurrency).key`.
 *
 *  @param jvRippleState  Hex string or object with `accounts` and `currency`.
 *  @param fieldName      Used in error messages; typically `jss::ripple_state`.
 *  @param apiVersion     Unused; present for uniform `FunctionType` signature.
 *  @return The `uint256` key, or an `rpcINVALID_PARAMS` error.
 *  @note Both accounts in the array must be distinct — a trust-line to self
 *      is rejected with `"Cannot have a trustline to self."`.
 */
static Expected<uint256, json::Value>
parseRippleState(
    json::Value const& jvRippleState,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    Currency uCurrency;

    if (!jvRippleState.isObject())
    {
        return parseObjectID(jvRippleState, fieldName);
    }

    if (auto const value =
            LedgerEntryHelpers::hasRequired(jvRippleState, {jss::currency, jss::accounts});
        !value)
    {
        return Unexpected(value.error());
    }

    if (!jvRippleState[jss::accounts].isArray() || jvRippleState[jss::accounts].size() != 2)
    {
        return LedgerEntryHelpers::invalidFieldError(
            "malformedRequest", jss::accounts, "length-2 array of Accounts");
    }

    auto const id1 = LedgerEntryHelpers::parse<AccountID>(jvRippleState[jss::accounts][0u]);
    auto const id2 = LedgerEntryHelpers::parse<AccountID>(jvRippleState[jss::accounts][1u]);
    if (!id1 || !id2)
    {
        return LedgerEntryHelpers::invalidFieldError(
            "malformedAddress", jss::accounts, "array of Accounts");
    }
    if (id1 == id2)
    {
        return LedgerEntryHelpers::malformedError(
            "malformedRequest", "Cannot have a trustline to self.");
    }

    if (!jvRippleState[jss::currency].isString() || jvRippleState[jss::currency] == "" ||
        !toCurrency(uCurrency, jvRippleState[jss::currency].asString()))
    {
        return LedgerEntryHelpers::invalidFieldError(
            "malformedCurrency", jss::currency, "Currency");
    }

    return keylet::line(*id1, *id2, uCurrency).key;
}

/** Compute the SignerList ledger-entry key from a raw hex key.
 *
 *  SignerList objects have no semantic constructor — clients must supply the
 *  256-bit key directly.  Delegates unconditionally to `parseObjectID`.
 *
 *  @param params     A 64-character hex string.
 *  @param fieldName  Used in error messages.
 *  @param apiVersion Unused; present for uniform `FunctionType` signature.
 *  @return The `uint256` key, or an `rpcINVALID_PARAMS` error.
 */
static Expected<uint256, json::Value>
parseSignerList(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    return parseObjectID(params, fieldName, "hex string");
}

/** Compute the Ticket ledger-entry key from account/ticket-sequence or a raw hex key.
 *
 *  Accepts either a 64-character hex string (direct lookup) or a JSON object
 *  with `account` (AccountID) and `ticket_seq` (uint32 — the sequence number
 *  reserved for the ticket).  The semantic form calls
 *  `getTicketIndex(*account, *ticketSeq)`.
 *
 *  @param params     Hex string or object with `account` and `ticket_seq`.
 *  @param fieldName  Used in error messages.
 *  @param apiVersion Unused; present for uniform `FunctionType` signature.
 *  @return The `uint256` key, or an `rpcINVALID_PARAMS` error.
 */
static Expected<uint256, json::Value>
parseTicket(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName);
    }

    auto const id = LedgerEntryHelpers::requiredAccountID(params, jss::account, "malformedAddress");
    if (!id)
        return Unexpected(id.error());

    auto const seq =
        LedgerEntryHelpers::requiredUInt32(params, jss::ticket_seq, "malformedRequest");
    if (!seq)
        return Unexpected(seq.error());

    return getTicketIndex(*id, *seq);
}

/** Compute the Vault ledger-entry key from owner/sequence or a raw hex key.
 *
 *  Accepts either a 64-character hex string (direct lookup) or a JSON object
 *  with `owner` (AccountID) and `seq` (uint32).  The semantic form calls
 *  `keylet::vault(*owner, *seq).key`.
 *
 *  @param params     Hex string or object with `owner` and `seq`.
 *  @param fieldName  Used in error messages.
 *  @param apiVersion Unused; present for uniform `FunctionType` signature.
 *  @return The `uint256` key, or an `rpcINVALID_PARAMS` error.
 */
static Expected<uint256, json::Value>
parseVault(
    json::Value const& params,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!params.isObject())
    {
        return parseObjectID(params, fieldName);
    }

    auto const id = LedgerEntryHelpers::requiredAccountID(params, jss::owner, "malformedOwner");
    if (!id)
        return Unexpected(id.error());

    auto const seq = LedgerEntryHelpers::requiredUInt32(params, jss::seq, "malformedRequest");
    if (!seq)
        return Unexpected(seq.error());

    return keylet::vault(*id, *seq).key;
}

/** Compute the XChainOwnedClaimID ledger-entry key from a bridge spec/sequence or a raw hex key.
 *
 *  Accepts either a 64-character hex string (direct lookup) or a JSON object
 *  containing full bridge specification fields (LockingChainDoor,
 *  LockingChainIssue, IssuingChainDoor, IssuingChainIssue) plus
 *  `xchain_owned_claim_id` (uint32).  The semantic form calls
 *  `keylet::xChainClaimID(*bridgeSpec, *seq).key`.
 *
 *  @param claimId    Hex string or object with bridge fields and
 *      `xchain_owned_claim_id`.
 *  @param fieldName  Used in error messages.
 *  @param apiVersion Unused; present for uniform `FunctionType` signature.
 *  @return The `uint256` key, or an `rpcINVALID_PARAMS` error.
 */
static Expected<uint256, json::Value>
parseXChainOwnedClaimID(
    json::Value const& claimId,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!claimId.isObject())
    {
        return parseObjectID(claimId, fieldName);
    }

    auto const bridgeSpec = LedgerEntryHelpers::parseBridgeFields(claimId);
    if (!bridgeSpec)
        return Unexpected(bridgeSpec.error());

    auto const seq = LedgerEntryHelpers::requiredUInt32(
        claimId, jss::xchain_owned_claim_id, "malformedXChainOwnedClaimID");
    if (!seq)
    {
        return Unexpected(seq.error());
    }

    Keylet keylet = keylet::xChainClaimID(*bridgeSpec, *seq);
    return keylet.key;
}

/** Compute the XChainOwnedCreateAccountClaimID key from a bridge spec/sequence or a raw hex key.
 *
 *  Accepts either a 64-character hex string (direct lookup) or a JSON object
 *  containing full bridge specification fields plus
 *  `xchain_owned_create_account_claim_id` (uint32).  The semantic form calls
 *  `keylet::xChainCreateAccountClaimID(*bridgeSpec, *seq).key`.
 *
 *  @param claimId    Hex string or object with bridge fields and
 *      `xchain_owned_create_account_claim_id`.
 *  @param fieldName  Used in error messages.
 *  @param apiVersion Unused; present for uniform `FunctionType` signature.
 *  @return The `uint256` key, or an `rpcINVALID_PARAMS` error.
 */
static Expected<uint256, json::Value>
parseXChainOwnedCreateAccountClaimID(
    json::Value const& claimId,
    json::StaticString const fieldName,
    [[maybe_unused]] unsigned const apiVersion)
{
    if (!claimId.isObject())
    {
        return parseObjectID(claimId, fieldName);
    }

    auto const bridgeSpec = LedgerEntryHelpers::parseBridgeFields(claimId);
    if (!bridgeSpec)
        return Unexpected(bridgeSpec.error());

    auto const seq = LedgerEntryHelpers::requiredUInt32(
        claimId,
        jss::xchain_owned_create_account_claim_id,
        "malformedXChainOwnedCreateAccountClaimID");
    if (!seq)
    {
        return Unexpected(seq.error());
    }

    Keylet keylet = keylet::xChainCreateAccountClaimID(*bridgeSpec, *seq);
    return keylet.key;
}

/** Dispatch table entry mapping a JSON field name to its parser and expected SLE type.
 *
 *  The static array `kLEDGER_ENTRY_PARSERS` in `doLedgerEntry` is populated
 *  from the `ledger_entries.macro` X-macro plus a small set of hand-appended
 *  aliases.  Each entry binds:
 *  - `fieldName` — the top-level JSON key that triggers this parser.
 *  - `parseFunction` — a `FunctionType` callable that derives the `uint256`
 *    ledger-state key from the field's value.
 *  - `expectedType` — the `LedgerEntryType` against which the fetched SLE is
 *    validated; `ltANY` skips the type check (used for `index` and generic
 *    hex-key paths).
 */
struct LedgerEntry
{
    json::StaticString fieldName;
    FunctionType parseFunction;
    LedgerEntryType expectedType;
};

/** Handle the `ledger_entry` JSON-RPC command.
 *
 *  Resolves a single ledger state object (SLE) from a caller-specified ledger.
 *  The request must contain exactly one entry-type descriptor field (e.g.
 *  `offer`, `ripple_state`, `index`, etc.) whose value is passed to the
 *  corresponding parser to derive the `uint256` state-tree key.  The SLE is
 *  then fetched from the `ReadView` and returned either as JSON (`node`) or
 *  as a hex-encoded serialized blob (`node_binary`) when `binary: true`.
 *
 *  The dispatch table (`kLEDGER_ENTRY_PARSERS`) is a static `std::array`
 *  built from the `ledger_entries.macro` X-macro, ensuring that every
 *  ledger entry type defined in the protocol is automatically handled.
 *  Two backward-compatible aliases (`account_root`, `ripple_state`) and the
 *  generic `index` entry are appended by hand after the macro block.
 *
 *  @param context  The JSON-RPC context, including params and API version.
 *  @return A `Json::Value` object containing the SLE data, or an error.
 *  @note `index` is always included in the response, even when the SLE is
 *      not found, so clients can confirm which key was queried.
 *  @note When the fetched SLE's type does not match the `expectedType` of the
 *      matched dispatch entry (and `expectedType` is not `ltANY`), the handler
 *      returns `rpcUNEXPECTED_LEDGER_TYPE`.  This prevents a client from
 *      receiving the wrong object type when supplying a raw hex key.
 *  @note For API v1, unrecognised fields produce `error: "unknownOption"` and
 *      `Json::Error` exceptions are re-thrown.  For API v2+, both conditions
 *      are translated to structured `rpcINVALID_PARAMS` responses.
 */
json::Value
doLedgerEntry(RPC::JsonContext& context)
{
    static auto kLEDGER_ENTRY_PARSERS = std::to_array<LedgerEntry>({
#pragma push_macro("LEDGER_ENTRY")
#undef LEDGER_ENTRY

#define LEDGER_ENTRY(tag, value, name, rpcName, fields) {jss::rpcName, parse##name, tag},

#include <xrpl/protocol/detail/ledger_entries.macro>

#undef LEDGER_ENTRY
#pragma pop_macro("LEDGER_ENTRY")
        {.fieldName = jss::index, .parseFunction = parseIndex, .expectedType = ltANY},
        // aliases
        {.fieldName = jss::account_root,
         .parseFunction = parseAccountRoot,
         .expectedType = ltACCOUNT_ROOT},
        {.fieldName = jss::ripple_state,
         .parseFunction = parseRippleState,
         .expectedType = ltRIPPLE_STATE},
    });

    auto const hasMoreThanOneMember = [&]() {
        int count = 0;

        for (auto const& ledgerEntry : kLEDGER_ENTRY_PARSERS)
        {
            if (context.params.isMember(ledgerEntry.fieldName))
            {
                count++;
                if (count > 1)  // Early exit if more than one is found
                    return true;
            }
        }
        return false;  // Return false if <= 1 is found
    }();

    if (hasMoreThanOneMember)
    {
        return RPC::makeParamError("Too many fields provided.");
    }

    std::shared_ptr<ReadView const> lpLedger;
    auto jvResult = RPC::lookupLedger(lpLedger, context);

    if (!lpLedger)
        return jvResult;

    uint256 uNodeIndex;
    LedgerEntryType expectedType = ltANY;

    try
    {
        bool found = false;
        for (auto const& ledgerEntry : kLEDGER_ENTRY_PARSERS)
        {
            if (context.params.isMember(ledgerEntry.fieldName))
            {
                expectedType = ledgerEntry.expectedType;
                // `Bridge` is the only type that involves two fields at the
                // `ledger_entry` param level.
                // So that parser needs to have the whole `params` field.
                // All other parsers only need the one field name's info.
                json::Value const& params = ledgerEntry.fieldName == jss::bridge
                    ? context.params
                    : context.params[ledgerEntry.fieldName];
                auto const result =
                    ledgerEntry.parseFunction(params, ledgerEntry.fieldName, context.apiVersion);
                if (!result)
                    return result.error();

                uNodeIndex = result.value();
                found = true;
                break;
            }
        }
        if (!found)
        {
            if (context.apiVersion < 2u)
            {
                jvResult[jss::error] = "unknownOption";
                return jvResult;
            }
            return RPC::makeParamError("No ledger_entry params provided.");
        }
    }
    catch (json::Error const& e)
    {
        if (context.apiVersion > 1u)
        {
            // For apiVersion 2 onwards, any parsing failures that throw
            // this exception return an invalidParam error.
            return RPC::makeError(RpcInvalidParams);
        }

        throw;
    }

    // Return the computed index regardless of whether the node exists.
    jvResult[jss::index] = to_string(uNodeIndex);

    if (uNodeIndex.isZero())
    {
        RPC::injectError(RpcEntryNotFound, jvResult);
        return jvResult;
    }

    auto const sleNode = lpLedger->read(keylet::unchecked(uNodeIndex));

    bool bNodeBinary = false;
    if (context.params.isMember(jss::binary))
        bNodeBinary = context.params[jss::binary].asBool();

    if (!sleNode)
    {
        // Not found.
        RPC::injectError(RpcEntryNotFound, jvResult);
        return jvResult;
    }

    if ((expectedType != ltANY) && (expectedType != sleNode->getType()))
    {
        RPC::injectError(RpcUnexpectedLedgerType, jvResult);
        return jvResult;
    }

    if (bNodeBinary)
    {
        Serializer s;

        sleNode->add(s);

        jvResult[jss::node_binary] = strHex(s.peekData());
    }
    else
    {
        jvResult[jss::node] = sleNode->getJson(JsonOptions::Values::None);
    }

    return jvResult;
}

/** Handle the `GetLedgerEntry` gRPC request.
 *
 *  Stripped-down counterpart to `doLedgerEntry`.  Accepts a raw 32-byte key
 *  from the protobuf request (no semantic parsing), resolves the target ledger
 *  via `RPC::ledgerFromRequest`, fetches the SLE, and returns it serialized
 *  with a `Serializer`.  There is no JSON/binary toggle and no type validation
 *  — the gRPC path always returns raw bytes.
 *
 *  @param context  The gRPC context wrapping a `GetLedgerEntryRequest`.
 *  @return A pair of `(GetLedgerEntryResponse, grpc::Status)`.  On any error
 *      the response object is empty and the status is non-OK:
 *      `INVALID_ARGUMENT` for bad parameters or a malformed key, `NOT_FOUND`
 *      for an unknown ledger or a missing SLE.
 */
std::pair<org::xrpl::rpc::v1::GetLedgerEntryResponse, grpc::Status>
doLedgerEntryGrpc(RPC::GRPCContext<org::xrpl::rpc::v1::GetLedgerEntryRequest>& context)
{
    org::xrpl::rpc::v1::GetLedgerEntryRequest const& request = context.params;
    org::xrpl::rpc::v1::GetLedgerEntryResponse response;
    grpc::Status const status = grpc::Status::OK;

    std::shared_ptr<ReadView const> ledger;
    if (auto status = RPC::ledgerFromRequest(ledger, context))
    {
        grpc::Status errorStatus;
        if (status.toErrorCode() == RpcInvalidParams)
        {
            errorStatus = grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, status.message());
        }
        else
        {
            errorStatus = grpc::Status(grpc::StatusCode::NOT_FOUND, status.message());
        }
        return {response, errorStatus};
    }

    auto const key = uint256::fromVoidChecked(request.key());
    if (!key)
    {
        grpc::Status const errorStatus{grpc::StatusCode::INVALID_ARGUMENT, "index malformed"};
        return {response, errorStatus};
    }

    auto const sleNode = ledger->read(keylet::unchecked(*key));
    if (!sleNode)
    {
        grpc::Status const errorStatus{grpc::StatusCode::NOT_FOUND, "object not found"};
        return {response, errorStatus};
    }

    Serializer s;
    sleNode->add(s);

    auto& stateObject = *response.mutable_ledger_object();
    stateObject.set_data(s.peekData().data(), s.getLength());
    stateObject.set_key(request.key());
    *(response.mutable_ledger()) = request.ledger();
    return {response, status};
}
}  // namespace xrpl
