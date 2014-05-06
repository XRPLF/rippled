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

/** Get the current ledger entry */
SLE::pointer DirectoryEntryIterator::getEntry (LedgerEntrySet& les, LedgerEntryType type)
{
    return les.entryCache (type, mEntryIndex);
}

/** Position the iterator at the first entry
*/
bool DirectoryEntryIterator::firstEntry (LedgerEntrySet& les)
{
    WriteLog (lsTRACE, Ledger) << "DirectoryEntryIterator::firstEntry(" <<
        to_string (mRootIndex) << ")";
    mEntry = 0;
    mDirNode.reset ();

    return nextEntry (les);
}

/** Advance the iterator to the next entry
*/
bool DirectoryEntryIterator::nextEntry (LedgerEntrySet& les)
{
    if (!mDirNode)
    {
        WriteLog (lsTRACE, Ledger) << "DirectoryEntryIterator::nextEntry(" <<
            to_string (mRootIndex) << ") need dir node";
        // Are we already at the end
        if (mDirIndex.isZero())
        {
            WriteLog (lsTRACE, Ledger) << "DirectoryEntryIterator::nextEntry(" <<
                to_string (mRootIndex) << ") at end";
            return false;
        }

        // Fetch the current directory
        mDirNode = les.entryCache (ltDIR_NODE, mRootIndex);
        if (!mDirNode)
        {
            WriteLog (lsTRACE, Ledger) << "DirectoryEntryIterator::nextEntry("
                << to_string (mRootIndex) << ") no dir node";
            mEntryIndex.zero();
            return false;
        }
    }

    if (!les.dirNext (mRootIndex, mDirNode, mEntry, mEntryIndex))
    {
        mDirIndex.zero();
        mDirNode.reset();
        WriteLog (lsTRACE, Ledger) << "DirectoryEntryIterator::nextEntry(" <<
            to_string (mRootIndex) << ") now at end";
        return false;
    }

    WriteLog (lsTRACE, Ledger) << "DirectoryEntryIterator::nextEntry(" <<
        to_string (mRootIndex) << ") now at " << mEntry;
    return true;
}

bool DirectoryEntryIterator::addJson (Json::Value& j) const
{
    if (mDirNode && (mEntry != 0))
    {
        j["dir_root"] = to_string (mRootIndex);
        j["dir_entry"] = static_cast<Json::UInt> (mEntry);

        if (mDirNode)
            j["dir_index"] = to_string (mDirIndex);

        return true;
    }
    return false;
}

bool DirectoryEntryIterator::setJson (Json::Value const& j, LedgerEntrySet& les)
{
    if (!j.isMember("dir_root") || !j.isMember("dir_index") || !j.isMember("dir_entry"))
        return false;
#if 0 // WRITEME
    Json::Value const& dirRoot = j["dir_root"];
    Json::Value const& dirIndex = j["dir_index"];
    Json::Value const& dirEntry = j["dir_entry"];

    assert(false); // CAUTION: This function is incomplete

    mEntry = j["dir_entry"].asUInt ();

    if (!mDirIndex.SetHex(j["dir_index"].asString()))
        return false;
#endif

    return true;
}

} // ripple
