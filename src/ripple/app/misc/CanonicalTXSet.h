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

#ifndef RIPPLE_APP_MISC_CANONICALTXSET_H_INCLUDED
#define RIPPLE_APP_MISC_CANONICALTXSET_H_INCLUDED

#include <ripple/protocol/RippleLedgerHash.h>
#include <ripple/protocol/STTx.h>

namespace ripple {

/** Holds transactions which were deferred to the next pass of consensus.

    "Canonical" refers to the order in which transactions are applied.

    - Puts transactions from the same account in sequence order

*/
// VFALCO TODO rename to SortedTxSet
class CanonicalTXSet
{
private:
    class Key
    {
    public:
        Key (uint256 const& account, std::uint32_t seq, uint256 const& id)
            : mAccount (account)
            , mTXid (id)
            , mSeq (seq)
        {
        }

        bool operator<  (Key const& rhs) const;
        bool operator>  (Key const& rhs) const;
        bool operator<= (Key const& rhs) const;
        bool operator>= (Key const& rhs) const;

        bool operator== (Key const& rhs) const
        {
            return mTXid == rhs.mTXid;
        }
        bool operator!= (Key const& rhs) const
        {
            return mTXid != rhs.mTXid;
        }

        uint256 const& getTXID () const
        {
            return mTXid;
        }

    private:
        uint256 mAccount;
        uint256 mTXid;
        std::uint32_t mSeq;
    };

public:
    using iterator = std::map <Key, std::shared_ptr<STTx const>>::iterator;
    using const_iterator = std::map <Key, std::shared_ptr<STTx const>>::const_iterator;

public:
    explicit CanonicalTXSet (LedgerHash const& saltHash)
        : mSetHash (saltHash)
    {
    }

    void insert (std::shared_ptr<STTx const> const& txn);

    // VFALCO TODO remove this function
    void reset (LedgerHash const& saltHash)
    {
        mSetHash = saltHash;

        mMap.clear ();
    }

    iterator erase (iterator const& it);

    iterator begin ()
    {
        return mMap.begin ();
    }
    iterator end ()
    {
        return mMap.end ();
    }
    const_iterator begin ()  const
    {
        return mMap.begin ();
    }
    const_iterator end () const
    {
        return mMap.end ();
    }
    size_t size () const
    {
        return mMap.size ();
    }
    bool empty () const
    {
        return mMap.empty ();
    }

private:
    // Used to salt the accounts so people can't mine for low account numbers
    uint256 mSetHash;

    std::map <Key, std::shared_ptr<STTx const>> mMap;
};

} // ripple

#endif
