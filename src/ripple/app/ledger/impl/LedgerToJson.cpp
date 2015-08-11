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

#include <ripple/app/ledger/LedgerToJson.h>
#include <ripple/basics/base_uint.h>

namespace ripple {

namespace {

bool isFull(LedgerFill const& fill)
{
    return fill.options & LedgerFill::full;
}

bool isExpanded(LedgerFill const& fill)
{
    return isFull(fill) || (fill.options & LedgerFill::expand);
}

bool isBinary(LedgerFill const& fill)
{
    return fill.options & LedgerFill::binary;
}

template <class Object>
void fillJson(Object& json, LedgerInfo const& info, bool bFull)
{
    json[jss::parent_hash]  = to_string (info.parentHash);
    json[jss::ledger_index] = to_string (info.seq);
    json[jss::seqNum]       = to_string (info.seq);      // DEPRECATED

    if (! info.open)
    {
        json[jss::closed] = true;
    }
    else if (!bFull)
    {
        json[jss::closed] = false;
        return;
    }

    json[jss::ledger_hash] = to_string (info.hash);
    json[jss::transaction_hash] = to_string (info.txHash);
    json[jss::account_hash] = to_string (info.accountHash);
    json[jss::total_coins] = to_string (info.drops);

    // These next three are DEPRECATED.
    json[jss::hash] = to_string (info.hash);
    json[jss::totalCoins] = to_string (info.drops);
    json[jss::accepted] = ! info.open;
    json[jss::close_flags] = info.closeFlags;

    // Always show fields that contribute to the ledger hash
    json[jss::parent_close_time] = info.parentCloseTime;
    json[jss::close_time] = info.closeTime;
    json[jss::close_time_resolution] = info.closeTimeResolution;

    if (auto closeTime = info.closeTime)
    {
        json[jss::close_time_human] = boost::posix_time::to_simple_string (
            ptFromSeconds (closeTime));
        if (! getCloseAgree(info))
            json[jss::close_time_estimated] = true;
    }
}

template <class Object>
void fillJsonTx (Object& json, LedgerFill const& fill)
{
    auto&& txns = setArray (json, jss::transactions);
    auto bBinary = isBinary(fill);
    auto bExpanded = isExpanded(fill);

    RPC::CountedYield count (
        fill.yieldStrategy.transactionYieldCount, fill.yield);

    try
    {
        for (auto& i: fill.ledger.txs)
        {
            count.yield();

            if (! bExpanded)
            {
                txns.append(to_string(i.first->getTransactionID()));
            }
            else if (bBinary)
            {
                auto&& txJson = appendObject (txns);
                txJson[jss::tx_blob] = serializeHex(*i.first);
                if (i.second)
                    txJson[jss::meta] = serializeHex(*i.second);
            }
            else
            {
                auto&& txJson = appendObject (txns);
                copyFrom(txJson, i.first->getJson(0));
                if (i.second)
                    txJson[jss::metaData] = i.second->getJson(0);
            }
        }
    }
    catch (...)
    {
        // Nothing the user can do about this.
    }
}

template <class Object>
void fillJsonState(Object& json, LedgerFill const& fill)
{
    auto& ledger = fill.ledger;
    auto&& array = Json::setArray (json, jss::accountState);
    RPC::CountedYield count (
        fill.yieldStrategy.accountYieldCount, fill.yield);

    auto expanded = isExpanded(fill);
    auto binary = isBinary(fill);

    for(auto const& sle : ledger.sles)
    {
        count.yield();
        if (binary)
        {
            auto&& obj = appendObject(array);
            obj[jss::hash] = to_string(sle->key());
            obj[jss::tx_blob] = serializeHex(*sle);
        }
        else if (expanded)
            array.append(sle->getJson(0));
        else
            array.append(to_string(sle->key()));
    }
}

template <class Object>
void fillJson (Object& json, LedgerFill const& fill)
{
    // TODO: what happens if bBinary and bExtracted are both set?
    // Is there a way to report this back?
    auto bFull = isFull(fill);
    fillJson(json, fill.ledger.info(), bFull);

    if (bFull || fill.options & LedgerFill::dumpTxrp)
        fillJsonTx(json, fill);

    if (bFull || fill.options & LedgerFill::dumpState)
        fillJsonState(json, fill);
}

} // namespace

void addJson (Json::Object& json, LedgerFill const& fill)
{
    auto&& object = Json::addObject (json, jss::ledger);
    fillJson (object, fill);
}

void addJson (Json::Value& json, LedgerFill const& fill)
{
    auto&& object = Json::addObject (json, jss::ledger);
    fillJson (object, fill);

}

Json::Value getJson (LedgerFill const& fill)
{
    Json::Value json;
    fillJson (json, fill);
    return json;
}

} // ripple
