#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/misc/DeliverMax.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

// {
//   ledger_hash : <ledger>,
//   ledger_index : <ledger_index>
// }
//
// XXX In this case, not specify either ledger does not mean ledger current. It
// means any ledger.
Json::Value
doTransactionEntry(RPC::JsonContext& context)
{
    std::shared_ptr<ReadView const> lpLedger;
    Json::Value jvResult = RPC::lookupLedger(lpLedger, context);

    if (!lpLedger)
        return jvResult;

    if (!context.params.isMember(jss::tx_hash))
    {
        jvResult[jss::error] = "fieldNotFoundTransaction";
    }
    else if (jvResult.get(jss::ledger_hash, Json::nullValue).isNull())
    {
        // We don't work on ledger current.

        // XXX We don't support any transaction yet.
        jvResult[jss::error] = "notYetImplemented";
    }
    else
    {
        uint256 uTransID;
        // XXX Relying on trusted WSS client. Would be better to have a strict
        // routine, returning success or failure.
        if (!uTransID.parseHex(context.params[jss::tx_hash].asString()))
        {
            jvResult[jss::error] = "malformedRequest";
            return jvResult;
        }

        auto [sttx, stobj] = lpLedger->txRead(uTransID);
        if (!sttx)
        {
            jvResult[jss::error] = "transactionNotFound";
        }
        else
        {
            if (context.apiVersion > 1)
            {
                jvResult[jss::tx_json] =
                    sttx->getJson(JsonOptions::disable_API_prior_V2);
                jvResult[jss::hash] = to_string(sttx->getTransactionID());

                if (!lpLedger->open())
                    jvResult[jss::ledger_hash] = to_string(
                        context.ledgerMaster.getHashBySeq(lpLedger->seq()));

                bool const validated =
                    context.ledgerMaster.isValidated(*lpLedger);

                jvResult[jss::validated] = validated;
                if (validated)
                {
                    jvResult[jss::ledger_index] = lpLedger->seq();
                    if (auto closeTime = context.ledgerMaster.getCloseTimeBySeq(
                            lpLedger->seq()))
                        jvResult[jss::close_time_iso] =
                            to_string_iso(*closeTime);
                }
            }
            else
                jvResult[jss::tx_json] = sttx->getJson(JsonOptions::none);

            RPC::insertDeliverMax(
                jvResult[jss::tx_json], sttx->getTxnType(), context.apiVersion);

            auto const json_meta =
                (context.apiVersion > 1 ? jss::meta : jss::metadata);
            if (stobj)
                jvResult[json_meta] = stobj->getJson(JsonOptions::none);
            // 'accounts'
            // 'engine_...'
            // 'ledger_...'
        }
    }

    return jvResult;
}

}  // namespace ripple
