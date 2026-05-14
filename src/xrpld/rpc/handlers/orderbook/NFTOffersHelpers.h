/** @file
 *  Shared enumeration logic for the `nft_buy_offers` and `nft_sell_offers`
 *  RPC handlers.
 *
 *  Both commands traverse an NFToken offer directory and serialize results
 *  with identical pagination semantics; they differ only in the `Keylet`
 *  used to locate that directory (buy-side vs. sell-side). Rather than
 *  duplicate this logic, the two handlers are implemented as thin wrappers
 *  around `enumerateNFTOffers` and `appendNftOfferJson`, which are defined
 *  here as `inline` free functions.
 */

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

/** Serialize a single NFToken offer SLE into a JSON accumulator array.
 *
 *  Always emits `nft_offer_index`, `flags`, `owner`, and `amount`.
 *  The optional fields `destination` and `expiration` are emitted only
 *  when present in the SLE, correctly reflecting their optional status
 *  in the on-ledger format.
 *
 *  @param app   The running Application instance (passed for context
 *      consistency with other JSON helpers; not used directly here).
 *  @param offer An SLE of type `ltNFTOKEN_OFFER` to serialize.
 *  @param offers The JSON array to which the new offer object is appended.
 */
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

/** Walk an NFToken offer directory and return a paginated JSON response.
 *
 *  Resolves the ledger, checks that `directory` exists, then iterates
 *  `ltNFTOKEN_OFFER` entries using `forEachItemAfter`. Results are bounded
 *  by `RPC::Tuning::kNFT_OFFERS` (min 50, default 250, max 500).
 *
 *  **Fresh queries (no `marker`):** fetches up to `limit + 1` items. If
 *  exactly `limit + 1` items are returned, the last item becomes the next
 *  `marker` and is excluded from the serialized `offers` array.
 *
 *  **Resume queries (with `marker`):** the marker is parsed as a hex
 *  `uint256` ledger key. The corresponding SLE is read and its
 *  `sfNFTokenID` is validated against `nftId` — mismatches return
 *  `rpcINVALID_PARAMS`, preventing cross-token marker reuse. The
 *  `sfNFTokenOfferNode` field is used as `startHint` so `forEachItemAfter`
 *  jumps directly to the correct directory page. The marker offer itself
 *  is prepended to the result before fetching the next `limit` items.
 *
 *  If `forEachItemAfter` returns `false` (corrupted or inconsistent
 *  directory), `rpcINVALID_PARAMS` is returned.
 *
 *  Sets `context.loadType` to `kFEE_MEDIUM_BURDEN_RPC` on success.
 *
 *  @param context    RPC dispatch context; `context.params` is read for
 *      `ledger_hash`, `ledger_index`, `limit`, and `marker`.
 *  @param nftId      The 256-bit token ID whose offers are enumerated.
 *  @param directory  Keylet of the offer directory to traverse — either
 *      `keylet::nftBuys(nftId)` or `keylet::nftSells(nftId)`.
 *  @return A JSON object containing `nft_id`, an `offers` array of
 *      serialized `ltNFTOKEN_OFFER` entries, and an optional `limit` and
 *      hex-encoded `marker` when the result set was truncated.
 *  @see appendNftOfferJson, doNFTBuyOffers, doNFTSellOffers
 */
inline json::Value
enumerateNFTOffers(RPC::JsonContext& context, uint256 const& nftId, Keylet const& directory)
{
    unsigned int limit = 0;
    if (auto err = readLimitField(limit, RPC::Tuning::kNFT_OFFERS, context))
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

    context.loadType = Resource::kFEE_MEDIUM_BURDEN_RPC;
    return result;
}

}  // namespace xrpl
