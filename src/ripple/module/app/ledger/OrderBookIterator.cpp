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

/** Iterate through the directories in an order book */
BookDirIterator::BookDirIterator(uint160 const& uInCurrency, uint160 const& uInIssuer,
    uint160 const& uOutCurrency, uint160 const& uOutIssuer)
{
    mBase = Ledger::getBookBase(uInCurrency, uInIssuer, uOutCurrency, uOutIssuer);
    mEnd = Ledger::getQualityNext(mBase);
    mIndex = mBase;
}

bool BookDirIterator::nextDirectory (LedgerEntrySet& les)
{
    WriteLog (lsTRACE, Ledger) << "BookDirectoryIterator:: nextDirectory";

    // Are we already at the end?
    if (mIndex.isZero ())
        return false;

    // Get the ledger index of the next directory
    mIndex = les.getNextLedgerIndex (mIndex, mEnd);

    if (mIndex.isZero ())
    {
        // We ran off the end of the book
        WriteLog (lsTRACE, Ledger) <<
            "BookDirectoryIterator:: no next ledger index";
        return false;
    }
    assert (mIndex < mEnd);

    WriteLog (lsTRACE, Ledger) <<
        "BookDirectoryIterator:: index " << to_string (mIndex);

    // Retrieve the SLE from the LES
    mOfferDir = les.entryCache (ltDIR_NODE, mIndex);
    assert (mOfferDir);

    return !!mOfferDir;
}

bool BookDirIterator::firstDirectory (LedgerEntrySet& les)
{
    WriteLog (lsTRACE, Ledger) <<
        "BookDirIterator(" << to_string (mBase) << ") firstDirectory";

    /** Jump to the beginning
    */
    mIndex = mBase;

    return nextDirectory (les);
}

/** The LES may have changed. Repoint to the current directory if it still exists,
    Otherwise, go to the next one.
*/
bool BookDirIterator::resync (LedgerEntrySet& les)
{
    if (mIndex.isZero ())
        mIndex = mBase;
    else if (mIndex != mBase)
        --mIndex;

    return nextDirectory (les);
}

DirectoryEntryIterator BookDirIterator::getOfferIterator () const
{
    WriteLog (lsTRACE, Ledger) << "BookDirIterator(" << 
        to_string (mBase) << ") get offer iterator";
    return DirectoryEntryIterator (mOfferDir);
}

std::uint64_t BookDirIterator::getRate () const
{
    return Ledger::getQuality(mIndex);
}

bool BookDirIterator::addJson (Json::Value& jv) const
{
    if (! (*this))
        return false;

    jv["book_index"] = to_string (mIndex);
    return true;
}

bool BookDirIterator::setJson(Json::Value const& jv)
{
    if (!jv.isMember("book_index"))
        return false;
    const Json::Value& bi = jv["book_index"];
    if (!bi.isString ())
        return false;
    mIndex.SetHexExact(bi.asString());
    return true;
}

bool OrderBookIterator::addJson (Json::Value& jv) const
{
    return mOfferIterator.addJson(jv) && mDirectoryIterator.addJson(jv);
}

bool OrderBookIterator::setJson (Json::Value const& jv)
{
    return mDirectoryIterator.setJson (jv) && mOfferIterator.setJson (jv, mEntrySet);
}

STAmount OrderBookIterator::getCurrentRate () const
{
    return mDirectoryIterator.getCurrentRate();
}

std::uint64_t OrderBookIterator::getCurrentQuality () const
{
    return mDirectoryIterator.getCurrentQuality();
}

uint256 OrderBookIterator::getCurrentDirectory () const
{
    return mOfferIterator.getDirectory ();
}

uint256 OrderBookIterator::getCurrentIndex () const
{
    return mOfferIterator.getEntryLedgerIndex();
}

/** Retrieve the offer the iterator points to
*/
SLE::pointer OrderBookIterator::getCurrentOffer ()
{
    return mOfferIterator.getEntry (mEntrySet, ltOFFER);
}

/** Go to the first offer in the first directory
*/
bool OrderBookIterator::firstOffer ()
{
    WriteLog (lsTRACE, Ledger) << "OrderBookIterator: first offer";
    // Go to first directory in order book
    if (!mDirectoryIterator.firstDirectory (mEntrySet))
    {
        WriteLog (lsTRACE, Ledger) << "OrderBookIterator: no first directory";
        return false;
    }
    mOfferIterator = mDirectoryIterator.getOfferIterator ();

    // Take the next offer
    return nextOffer();
}

/** Go to the next offer, possibly changing directories
*/
bool OrderBookIterator::nextOffer ()
{
    WriteLog (lsTRACE, Ledger) << "OrderBookIterator: next offer";
    do
    {

         // Is there a next offer in the current directory
         if (mOfferIterator.nextEntry (mEntrySet))
         {
             WriteLog (lsTRACE, Ledger) << "OrderBookIterator: there is a next offer in this directory";
             return true;
         }

         // Is there a next directory
         if (!mDirectoryIterator.nextDirectory (mEntrySet))
         {
             WriteLog (lsTRACE, Ledger) << "OrderBookIterator: there is no next directory";
             return false;
         }
         WriteLog (lsTRACE, Ledger) << "OrderBookIterator: going to next directory";

         // Set to before its first offer
         mOfferIterator = mDirectoryIterator.getOfferIterator ();
     }
     while (1);
}

/** Rewind to the beginning of this directory, then take the next offer
*/
bool OrderBookIterator::rewind ()
{
    if (!mDirectoryIterator.resync (mEntrySet))
        return false;

    mOfferIterator = mDirectoryIterator.getOfferIterator ();
    return nextOffer ();
}

/** Go to before the first offer in the next directory
*/
bool OrderBookIterator::nextDir ()
{
    if (!mDirectoryIterator.nextDirectory (mEntrySet))
        return false;

    mOfferIterator = mDirectoryIterator.getOfferIterator ();

    return true;
}

/** Advance to the next offer in this directory
*/
bool OrderBookIterator::nextOfferInDir ()
{
    return mOfferIterator.nextEntry (mEntrySet);
}

} // ripple
