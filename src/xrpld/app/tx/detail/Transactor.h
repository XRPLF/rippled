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

#include <xrpld/app/tx/applySteps.h>
#include <xrpld/app/tx/detail/ApplyContext.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/WrappedSink.h>
#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/XRPAmount.h>

namespace ripple {

/** State information when preflighting a tx. */
struct PreflightContext
{
public:
    Application& app;
    STTx const& tx;
    Rules const rules;
    ApplyFlags flags;
    std::optional<uint256 const> parentBatchId;
    beast::Journal const j;

    PreflightContext(
        Application& app_,
        STTx const& tx_,
        uint256 parentBatchId_,
        Rules const& rules_,
        ApplyFlags flags_,
        beast::Journal j_ = beast::Journal{beast::Journal::getNullSink()})
        : app(app_)
        , tx(tx_)
        , rules(rules_)
        , flags(flags_)
        , parentBatchId(parentBatchId_)
        , j(j_)
    {
        XRPL_ASSERT(
            (flags_ & tapBATCH) == tapBATCH, "Batch apply flag should be set");
    }

    PreflightContext(
        Application& app_,
        STTx const& tx_,
        Rules const& rules_,
        ApplyFlags flags_,
        beast::Journal j_ = beast::Journal{beast::Journal::getNullSink()})
        : app(app_), tx(tx_), rules(rules_), flags(flags_), j(j_)
    {
        XRPL_ASSERT(
            (flags_ & tapBATCH) == 0, "Batch apply flag should not be set");
    }

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
    ApplyFlags flags;
    STTx const& tx;
    std::optional<uint256 const> const parentBatchId;
    beast::Journal const j;

    PreclaimContext(
        Application& app_,
        ReadView const& view_,
        TER preflightResult_,
        STTx const& tx_,
        ApplyFlags flags_,
        std::optional<uint256> parentBatchId_,
        beast::Journal j_ = beast::Journal{beast::Journal::getNullSink()})
        : app(app_)
        , view(view_)
        , preflightResult(preflightResult_)
        , flags(flags_)
        , tx(tx_)
        , parentBatchId(parentBatchId_)
        , j(j_)
    {
        XRPL_ASSERT(
            parentBatchId.has_value() == ((flags_ & tapBATCH) == tapBATCH),
            "Parent Batch ID should be set if batch apply flag is set");
    }

    PreclaimContext(
        Application& app_,
        ReadView const& view_,
        TER preflightResult_,
        STTx const& tx_,
        ApplyFlags flags_,
        beast::Journal j_ = beast::Journal{beast::Journal::getNullSink()})
        : PreclaimContext(
              app_,
              view_,
              preflightResult_,
              tx_,
              flags_,
              std::nullopt,
              j_)
    {
        XRPL_ASSERT(
            (flags_ & tapBATCH) == 0, "Batch apply flag should not be set");
    }

    PreclaimContext&
    operator=(PreclaimContext const&) = delete;
};

class TxConsequences;
struct PreflightResult;
// Needed for preflight specialization
class Change;

class Transactor
{
protected:
    ApplyContext& ctx_;
    beast::WrappedSink sink_;
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
    ApplyResult
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
    checkFee(PreclaimContext const& ctx, XRPAmount baseFee);

    static NotTEC
    checkSign(PreclaimContext const& ctx);

    static NotTEC
    checkBatchSign(PreclaimContext const& ctx);

    // Returns the fee in fee units, not scaled for load.
    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    /* Do NOT define an invokePreflight function in a derived class.
       Instead, define:

        // Optional if the transaction is gated on an amendment
        static bool
        isEnabled(PreflightContext const& ctx);

        // Optional if the transaction uses any flags other than tfUniversal
        static std::uint32_t
        getFlagsMask(PreflightContext const& ctx);

        // Required, even if it just returns tesSUCCESS.
        static NotTEC
        preflight(PreflightContext const& ctx);

       * Do not try to call preflight1 or preflight2 directly.
       * Do not check whether relevant amendments are enabled in preflight.
         Instead, define isEnabled.
       * Do not check flags in preflight. Instead, define getFlagsMask.
    */
    template <class T>
    static NotTEC
    invokePreflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx)
    {
        // Most transactors do nothing
        // after checkSeq/Fee/Sign.
        return tesSUCCESS;
    }

