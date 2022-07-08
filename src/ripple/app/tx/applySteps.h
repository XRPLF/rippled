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

#ifndef RIPPLE_TX_APPLYSTEPS_H_INCLUDED
#define RIPPLE_TX_APPLYSTEPS_H_INCLUDED

#include <ripple/beast/utility/Journal.h>
#include <ripple/ledger/ApplyViewImpl.h>

namespace ripple {

class Application;
class STTx;
class TxQ;

/** Return true if the transaction can claim a fee (tec),
    and the `ApplyFlags` do not allow soft failures.
 */
inline bool
isTecClaimHardFail(TER ter, ApplyFlags flags)
{
    return isTecClaim(ter) && !(flags & tapRETRY);
}

/** Class describing the consequences to the account
    of applying a transaction if the transaction consumes
    the maximum XRP allowed.
*/
class TxConsequences
{
public:
    /// Describes how the transaction affects subsequent
    /// transactions
    enum Category {
        /// Moves currency around, creates offers, etc.
        normal = 0,
        /// Affects the ability of subsequent transactions
        /// to claim a fee. Eg. `SetRegularKey`
        blocker
    };

private:
    /// Describes how the transaction affects subsequent
    /// transactions
    bool isBlocker_;
    /// Transaction fee
    XRPAmount fee_;
    /// Does NOT include the fee.
    XRPAmount potentialSpend_;
    /// SeqProxy of transaction.
    SeqProxy seqProx_;
    /// Number of sequences consumed.
    std::uint32_t sequencesConsumed_;

public:
    // Constructor if preflight returns a value other than tesSUCCESS.
    // Asserts if tesSUCCESS is passed.
    explicit TxConsequences(NotTEC pfresult);

    /// Constructor if the STTx has no notable consequences for the TxQ.
    explicit TxConsequences(STTx const& tx);

    /// Constructor for a blocker.
    TxConsequences(STTx const& tx, Category category);

    /// Constructor for an STTx that may consume more XRP than the fee.
    TxConsequences(STTx const& tx, XRPAmount potentialSpend);

    /// Constructor for an STTx that consumes more than the usual sequences.
    TxConsequences(STTx const& tx, std::uint32_t sequencesConsumed);

    /// Copy constructor
    TxConsequences(TxConsequences const&) = default;
    /// Copy assignment operator
    TxConsequences&
    operator=(TxConsequences const&) = default;
    /// Move constructor
    TxConsequences(TxConsequences&&) = default;
    /// Move assignment operator
    TxConsequences&
    operator=(TxConsequences&&) = default;

    /// Fee
    XRPAmount
    fee() const
    {
        return fee_;
    }

    /// Potential Spend
    XRPAmount const&
    potentialSpend() const
    {
        return potentialSpend_;
    }

    /// SeqProxy
    SeqProxy
    seqProxy() const
    {
        return seqProx_;
    }

    /// Sequences consumed
    std::uint32_t
    sequencesConsumed() const
    {
        return sequencesConsumed_;
    }

    /// Returns true if the transaction is a blocker.
    bool
    isBlocker() const
    {
        return isBlocker_;
    }

    // Return the SeqProxy that would follow this.
    SeqProxy
    followingSeq() const
    {
        SeqProxy following = seqProx_;
        following.advanceBy(sequencesConsumed());
        return following;
    }
};

/** Describes the results of the `preflight` check

    @note All members are const to make it more difficult
        to "fake" a result without calling `preflight`.
    @see preflight, preclaim, doApply, apply
*/
struct PreflightResult
{
public:
    /// From the input - the transaction
    STTx const& tx;
    /// From the input - the rules
    Rules const rules;
    /// Consequences of the transaction
    TxConsequences const consequences;
    /// From the input - the flags
    ApplyFlags const flags;
    /// From the input - the journal
    beast::Journal const j;

    /// Intermediate transaction result
    NotTEC const ter;

    /// Constructor
    template <class Context>
    PreflightResult(
        Context const& ctx_,
        std::pair<NotTEC, TxConsequences> const& result)
        : tx(ctx_.tx)
        , rules(ctx_.rules)
        , consequences(result.second)
        , flags(ctx_.flags)
        , j(ctx_.j)
        , ter(result.first)
    {
    }

