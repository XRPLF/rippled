#ifndef RIPPLE_ACCOUNTSTATE_H
#define RIPPLE_ACCOUNTSTATE_H

//
// Provide abstract access to an account's state, such that access to the serialized format is hidden.
//

class AccountState
{
public:
    typedef boost::shared_ptr<AccountState> pointer;

public:
    // For new accounts
    explicit AccountState (RippleAddress const& naAccountID);

    // For accounts in a ledger
    AccountState (SLE::ref ledgerEntry, RippleAddress const& naAccountI);

    bool haveAuthorizedKey ()
    {
        return mLedgerEntry->isFieldPresent (sfRegularKey);
    }

    RippleAddress getAuthorizedKey ()
    {
        return mLedgerEntry->getFieldAccount (sfRegularKey);
    }

    STAmount getBalance () const
    {
        return mLedgerEntry->getFieldAmount (sfBalance);
    }

    uint32 getSeq () const
    {
        return mLedgerEntry->getFieldU32 (sfSequence);
    }

    SerializedLedgerEntry::pointer getSLE ()
    {
        return mLedgerEntry;
    }

    SerializedLedgerEntry const& peekSLE () const
    {
        return *mLedgerEntry;
    }

    SerializedLedgerEntry& peekSLE ()
    {
        return *mLedgerEntry;
    }

    Blob getRaw () const;

    void addJson (Json::Value& value);

    void dump ();

    static std::string createGravatarUrl (uint128 uEmailHash);

private:
    RippleAddress const mAccountID;
    RippleAddress                  mAuthorizedKey;
    SerializedLedgerEntry::pointer mLedgerEntry;

    bool                           mValid;
};

#endif
