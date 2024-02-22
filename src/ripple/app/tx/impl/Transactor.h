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

#include <ripple/app/hook/applyHook.h>
#include <ripple/app/tx/applySteps.h>
#include <ripple/app/tx/impl/ApplyContext.h>
#include <ripple/basics/XRPAmount.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/ledger/PaymentSandbox.h>
#include <ripple/ledger/detail/ApplyViewBase.h>
#include <variant>

namespace ripple {

/** State information when preflighting a tx. */
struct PreflightContext
{
public:
    Application& app;
    STTx const& tx;
    Rules const rules;
    ApplyFlags flags;
    beast::Journal const j;

    PreflightContext(
        Application& app_,
        STTx const& tx_,
        Rules const& rules_,
        ApplyFlags flags_,
        beast::Journal j_);

    PreflightContext&
    operator=(PreflightContext const&) = delete;
};


/** State information when determining if a tx is likely to claim a fee. */
struct PreclaimContext
{
public:
    Application& app;
    ReadView const& view;
    TER preflightResult;
    STTx const& tx;
    ApplyFlags flags;
    beast::Journal const j;

    PreclaimContext(
        Application& app_,
        ReadView const& view_,
        TER preflightResult_,
        STTx const& tx_,
        ApplyFlags flags_,
        beast::Journal j_ = beast::Journal{beast::Journal::getNullSink()})
        : app(app_)
        , view(view_)
        , preflightResult(preflightResult_)
        , tx(tx_)
        , flags(flags_)
        , j(j_)
    {
    }

    PreclaimContext&
    operator=(PreclaimContext const&) = delete;
};

class TxConsequences;
struct PreflightResult;

class Transactor
{
protected:
    ApplyContext& ctx_;
    beast::Journal const j_;

    AccountID const account_;
    XRPAmount mPriorBalance;   // Balance before fees.
    XRPAmount mSourceBalance;  // Balance after fees.

    virtual ~Transactor() = default;
    Transactor(Transactor const&) = delete;
    Transactor&
    operator=(Transactor const&) = delete;

public:
    enum ConsequencesFactoryType { Normal, Blocker, Custom };

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
    checkFee(PreclaimContext const& ctx, FeeUnit64 baseFee);

    static NotTEC
    checkSign(PreclaimContext const& ctx);


    // Returns the fee in fee units, not scaled for load.
    static FeeUnit64
    calculateBaseFee(ReadView const& view, STTx const& tx);


    // Returns a list of zero or more accounts which are
    // not the originator of the transaction but which are
    // stakeholders in the transaction. The bool parameter
    // determines whether or not the specified account has
    // permission for their hook/s to cause a rollback on
    // the transaction.
    static std::vector<std::pair<AccountID, bool>>
    getTransactionalStakeHolders(STTx const& tx);


    static TER
    preclaim(PreclaimContext const& ctx)
    {
        // Most transactors do nothing
        // after checkSeq/Fee/Sign.
        return tesSUCCESS;
    }
    /////////////////////////////////////////////////////

    // Interface used by DeleteAccount
    static TER
    ticketDelete(
        ApplyView& view,
        AccountID const& account,
        uint256 const& ticketIndex,
        beast::Journal j);


    // Hooks

    static FeeUnit64
    calculateHookChainFee(ReadView const& view, STTx const& tx, Keylet const& hookKeylet,
            bool collectCallsOnly = false);

protected:

    void
    doHookCallback(
        std::shared_ptr<STObject const> const& provisionalMeta);

    TER
    doTSH(
        bool strong,                                // only do strong TSH iff true, otheriwse only weak
        hook::HookStateMap& stateMap,
        std::vector<hook::HookResult>& result,
        std::shared_ptr<STObject const> const& provisionalMeta);


    // Execute a hook "Again As Weak" is a feature that allows
    // a hook that which is being executed pre-application of the otxn
    // to request an additional post-application execution.
    void
    doAgainAsWeak(
        AccountID const& hookAccountID,
        std::set<uint256> const& hookHashes,
        hook::HookStateMap& stateMap,
        std::vector<hook::HookResult>& results,
        std::shared_ptr<STObject const> const& provisionalMeta);

    TER
    executeHookChain(
        std::shared_ptr<ripple::STLedgerEntry const> const& hookSLE,
        hook::HookStateMap& stateMap,
        std::vector<hook::HookResult>& results,
        ripple::AccountID const& account,
        bool strong,
        std::shared_ptr<STObject const> const& provisionalMeta);


    void
    addWeakTSHFromSandbox(detail::ApplyViewBase const& pv);

    // hooks amendment fields, these are unpopulated and unused unless featureHooks is enabled
    int executedHookCount_ = 0;              // record how many hooks have executed across the whole transactor
    std::set<AccountID> additionalWeakTSH_;  // any TSH that needs weak hook execution at the end
                                             // of the transactor, who isn't able to be deduced until after apply
                                             // i.e. pathing participants, crossed offers

    ///////////////////////////////////////////////////


    TER
    apply();

    explicit Transactor(ApplyContext& ctx);

    virtual void
    preCompute();

    virtual TER
    doApply() = 0;

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
        FeeUnit64 baseFee,
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

template<class C>
inline
static
std::variant<uint32_t, uint256>
seqID(C const& ctx_)
{
    if (ctx_.view().rules().enabled(featureHooks) && ctx_.tx.isFieldPresent(sfEmitDetails))
        return ctx_.tx.getTransactionID();

    return ctx_.tx.getSeqProxy().value();
}

}  // namespace ripple

#endif
