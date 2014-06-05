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

#ifndef RIPPLE_ACCOUNTITEM_H
#define RIPPLE_ACCOUNTITEM_H

namespace ripple {

//
// Fetch ledger entries from an account's owner dir.
//
/** Base class representing account items.

    Account items include:

    - Offers
    - Trust Lines

    NOTE these are deprecated and will go away, to be replaced with
    simple visitor patterns.
*/
class AccountItem
{
public:
    typedef std::shared_ptr <AccountItem> pointer;
    typedef const pointer& ref;

public:
    AccountItem ()
    { }

    /** Construct from a flat ledger entry.
    */
    explicit AccountItem (SerializedLedgerEntry::ref ledger);

    virtual ~AccountItem ()
    {
        ;
    }

    virtual AccountItem::pointer makeItem (const uint160& accountID, SerializedLedgerEntry::ref ledgerEntry) = 0;

    // VFALCO TODO Make this const and change derived classes
    virtual LedgerEntryType getType () = 0;

    // VFALCO TODO Document the int parameter
    virtual Json::Value getJson (int) = 0;

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

    // VFALCO TODO Make this private and use the existing accessors
    //
protected:
    // VFALCO TODO Research making the object pointed to const
    SerializedLedgerEntry::pointer mLedgerEntry;
};

} // ripple

#endif
