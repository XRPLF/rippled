#include <xrpld/rpc/handlers/transaction/TransactionEntry.h>

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/misc/DeliverMax.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>

#include <rpcspec/Errors.hpp>
#include <rpcspec/handlers/transaction_entry/Types.hpp>

#include <expected>
#include <string>
#include <tuple>

namespace xrpl::rpc {

TransactionEntryHandler::TransactionEntryHandler(JsonContext& context) : context_(context)
{
}

// With no ledger specified, this uses the current ledger, as every other method
// does. An open ledger is refused with notYetImplemented, so a request with no
// ledger fails the same way as one that asks for "current".
std::expected<TransactionEntryHandler::Output, ::rpc::Status>
TransactionEntryHandler::process(Input const& input) const
{
    Output output;
    if (auto const status = getLedger(output.ledger, input.ledger, context_.get()))
        return std::unexpected{status};

    if (!input.txHash &&
        input.txHash.error() == ::rpc::spec::handlers::transaction_entry::TxHashError::Missing)
    {
        output.error = ::rpc::Status{
            RpcFieldNotFoundTransaction, missingFieldMessage(std::string{jss::tx_hash.cStr()})};
    }
    else if (output.ledger->open())
    {
        // We don't work on ledger current.
        output.error = ::rpc::Status{RpcNotYetImplemented};
    }
    else if (!input.txHash)
    {
        output.error = ::rpc::Status{RpcMalformedRequest};
    }
    else
    {
        std::tie(output.tx, output.meta) = output.ledger->txRead(*input.txHash);
        if (!output.tx)
            output.error = ::rpc::Status{RpcTransactionNotFound};
    }

    return output;
}

void
TransactionEntryHandler::writeResult(json::Value& value, Output const& output) const
{
    auto const& context = context_.get();
    auto const& ledger = *output.ledger;

    injectLedgerFields(ledger, context, value);

    if (output.error)
    {
        // The error travels in the Output, not as a failed process(), so the reply keeps the
        // ledger fields written above beside the token, code and message.
        injectSpecError(value, *output.error);
        return;
    }

    if (context.apiVersion > 1)
    {
        value[jss::tx_json] = output.tx->getJson(JsonOptions::Values::DisableApiPriorV2);
        value[jss::hash] = to_string(output.tx->getTransactionID());

        if (!ledger.open())
            value[jss::ledger_hash] = to_string(context.ledgerMaster.getHashBySeq(ledger.seq()));

        bool const validated = context.ledgerMaster.isValidated(ledger);

        value[jss::validated] = validated;
        if (validated)
        {
            value[jss::ledger_index] = ledger.seq();
            if (auto closeTime = context.ledgerMaster.getCloseTimeBySeq(ledger.seq()))
                value[jss::close_time_iso] = toStringIso(*closeTime);
        }
    }
    else
    {
        value[jss::tx_json] = output.tx->getJson(JsonOptions::Values::None);
    }

    insertDeliverMax(value[jss::tx_json], output.tx->getTxnType(), context.apiVersion);

    auto const jsonMeta = (context.apiVersion > 1 ? jss::meta : jss::metadata);
    if (output.meta)
        value[jsonMeta] = output.meta->getJson(JsonOptions::Values::None);
}

}  // namespace xrpl::rpc
