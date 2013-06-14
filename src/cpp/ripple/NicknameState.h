#ifndef RIPPLE_NICKNAMESTATE_H
#define RIPPLE_NICKNAMESTATE_H

//
// State of a nickname node.
// - Isolate ledger entry format.
//

class NicknameState
{
public:
    typedef boost::shared_ptr <NicknameState> pointer;

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

#endif
// vim:ts=4
