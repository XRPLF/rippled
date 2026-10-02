#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/jss.h>

#include <memory>
#include <optional>

namespace xrpl {

static std::optional<UInt256>
parseVault(json::Value const& params, json::Value& jvResult)
{
    auto const hasVaultId = params.isMember(jss::vault_id);
    auto const hasOwner = params.isMember(jss::owner);
    auto const hasSeq = params.isMember(jss::seq);

    UInt256 uNodeIndex = beast::kZero;
    if (hasVaultId && !hasOwner && !hasSeq)
    {
        // asString() throws on an object or an array, so the type comes first.
        auto const& vaultId = params[jss::vault_id];
        if (!vaultId.isString() || !uNodeIndex.parseHex(vaultId.asString()))
        {
            rpc::injectError(
                RpcInvalidParams, rpc::expectedFieldMessage(jss::vault_id, "hex string"), jvResult);
            return std::nullopt;
        }
        // else uNodeIndex holds the value we need
    }
    else if (!hasVaultId && hasOwner && hasSeq)
    {
        auto const& owner = params[jss::owner];
        auto const id = owner.isString() ? parseBase58<AccountID>(owner.asString())
                                         : std::optional<AccountID>{};
        if (!id)
        {
            rpc::injectError(
                RpcActMalformed, rpc::expectedFieldMessage(jss::owner, "AccountID"), jvResult);
            return std::nullopt;
        }

        // Int and UInt are both 32 bits wide, so the type check is the only upper bound needed.
        auto const& seqField = params[jss::seq];
        if (!(seqField.isInt() || seqField.isUInt()) || seqField.asDouble() <= 0.0)
        {
            rpc::injectError(
                RpcInvalidParams,
                rpc::expectedFieldMessage(jss::seq, "a positive 32-bit integer"),
                jvResult);
            return std::nullopt;
        }

        auto const seq = SeqProxy::rawSequence(seqField.asUInt());
        uNodeIndex = keylet::vault(*id, seq).key;
    }
    else
    {
        rpc::injectError(
            RpcInvalidParams,
            "Must specify either 'vault_id' or both 'owner' and 'seq'.",
            jvResult);
        return std::nullopt;
    }

    return uNodeIndex;
}

json::Value
doVaultInfo(rpc::JsonContext& context)
{
    std::shared_ptr<ReadView const> lpLedger;
    auto jvResult = rpc::lookupLedger(lpLedger, context);

    if (!lpLedger)
        return jvResult;

    // No key means the request could not be turned into one, and parseVault has already said why.
    auto const uNodeIndex = parseVault(context.params, jvResult);
    if (!uNodeIndex)
        return jvResult;

    // A zero key names an entry that cannot exist, and the ledger refuses to be asked for one.
    if (*uNodeIndex == beast::kZero)
    {
        rpc::injectError(RpcEntryNotFound, jvResult);
        return jvResult;
    }

    auto const sleVault = lpLedger->read(keylet::vault(*uNodeIndex));
    auto const sleIssuance = sleVault == nullptr  //
        ? nullptr
        : lpLedger->read(keylet::mptokenIssuance(sleVault->at(sfShareMPTID)));
    if (!sleVault || !sleIssuance)
    {
        rpc::injectError(RpcEntryNotFound, jvResult);
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
