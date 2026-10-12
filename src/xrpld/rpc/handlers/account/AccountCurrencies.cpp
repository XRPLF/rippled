#include <xrpld/rpc/handlers/account/AccountCurrencies.h>

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/TrustLine.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <rpcspec/Errors.hpp>

#include <expected>

namespace xrpl::rpc {

AccountCurrenciesHandler::AccountCurrenciesHandler(JsonContext& context) : context_(context)
{
}

std::expected<AccountCurrenciesHandler::Output, ::rpc::Status>
AccountCurrenciesHandler::process(Input const& input) const
{
    Output output;
    if (auto const status = getLedger(output.ledger, input.ledger, context_.get());
        status != ::rpc::Status::kOK)
        return std::unexpected{status};

    if (!input.account.has_value())
    {
        output.error = RpcActMalformed;
        return output;
    }
    auto const& accountID = *input.account;

    if (!output.ledger->exists(keylet::account(accountID)))
        return std::unexpected{RpcActNotFound};

    for (auto const& rspEntry : RPCTrustLine::getItems(accountID, *output.ledger))
    {
        STAmount const& saBalance = rspEntry.getBalance();

        if (saBalance < rspEntry.getLimit())
            output.receive.insert(saBalance.get<Issue>().currency);
        if ((-saBalance) < rspEntry.getLimitPeer())
            output.send.insert(saBalance.get<Issue>().currency);
    }

    output.send.erase(badCurrency());
    output.receive.erase(badCurrency());

    return output;
}

void
AccountCurrenciesHandler::writeResult(json::Value& value, Output const& output) const
{
    injectLedgerFields(*output.ledger, context_.get(), value);

    if (output.error.has_value())
    {
        injectError(*output.error, value);
        return;
    }

    json::Value& sendCurrencies = (value[jss::send_currencies] = json::ValueType::Array);
    for (auto const& c : output.send)
        sendCurrencies.append(to_string(c));

    json::Value& recvCurrencies = (value[jss::receive_currencies] = json::ValueType::Array);
    for (auto const& c : output.receive)
        recvCurrencies.append(to_string(c));
}

}  // namespace xrpl::rpc
