#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace xrpl {

namespace {

std::optional<XRPAmount>
parseDrops(Json::Value const& value)
{
    std::int64_t drops;
    if (value.isInt())
    {
        drops = value.asInt();
    }
    else if (value.isUInt())
    {
        drops = value.asUInt();
    }
    else if (value.isString())
    {
        try
        {
            drops = beast::lexicalCastThrow<std::int64_t>(value.asString());
        }
        catch (...)
        {
            return std::nullopt;
        }
    }
    else
    {
        return std::nullopt;
    }

    if (drops < 0)
        return std::nullopt;

    return XRPAmount{drops};
}

Json::Value
makeFeeVoteResult(XRPAmount referenceFee, XRPAmount accountReserve, XRPAmount ownerReserve)
{
    Json::Value result{Json::objectValue};
    result[jss::reference_fee] = referenceFee.jsonClipped();
    result[jss::account_reserve] = accountReserve.jsonClipped();
    result[jss::owner_reserve] = ownerReserve.jsonClipped();
    return result;
}

}  // namespace

Json::Value
doFeeVote(RPC::JsonContext& context)
{
    auto [referenceFee, accountReserve, ownerReserve] = context.app.getOPs().getFeeVote();

    auto const hasReference = context.params.isMember(jss::reference_fee);
    auto const hasAccount = context.params.isMember(jss::account_reserve);
    auto const hasOwner = context.params.isMember(jss::owner_reserve);
    auto const updateVote = hasReference || hasAccount || hasOwner;

    if (updateVote && context.role != Role::ADMIN)
        return rpcError(rpcNO_PERMISSION);

    if (hasReference)
    {
        auto const parsed = parseDrops(context.params[jss::reference_fee]);
        if (!parsed)
            return RPC::make_param_error("Field 'reference_fee' must be a non-negative integer.");
        referenceFee = *parsed;
    }

    if (hasAccount)
    {
        auto const parsed = parseDrops(context.params[jss::account_reserve]);
        if (!parsed)
            return RPC::make_param_error(
                "Field 'account_reserve' must be a non-negative integer.");
        if (!parsed->dropsAs<std::uint32_t>())
            return RPC::make_param_error(
                "Field 'account_reserve' must be less than or equal to 4294967295.");
        accountReserve = *parsed;
    }

    if (hasOwner)
    {
        auto const parsed = parseDrops(context.params[jss::owner_reserve]);
        if (!parsed)
            return RPC::make_param_error("Field 'owner_reserve' must be a non-negative integer.");
        if (!parsed->dropsAs<std::uint32_t>())
            return RPC::make_param_error(
                "Field 'owner_reserve' must be less than or equal to 4294967295.");
        ownerReserve = *parsed;
    }

    if (updateVote)
        context.app.getOPs().setFeeVote(referenceFee, accountReserve, ownerReserve);

    return makeFeeVoteResult(referenceFee, accountReserve, ownerReserve);
}

}  // namespace xrpl
