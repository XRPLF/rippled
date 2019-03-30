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

#ifndef RIPPLE_APP_LEDGER_LEDGERTOJSON_H_INCLUDED
#define RIPPLE_APP_LEDGER_LEDGERTOJSON_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/protocol/jss.h>
#include <ripple/protocol/STTx.h>
#include <ripple/json/Object.h>

namespace ripple {

struct LedgerFill
{
    LedgerFill (ReadView const& l, int o = 0, std::vector<TxQ::TxDetails> q = {},
                LedgerEntryType t = ltINVALID)
        : ledger (l)
        , options (o)
        , txQueue(std::move(q))
        , type (t)
    {
    }

    enum Options {
        dumpTxrp = 1,
        dumpState = 2,
        expand = 4,
        full = 8,
        binary = 16,
        ownerFunds = 32,
        dumpQueue = 64
    };

    ReadView const& ledger;
    int options;
    std::vector<TxQ::TxDetails> txQueue;
    LedgerEntryType type;
};

/** Given a Ledger and options, fill a Json::Object or Json::Value with a
    description of the ledger.
 */

void addJson(Json::Value&, LedgerFill const&);

/** Return a new Json::Value representing the ledger with given options.*/
Json::Value getJson (LedgerFill const&);

/** Serialize an object to a blob. */
template <class Object>
Blob serializeBlob(Object const& o)
{
    Serializer s;
    o.add(s);
    return s.peekData();
}

/** Serialize an object to a hex string. */
inline
std::string serializeHex(STObject const& o)
{
    return strHex(serializeBlob(o));
}
} // ripple

#endif
