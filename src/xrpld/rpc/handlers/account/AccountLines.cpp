#include <xrpld/rpc/handlers/account/AccountLines.h>

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/TrustLine.h>
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
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>

#include <rpcspec/AccountTypes.hpp>
#include <rpcspec/Errors.hpp>
#include <rpcspec/handlers/account_lines/Types.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>

namespace xrpl::rpc {

static_assert(::rpc::spec::handlers::account_lines::kLimitMin == tuning::kAccountLines.rmin);
static_assert(
    ::rpc::spec::handlers::account_lines::kLimitDefault == tuning::kAccountLines.rDefault);
static_assert(::rpc::spec::handlers::account_lines::kLimitMax == tuning::kAccountLines.rmax);

namespace {

void
appendLineJson(RPCTrustLine const& line, json::Value& lines)
{
    STAmount const& saBalance(line.getBalance());
    STAmount const& saLimit(line.getLimit());
    STAmount const& saLimitPeer(line.getLimitPeer());
    json::Value& jPeer(lines.append(json::ValueType::Object));

    jPeer[jss::account] = to_string(line.getAccountIDPeer());
    // Amount reported is positive if current account holds other
    // account's IOUs.
    //
    // Amount reported is negative if other account holds current
    // account's IOUs.
    jPeer[jss::balance] = saBalance.getText();
    jPeer[jss::currency] = to_string(saBalance.get<Issue>().currency);
    jPeer[jss::limit] = saLimit.getText();
    jPeer[jss::limit_peer] = saLimitPeer.getText();
    jPeer[jss::quality_in] = line.getQualityIn().value;
    jPeer[jss::quality_out] = line.getQualityOut().value;
    if (line.getAuth())
        jPeer[jss::authorized] = true;
    if (line.getAuthPeer())
        jPeer[jss::peer_authorized] = true;
    if (line.getNoRipple())
        jPeer[jss::no_ripple] = true;
    if (line.getNoRipplePeer())
        jPeer[jss::no_ripple_peer] = true;
    if (line.getFreeze())
        jPeer[jss::freeze] = true;
    if (line.getFreezePeer())
        jPeer[jss::freeze_peer] = true;
    if (line.getDeepFreeze())
        jPeer[jss::deep_freeze] = true;
    if (line.getDeepFreezePeer())
        jPeer[jss::deep_freeze_peer] = true;
}

}  // namespace

AccountLinesHandler::AccountLinesHandler(JsonContext& context) : context_(context)
{
}

std::expected<AccountLinesHandler::Output, ::rpc::Status>
AccountLinesHandler::process(Input const& input) const
{
    auto& context = context_.get();

    Output output;
    if (auto const status = getLedger(output.ledger, input.ledger, context);
        status != ::rpc::Status::kOK)
        return std::unexpected{status};
    auto const& ledger = *output.ledger;

    if (!input.account.has_value())
    {
        output.error = RpcActMalformed;
        return output;
    }
    auto const accountID = *input.account;
    output.account = accountID;

    if (!ledger.exists(keylet::account(accountID)))
        return std::unexpected{RpcActNotFound};

    auto const& peer = input.peer;
    if (!peer.has_value() && peer.error() == ::rpc::spec::AccountError::Malformed)
    {
        output.error = RpcActMalformed;
        return output;
    }

    auto const limit = clampLimit(input.limit, tuning::kAccountLines, context.role);

    auto const parsed = input.marker.has_value()
        ? parseAccountMarker(*input.marker)
        : std::optional{std::pair{uint256{beast::kZero}, std::uint64_t{0}}};
    if (!parsed.has_value())
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::rpc::AccountLinesHandler::process : marker validated by the spec");
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

    bool const ignoreDefault = input.ignoreDefault;
    std::uint32_t count = 0;
    std::optional<uint256> marker;
    std::uint64_t nextHint = 0;
    if (!forEachItemAfter(
            ledger,
            accountID,
            startAfter,
            startHint,
            limit + 1,
            [&output, &accountID, &peer, ignoreDefault, &count, &limit, &marker, &nextHint](
                SLE::ConstRef sle) {
                if (!sle)
                {
                    // LCOV_EXCL_START
                    UNREACHABLE("xrpl::rpc::AccountLinesHandler::process : null SLE");
                    return false;
                    // LCOV_EXCL_STOP
                }

                if (++count == limit)
                {
                    marker = sle->key();
                    nextHint = getStartHint(sle, accountID);
                }

                if (sle->getType() != ltRIPPLE_STATE)
                    return true;

                bool ignore = false;
                if (ignoreDefault)
                {
                    if (sle->getFieldAmount(sfLowLimit).getIssuer() == accountID)
                    {
                        ignore = !sle->isFlag(lsfLowReserve);
                    }
                    else
                    {
                        ignore = !sle->isFlag(lsfHighReserve);
                    }
                }

                if (!ignore && count <= limit)
                {
                    auto const line = RPCTrustLine::makeItem(accountID, sle);

                    if (line.has_value() &&
                        (!peer.has_value() || *peer == line->getAccountIDPeer()))
                    {
                        output.lines.emplace_back(*line);
                    }
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
AccountLinesHandler::writeResult(json::Value& value, Output const& output) const
{
    injectLedgerFields(*output.ledger, context_.get(), value);

    if (output.error.has_value())
    {
        injectError(*output.error, value);
        return;
    }

    json::Value& lines(value[jss::lines] = json::ValueType::Array);
    for (auto const& line : output.lines)
        appendLineJson(line, lines);

    if (output.marker.has_value())
    {
        value[jss::limit] = output.limit;
        value[jss::marker] = to_string(*output.marker) + "," + std::to_string(output.nextHint);
    }

    value[jss::account] = toBase58(output.account);
}

}  // namespace xrpl::rpc
