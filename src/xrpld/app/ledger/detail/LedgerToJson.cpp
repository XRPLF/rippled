//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2015 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/LedgerToJson.h>
#include <xrpld/app/misc/DeliverMax.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/DeliveredAmount.h>
#include <xrpld/rpc/MPTokenIssuanceID.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

namespace {

bool
isFull(LedgerFill const& fill)
{
    return fill.options & LedgerFill::full;
}

bool
isExpanded(LedgerFill const& fill)
{
    return isFull(fill) || (fill.options & LedgerFill::expand);
}

bool
isBinary(LedgerFill const& fill)
{
    return fill.options & LedgerFill::binary;
}

template <class Object>
void
fillJson(
    Object& json,
    bool closed,
    LedgerInfo const& info,
    bool bFull,
    unsigned apiVersion)
{
    json[jss::parent_hash] = to_string(info.parentHash);
    json[jss::ledger_index] = (apiVersion > 1)
        ? Json::Value(info.seq)
        : Json::Value(std::to_string(info.seq));

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
    json[jss::parent_close_time] =
        info.parentCloseTime.time_since_epoch().count();
    json[jss::close_time] = info.closeTime.time_since_epoch().count();
    json[jss::close_time_resolution] = info.closeTimeResolution.count();

    if (info.closeTime != NetClock::time_point{})
    {
        json[jss::close_time_human] = to_string(info.closeTime);
        if (!getCloseAgree(info))
            json[jss::close_time_estimated] = true;
        json[jss::close_time_iso] = to_string_iso(info.closeTime);
    }
}

template <class Object>
void
fillJsonBinary(Object& json, bool closed, LedgerInfo const& info)
{
    if (!closed)
        json[jss::closed] = false;
    else
    {
        json[jss::closed] = true;

        Serializer s;
        addRaw(info, s);
        json[jss::ledger_data] = strHex(s.peekData());
    }
}

Json::Value
fillJsonTx(
    LedgerFill const& fill,
    bool bBinary,
    bool bExpanded,
    std::shared_ptr<STTx const> const& txn,
    std::shared_ptr<STObject const> const& stMeta)
{
    if (!bExpanded)
        return to_string(txn->getTransactionID());

    Json::Value txJson{Json::objectValue};
    auto const txnType = txn->getTxnType();
    if (bBinary)
    {
        txJson[jss::tx_blob] = serializeHex(*txn);
        if (fill.context->apiVersion > 1)
            txJson[jss::hash] = to_string(txn->getTransactionID());

        auto const json_meta =
            (fill.context->apiVersion > 1 ? jss::meta_blob : jss::meta);
        if (stMeta)
            txJson[json_meta] = serializeHex(*stMeta);
    }
    else if (fill.context->apiVersion > 1)
    {
        copyFrom(
            txJson[jss::tx_json],
            txn->getJson(JsonOptions::disable_API_prior_V2, false));
        txJson[jss::hash] = to_string(txn->getTransactionID());
        RPC::insertDeliverMax(
            txJson[jss::tx_json], txnType, fill.context->apiVersion);

        if (stMeta)
        {
            txJson[jss::meta] = stMeta->getJson(JsonOptions::none);

            // If applicable, insert delivered amount
            if (txnType == ttPAYMENT || txnType == ttCHECK_CASH)
                RPC::insertDeliveredAmount(
                    txJson[jss::meta],
                    fill.ledger,
                    txn,
                    {txn->getTransactionID(), fill.ledger.seq(), *stMeta});

            // If applicable, insert mpt issuance id
            RPC::insertMPTokenIssuanceID(
                txJson[jss::meta],
                txn,
                {txn->getTransactionID(), fill.ledger.seq(), *stMeta});
        }

        if (!fill.ledger.open())
            txJson[jss::ledger_hash] = to_string(fill.ledger.info().hash);

        bool const validated =
            fill.context->ledgerMaster.isValidated(fill.ledger);
        txJson[jss::validated] = validated;
        if (validated)
        {
            auto const seq = fill.ledger.seq();
            txJson[jss::ledger_index] = seq;
            if (fill.closeTime)
                txJson[jss::close_time_iso] = to_string_iso(*fill.closeTime);
        }
    }
    else
    {
        copyFrom(txJson, txn->getJson(JsonOptions::none));
        RPC::insertDeliverMax(txJson, txnType, fill.context->apiVersion);
        if (stMeta)
        {
            txJson[jss::metaData] = stMeta->getJson(JsonOptions::none);

            // If applicable, insert delivered amount
            if (txnType == ttPAYMENT || txnType == ttCHECK_CASH)
                RPC::insertDeliveredAmount(
                    txJson[jss::metaData],
                    fill.ledger,
                    txn,
                    {txn->getTransactionID(), fill.ledger.seq(), *stMeta});

            // If applicable, insert mpt issuance id
            RPC::insertMPTokenIssuanceID(
                txJson[jss::metaData],
                txn,
                {txn->getTransactionID(), fill.ledger.seq(), *stMeta});
        }
    }

    if ((fill.options & LedgerFill::ownerFunds) &&
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
                fhIGNORE_FREEZE,
                beast::Journal{beast::Journal::getNullSink()});
            txJson[jss::owner_funds] = ownerFunds.getText();
        }
    }

    return txJson;
}