    static TER
    checkPermission(ReadView const& view, STTx const& tx);
    /////////////////////////////////////////////////////

    // Interface used by DeleteAccount
    static TER
    ticketDelete(
        ApplyView& view,
        AccountID const& account,
        uint256 const& ticketIndex,
        beast::Journal j);

protected:
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
        XRPAmount baseFee,
        Fees const& fees,
        ApplyFlags flags);

    // Returns the fee in fee units, not scaled for load.
    static XRPAmount
    calculateOwnerReserveFee(ReadView const& view, STTx const& tx);

    // Base class always returns true
    static bool
    isEnabled(PreflightContext const& ctx);

    // Base class always returns tfUniversalMask
    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    static bool
    validDataLength(std::optional<Slice> const& slice, std::size_t maxLength);

    template <class T>
    static bool
    validNumericRange(std::optional<T> value, T max, T min = {});

    template <class T, class Unit>
    static bool
    validNumericRange(
        std::optional<T> value,
        unit::ValueUnit<Unit, T> max,
        unit::ValueUnit<Unit, T> min = {});

private:
    std::pair<TER, XRPAmount>
    reset(XRPAmount fee);

    TER
    consumeSeqProxy(SLE::pointer const& sleAccount);
    TER
    payFee();
    static NotTEC
    checkSingleSign(
        AccountID const& idSigner,
        AccountID const& idAccount,
        std::shared_ptr<SLE const> sleAccount,
        Rules const& rules,
        beast::Journal j);
    static NotTEC
    checkMultiSign(
        ReadView const& view,
        AccountID const& idAccount,
        STArray const& txSigners,
        ApplyFlags const& flags,
        beast::Journal j);

    void trapTransaction(uint256) const;

    /** Performs early sanity checks on the account and fee fields.

        (And passes flagMask to preflight0)

        Do not try to call preflight1 from preflight() in derived classes. See
        the description of invokePreflight for details.
    */
    static NotTEC
    preflight1(PreflightContext const& ctx, std::uint32_t flagMask);

    /** Checks whether the signature appears valid

        Do not try to call preflight2 from preflight() in derived classes. See
        the description of invokePreflight for details.
    */
    static NotTEC
    preflight2(PreflightContext const& ctx);
};

inline bool
Transactor::isEnabled(PreflightContext const& ctx)
{
    return true;
}

/** Performs early sanity checks on the txid and flags */
NotTEC
preflight0(PreflightContext const& ctx, std::uint32_t flagMask);

namespace detail {

/** Checks the validity of the transactor signing key.
 *
 * Normally called from preflight1 with ctx.tx.
 */
NotTEC
preflightCheckSigningKey(STObject const& sigObject, beast::Journal j);

/** Checks the special signing key state needed for simulation
 *
 * Normally called from preflight2 with ctx.tx.
 */
std::optional<NotTEC>
preflightCheckSimulateKeys(
    ApplyFlags flags,
    STObject const& sigObject,
    beast::Journal j);
}  // namespace detail

// Defined in Change.cpp
template <>
NotTEC
Transactor::invokePreflight<Change>(PreflightContext const& ctx);

template <class T>
NotTEC
Transactor::invokePreflight(PreflightContext const& ctx)
{
    // TODO: If #5650 is merged, use its transaction -> amendment lookup here to
    // do a first-pass check. Rewrite or remove any `isEnabled` overloads that
    // check those default amendments.
    if (!T::isEnabled(ctx))
        return temDISABLED;

    if (auto const ret = preflight1(ctx, T::getFlagsMask(ctx)))
        return ret;

    if (auto const ret = T::preflight(ctx))
        return ret;

    return preflight2(ctx);
}

template <class T>
bool
Transactor::validNumericRange(std::optional<T> value, T max, T min)
{
    if (!value)
        return true;
    return value >= min && value <= max;
}

template <class T, class Unit>
bool
Transactor::validNumericRange(
    std::optional<T> value,
    unit::ValueUnit<Unit, T> max,
    unit::ValueUnit<Unit, T> min)
{
    return validNumericRange(value, max.value(), min.value());
}

}  // namespace ripple

#endif
