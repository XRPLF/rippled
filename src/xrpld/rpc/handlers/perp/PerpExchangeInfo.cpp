#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/handlers/perp/PerpRPCHelpers.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/PerpHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/jss.h>

#include <memory>
#include <optional>

namespace xrpl {

namespace {

/**
 * The exchange a request names, by exchange_id or by owner and seq.
 */
std::optional<uint256>
parseExchange(json::Value const& params, json::Value& result)
{
    bool const hasID = params.isMember(jss::exchange_id);
    bool const hasOwner = params.isMember(jss::owner);
    bool const hasSeq = params.isMember(jss::seq);
    if (hasID && !hasOwner && !hasSeq)
        return rpc::parsePerpID(params, jss::exchange_id, result);

    if (hasID || !hasOwner || !hasSeq)
    {
        rpc::injectError(
            RpcInvalidParams,
            "Must specify either 'exchange_id' or both 'owner' and 'seq'.",
            result);
        return std::nullopt;
    }

    auto const& owner = params[jss::owner];
    auto const id =
        owner.isString() ? parseBase58<AccountID>(owner.asString()) : std::optional<AccountID>{};
    if (!id)
    {
        rpc::injectError(
            RpcActMalformed, rpc::expectedFieldMessage(jss::owner, "AccountID"), result);
        return std::nullopt;
    }

    // Int and UInt are both 32 bits wide, so the type check is the only upper bound needed.
    auto const& seq = params[jss::seq];
    if (!(seq.isInt() || seq.isUInt()) || seq.asDouble() <= 0.0)
    {
        rpc::injectError(
            RpcInvalidParams,
            rpc::expectedFieldMessage(jss::seq, "a positive 32-bit integer"),
            result);
        return std::nullopt;
    }
    return keylet::perpExchange(*id, SeqProxy::rawSequence(seq.asUInt())).key;
}

}  // namespace

json::Value
doPerpExchangeInfo(rpc::JsonContext const& context)
{
    std::shared_ptr<ReadView const> ledger;
    auto result = rpc::lookupLedger(ledger, context);
    if (!ledger)
        return result;

    auto const id = parseExchange(context.params, result);
    if (!id)
        return result;

    // A zero key names no entry, and the ledger refuses to be asked for one.
    auto const exchange = *id == beast::kZero ? nullptr : ledger->read(keylet::perpExchange(*id));
    if (!exchange)
    {
        rpc::injectError(RpcEntryNotFound, result);
        return result;
    }

    result[jss::perp_exchange] = exchange->getJson(JsonOptions::Values::None);
    result[jss::withdrawable_cover] = to_string(perp::withdrawableCover(*exchange));
    // Without a haircut rate margin withdrawals wait, so none is reported.
    if (auto const haircut = perp::haircutRate(*exchange))
        result[jss::haircut] = to_string(*haircut);
    return result;
}

}  // namespace xrpl
