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

static std::optional<uint256>
parseVault(json::Value const& params, json::Value& jvResult)
{
    auto const hasVaultId = params.isMember(jss::vault_id);
    auto const hasOwner = params.isMember(jss::owner);
    auto const hasSeq = params.isMember(jss::seq);

    uint256 uNodeIndex = beast::kZero;
    if (hasVaultId && !hasOwner && !hasSeq)
    {
        if (!uNodeIndex.parseHex(params[jss::vault_id].asString()))
        {
            RPC::injectError(RpcInvalidParams, jvResult);
            return std::nullopt;
        }
        // else uNodeIndex holds the value we need
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
            params[jss::seq].asDouble() > double(json::Value::kMaxUInt))
        {
            RPC::injectError(RpcInvalidParams, jvResult);
            return std::nullopt;
        }

        uNodeIndex = keylet::vault(*id, params[jss::seq].asUInt()).key;
    }
    else
    {
        // Invalid combination of fields vault_id/owner/seq
        RPC::injectError(RpcInvalidParams, jvResult);
        return std::nullopt;
    }

    return uNodeIndex;
}

json::Value
doVaultInfo(RPC::JsonContext& context)
{
    std::shared_ptr<ReadView const> lpLedger;
    auto jvResult = RPC::lookupLedger(lpLedger, context);

    if (!lpLedger)
        return jvResult;

    auto const uNodeIndex = parseVault(context.params, jvResult).value_or(beast::kZero);
    if (uNodeIndex == beast::kZero)
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
