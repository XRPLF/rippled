#pragma once

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>

namespace xrpl {

inline void
appendNftOfferJson(
    Application const& app,
    std::shared_ptr<SLE const> const& offer,
    json::Value& offers)
{
    json::Value& obj(offers.append(json::ValueType::Object));

    obj[jss::nft_offer_index] = to_string(offer->key());
    obj[jss::flags] = (*offer)[sfFlags];
    obj[jss::owner] = toBase58(offer->getAccountID(sfOwner));

    if (offer->isFieldPresent(sfDestination))
        obj[jss::destination] = toBase58(offer->getAccountID(sfDestination));

    if (offer->isFieldPresent(sfExpiration))
        obj[jss::expiration] = offer->getFieldU32(sfExpiration);

    offer->getFieldAmount(sfAmount).setJson(obj[jss::amount]);
}

// {
//   nft_id: <token hash>
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
//   limit: integer                 // optional
//   marker: opaque                 // optional, resume previous query
// }
inline json::Value
enumerateNFTOffers(RPC::JsonContext& context, uint256 const& nftId, Keylet const& directory)
{
    unsigned int limit = 0;
    if (auto err = readLimitField(limit, RPC::Tuning::kNftOffers, context))
        return *err;

    std::shared_ptr<ReadView const> ledger;

    if (auto result = RPC::lookupLedger(ledger, context); !ledger)
        return result;

    if (!ledger->exists(directory))
        return rpcError(RpcObjectNotFound);

    json::Value result;
    result[jss::nft_id] = to_string(nftId);

    json::Value& jsonOffers(result[jss::offers] = json::ValueType::Array);

    std::vector<std::shared_ptr<SLE const>> offers;
    unsigned int reserve(limit);
    uint256 startAfter;
    std::uint64_t startHint = 0;

    if (context.params.isMember(jss::marker))
    {
        // We have a start point. Use limit - 1 from the result and use the
        // very last one for the resume.
        json::Value const& marker(context.params[jss::marker]);

        if (!marker.isString())
            return RPC::expectedFieldError(jss::marker, "string");

        if (!startAfter.parseHex(marker.asString()))
            return rpcError(RpcInvalidParams);

        auto const sle = ledger->read(keylet::nftoffer(startAfter));

        if (!sle || nftId != sle->getFieldH256(sfNFTokenID))
            return rpcError(RpcInvalidParams);

        startHint = sle->getFieldU64(sfNFTokenOfferNode);
        appendNftOfferJson(context.app, sle, jsonOffers);
        offers.reserve(reserve);
    }
    else
    {
        // We have no start point, limit should be one higher than requested.
        offers.reserve(++reserve);
    }

    if (!forEachItemAfter(
            *ledger,
            directory,
            startAfter,
            startHint,
            reserve,
            [&offers](std::shared_ptr<SLE const> const& offer) {
                if (offer->getType() == ltNFTOKEN_OFFER)
                {
                    offers.emplace_back(offer);
                    return true;
                }

                return false;
            }))
    {
        return rpcError(RpcInvalidParams);
    }

    if (offers.size() == reserve)
    {
        result[jss::limit] = limit;
        result[jss::marker] = to_string(offers.back()->key());
        offers.pop_back();
    }

    for (auto const& offer : offers)
        appendNftOfferJson(context.app, offer, jsonOffers);

    context.loadType = Resource::kFeeMediumBurdenRpc;
    return result;
}

}  // namespace xrpl