    PreflightResult(PreflightResult const&) = default;
    /// Deleted copy assignment operator
    PreflightResult&
    operator=(PreflightResult const&) = delete;
};

/** Describes the results of the `preclaim` check

    @note All members are const to make it more difficult
        to "fake" a result without calling `preclaim`.
    @see preflight, preclaim, doApply, apply
*/
struct PreclaimResult
{
public:
    /// From the input - the ledger view
    ReadView const& view;
    /// From the input - the transaction
    STTx const& tx;
    /// From the input - the flags
    ApplyFlags const flags;
    /// From the input - the journal
    beast::Journal const j;

    /// Intermediate transaction result
    TER const ter;
    /// Success flag - whether the transaction is likely to
    /// claim a fee
    bool const likelyToClaimFee;

    /// Constructor
    template <class Context>
    PreclaimResult(Context const& ctx_, TER ter_)
        : view(ctx_.view)
        , tx(ctx_.tx)
        , flags(ctx_.flags)
        , j(ctx_.j)
        , ter(ter_)
        , likelyToClaimFee(ter == tesSUCCESS || isTecClaimHardFail(ter, flags))
    {
    }

    PreclaimResult(PreclaimResult const&) = default;
    /// Deleted copy assignment operator
    PreclaimResult&
    operator=(PreclaimResult const&) = delete;
};

/** Gate a transaction based on static information.

    The transaction is checked against all possible
    validity constraints that do not require a ledger.

    @param app The current running `Application`.
    @param rules The `Rules` in effect at the time of the check.
    @param tx The transaction to be checked.
    @param flags `ApplyFlags` describing processing options.
    @param j A journal.

    @see PreflightResult, preclaim, doApply, apply

    @return A `PreflightResult` object containing, among
    other things, the `TER` code.
*/
PreflightResult
preflight(
    Application& app,
    Rules const& rules,
    STTx const& tx,
    ApplyFlags flags,
    beast::Journal j);

/** Gate a transaction based on static ledger information.

    The transaction is checked against all possible
    validity constraints that DO require a ledger.

    If preclaim succeeds, then the transaction is very
    likely to claim a fee. This will determine if the
    transaction is safe to relay without being applied
    to the open ledger.

    "Succeeds" in this case is defined as returning a
    `tes` or `tec`, since both lead to claiming a fee.

    @pre The transaction has been checked
    and validated using `preflight`

    @param preflightResult The result of a previous
        call to `preflight` for the transaction.
    @param app The current running `Application`.
    @param view The open ledger that the transaction
        will attempt to be applied to.

    @see PreclaimResult, preflight, doApply, apply

    @return A `PreclaimResult` object containing, among
    other things the `TER` code and the base fee value for
    this transaction.
*/
PreclaimResult
preclaim(
    PreflightResult const& preflightResult,
    Application& app,
    OpenView const& view);

/** Compute only the expected base fee for a transaction.

    Base fees are transaction specific, so any calculation
    needing them must get the base fee for each transaction.

    No validation is done or implied by this function.

    Caller is responsible for handling any exceptions.
    Since none should be thrown, that will usually
    mean terminating.

    @param view The current open ledger.
    @param tx The transaction to be checked.

    @return The base fee.
*/
FeeUnit64
calculateBaseFee(ReadView const& view, STTx const& tx);

/** Return the minimum fee that an "ordinary" transaction would pay.

    When computing the FeeLevel for a transaction the TxQ sometimes needs
    the know what an "ordinary" or reference transaction would be required
    to pay.

    @param view The current open ledger.
    @param tx The transaction so the correct multisigner count is used.

    @return The base fee in XRPAmount.
*/
XRPAmount
calculateDefaultBaseFee(ReadView const& view, STTx const& tx);

/** Apply a prechecked transaction to an OpenView.

    @pre The transaction has been checked
    and validated using `preflight` and `preclaim`

    @param preclaimResult The result of a previous
    call to `preclaim` for the transaction.
    @param app The current running `Application`.
    @param view The open ledger that the transaction
    will attempt to be applied to.

    @see preflight, preclaim, apply

    @return A pair with the `TER` and a `bool` indicating
    whether or not the transaction was applied.
*/
std::pair<TER, bool>
doApply(PreclaimResult const& preclaimResult, Application& app, OpenView& view);

FeeUnit64
invoke_calculateBaseFee(ReadView const& view, STTx const& tx);

}  // namespace ripple

#endif
