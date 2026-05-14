#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>

#include <boost/lexical_cast.hpp>
#include <boost/lexical_cast/bad_lexical_cast.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace xrpl {

/**
 * @file AccountOffers.cpp
 * @brief Implements the `account_offers` JSON-RPC handler.
 *
 * Exports two symbols: `appendOfferJson` (serialization helper) and
 * `doAccountOffers` (handler entry point). Follows the same paginated
 * owner-directory-walk pattern used by `AccountChannels`, `AccountLines`,
 * and `AccountObjects`.
 */

/** Serialize a single `ltOFFER` ledger entry into the output JSON array.
 *
 *  The exchange rate (`quality`) is recovered cheaply from the last 64 bits
 *  of `sfBookDirectory` — the hash key of the order-book directory node the
 *  offer belongs to. `getQuality()` extracts those bits and
 *  `amountFromQuality()` reconstructs an `STAmount` whose decimal string is
 *  units of `TakerPays` per unit of `TakerGets`. This avoids a division at
 *  read time; the quality is already materialized in the directory hash.
 *
 *  `sfExpiration` is only emitted when present, keeping the response lean for
 *  non-expiring offers.
 *
 *  @param offer   Ledger entry of type `ltOFFER` to serialize.
 *  @param offers  JSON array to which the new offer object is appended.
 */
void
appendOfferJson(std::shared_ptr<SLE const> const& offer, json::Value& offers)
{
    STAmount const dirRate = amountFromQuality(getQuality(offer->getFieldH256(sfBookDirectory)));
    json::Value& obj(offers.append(json::ValueType::Object));
    offer->getFieldAmount(sfTakerPays).setJson(obj[jss::taker_pays]);
    offer->getFieldAmount(sfTakerGets).setJson(obj[jss::taker_gets]);
    obj[jss::seq] = offer->getFieldU32(sfSequence);
    obj[jss::flags] = offer->getFieldU32(sfFlags);
    obj[jss::quality] = dirRate.getText();
    if (offer->isFieldPresent(sfExpiration))
        obj[jss::expiration] = offer->getFieldU32(sfExpiration);
};

/** Handler for the `account_offers` RPC command.
 *
 *  Returns the list of open DEX offers currently owned by a given account,
 *  paginated via a marker. Validation runs in strict order before any ledger
 *  iteration: field presence and type, ledger resolution, Base58 account
 *  parse, account existence, limit clamping (range [10, 400], default 200),
 *  and — when a marker is supplied — ownership verification that the marker
 *  object belongs to the requested account. This last check prevents a caller
 *  from crafting a marker that skips into another account's directory region.
 *
 *  Iteration uses `forEachItemAfter` with a `limit + 1` cap. The extra item
 *  is consumed but not returned; its sole purpose is to confirm that a next
 *  page exists. The marker is only emitted when both conditions hold: the
 *  marker was set at the limit-th item *and* a limit+1-th item was actually
 *  reached. An account's owner directory can contain mixed object types; only
 *  `ltOFFER` entries are serialized, but all types count toward the limit.
 *
 *  Resource cost is `feeMediumBurdenRPC` — directory walks involving ledger
 *  I/O are proportionally more expensive than simple point lookups.
 *
 *  @param context  RPC dispatch context carrying params, app, and loadType.
 *  @return JSON result with an `offers` array and, when more pages exist,
 *      `limit` and `marker` fields for the next page. Returns an RPC error
 *      JSON object on any validation failure.
 */
json::Value
doAccountOffers(RPC::JsonContext& context)
{
    auto const& params(context.params);
    if (!params.isMember(jss::account))
        return RPC::missingFieldError(jss::account);

    if (!params[jss::account].isString())
        return RPC::invalidFieldError(jss::account);

    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);
    if (!ledger)
        return result;

    auto id = parseBase58<AccountID>(params[jss::account].asString());
    if (!id)
    {
        RPC::injectError(RpcActMalformed, result);
        return result;
    }
    auto const accountID{id.value()};

    result[jss::account] = toBase58(accountID);

    if (!ledger->exists(keylet::account(accountID)))
        return rpcError(RpcActNotFound);

    unsigned int limit = 0;
    if (auto err = readLimitField(limit, RPC::Tuning::kACCOUNT_OFFERS, context))
        return *err;

    json::Value& jsonOffers(result[jss::offers] = json::ValueType::Array);
    std::vector<std::shared_ptr<SLE const>> offers;
    uint256 startAfter = beast::kZERO;
    std::uint64_t startHint = 0;

    if (params.isMember(jss::marker))
    {
        if (!params[jss::marker].isString())
            return RPC::expectedFieldError(jss::marker, "string");

        std::stringstream marker(params[jss::marker].asString());
        std::string value;
        if (!std::getline(marker, value, ','))
            return RPC::invalidFieldError(jss::marker);

        if (!startAfter.parseHex(value))
            return RPC::invalidFieldError(jss::marker);

        if (!std::getline(marker, value, ','))
            return RPC::invalidFieldError(jss::marker);

        try
        {
            startHint = boost::lexical_cast<std::uint64_t>(value);
        }
        catch (boost::bad_lexical_cast&)
        {
            return RPC::invalidFieldError(jss::marker);
        }

        auto const sle = ledger->read({ltANY, startAfter});

        if (!sle)
            return rpcError(RpcInvalidParams);

        if (!RPC::isRelatedToAccount(*ledger, sle, accountID))
            return rpcError(RpcInvalidParams);
    }

    auto count = 0;
    std::optional<uint256> marker = {};
    std::uint64_t nextHint = 0;
    if (!forEachItemAfter(
            *ledger,
            accountID,
            startAfter,
            startHint,
            limit + 1,
            [&offers, &count, &marker, &limit, &nextHint, &accountID](
                std::shared_ptr<SLE const> const& sle) {
                if (!sle)
                {
                    // LCOV_EXCL_START
                    UNREACHABLE("xrpl::doAccountOffers : null SLE");
                    return false;
                    // LCOV_EXCL_STOP
                }

                if (++count == limit)
                {
                    marker = sle->key();
                    nextHint = RPC::getStartHint(sle, accountID);
                }

                if (count <= limit && sle->getType() == ltOFFER)
                {
                    offers.emplace_back(sle);
                }

                return true;
            }))
    {
        return rpcError(RpcInvalidParams);
    }

    if (count == limit + 1 && marker)
    {
        result[jss::limit] = limit;
        result[jss::marker] = to_string(*marker) + "," + std::to_string(nextHint);
    }

    for (auto const& offer : offers)
        appendOfferJson(offer, jsonOffers);

    context.loadType = Resource::kFEE_MEDIUM_BURDEN_RPC;
    return result;
}

}  // namespace xrpl
