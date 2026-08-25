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

#include <memory>

namespace xrpl {

// {
//   ledger_hash : <ledger>,
//   ledger_index : <ledger_index>
// }
//
// Note: not specifying either ledger does not mean ledger current — it
// means any ledger.
json::Value
doTransactionEntry(rpc::JsonContext& context)
{
    std::shared_ptr<ReadView const> lpLedger;
    json::Value jvResult = rpc::lookupLedger(lpLedger, context);

    if (!lpLedger)
        return jvResult;

    if (!context.params.isMember(jss::tx_hash))
    {
        // AC-1: missing tx_hash parameter
        rpc::injectError(RpcInvalidParams, jvResult);
    }
    else if (jvResult.get(jss::ledger_hash, json::ValueType::Null).isNull())
    {
        // We don't work on ledger current.
        // AC-2: no closed ledger specified
        rpc::injectError(RpcNotImpl, jvResult);
    }
    else
    {
        uint256 uTransID;
        if (!uTransID.parseHex(context.params[jss::tx_hash].asString()))
        {
            // AC-3: tx_hash is not valid hex
            rpc::injectError(RpcInvalidParams, jvResult);
            return jvResult;
        }

        auto [sttx, stobj] = lpLedger->txRead(uTransID);
        if (!sttx)
        {
            // AC-4: transaction not found in ledger
            rpc::injectError(RpcTxnNotFound, jvResult);
        }
        else
        {
            if (context.apiVersion > 1)
            {
                jvResult[jss::tx_json] = sttx->getJson(JsonOptions::Values::DisableApiPriorV2);
                jvResult[jss::hash] = to_string(sttx->getTransactionID());

                if (!lpLedger->open())
                {
                    jvResult[jss::ledger_hash] =
                        to_string(context.ledgerMaster.getHashBySeq(lpLedger->seq()));
                }

                bool const validated = context.ledgerMaster.isValidated(*lpLedger);

                jvResult[jss::validated] = validated;
                if (validated)
                {
                    jvResult[jss::ledger_index] = lpLedger->seq();
                    if (auto closeTime = context.ledgerMaster.getCloseTimeBySeq(lpLedger->seq()))
                        jvResult[jss::close_time_iso] = toStringIso(*closeTime);
                }
            }
            else
            {
                jvResult[jss::tx_json] = sttx->getJson(JsonOptions::Values::None);
            }

            rpc::insertDeliverMax(jvResult[jss::tx_json], sttx->getTxnType(), context.apiVersion);

            auto const jsonMeta = (context.apiVersion > 1 ? jss::meta : jss::metadata);
            if (stobj)
                jvResult[jsonMeta] = stobj->getJson(JsonOptions::Values::None);
            // 'accounts'
            // 'engine_...'
            // 'ledger_...'
        }
    }

    return jvResult;
}

}  // namespace xrpl
