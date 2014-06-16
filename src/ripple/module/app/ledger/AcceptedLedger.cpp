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

namespace ripple {

// VFALCO TODO Remove this global and make it a member of the App
//             Use a dependency injection to give AcceptedLedger access.
//
TaggedCache <uint256, AcceptedLedger> AcceptedLedger::s_cache (
    "AcceptedLedger", 4, 60, get_seconds_clock (),
        LogPartition::getJournal <TaggedCacheLog> ());

AcceptedLedger::AcceptedLedger (Ledger::ref ledger) : mLedger (ledger)
{
    SHAMap& txSet = *ledger->peekTransactionMap ();

    for (SHAMapItem::pointer item = txSet.peekFirstItem (); !!item; item = txSet.peekNextItem (item->getTag ()))
    {
        SerializerIterator sit (item->peekSerializer ());
        insert (std::make_shared<AcceptedLedgerTx> (ledger->getLedgerSeq (), std::ref (sit)));
    }
}

AcceptedLedger::pointer AcceptedLedger::makeAcceptedLedger (Ledger::ref ledger)
{
    AcceptedLedger::pointer ret = s_cache.fetch (ledger->getHash ());

    if (ret)
        return ret;

    ret = AcceptedLedger::pointer (new AcceptedLedger (ledger));
    s_cache.canonicalize (ledger->getHash (), ret);
    return ret;
}

void AcceptedLedger::insert (AcceptedLedgerTx::ref at)
{
    assert (mMap.find (at->getIndex ()) == mMap.end ());
    mMap.insert (std::make_pair (at->getIndex (), at));
}

AcceptedLedgerTx::pointer AcceptedLedger::getTxn (int i) const
{
    map_t::const_iterator it = mMap.find (i);

    if (it == mMap.end ())
        return AcceptedLedgerTx::pointer ();

    return it->second;
}

} // ripple
