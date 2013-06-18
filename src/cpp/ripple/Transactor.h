//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef __TRANSACTOR__
#define __TRANSACTOR__

class Transactor
{
public:
    typedef boost::shared_ptr<Transactor> pointer;

    static UPTR_T<Transactor> makeTransactor (const SerializedTransaction& txn, TransactionEngineParams params, TransactionEngine* engine);

    TER apply ();

protected:
    const SerializedTransaction&    mTxn;
    TransactionEngine*              mEngine;
    TransactionEngineParams         mParams;

    uint160                         mTxnAccountID;
    STAmount                        mFeeDue;
    STAmount                        mPriorBalance;  // Balance before fees.
    STAmount                        mSourceBalance; // Balance after fees.
    SLE::pointer                    mTxnAccount;
    bool                            mHasAuthKey;
    bool                            mSigMaster;
    RippleAddress                   mSigningPubKey;

    virtual TER preCheck ();
    virtual TER checkSeq ();
    virtual TER payFee ();

    void calculateFee ();

    // Returns the fee, not scaled for load (Should be in fee units. FIXME)
    virtual uint64 calculateBaseFee ();

    virtual TER checkSig ();
    virtual TER doApply () = 0;

    Transactor (const SerializedTransaction& txn, TransactionEngineParams params, TransactionEngine* engine);

    virtual bool mustHaveValidAccount ()
    {
        return true;
    }
};

#endif

// vim:ts=4
