#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/misc/DeliverMax.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/jss.h>

#include <memory>

namespace xrpl {

/** Retrieve a transaction and its metadata from a specific, caller-identified ledger.
 *
 *  Implements the `transaction_entry` RPC command. Unlike `tx` (which searches all
 *  available history by hash alone), this handler requires the caller to name the
 *  containing ledger via `ledger_hash` or `ledger_index`. Omitting both fields is
 *  NOT equivalent to requesting the current ledger — `lookupLedger` will resolve
 *  to an arbitrary ledger, and the open-ledger guard below will then reject it
 *  with `notYetImplemented`. Callers must always supply an explicit ledger specifier.
 *
 *  The validation chain is layered from cheapest to most expensive:
 *  1. Ledger resolution — `RPC::lookupLedger` validates the specifier and populates
 *     `lpLedger`; returns the error immediately if the ledger cannot be resolved.
 *  2. `tx_hash` presence — absence yields `fieldNotFoundTransaction`.
 *  3. Open/current ledger rejection — if `lookupLedger` did not set `ledger_hash`
 *     in `jvResult` the resolved ledger is open/current, which is unsupported;
 *     yields `notYetImplemented`.
 *  4. Hex parsing — `parseHex` on the `tx_hash` string; invalid input yields
 *     `malformedRequest`. In practice `parseHex` rejects any non-hex or wrong-length
 *     value, so this is the effective input-validation gate.
 *  5. Transaction existence — `txRead` against the `ReadView`; absence yields
 *     `transactionNotFound`.
 *
 *  Response shape diverges by API version:
 *  - **v1**: `tx_json` with legacy serialisation (`JsonOptions::none`); metadata
 *    key is `metadata`; `hash` is embedded inside `tx_json`.
 *  - **v2+**: `tx_json` with `DisableApiPriorV2` serialisation; top-level `hash`,
 *    `ledger_hash` (closed ledgers only), `validated`, `ledger_index`, and
 *    `close_time_iso` (validated ledgers only); metadata key is `meta`.
 *  For Payment transactions both versions have `insertDeliverMax` applied, which
 *  copies (v1) or renames (v2+) `Amount` to `DeliverMax`.
 *
 *  @param context  RPC dispatch context carrying `params`, `apiVersion`,
 *      `ledgerMaster`, and the resource consumer for this request.
 *  @return JSON object with `tx_json` and metadata on success, or an error
 *      object whose `error` field is one of: `fieldNotFoundTransaction`,
 *      `notYetImplemented`, `malformedRequest`, `transactionNotFound`, or
 *      any error code propagated by `RPC::lookupLedger`.
 *  @note `stobj` (transaction metadata) may legitimately be null for
 *      transactions in open ledgers — metadata is produced only at close.
 *      The open-ledger guard above makes this case unreachable in practice,
 *      but the serialisation step is still guarded by a null check for safety.
 *  @see doTxJson for the broader sibling command that searches all history.
 */
json::Value
doTransactionEntry(RPC::JsonContext& context)
{
    std::shared_ptr<ReadView const> lpLedger;
    json::Value jvResult = RPC::lookupLedger(lpLedger, context);

    if (!lpLedger)
        return jvResult;

    if (!context.params.isMember(jss::tx_hash))
    {
        jvResult[jss::error] = "fieldNotFoundTransaction";
    }
    else if (jvResult.get(jss::ledger_hash, json::ValueType::Null).isNull())
    {
        jvResult[jss::error] = "notYetImplemented";
    }
    else
    {
        uint256 uTransID;
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

            RPC::insertDeliverMax(jvResult[jss::tx_json], sttx->getTxnType(), context.apiVersion);

            auto const jsonMeta = (context.apiVersion > 1 ? jss::meta : jss::metadata);
            if (stobj)
                jvResult[jsonMeta] = stobj->getJson(JsonOptions::Values::None);
        }
    }

    return jvResult;
}

}  // namespace xrpl
