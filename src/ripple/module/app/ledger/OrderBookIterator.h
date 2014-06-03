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

#ifndef RIPPLE_ORDERBOOKITERATOR_H_INCLUDED
#define RIPPLE_ORDERBOOKITERATOR_H_INCLUDED

namespace ripple {

/** An iterator that walks the directories in a book */
class BookDirIterator
{

public:

    BookDirIterator ()
    {
    }

    BookDirIterator (
        uint160 const& uInCurrency, uint160 const& uInIssuer,
        uint160 const& uOutCurrency, uint160 const& uOutIssuer);

    uint256 const& getBookBase () const
    {
        return mBase;
    }

    uint256 const& getBookEnd () const
    {
        return mEnd;
    }

    uint256 const& getCurrentIndex() const
    {
        return mIndex;
    }

    void setCurrentIndex(uint256 const& index)
    {
        mIndex = index;
    }

    /** Get the current exchange rate
    */
    STAmount getCurrentRate () const
    {
        return STAmount::setRate (getCurrentQuality());
    }

    /** Get the current quality
    */
    std::uint64_t getCurrentQuality () const
    {
        return Ledger::getQuality(mIndex);
    }

    /** Make this iterator refer to the next book
    */
    bool nextDirectory (LedgerEntrySet&);

    /** Make this iterator refer to the first book
    */
    bool firstDirectory (LedgerEntrySet&);

    /** The LES may have changed
        Resync the iterator
    */
    bool resync (LedgerEntrySet&);

    /** Get an iterator to the offers in this directory
    */
    DirectoryEntryIterator getOfferIterator () const;

    std::uint64_t getRate () const;

    bool addJson (Json::Value&) const;

    bool setJson (Json::Value const&);

    // Does this iterator currently point to a valid directory
    explicit
    operator bool () const
    {
        return mOfferDir &&  (mOfferDir->getIndex() == mIndex);
    }    

    bool
    operator== (BookDirIterator const& other) const
    {
        assert (! mIndex.isZero() && ! other.mIndex.isZero());
        return mIndex == other.mIndex;
    }

    bool
    operator!= (BookDirIterator const& other) const
    {
        return ! (*this == other);
    }

private:
    uint256      mBase;     // The first index a directory in the book can have
    uint256      mEnd;      // The first index a directory in the book cannot have
    uint256      mIndex;    // The index we are currently on
    SLE::pointer mOfferDir; // The directory page we are currently on
};

//------------------------------------------------------------------------------

/** An iterator that walks the offers in a book
    CAUTION: The LedgerEntrySet must remain valid for the life of the iterator
*/
class OrderBookIterator
{
public:
    OrderBookIterator (
        LedgerEntrySet& set,
        uint160 const& uInCurrency,
        uint160 const& uInIssuer,
        uint160 const& uOutCurrency,
        uint160 const& uOutIssuer) :
            mEntrySet (set),
            mDirectoryIterator (uInCurrency, uInIssuer, uOutCurrency, uOutIssuer)
    {
    }

    OrderBookIterator&
    operator= (OrderBookIterator const&) = default;

    bool addJson (Json::Value&) const;

    bool setJson (Json::Value const&);

    STAmount getCurrentRate () const;

    std::uint64_t getCurrentQuality () const;

    uint256 getCurrentIndex () const;

    uint256 getCurrentDirectory () const;

    SLE::pointer getCurrentOffer ();

    /** Position the iterator at the first offer in the first directory.
        Returns whether there is an offer to point to.
    */
    bool firstOffer ();

    /** Position the iterator at the next offer, going to the next directory if needed
        Returns whether there is a next offer.
    */
    bool nextOffer ();

    /** Position the iterator at the first offer in the next directory.
        Returns whether there is a next directory to point to.
    */
    bool nextDir ();

    /** Position the iterator at the first offer in the current directory.
        Returns whether there is an offer in the directory.
    */
    bool firstOfferInDir ();

    /** Position the iterator at the next offer in the current directory.
        Returns whether there is a next offer in the directory.
    */
    bool nextOfferInDir ();

    /** Position the iterator at the first offer at the current quality.
        If none, position the iterator at the first offer at the next quality.
        This rather odd semantic is required by the payment engine.
    */
    bool rewind ();

    LedgerEntrySet& peekEntrySet ()
    {
        return mEntrySet;
    }

    BookDirIterator const& peekDirIterator () const
    {
        return mDirectoryIterator;
    }

    DirectoryEntryIterator const& peekDirectoryEntryIterator () const
    {
        return mOfferIterator;
    }

    BookDirIterator& peekDirIterator ()
    {
        return mDirectoryIterator;
    }

    DirectoryEntryIterator& peekDirectoryEntryIterator ()
    {
        return mOfferIterator;
    }

    bool
    operator== (OrderBookIterator const& other) const
    {
        return
            std::addressof(mEntrySet) == std::addressof(other.mEntrySet) &&
            mDirectoryIterator == other.mDirectoryIterator &&
            mOfferIterator == mOfferIterator;
    }

    bool
    operator!= (OrderBookIterator const& other) const
    {
        return ! (*this == other);
    }

private:
    std::reference_wrapper <LedgerEntrySet> mEntrySet;
    BookDirIterator         mDirectoryIterator;
    DirectoryEntryIterator  mOfferIterator;
};

} // ripple

#endif