template <class Object>
void
fillJsonTx(Object& json, LedgerFill const& fill)
{
    auto&& txns = setArray(json, jss::transactions);
    auto bBinary = isBinary(fill);
    auto bExpanded = isExpanded(fill);

    try
    {
        auto appendAll = [&](auto const& txs) {
            for (auto& i : txs)
            {
                txns.append(
                    fillJsonTx(fill, bBinary, bExpanded, i.first, i.second));
            }
        };

        appendAll(fill.ledger.txs);
    }
    catch (std::exception const& ex)
    {
        // Nothing the user can do about this.
        if (fill.context)
        {
            JLOG(fill.context->j.error())
                << "Exception in " << __func__ << ": " << ex.what();
        }
    }
}

template <class Object>
void
fillJsonState(Object& json, LedgerFill const& fill)
{
    auto& ledger = fill.ledger;
    auto&& array = Json::setArray(json, jss::accountState);
    auto expanded = isExpanded(fill);
    auto binary = isBinary(fill);

    for (auto const& sle : ledger.sles)
    {
        if (binary)
        {
            auto&& obj = appendObject(array);
            obj[jss::hash] = to_string(sle->key());
            obj[jss::tx_blob] = serializeHex(*sle);
        }
        else if (expanded)
            array.append(sle->getJson(JsonOptions::none));
        else
            array.append(to_string(sle->key()));
    }
}

template <class Object>
void
fillJsonQueue(Object& json, LedgerFill const& fill)
{
    auto&& queueData = Json::setArray(json, jss::queue_data);
    auto bBinary = isBinary(fill);
    auto bExpanded = isExpanded(fill);

    for (auto const& tx : fill.txQueue)
    {
        auto&& txJson = appendObject(queueData);
        txJson[jss::fee_level] = to_string(tx.feeLevel);
        if (tx.lastValid)
            txJson[jss::LastLedgerSequence] = *tx.lastValid;

        txJson[jss::fee] = to_string(tx.consequences.fee());
        auto const spend =
            tx.consequences.potentialSpend() + tx.consequences.fee();
        txJson[jss::max_spend_drops] = to_string(spend);
        txJson[jss::auth_change] = tx.consequences.isBlocker();

        txJson[jss::account] = to_string(tx.account);
        txJson["retries_remaining"] = tx.retriesRemaining;
        txJson["preflight_result"] = transToken(tx.preflightResult);
        if (tx.lastResult)
            txJson["last_result"] = transToken(*tx.lastResult);

        auto&& temp = fillJsonTx(fill, bBinary, bExpanded, tx.txn, nullptr);
        if (fill.context->apiVersion > 1)
            copyFrom(txJson, temp);
        else
            copyFrom(txJson[jss::tx], temp);
    }
}

template <class Object>
void
fillJson(Object& json, LedgerFill const& fill)
{
    // TODO: what happens if bBinary and bExtracted are both set?
    // Is there a way to report this back?
    auto bFull = isFull(fill);
    if (isBinary(fill))
        fillJsonBinary(json, !fill.ledger.open(), fill.ledger.info());
    else
        fillJson(
            json,
            !fill.ledger.open(),
            fill.ledger.info(),
            bFull,
            (fill.context ? fill.context->apiVersion
                          : RPC::apiMaximumSupportedVersion));

    if (bFull || fill.options & LedgerFill::dumpTxrp)
        fillJsonTx(json, fill);

    if (bFull || fill.options & LedgerFill::dumpState)
        fillJsonState(json, fill);
}

}  // namespace

void
addJson(Json::Value& json, LedgerFill const& fill)
{
    auto&& object = Json::addObject(json, jss::ledger);
    fillJson(object, fill);

    if ((fill.options & LedgerFill::dumpQueue) && !fill.txQueue.empty())
        fillJsonQueue(json, fill);
}

Json::Value
getJson(LedgerFill const& fill)
{
    Json::Value json;
    fillJson(json, fill);
    return json;
}

}  // namespace ripple
