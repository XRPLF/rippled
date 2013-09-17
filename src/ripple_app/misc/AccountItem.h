//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_ACCOUNTITEM_H
#define RIPPLE_ACCOUNTITEM_H

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
    typedef boost::shared_ptr <AccountItem> pointer;
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

#endif
