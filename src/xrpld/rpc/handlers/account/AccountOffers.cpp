#include <xrpld/rpc/handlers/account/AccountOffers.h>

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>

#include <rpcspec/Errors.hpp>
#include <rpcspec/handlers/account_offers/Types.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>

namespace xrpl::rpc {

static_assert(::rpc::spec::handlers::account_offers::kLimitMin == tuning::kAccountOffers.rmin);
static_assert(
    ::rpc::spec::handlers::account_offers::kLimitDefault == tuning::kAccountOffers.rDefault);
static_assert(::rpc::spec::handlers::account_offers::kLimitMax == tuning::kAccountOffers.rmax);

namespace {

void
appendOfferJson(SLE::ConstRef offer, json::Value& offers)
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
}

}  // namespace

AccountOffersHandler::AccountOffersHandler(JsonContext& context) : context_(context)
{
}

std::expected<AccountOffersHandler::Output, ::rpc::Status>
AccountOffersHandler::process(Input const& input) const
{
    auto& context = context_.get();

    Output output;
    if (auto const status = getLedger(output.ledger, input.ledger, context);
        status != ::rpc::Status::kOK)
        return std::unexpected{status};
    auto const& ledger = *output.ledger;

    if (not input.account.has_value())
    {
        output.error = RpcActMalformed;
        return output;
    }
    auto const accountID = *input.account;
    output.account = accountID;

    if (!ledger.exists(keylet::account(accountID)))
        return std::unexpected{RpcActNotFound};

    auto const limit = clampLimit(input.limit, tuning::kAccountOffers, context.role);

    auto const parsed = input.marker.has_value()
        ? parseAccountMarker(*input.marker)
        : std::optional{std::pair{uint256{beast::kZero}, std::uint64_t{0}}};
    if (not parsed.has_value())
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::rpc::AccountOffersHandler::process : marker validated by the spec");
        return std::unexpected{::rpc::Status{RpcInvalidParams, invalidFieldMessage(jss::marker)}};
        // LCOV_EXCL_STOP
    }
    auto const [startAfter, startHint] = *parsed;

    if (input.marker.has_value())
    {
        // We then must check if the object pointed to by the marker is actually
        // owned by the account in the request.
        auto const sle = ledger.read({ltANY, startAfter});

        if (!sle)
            return std::unexpected{RpcInvalidParams};

        if (!isRelatedToAccount(ledger, sle, accountID))
            return std::unexpected{RpcInvalidParams};
    }

    std::uint32_t count = 0;
    std::optional<UInt256> marker = {};
    std::uint64_t nextHint = 0;
    if (!forEachItemAfter(
            ledger,
            accountID,
            startAfter,
            startHint,
            limit + 1,
            [&output, &count, &marker, &limit, &nextHint, &accountID](SLE::ConstRef sle) {
                if (!sle)
                {
                    // LCOV_EXCL_START
                    UNREACHABLE("xrpl::rpc::AccountOffersHandler::process : null SLE");
                    return false;
                    // LCOV_EXCL_STOP
                }

                if (++count == limit)
                {
                    marker = sle->key();
                    nextHint = getStartHint(sle, accountID);
                }

                if (count <= limit && sle->getType() == ltOFFER)
                {
                    output.offers.emplace_back(sle);
                }

                return true;
            }))
    {
        return std::unexpected{RpcInvalidParams};
    }

    // Both conditions need to be checked because marker is set on the limit-th
    // item, but if there is no item on the limit + 1 iteration, then there is
    // no need to return a marker.
    if (count == limit + 1 && marker.has_value())
    {
        output.limit = limit;
        output.marker = marker;
        output.nextHint = nextHint;
    }

    context.loadType = resource::kFeeMediumBurdenRpc;
    return output;
}

void
AccountOffersHandler::writeResult(json::Value& value, Output const& output) const
{
    injectLedgerFields(*output.ledger, context_.get(), value);

    if (output.error.has_value())
    {
        injectError(*output.error, value);
        return;
    }

    value[jss::account] = toBase58(output.account);

    json::Value& jsonOffers(value[jss::offers] = json::ValueType::Array);
    for (auto const& offer : output.offers)
        appendOfferJson(offer, jsonOffers);

    if (output.marker.has_value())
    {
        value[jss::limit] = output.limit;
        value[jss::marker] = to_string(*output.marker) + "," + std::to_string(output.nextHint);
    }
}

}  // namespace xrpl::rpc
