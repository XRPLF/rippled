#include <xrpld/app/ledger/LedgerToJson.h>

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/misc/DeliverMax.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/DeliveredAmount.h>
#include <xrpld/rpc/MPTokenIssuanceID.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/serialize.h>

#include <exception>
#include <memory>
#include <string>

namespace xrpl {

namespace {

bool
isFull(LedgerFill const& fill)
{
    return (fill.options & static_cast<int>(LedgerFill::Options::Full)) != 0;
}

bool
isExpanded(LedgerFill const& fill)
{
    return isFull(fill) || ((fill.options & static_cast<int>(LedgerFill::Options::Expand)) != 0);
}

bool
isBinary(LedgerFill const& fill)
{
    return (fill.options & static_cast<int>(LedgerFill::Options::Binary)) != 0;
}

void
fillJson(json::Value& json, bool closed, LedgerHeader const& info, bool bFull, unsigned apiVersion)
{
    json[jss::parent_hash] = to_string(info.parentHash);
    json[jss::ledger_index] =
        (apiVersion > 1) ? json::Value(info.seq) : json::Value(std::to_string(info.seq));

    if (closed)
    {
        json[jss::closed] = true;
    }
    else if (!bFull)
    {
        json[jss::closed] = false;
        return;
    }

    json[jss::ledger_hash] = to_string(info.hash);
    json[jss::transaction_hash] = to_string(info.txHash);
    json[jss::account_hash] = to_string(info.accountHash);
    json[jss::total_coins] = to_string(info.drops);

    json[jss::close_flags] = info.closeFlags;

    // Always show fields that contribute to the ledger hash
    json[jss::parent_close_time] = info.parentCloseTime.time_since_epoch().count();
    json[jss::close_time] = info.closeTime.time_since_epoch().count();
    json[jss::close_time_resolution] = info.closeTimeResolution.count();

    if (info.closeTime != NetClock::time_point{})
    {
        json[jss::close_time_human] = to_string(info.closeTime);
        if (!getCloseAgree(info))
            json[jss::close_time_estimated] = true;
        json[jss::close_time_iso] = toStringIso(info.closeTime);
    }
}

void
fillJsonBinary(json::Value& json, bool closed, LedgerHeader const& info)
{
    if (!closed)
    {
        json[jss::closed] = false;
    }
    else
    {
        json[jss::closed] = true;

        Serializer s;
        addRaw(info, s);
        json[jss::ledger_data] = strHex(s.peekData());
    }
}

json::Value
fillJsonTx(
    LedgerFill const& fill,
    bool bBinary,
    bool bExpanded,
    std::shared_ptr<STTx const> const& txn,
    std::shared_ptr<STObject const> const& stMeta)
{
    if (!bExpanded)
        return to_string(txn->getTransactionID());

    json::Value txJson{json::ValueType::Object};
    auto const txnType = txn->getTxnType();
    if (bBinary)
    {
        txJson[jss::tx_blob] = serializeHex(*txn);
        if (fill.context->apiVersion > 1)
            txJson[jss::hash] = to_string(txn->getTransactionID());

        auto const jsonMeta = (fill.context->apiVersion > 1 ? jss::meta_blob : jss::meta);
        if (stMeta)
            txJson[jsonMeta] = serializeHex(*stMeta);
    }
    else if (fill.context->apiVersion > 1)
    {
        copyFrom(txJson[jss::tx_json], txn->getJson(JsonOptions::Values::DisableApiPriorV2, false));
        txJson[jss::hash] = to_string(txn->getTransactionID());
        RPC::insertDeliverMax(txJson[jss::tx_json], txnType, fill.context->apiVersion);

        if (stMeta)
        {
            txJson[jss::meta] = stMeta->getJson(JsonOptions::Values::None);

            // If applicable, insert delivered amount
            if (txnType == ttPAYMENT || txnType == ttCHECK_CASH)
            {
                RPC::insertDeliveredAmount(
                    txJson[jss::meta],
                    fill.ledger,
                    txn,
                    {txn->getTransactionID(), fill.ledger.seq(), *stMeta});
            }

            // If applicable, insert mpt issuance id
            RPC::insertMPTokenIssuanceID(
                txJson[jss::meta], txn, {txn->getTransactionID(), fill.ledger.seq(), *stMeta});
        }

        if (!fill.ledger.open())
            txJson[jss::ledger_hash] = to_string(fill.ledger.header().hash);

        bool const validated = fill.context->ledgerMaster.isValidated(fill.ledger);
        txJson[jss::validated] = validated;
        if (validated)
        {
            auto const seq = fill.ledger.seq();
            txJson[jss::ledger_index] = seq;
            if (fill.closeTime)
                txJson[jss::close_time_iso] = toStringIso(*fill.closeTime);
        }
    }
    else
    {
        copyFrom(txJson, txn->getJson(JsonOptions::Values::None));
        RPC::insertDeliverMax(txJson, txnType, fill.context->apiVersion);
        if (stMeta)
        {
            txJson[jss::metaData] = stMeta->getJson(JsonOptions::Values::None);

            // If applicable, insert delivered amount
            if (txnType == ttPAYMENT || txnType == ttCHECK_CASH)
            {
                RPC::insertDeliveredAmount(
                    txJson[jss::metaData],
                    fill.ledger,
                    txn,
                    {txn->getTransactionID(), fill.ledger.seq(), *stMeta});
            }

            // If applicable, insert mpt issuance id
            RPC::insertMPTokenIssuanceID(
                txJson[jss::metaData], txn, {txn->getTransactionID(), fill.ledger.seq(), *stMeta});
        }
    }

    if (((fill.options & static_cast<int>(LedgerFill::Options::OwnerFunds)) != 0) &&
        txn->getTxnType() == ttOFFER_CREATE)
    {
        auto const account = txn->getAccountID(sfAccount);
        auto const amount = txn->getFieldAmount(sfTakerGets);

        // If the offer create is not self funded then add the
        // owner balance
        if (account != amount.getIssuer())
        {
            auto const ownerFunds = accountFunds(
                fill.ledger,
                account,
                amount,
                FreezeHandling::IgnoreFreeze,
                beast::Journal{beast::Journal::getNullSink()});
            txJson[jss::owner_funds] = ownerFunds.getText();
        }
    }

    return txJson;
}

void
fillJsonTx(json::Value& json, LedgerFill const& fill)
{
    auto& txns = json[jss::transactions] = json::ValueType::Array;
    auto bBinary = isBinary(fill);
    auto bExpanded = isExpanded(fill);

    try
    {
        auto appendAll = [&](auto const& txs) {
            for (auto& i : txs)
            {
                txns.append(fillJsonTx(fill, bBinary, bExpanded, i.first, i.second));
            }
        };

        appendAll(fill.ledger.txs);
    }
    catch (std::exception const& ex)
    {
        // Nothing the user can do about this.
        if (fill.context != nullptr)
        {
            JLOG(fill.context->j.error()) << "Exception in " << __func__ << ": " << ex.what();
        }
    }
}

void
fillJsonState(json::Value& json, LedgerFill const& fill)
{
    auto& ledger = fill.ledger;
    auto& array = json[jss::accountState] = json::ValueType::Array;
    auto expanded = isExpanded(fill);
    auto binary = isBinary(fill);

    for (auto const& sle : ledger.sles)
    {
        if (binary)
        {
            auto& obj = array.append(json::ValueType::Object);
            obj[jss::hash] = to_string(sle->key());
            obj[jss::tx_blob] = serializeHex(*sle);
        }
        else if (expanded)
        {
            array.append(sle->getJson(JsonOptions::Values::None));
        }
        else
        {
            array.append(to_string(sle->key()));
        }
    }
}

void
fillJsonQueue(json::Value& json, LedgerFill const& fill)
{
    auto& queueData = json[jss::queue_data] = json::ValueType::Array;
    auto bBinary = isBinary(fill);
    auto bExpanded = isExpanded(fill);

    for (auto const& tx : fill.txQueue)
    {
        auto& txJson = queueData.append(json::ValueType::Object);
        txJson[jss::fee_level] = to_string(tx.feeLevel);
        if (tx.lastValid)
            txJson[jss::LastLedgerSequence] = *tx.lastValid;

        txJson[jss::fee] = to_string(tx.consequences.fee());
        auto const spend = tx.consequences.potentialSpend() + tx.consequences.fee();
        txJson[jss::max_spend_drops] = to_string(spend);
        txJson[jss::auth_change] = tx.consequences.isBlocker();

        txJson[jss::account] = to_string(tx.account);
        txJson["retries_remaining"] = tx.retriesRemaining;
        txJson["preflight_result"] = transToken(tx.preflightResult);
        if (tx.lastResult)
            txJson["last_result"] = transToken(*tx.lastResult);

        auto&& temp = fillJsonTx(fill, bBinary, bExpanded, tx.txn, nullptr);
        if (temp.isObject())
        {
            if (fill.context->apiVersion > 1)
            {
                copyFrom(txJson, temp);
            }
            else
            {
                copyFrom(txJson[jss::tx], temp);
            }
        }
        else if (fill.context->apiVersion > 1)
        {
            txJson[jss::hash] = temp;
        }
        else
        {
            txJson[jss::tx] = temp;
        }
    }
}

void
fillJson(json::Value& json, LedgerFill const& fill)
{
    // TODO: what happens if bBinary and bExtracted are both set?
    // Is there a way to report this back?
    auto bFull = isFull(fill);
    if (isBinary(fill))
    {
        fillJsonBinary(json, !fill.ledger.open(), fill.ledger.header());
    }
    else
    {
        fillJson(
            json,
            !fill.ledger.open(),
            fill.ledger.header(),
            bFull,
            ((fill.context != nullptr) ? fill.context->apiVersion
                                       : RPC::kApiMaximumSupportedVersion));
    }

    if (bFull || ((fill.options & static_cast<int>(LedgerFill::Options::DumpTxrp)) != 0))
        fillJsonTx(json, fill);

    if (bFull || ((fill.options & static_cast<int>(LedgerFill::Options::DumpState)) != 0))
        fillJsonState(json, fill);
}

}  // namespace

void
addJson(json::Value& json, LedgerFill const& fill)
{
    auto& object = json[jss::ledger] = json::ValueType::Object;
    fillJson(object, fill);

    if (((fill.options & static_cast<int>(LedgerFill::Options::DumpQueue)) != 0) &&
        !fill.txQueue.empty())
    {
        fillJsonQueue(json, fill);
    }
}

json::Value
getJson(LedgerFill const& fill)
{
    json::Value json;
    fillJson(json, fill);
    return json;
}

void
copyFrom(json::Value& to, json::Value const& from)
{
    if (!to)
    {  // Short circuit this very common case.
        to = from;
    }
    else
    {
        // TODO: figure out if there is a way to remove this clause
        // or check that it does/needs to do a deep copy
        XRPL_ASSERT(from.isObjectOrNull(), "copyFrom : invalid input type");
        auto const members = from.getMemberNames();
        for (auto const& m : members)
            to[m] = from[m];
    }
}

}  // namespace xrpl
