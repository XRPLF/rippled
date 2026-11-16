#include <xrpld/rpc/handlers/orderbook/NFTOffersCommon.h>

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>

#include <rpcspec/Errors.hpp>
#include <rpcspec/handlers/nft_offers_common/Types.hpp>

#include <cstdint>
#include <expected>
#include <memory>

namespace xrpl::rpc {

static_assert(::rpc::spec::handlers::nft_offers_common::kLimitMin == tuning::kNftOffers.rmin);
static_assert(
    ::rpc::spec::handlers::nft_offers_common::kLimitDefault == tuning::kNftOffers.rDefault);
static_assert(::rpc::spec::handlers::nft_offers_common::kLimitMax == tuning::kNftOffers.rmax);

namespace {

void
appendNftOfferJson(SLE const& offer, json::Value& offers)
{
    json::Value& obj(offers.append(json::ValueType::Object));

    obj[jss::nft_offer_index] = to_string(offer.key());
    obj[jss::flags] = offer[sfFlags];
    obj[jss::owner] = toBase58(offer.getAccountID(sfOwner));

    if (offer.isFieldPresent(sfDestination))
        obj[jss::destination] = toBase58(offer.getAccountID(sfDestination));

    if (offer.isFieldPresent(sfExpiration))
        obj[jss::expiration] = offer.getFieldU32(sfExpiration);

    offer.getFieldAmount(sfAmount).setJson(obj[jss::amount]);
}

}  // namespace

NFTOffersHandlerBase::NFTOffersHandlerBase(JsonContext& context) : context_(context)
{
}

std::expected<NFTOffersHandlerBase::Output, ::rpc::Status>
NFTOffersHandlerBase::enumerateOffers(Input const& input, Keylet const& directory) const
{
    auto& context = context_.get();
    auto const limit = clampLimit(input.limit, tuning::kNftOffers, context.role);

    std::shared_ptr<ReadView const> ledger;
    if (auto const status = getLedger(ledger, input.ledger, context); status != ::rpc::Status::kOK)
        return std::unexpected{status};

    if (!ledger->exists(directory))
        return std::unexpected{RpcObjectNotFound};

    Output output;
    output.nftID = input.nftID;

    unsigned int reserve(limit);
    UInt256 startAfter;
    std::uint64_t startHint = 0;

    if (input.marker.has_value())
    {
        // We have a start point. Use limit - 1 from the result and use the
        // very last one for the resume.
        startAfter = *input.marker;

        auto const sle = ledger->read(keylet::nftokenOffer(startAfter));

        if (!sle || input.nftID != sle->getFieldH256(sfNFTokenID))
            return std::unexpected{RpcInvalidParams};

        // Reject a marker that references an offer on the opposite side
        // (buy vs. sell) of the directory being enumerated.  Without this
        // check the marker's node hint points into the other directory, so
        // forEachItemAfter never finds `startAfter` and instead scans every
        // page of `directory` before returning invalidParams -- turning an
        // O(1) rejection into an O(directory size) walk.
        auto const offerDir = sle->isFlag(lsfSellNFToken) ? keylet::nftSells(input.nftID)
                                                          : keylet::nftBuys(input.nftID);
        if (directory.key != offerDir.key)
            return std::unexpected{RpcInvalidParams};

        startHint = sle->getFieldU64(sfNFTokenOfferNode);
        output.offers.reserve(reserve + 1);
        output.offers.emplace_back(sle);
    }
    else
    {
        // We have no start point, limit should be one higher than requested.
        output.offers.reserve(++reserve);
    }

    auto const resumed = output.offers.size();
    if (!forEachItemAfter(
            *ledger, directory, startAfter, startHint, reserve, [&output](SLE::ConstRef offer) {
                if (offer->getType() == ltNFTOKEN_OFFER)
                {
                    output.offers.emplace_back(offer);
                    return true;
                }

                return false;
            }))
    {
        return std::unexpected{RpcInvalidParams};
    }

    if (output.offers.size() - resumed == reserve)
    {
        output.limit = limit;
        output.marker = output.offers.back()->key();
        output.offers.pop_back();
    }

    context.loadType = resource::kFeeMediumBurdenRpc;
    return output;
}

void
NFTOffersHandlerBase::writeResult(json::Value& value, Output const& output)
{
    value[jss::nft_id] = to_string(output.nftID);

    json::Value& offers(value[jss::offers] = json::ValueType::Array);
    for (auto const& offer : output.offers)
        appendNftOfferJson(*offer, offers);

    if (output.marker.has_value())
    {
        value[jss::limit] = output.limit;
        value[jss::marker] = to_string(*output.marker);
    }
}

}  // namespace xrpl::rpc
