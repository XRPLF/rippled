#include <xrpld/rpc/handlers/orderbook/BookChanges.h>

#include <xrpld/rpc/BookChanges.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/STArray.h>  // IWYU pragma: keep

#include <rpcspec/Errors.hpp>

#include <expected>

namespace xrpl::rpc {

BookChangesHandler::BookChangesHandler(JsonContext& context) : context_(context)
{
}

std::expected<BookChangesHandler::Output, ::rpc::Status>
BookChangesHandler::process(Input const& input) const
{
    Output output;
    if (auto const status = getLedger(output.ledger, input.ledger, context_.get()))
        return std::unexpected{status};

    return output;
}

void
BookChangesHandler::writeResult(json::Value& value, Output const& output)
{
    value = computeBookChanges(output.ledger);
}

}  // namespace xrpl::rpc
