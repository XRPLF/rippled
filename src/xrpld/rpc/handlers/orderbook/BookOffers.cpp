#include <xrpld/rpc/handlers/orderbook/BookOffers.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/Job.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>
#include <xrpl/server/NetworkOPs.h>

#include <rpcspec/Errors.hpp>
#include <rpcspec/handlers/book_offers/Types.hpp>

#include <expected>
#include <memory>
#include <optional>

namespace xrpl::rpc {

static_assert(::rpc::spec::handlers::book_offers::kLimitMin == tuning::kBookOffers.rmin);
static_assert(::rpc::spec::handlers::book_offers::kLimitDefault == tuning::kBookOffers.rDefault);
static_assert(::rpc::spec::handlers::book_offers::kLimitMax == tuning::kBookOffers.rmax);

BookOffersHandler::BookOffersHandler(JsonContext& context) : context_(context)
{
}

std::expected<BookOffersHandler::Output, ::rpc::Status>
BookOffersHandler::process(Input const& input) const
{
    auto& context = context_.get();

    // VFALCO TODO Here is a terrible place for this kind of business
    //             logic. It needs to be moved elsewhere and documented,
    //             and encapsulated into a function.
    if (context.app.getJobQueue().getJobCountGE(JtClient) > 200)
        return std::unexpected{RpcTooBusy};

    Output output;
    if (auto const status = getLedger(output.ledger, input.ledger, context);
        status != ::rpc::Status::kOK)
    {
        return std::unexpected{status};
    }

    std::optional<UInt256> domain;
    if (input.domain.has_value())
    {
        domain.emplace();
        [[maybe_unused]] bool const parsed = domain->parseHex(*input.domain);
        XRPL_ASSERT(parsed, "xrpl::rpc::BookOffersHandler::process : domain is hex");
    }

    if (input.takerPays == input.takerGets)
    {
        JLOG(context.j.info()) << "taker_gets same as taker_pays.";
        return std::unexpected{RpcBadMarket};
    }

    auto const limit = clampLimit(input.limit, tuning::kBookOffers, context.role);

    context.netOps.getBookPage(
        output.ledger,
        {input.takerPays, input.takerGets, domain},
        input.taker ? *input.taker : beast::kZero,
        context.params.isMember(jss::proof),
        limit,
        context.params.isMember(jss::marker) ? context.params[jss::marker]
                                             : json::Value(json::ValueType::Null),
        output.page);

    context.loadType = resource::kFeeMediumBurdenRpc;
    return output;
}

void
BookOffersHandler::writeResult(json::Value& value, Output const& output) const
{
    value = output.page;
    injectLedgerFields(*output.ledger, context_.get(), value);
}

}  // namespace xrpl::rpc
