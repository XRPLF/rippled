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

#ifndef RIPPLE_DIRECTORYENTRYITERATOR_H_INCLUDED
#define RIPPLE_DIRECTORYENTRYITERATOR_H_INCLUDED

namespace ripple {

/** An iterator that walks the ledger entries in a single directory */
class DirectoryEntryIterator
{

public:
    DirectoryEntryIterator ()
        : mEntry(0)
    {
    }

    DirectoryEntryIterator (uint256 const& index)
        : mRootIndex(index), mEntry(0)
    {
    }

    /** Construct from a reference to the root directory
    */
    DirectoryEntryIterator (SLE::ref directory) : mEntry (0), mDirNode (directory)
    {
        if (mDirNode)
            mRootIndex = mDirNode->getIndex();
    }

    /** Get the SLE this iterator currently references */
    SLE::pointer getEntry (LedgerEntrySet& les, LedgerEntryType type);

    /** Make this iterator point to the first offer */
    bool firstEntry (LedgerEntrySet&);

    /** Make this iterator point to the next offer */
    bool nextEntry (LedgerEntrySet&);

    /** Add this iterator's position to a JSON object */
    bool addJson (Json::Value&) const;

    /** Set this iterator's position from a JSON object */
    bool setJson (Json::Value const&, LedgerEntrySet& les);

    uint256 const& getEntryLedgerIndex () const
    {
        return mEntryIndex;
    }

    uint256 getDirectory () const
    {
        return mDirNode ? mDirNode->getIndex () : uint256();
    }

    bool
    operator== (DirectoryEntryIterator const& other) const
    {
        return mEntry == other.mEntry && mDirIndex == other.mDirIndex;
    }

    bool
    operator!= (DirectoryEntryIterator const& other) const
    {
        return ! (*this == other);
    }

private:
    uint256      mRootIndex;    // ledger index of the root directory
    uint256      mDirIndex;     // ledger index of the current directory
    unsigned int mEntry;        // entry index we are on (0 means first is next)
    uint256      mEntryIndex;   // ledger index of the current entry
    SLE::pointer mDirNode;      // SLE for the entry we are on
};

} // ripple

#endif
