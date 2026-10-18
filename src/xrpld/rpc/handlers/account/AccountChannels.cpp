#include <xrpld/rpc/handlers/account/AccountChannels.h>

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/tokens.h>
#include <xrpl/resource/Fees.h>

#include <rpcspec/Errors.hpp>
#include <rpcspec/handlers/account_channels/Types.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>

namespace xrpl::rpc {

static_assert(::rpc::spec::handlers::account_channels::kLimitMin == tuning::kAccountChannels.rmin);
static_assert(
    ::rpc::spec::handlers::account_channels::kLimitDefault == tuning::kAccountChannels.rDefault);
static_assert(::rpc::spec::handlers::account_channels::kLimitMax == tuning::kAccountChannels.rmax);

namespace {

void
appendChannelJson(SLE const& channel, json::Value& channels)
{
    json::Value& jDst(channels.append(json::ValueType::Object));
    jDst[jss::channel_id] = to_string(channel.key());
    jDst[jss::account] = to_string(channel[sfAccount]);
    jDst[jss::destination_account] = to_string(channel[sfDestination]);
    jDst[jss::amount] = channel[sfAmount].getText();
    jDst[jss::balance] = channel[sfBalance].getText();
    if (publicKeyType(channel[sfPublicKey]))
    {
        PublicKey const pk(channel[sfPublicKey]);
        jDst[jss::public_key] = toBase58(TokenType::AccountPublic, pk);
        jDst[jss::public_key_hex] = strHex(pk);
    }
    jDst[jss::settle_delay] = channel[sfSettleDelay];
    if (auto const& v = channel[~sfExpiration])
        jDst[jss::expiration] = *v;
    if (auto const& v = channel[~sfCancelAfter])
        jDst[jss::cancel_after] = *v;
    if (auto const& v = channel[~sfSourceTag])
        jDst[jss::source_tag] = *v;
    if (auto const& v = channel[~sfDestinationTag])
        jDst[jss::destination_tag] = *v;
}

}  // namespace

AccountChannelsHandler::AccountChannelsHandler(JsonContext& context) : context_(context)
{
}

std::expected<AccountChannelsHandler::Output, ::rpc::Status>
AccountChannelsHandler::process(Input const& input) const
{
    auto& context = context_.get();

    Output output;
    if (auto const status = getLedger(output.ledger, input.ledger, context);
        status != ::rpc::Status::kOK)
        return std::unexpected{status};
    auto const& ledger = *output.ledger;

    auto const accountID = input.account;
    output.account = accountID;

    if (!ledger.exists(keylet::account(accountID)))
        return std::unexpected{RpcActNotFound};

    auto const& destination = input.destinationAccount;
    auto const limit = clampLimit(input.limit, tuning::kAccountChannels, context.role);

    auto const parsed = input.marker.has_value()
        ? parseAccountMarker(*input.marker)
        : std::optional{std::pair{uint256{beast::kZero}, std::uint64_t{0}}};
    if (!parsed.has_value())
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::rpc::AccountChannelsHandler::process : marker validated by the spec");
        return std::unexpected{RpcInvalidParams};
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
    std::optional<uint256> marker;
    std::uint64_t nextHint = 0;
    if (!forEachItemAfter(
            ledger,
            accountID,
            startAfter,
            startHint,
            limit + 1,
            [&output, &accountID, &destination, &count, &limit, &marker, &nextHint](
                SLE::ConstRef sle) {
                if (!sle)
                {
                    // LCOV_EXCL_START
                    UNREACHABLE("xrpl::rpc::AccountChannelsHandler::process : null SLE");
                    return false;
                    // LCOV_EXCL_STOP
                }

                if (++count == limit)
                {
                    marker = sle->key();
                    nextHint = getStartHint(sle, accountID);
                }

                if (count <= limit && sle->getType() == ltPAYCHAN &&
                    (*sle)[sfAccount] == accountID &&
                    (!destination.has_value() || *destination == (*sle)[sfDestination]))
                {
                    output.channels.emplace_back(sle);
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
AccountChannelsHandler::writeResult(json::Value& value, Output const& output) const
{
    injectLedgerFields(*output.ledger, context_.get(), value);

    if (output.marker.has_value())
    {
        value[jss::limit] = output.limit;
        value[jss::marker] = to_string(*output.marker) + "," + std::to_string(output.nextHint);
    }

    value[jss::account] = toBase58(output.account);

    json::Value& channels(value[jss::channels] = json::ValueType::Array);
    for (auto const& channel : output.channels)
        appendChannelJson(*channel, channels);
}

}  // namespace xrpl::rpc
