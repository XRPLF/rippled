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

bool CanonicalTXSet::Key::operator< (Key const& rhs) const
{
    if (mAccount < rhs.mAccount) return true;

    if (mAccount > rhs.mAccount) return false;

    if (mSeq < rhs.mSeq) return true;

    if (mSeq > rhs.mSeq) return false;

    return mTXid < rhs.mTXid;
}

bool CanonicalTXSet::Key::operator> (Key const& rhs) const
{
    if (mAccount > rhs.mAccount) return true;

    if (mAccount < rhs.mAccount) return false;

    if (mSeq > rhs.mSeq) return true;

    if (mSeq < rhs.mSeq) return false;

    return mTXid > rhs.mTXid;
}

bool CanonicalTXSet::Key::operator<= (Key const& rhs) const
{
    if (mAccount < rhs.mAccount) return true;

    if (mAccount > rhs.mAccount) return false;

    if (mSeq < rhs.mSeq) return true;

    if (mSeq > rhs.mSeq) return false;

    return mTXid <= rhs.mTXid;
}

bool CanonicalTXSet::Key::operator>= (Key const& rhs)const
{
    if (mAccount > rhs.mAccount) return true;

    if (mAccount < rhs.mAccount) return false;

    if (mSeq > rhs.mSeq) return true;

    if (mSeq < rhs.mSeq) return false;

    return mTXid >= rhs.mTXid;
}

void CanonicalTXSet::push_back (SerializedTransaction::ref txn)
{
    uint256 effectiveAccount = mSetHash;

    effectiveAccount ^= to256 (txn->getSourceAccount ().getAccountID ());

    mMap.insert (std::make_pair (
                     Key (effectiveAccount, txn->getSequence (), txn->getTransactionID ()),
                     txn));
}

CanonicalTXSet::iterator CanonicalTXSet::erase (iterator const& it)
{
    iterator tmp = it;
    ++tmp;
    mMap.erase (it);
    return tmp;
}

} // ripple
