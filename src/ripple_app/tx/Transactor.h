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
