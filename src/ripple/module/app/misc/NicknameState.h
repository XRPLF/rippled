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

#ifndef RIPPLE_NICKNAMESTATE_H
#define RIPPLE_NICKNAMESTATE_H

namespace ripple {

//
// State of a nickname node.
// - Isolate ledger entry format.
//

class NicknameState
{
public:
    typedef std::shared_ptr <NicknameState> pointer;

public:
    explicit NicknameState (SerializedLedgerEntry::pointer ledgerEntry);    // For accounts in a ledger

    bool                    haveMinimumOffer () const;
    STAmount                getMinimumOffer () const;
    RippleAddress           getAccountID () const;

    SerializedLedgerEntry::pointer getSLE ()
    {
        return mLedgerEntry;
    }
    const SerializedLedgerEntry& peekSLE () const
    {
        return *mLedgerEntry;
    }
    SerializedLedgerEntry& peekSLE ()
    {
        return *mLedgerEntry;
    }

    Blob getRaw () const;
    void addJson (Json::Value& value);

private:
    SerializedLedgerEntry::pointer  mLedgerEntry;
};

} // ripple

#endif
