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

#ifndef RIPPLE_APP_TX_APPLYHANDLER_H_INCLUDED
#define RIPPLE_APP_TX_APPLYHANDLER_H_INCLUDED

#include <ripple/app/tx/applySteps.h>
#include <ripple/app/tx/impl/ApplyContext.h>
#include <ripple/basics/XRPAmount.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/app/tx/impl/Transactor.h>

namespace ripple {

typedef TER (*doApplyPtr)(ApplyContext& ctx, XRPAmount mPriorBalance, XRPAmount mSourceBalance);

class ApplyHandler
{
protected:
    ApplyContext& ctx;
    doApplyPtr doApply_;

    XRPAmount mPriorBalance;   // Balance before fees.
    XRPAmount mSourceBalance;  // Balance after fees.

    ApplyHandler&
    operator=(ApplyHandler const&) = delete;

public:
    explicit ApplyHandler(ApplyContext& applyCtx, doApplyPtr doApplyFn);
    ApplyHandler(ApplyHandler const&) = delete;
    ~ApplyHandler() = default;

    enum ConsequencesFactoryType { Normal, Blocker, Custom };
    /** Process the transaction. */
    std::pair<TER, bool>
    operator()();

    /////////////////////////////////////////////////////
    /*
    These static functions are called from invoke_preclaim<Tx>
    using name hiding to accomplish compile-time polymorphism,
    so derived classes can override for different or extra
    functionality. Use with care, as these are not really
    virtual and so don't have the compiler-time protection that
    comes with it.
    */

    static NotTEC
    checkSeqProxy(ReadView const& view, STTx const& tx, beast::Journal j);

    static NotTEC
    checkPriorTxAndLastLedger(PreclaimContext const& ctx);

    static TER
    checkFee(PreclaimContext const& ctx, XRPAmount baseFee);

    static NotTEC
    checkSign(PreclaimContext const& ctx);

    // Returns the fee in fee units, not scaled for load.
    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    static TER
    preclaim(PreclaimContext const& ctx)
    {
        // Most transactors do nothing
        // after checkSeq/Fee/Sign.
        return tesSUCCESS;
    }
    /////////////////////////////////////////////////////

protected:
    TER
    apply();

    virtual void
    preCompute();

    /** Compute the minimum fee required to process a transaction
        with a given baseFee based on the current server load.

        @param app The application hosting the server
        @param baseFee The base fee of a candidate transaction
            @see ripple::calculateBaseFee
        @param fees Fee settings from the current ledger
        @param flags Transaction processing fees
     */
    static XRPAmount
    minimumFee(
        Application& app,
        XRPAmount baseFee,
        Fees const& fees,
        ApplyFlags flags);

private:
    std::pair<TER, XRPAmount>
    reset(XRPAmount fee);

    TER
    consumeSeqProxy(SLE::pointer const& sleAccount);
    TER
    payFee();
    static NotTEC
    checkSingleSign(PreclaimContext const& ctx);
    static NotTEC
    checkMultiSign(PreclaimContext const& ctx);
};

/** Performs early sanity checks on the txid */
NotTEC
preflight0(PreflightContext const& ctx);

/** Performs early sanity checks on the account and fee fields */
NotTEC
preflight1(PreflightContext const& ctx);

/** Checks whether the signature appears valid */
NotTEC
preflight2(PreflightContext const& ctx);

}  // namespace ripple

#endif
