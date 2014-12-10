//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLED_RIPPLE_APP_LEDGER_LEDGERTOJSON_H
#define RIPPLED_RIPPLE_APP_LEDGER_LEDGERTOJSON_H

#include <ripple/app/ledger/Ledger.h>
#include <ripple/basics/Time.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/STTx.h>
#include <ripple/rpc/Yield.h>
#include <ripple/rpc/impl/JsonObject.h>
#include <boost/date_time/posix_time/posix_time.hpp>

namespace ripple {

struct LedgerFill
{
    LedgerFill (Ledger const& l,
                int o = 0,
                RPC::Yield const& y = {},
                RPC::YieldStrategy const& ys = {})
            : ledger (l),
              options (o),
              yield (y),
              yieldStrategy (ys)
    {
    }

    Ledger const& ledger;
    int options;
    RPC::Yield yield;
    RPC::YieldStrategy yieldStrategy;
};

/** Given a Ledger, options, and a generic Object that has Json semantics,
    fill the Object with a description of the ledger.
*/
template <class Object>
void fillJson (Object&, LedgerFill const&);

/** Add Json to an existing generic Object. */
template <class Object>
void addJson (Object&, LedgerFill const&);

/** Return a new Json::Value representing the ledger with given options.*/
Json::Value getJson (LedgerFill const&);

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// Implementations.

template <typename Object>
void fillJson (Object& json, LedgerFill const& fill)
{
    auto const& ledger = fill.ledger;

    bool const bFull (fill.options & LEDGER_JSON_FULL);
    bool const bExpand (fill.options & LEDGER_JSON_EXPAND);

    // DEPRECATED
    json[jss::seqNum]       = to_string (ledger.getLedgerSeq());
    json[jss::parent_hash]  = to_string (ledger.getParentHash());
    json[jss::ledger_index] = to_string (ledger.getLedgerSeq());

    if (ledger.isClosed() || bFull)
    {
        if (ledger.isClosed())
            json[jss::closed] = true;

        // DEPRECATED
        json[jss::hash] = to_string (ledger.getRawHash());

        // DEPRECATED
        json[jss::totalCoins]        = to_string (ledger.getTotalCoins());
        json[jss::ledger_hash]       = to_string (ledger.getRawHash());
        json[jss::transaction_hash]  = to_string (ledger.getTransHash());
        json[jss::account_hash]      = to_string (ledger.getAccountHash());
        json[jss::accepted]          = ledger.isAccepted();
        json[jss::total_coins]       = to_string (ledger.getTotalCoins());

        auto closeTime = ledger.getCloseTimeNC();
        if (closeTime != 0)
        {
            json[jss::close_time]            = closeTime;
            json[jss::close_time_human]
                    = boost::posix_time::to_simple_string (
                        ptFromSeconds (closeTime));
            json[jss::close_time_resolution] = ledger.getCloseResolution();

            if (!ledger.getCloseAgree())
                json[jss::close_time_estimated] = true;
        }
    }
    else
    {
        json[jss::closed] = false;
    }

    auto &transactionMap = ledger.peekTransactionMap();
    if (transactionMap && (bFull || fill.options & LEDGER_JSON_DUMP_TXRP))
    {
        auto&& txns = RPC::addArray (json, jss::transactions);
        SHAMapTreeNode::TNType type;

        RPC::CountedYield count (
            fill.yieldStrategy.transactionYieldCount, fill.yield);
        for (auto item = transactionMap->peekFirstItem (type); item;
             item = transactionMap->peekNextItem (item->getTag (), type))
        {
            count.yield();
            if (bFull || bExpand)
            {
                if (type == SHAMapTreeNode::tnTRANSACTION_NM)
                {
                    SerializerIterator sit (item->peekSerializer ());
                    STTx txn (sit);
                    txns.append (txn.getJson (0));
                }
                else if (type == SHAMapTreeNode::tnTRANSACTION_MD)
                {
                    SerializerIterator sit (item->peekSerializer ());
                    Serializer sTxn (sit.getVL ());

                    SerializerIterator tsit (sTxn);
                    STTx txn (tsit);

                    TransactionMetaSet meta (
                        item->getTag (), ledger.getLedgerSeq(), sit.getVL ());
                    Json::Value txJson = txn.getJson (0);
                    txJson[jss::metaData] = meta.getJson (0);
                    txns.append (txJson);
                }
                else
                {
                    Json::Value error = Json::objectValue;
                    error[to_string (item->getTag ())] = type;
                    txns.append (error);
                }
            }
            else txns.append (to_string (item->getTag ()));
        }
    }

    auto& accountStateMap = ledger.peekAccountStateMap();
    if (accountStateMap && (bFull || fill.options & LEDGER_JSON_DUMP_STATE))
    {
        auto&& array = RPC::addArray (json, jss::accountState);
        RPC::CountedYield count (
            fill.yieldStrategy.accountYieldCount, fill.yield);
        if (bFull || bExpand)
        {
            ledger.visitStateItems (
                [&array, &count, &fill] (SLE::ref sle)
                {
                    count.yield();
                    array.append (sle->getJson(0));
                });
        }
        else
        {
            accountStateMap->visitLeaves(
                [&array, &count, &fill] (SHAMapItem::ref smi)
                {
                    count.yield();
                    array.append (to_string(smi->getTag ()));
                });
        }
    }
}

/** Add Json to an existing generic Object. */
template <class Object>
void addJson (Object& json, LedgerFill const& fill)
{
    auto&& object = RPC::addObject (json, jss::ledger);
    fillJson (object, fill);
}

inline
Json::Value getJson (LedgerFill const& fill)
{
    Json::Value json;
    fillJson (json, fill);
    return json;
}

} // ripple

#endif
