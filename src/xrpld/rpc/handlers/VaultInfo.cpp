#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>

#include <memory>
#include <optional>

namespace xrpl {

/** Resolve a vault request to the 256-bit ledger object key for an
 *  `ltVAULT` entry.
 *
 *  Exactly one of the following mutually exclusive forms must be present in
 *  `params`:
 *  - `vault_id` only: a hex-encoded 256-bit key used directly.
 *  - `owner` + `seq` only: a base58-encoded account address and the
 *    sequence number of the creating transaction; the key is derived via
 *    `keylet::vault(accountID, seq)`.
 *
 *  On failure the error is injected into `jvResult` before returning, so
 *  the caller only needs to test the returned optional.
 *
 *  @param params  The JSON request parameters object.
 *  @param jvResult The response object into which parse errors are injected.
 *  @return The resolved vault key, or `std::nullopt` if the parameters are
 *      invalid or cannot be parsed.
 *  @note `seq` must be a JSON integer or unsigned integer type in the range
 *      (0, `Json::Value::maxUInt`]. Strings, floats, booleans, zero, and
 *      negative values all produce `invalidParams`. The double-comparison
 *      guards against JSON parsers that may lose integer precision.
 *  @note Providing all three fields, mixing forms (e.g. `owner` without
 *      `seq`), or providing none are all rejected with `invalidParams`.
 */
static std::optional<uint256>
parseVault(json::Value const& params, json::Value& jvResult)
{
    auto const hasVaultId = params.isMember(jss::vault_id);
    auto const hasOwner = params.isMember(jss::owner);
    auto const hasSeq = params.isMember(jss::seq);

    uint256 uNodeIndex = beast::kZERO;
    if (hasVaultId && !hasOwner && !hasSeq)
    {
        if (!uNodeIndex.parseHex(params[jss::vault_id].asString()))
        {
            RPC::injectError(RpcInvalidParams, jvResult);
            return std::nullopt;
        }
    }
    else if (!hasVaultId && hasOwner && hasSeq)
    {
        auto const id = parseBase58<AccountID>(params[jss::owner].asString());
        if (!id)
        {
            RPC::injectError(RpcActMalformed, jvResult);
            return std::nullopt;
        }
        if (!(params[jss::seq].isInt() || params[jss::seq].isUInt()) ||
            params[jss::seq].asDouble() <= 0.0 ||
            params[jss::seq].asDouble() > double(json::Value::kMAX_UINT))
        {
            RPC::injectError(RpcInvalidParams, jvResult);
            return std::nullopt;
        }

        uNodeIndex = keylet::vault(*id, params[jss::seq].asUInt()).key;
    }
    else
    {
        RPC::injectError(RpcInvalidParams, jvResult);
        return std::nullopt;
    }

    return uNodeIndex;
}

/** Retrieve the current state of a vault and its associated share issuance.
 *
 *  Resolves the target ledger first; if the ledger reference is invalid the
 *  handler returns immediately. The vault key is then resolved via
 *  `parseVault`. Two ledger entries are read in sequence: the `ltVAULT` SLE
 *  and the `ltMPTOKEN_ISSUANCE` SLE identified by the vault's `sfShareMPTID`
 *  field. If either is absent the handler returns `"entryNotFound"`.
 *
 *  On success the response contains a `vault` object (the serialized vault
 *  SLE) with the share issuance SLE nested under `vault.shares`.
 *
 *  @param context The RPC dispatch context carrying the request parameters,
 *      application state, and ledger master reference.
 *  @return A `Json::Value` containing either the vault and share data or an
 *      error field. Error values: `"lgrNotFound"` (bad ledger reference),
 *      `"malformedRequest"` (bad vault identifier), `"entryNotFound"` (vault
 *      or its share issuance does not exist in the resolved ledger).
 *  @note A missing share issuance for an existing vault would indicate ledger
 *      inconsistency. Both absences are collapsed into the same
 *      `"entryNotFound"` response since no useful partial result exists.
 */
json::Value
doVaultInfo(RPC::JsonContext& context)
{
    std::shared_ptr<ReadView const> lpLedger;
    auto jvResult = RPC::lookupLedger(lpLedger, context);

    if (!lpLedger)
        return jvResult;

    auto const uNodeIndex = parseVault(context.params, jvResult).value_or(beast::kZERO);
    if (uNodeIndex == beast::kZERO)
    {
        jvResult[jss::error] = "malformedRequest";
        return jvResult;
    }

    auto const sleVault = lpLedger->read(keylet::vault(uNodeIndex));
    auto const sleIssuance = sleVault == nullptr  //
        ? nullptr
        : lpLedger->read(keylet::mptIssuance(sleVault->at(sfShareMPTID)));
    if (!sleVault || !sleIssuance)
    {
        jvResult[jss::error] = "entryNotFound";
        return jvResult;
    }

    json::Value& vault = jvResult[jss::vault];
    vault = sleVault->getJson(JsonOptions::Values::None);
    auto& share = vault[jss::shares];
    share = sleIssuance->getJson(JsonOptions::Values::None);

    jvResult[jss::vault] = vault;
    return jvResult;
}

}  // namespace xrpl
