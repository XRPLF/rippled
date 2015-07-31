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

#ifndef RIPPLE_APP_TX_TRANSACTOR_H_INCLUDED
#define RIPPLE_APP_TX_TRANSACTOR_H_INCLUDED

#include <ripple/app/tx/impl/ApplyContext.h>
#include <beast/utility/Journal.h>

namespace ripple {

/** State information when preflighting a tx. */
struct PreflightContext
{
public:
    STTx const& tx;
    Rules const& rules;
    ApplyFlags flags;
    SigVerify verify;
    Config const& config;
    beast::Journal j;

    PreflightContext(STTx const& tx_,
        Rules const& rules_, ApplyFlags flags_,
            SigVerify verify_, Config const& config_,
                beast::Journal j_)
        : tx(tx_)
        , rules(rules_)
        , flags(flags_)
        , verify(verify_)
        , config(config_)
        , j(j_)
    {
    }
};

class Transactor
{
protected:
    ApplyContext& ctx_;
    beast::Journal j_;

    AccountID     account_;
    STAmount      mFeeDue;
    STAmount      mPriorBalance;  // Balance before fees.
    STAmount      mSourceBalance; // Balance after fees.
    bool          mHasAuthKey;
    bool          mSigMaster;
    RippleAddress mSigningPubKey;

public:
    /** Process the transaction. */
    std::pair<TER, bool>
    operator()();

    ApplyView&
    view()
    {
        return ctx_.view();
    }

    ApplyView const&
    view() const
    {
        return ctx_.view();
    }

    STTx const&
    tx() const
    {
        return ctx_.tx;
    }

protected:
    TER
    apply();

    explicit
    Transactor (ApplyContext& ctx);

    void calculateFee ();

    // VFALCO This is the equivalent of dynamic_cast
    //        to discover the type of the derived class,
    //        and therefore bad.
    virtual
    bool
    mustHaveValidAccount()
    {
        return true;
    }

    // Returns the fee, not scaled for load (Should be in fee units. FIXME)
    virtual std::uint64_t calculateBaseFee ();

    virtual void preCompute();
    virtual TER checkSeq ();
    virtual TER payFee ();
    virtual TER checkSign ();
    virtual TER doApply () = 0;

private:
    TER checkSingleSign ();
    TER checkMultiSign ();
};

/** Performs early sanity checks on the account and fee fields */
TER
preflight1 (PreflightContext const& ctx);

/** Checks whether the signature appears valid */
TER
preflight2 (PreflightContext const& ctx);

}

#endif
