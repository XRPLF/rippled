#include <xrpld/rpc/handlers/account/AccountOffers.h>

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/StringUtilities.h>
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
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>

#include <rpcspec/Errors.hpp>

#include <algorithm>
#include <cstdint>
#include <expected>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>

namespace xrpl::rpc {

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

// Marker is composed of a comma separated index and start hint. The former is
// read as hex, and the latter as a decimal integer.
std::optional<std::pair<uint256, std::uint64_t>>
parseMarker(std::string const& marker)
{
    std::stringstream stream(marker);
    std::string value;
    uint256 startAfter;
    if (!std::getline(stream, value, ',') || !startAfter.parseHex(value))
        return std::nullopt;

    if (!std::getline(stream, value, ','))
        return std::nullopt;

    auto const hint = toUInt64(value);
    if (!hint.has_value())
        return std::nullopt;

    return std::pair{startAfter, *hint};
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
    if (auto const status = getLedger(output.ledger, input.ledger, context))
        return std::unexpected{status};
    auto const& ledger = *output.ledger;

    if (!input.account)
    {
        output.error = RpcActMalformed;
        return output;
    }
    auto const accountID = *input.account;
    output.account = accountID;

    if (!ledger.exists(keylet::account(accountID)))
        return std::unexpected{RpcActNotFound};

    auto limit = input.limit;
    if (!isUnlimited(context.role))
        limit = std::clamp(limit, tuning::kAccountOffers.rmin, tuning::kAccountOffers.rmax);

    UInt256 startAfter = beast::kZero;
    std::uint64_t startHint = 0;

    if (input.marker)
    {
        auto const parsed = parseMarker(*input.marker);
        if (!parsed)
        {
            // LCOV_EXCL_START
            return std::unexpected{
                ::rpc::Status{RpcInvalidParams, invalidFieldMessage(jss::marker)}};
            // LCOV_EXCL_STOP
        }
        std::tie(startAfter, startHint) = *parsed;

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
    if (count == limit + 1 && marker)
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

    if (output.error)
    {
        injectError(*output.error, value);
        return;
    }

    value[jss::account] = toBase58(output.account);

    json::Value& jsonOffers(value[jss::offers] = json::ValueType::Array);
    for (auto const& offer : output.offers)
        appendOfferJson(offer, jsonOffers);

    if (output.marker)
    {
        value[jss::limit] = output.limit;
        value[jss::marker] = to_string(*output.marker) + "," + std::to_string(output.nextHint);
    }
}

}  // namespace xrpl::rpc
