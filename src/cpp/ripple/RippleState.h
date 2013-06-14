#ifndef __RIPPLESTATE__
#define __RIPPLESTATE__

//
// A ripple line's state.
// - Isolate ledger entry format.
//

class RippleState : public AccountItem
{
public:
    typedef boost::shared_ptr<RippleState> pointer;

private:
    bool                            mValid;
    bool                            mViewLowest;

    uint32                          mFlags;

    STAmount                        mLowLimit;
    STAmount                        mHighLimit;

    uint160                         mLowID;
    uint160                         mHighID;

    uint64                          mLowQualityIn;
    uint64                          mLowQualityOut;
    uint64                          mHighQualityIn;
    uint64                          mHighQualityOut;

    STAmount                        mBalance;

    RippleState (SerializedLedgerEntry::ref ledgerEntry);   // For accounts in a ledger

public:
    RippleState () { }
    virtual ~RippleState () {}
    AccountItem::pointer makeItem (const uint160& accountID, SerializedLedgerEntry::ref ledgerEntry);
    LedgerEntryType getType ()
    {
        return (ltRIPPLE_STATE);
    }

    void                setViewAccount (const uint160& accountID);

    const uint160&      getAccountID () const
    {
        return  mViewLowest ? mLowID : mHighID;
    }
    const uint160&      getAccountIDPeer () const
    {
        return !mViewLowest ? mLowID : mHighID;
    }

    // True, Provided auth to peer.
    bool                getAuth () const
    {
        return isSetBit (mFlags,  mViewLowest ? lsfLowAuth : lsfHighAuth);
    }
    bool                getAuthPeer () const
    {
        return isSetBit (mFlags, !mViewLowest ? lsfLowAuth : lsfHighAuth);
    }

    const STAmount&     getBalance () const
    {
        return mBalance;
    }
    const STAmount&     getLimit () const
    {
        return  mViewLowest ? mLowLimit : mHighLimit;
    }
    const STAmount&     getLimitPeer () const
    {
        return !mViewLowest ? mLowLimit : mHighLimit;
    }

    uint32              getQualityIn () const
    {
        return ((uint32) (mViewLowest ? mLowQualityIn : mHighQualityIn));
    }
    uint32              getQualityOut () const
    {
        return ((uint32) (mViewLowest ? mLowQualityOut : mHighQualityOut));
    }

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
    Json::Value getJson (int);

    Blob getRaw () const;
};
#endif
// vim:ts=4
