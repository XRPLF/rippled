#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>

namespace ripple {

static void
appendNftOfferJson(
    Application const& app,
    std::shared_ptr<SLE const> const& offer,
    Json::Value& offers)
{
    Json::Value& obj(offers.append(Json::objectValue));

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
static Json::Value
enumerateNFTOffers(
    RPC::JsonContext& context,
    uint256 const& nftId,
    Keylet const& directory)
{
    unsigned int limit;
    if (auto err = readLimitField(limit, RPC::Tuning::nftOffers, context))
        return *err;

    std::shared_ptr<ReadView const> ledger;

    if (auto result = RPC::lookupLedger(ledger, context); !ledger)
        return result;

    if (!ledger->exists(directory))
        return rpcError(rpcOBJECT_NOT_FOUND);

    Json::Value result;
    result[jss::nft_id] = to_string(nftId);

    Json::Value& jsonOffers(result[jss::offers] = Json::arrayValue);

    std::vector<std::shared_ptr<SLE const>> offers;
    unsigned int reserve(limit);
    uint256 startAfter;
    std::uint64_t startHint = 0;

    if (context.params.isMember(jss::marker))
    {
        // We have a start point. Use limit - 1 from the result and use the
        // very last one for the resume.
        Json::Value const& marker(context.params[jss::marker]);

        if (!marker.isString())
            return RPC::expected_field_error(jss::marker, "string");

        if (!startAfter.parseHex(marker.asString()))
            return rpcError(rpcINVALID_PARAMS);

        auto const sle = ledger->read(keylet::nftoffer(startAfter));

        if (!sle || nftId != sle->getFieldH256(sfNFTokenID))
            return rpcError(rpcINVALID_PARAMS);

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
        return rpcError(rpcINVALID_PARAMS);
    }

    if (offers.size() == reserve)
    {
        result[jss::limit] = limit;
        result[jss::marker] = to_string(offers.back()->key());
        offers.pop_back();
    }

    for (auto const& offer : offers)
        appendNftOfferJson(context.app, offer, jsonOffers);

    context.loadType = Resource::feeMediumBurdenRPC;
    return result;
}

Json::Value
doNFTSellOffers(RPC::JsonContext& context)
{
    if (!context.params.isMember(jss::nft_id))
        return RPC::missing_field_error(jss::nft_id);

    uint256 nftId;

    if (!nftId.parseHex(context.params[jss::nft_id].asString()))
        return RPC::invalid_field_error(jss::nft_id);

    return enumerateNFTOffers(context, nftId, keylet::nft_sells(nftId));
}

Json::Value
doNFTBuyOffers(RPC::JsonContext& context)
{
    if (!context.params.isMember(jss::nft_id))
        return RPC::missing_field_error(jss::nft_id);

    uint256 nftId;

    if (!nftId.parseHex(context.params[jss::nft_id].asString()))
        return RPC::invalid_field_error(jss::nft_id);

    return enumerateNFTOffers(context, nftId, keylet::nft_buys(nftId));
}

}  // namespace ripple
