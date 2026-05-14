/** @file
 *  Invariant checker implementations for Multi-Purpose Tokens (MPT).
 *
 *  Provides `ValidMPTIssuance` and `ValidMPTPayment`, which together form
 *  the post-apply safety net for all MPT-related ledger state changes.
 *  Both classes follow the standard two-phase checker contract — `visitEntry()`
 *  accumulates per-SLE data, `finalize()` renders a pass/fail verdict — and are
 *  registered in the `InvariantChecks` tuple in `InvariantCheck.h`.
 */

#include <xrpl/tx/invariants/MPTInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/invariants/InvariantCheckPrivilege.h>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace xrpl {

/** Accumulate MPT object counts from a single modified ledger entry.
 *
 *  Called once per SLE that the transaction touched.  Updates the four
 *  creation/deletion counters and sets `mptCreatedByIssuer_` when a new
 *  `ltMPTOKEN` entry belongs to the issuance's own issuer account —
 *  a condition that is always a protocol violation.
 *
 *  @param isDelete `true` when the entry is being removed from the ledger.
 *  @param before   Snapshot of the SLE before the transaction; null if the
 *      entry did not exist prior to this transaction.
 *  @param after    Snapshot of the SLE after the transaction; null when
 *      `isDelete` is `true` and the entry has been erased.
 */
void
ValidMPTIssuance::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (after && after->getType() == ltMPTOKEN_ISSUANCE)
    {
        if (isDelete)
        {
            mptIssuancesDeleted_++;
        }
        else if (!before)
        {
            mptIssuancesCreated_++;
        }
    }

    if (after && after->getType() == ltMPTOKEN)
    {
        if (isDelete)
        {
            mptokensDeleted_++;
        }
        else if (!before)
        {
            mptokensCreated_++;
            MPTIssue const mptIssue{after->at(sfMPTokenIssuanceID)};
            if (mptIssue.getIssuer() == after->at(sfAccount))
                mptCreatedByIssuer_ = true;
        }
    }
}

/** Verify that the transaction's MPT object lifecycle changes were authorized.
 *
 *  Dispatches on the transaction's privilege mask (see `InvariantCheckPrivilege.h`)
 *  rather than its type, keeping the logic independent of the growing set of
 *  MPT-capable transaction types.  The hierarchy is:
 *
 *  - **`CreateMptIssuance`**: exactly one `ltMPTOKEN_ISSUANCE` created, none deleted.
 *  - **`DestroyMptIssuance`**: exactly one deleted, none created.
 *  - **`MustAuthorizeMpt | MayAuthorizeMpt`**: no issuance changes; `ltMPTOKEN`
 *      changes bounded by authorization semantics.  `ttAMM_WITHDRAW` and
 *      `ttAMM_CLAWBACK` get a wider allowance (≤1 created, ≤2 deleted) to
 *      cover both token sides of an AMM pool dissolution.
 *  - **`MayCreateMpt`**: no issuances and no deletions; auto-creation of
 *      `ltMPTOKEN` entries allowed for non-issuers (up to 2 for `ttAMM_CREATE`,
 *      up to 1 for `ttCHECK_CASH`).
 *  - **`MayDeleteMpt`**: only deletions (≤2 for `ttAMM_DELETE`), no creations.
 *  - **No privilege**: all counters must be zero on a successful result.
 *
 *  The `mptCreatedByIssuer_` check uses the `assert(enforce)` soft-rollout
 *  pattern: the fatal log fires unconditionally; the hard `false` return and
 *  the `XRPL_ASSERT_PARTS` fire only when `featureSingleAssetVault` or
 *  `featureLendingProtocol` is active, preserving backward compatibility for
 *  older amendment environments while still catching mistakes in dev builds.
 *
 *  @note With `featureMPTokensV2` enabled, `tecINCOMPLETE` results are also
 *      subject to full invariant checking (partial progress is still valid
 *      ledger state).
 *
 *  @param tx     The transaction that was applied.
 *  @param result The `TER` returned by `doApply()` (or the post-reset result).
 *  @param fee    The fee deducted (unused by this checker).
 *  @param view   Read-only view of the post-apply ledger state.
 *  @param j      Journal for fatal-level diagnostics on violation.
 *  @return `true` if all MPT lifecycle constraints are satisfied.
 */
bool
ValidMPTIssuance::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const fee,
    ReadView const& view,
    beast::Journal const& j) const
{
    auto const& rules = view.rules();
    bool const mptV2Enabled = rules.enabled(featureMPTokensV2);
    if (isTesSuccess(result) || (mptV2Enabled && result == tecINCOMPLETE))
    {
        [[maybe_unused]]
        bool const enforceCreatedByIssuer =
            rules.enabled(featureSingleAssetVault) || rules.enabled(featureLendingProtocol);
        if (mptCreatedByIssuer_)
        {
            JLOG(j.fatal()) << "Invariant failed: MPToken created for the MPT issuer";
            XRPL_ASSERT_PARTS(
                enforceCreatedByIssuer, "xrpl::ValidMPTIssuance::finalize", "no issuer MPToken");
            if (enforceCreatedByIssuer)
                return false;
        }

        auto const txnType = tx.getTxnType();
        if (hasPrivilege(tx, CreateMptIssuance))
        {
            if (mptIssuancesCreated_ == 0)
            {
                JLOG(j.fatal()) << "Invariant failed: transaction "
                                   "succeeded without creating a MPT issuance";
            }
            else if (mptIssuancesDeleted_ != 0)
            {
                JLOG(j.fatal()) << "Invariant failed: transaction "
                                   "succeeded while removing MPT issuances";
            }
            else if (mptIssuancesCreated_ > 1)
            {
                JLOG(j.fatal()) << "Invariant failed: transaction "
                                   "succeeded but created multiple issuances";
            }

            return mptIssuancesCreated_ == 1 && mptIssuancesDeleted_ == 0;
        }

        if (hasPrivilege(tx, DestroyMptIssuance))
        {
            if (mptIssuancesDeleted_ == 0)
            {
                JLOG(j.fatal()) << "Invariant failed: MPT issuance deletion "
                                   "succeeded without removing a MPT issuance";
            }
            else if (mptIssuancesCreated_ > 0)
            {
                JLOG(j.fatal()) << "Invariant failed: MPT issuance deletion "
                                   "succeeded while creating MPT issuances";
            }
            else if (mptIssuancesDeleted_ > 1)
            {
                JLOG(j.fatal()) << "Invariant failed: MPT issuance deletion "
                                   "succeeded but deleted multiple issuances";
            }

            return mptIssuancesCreated_ == 0 && mptIssuancesDeleted_ == 1;
        }

        bool const lendingProtocolEnabled = rules.enabled(featureLendingProtocol);
        // ttESCROW_FINISH may authorize an MPT, but it can't have the
        // mayAuthorizeMPT privilege, because that may cause
        // non-amendment-gated side effects.
        bool const enforceEscrowFinish = (txnType == ttESCROW_FINISH) &&
            (rules.enabled(featureSingleAssetVault) || lendingProtocolEnabled);
        if (hasPrivilege(tx, MustAuthorizeMpt | MayAuthorizeMpt) || enforceEscrowFinish)
        {
            bool const submittedByIssuer = tx.isFieldPresent(sfHolder);

            if (mptIssuancesCreated_ > 0)
            {
                JLOG(j.fatal()) << "Invariant failed: MPT authorize "
                                   "succeeded but created MPT issuances";
                return false;
            }
            if (mptIssuancesDeleted_ > 0)
            {
                JLOG(j.fatal()) << "Invariant failed: MPT authorize "
                                   "succeeded but deleted issuances";
                return false;
            }
            if (mptV2Enabled && hasPrivilege(tx, MayAuthorizeMpt) &&
                (txnType == ttAMM_WITHDRAW || txnType == ttAMM_CLAWBACK))
            {
                if (submittedByIssuer && txnType == ttAMM_WITHDRAW && mptokensCreated_ > 0)
                {
                    JLOG(j.fatal()) << "Invariant failed: MPT authorize "
                                       "submitted by issuer succeeded "
                                       "but created bad number of mptokens";
                    return false;
                }
                //  At most one MPToken may be created on withdraw/clawback since:
                //  - Liquidity Provider must have at least one token in order
                //    participate in AMM pool liquidity.
                //  - At most two MPTokens may be deleted if AMM pool, which has exactly
                //    two tokens, is empty after withdraw/clawback.
                if (mptokensCreated_ > 1 || mptokensDeleted_ > 2)
                {
                    JLOG(j.fatal()) << "Invariant failed: MPT authorize  succeeded "
                                       "but created/deleted bad number of mptokens";
                    return false;
                }
            }
            else if (lendingProtocolEnabled && (mptokensCreated_ + mptokensDeleted_) > 1)
            {
                JLOG(j.fatal()) << "Invariant failed: MPT authorize succeeded "
                                   "but created/deleted bad number mptokens";
                return false;
            }
            else if (submittedByIssuer && (mptokensCreated_ > 0 || mptokensDeleted_ > 0))
            {
                JLOG(j.fatal()) << "Invariant failed: MPT authorize submitted by issuer "
                                   "succeeded but created/deleted mptokens";
                return false;
            }
            else if (
                !submittedByIssuer && hasPrivilege(tx, MustAuthorizeMpt) &&
                (mptokensCreated_ + mptokensDeleted_ != 1))
            {
                // if the holder submitted this tx, then a mptoken must be
                // either created or deleted.
                JLOG(j.fatal()) << "Invariant failed: MPT authorize submitted by holder "
                                   "succeeded but created/deleted bad number of mptokens";
                return false;
            }

            return true;
        }

        if (hasPrivilege(tx, MayCreateMpt))
        {
            bool const submittedByIssuer = tx.isFieldPresent(sfHolder);

            if (mptIssuancesCreated_ > 0)
            {
                JLOG(j.fatal()) << "Invariant failed: MPT authorize "
                                   "succeeded but created MPT issuances";
                return false;
            }
            if (mptIssuancesDeleted_ > 0)
            {
                JLOG(j.fatal()) << "Invariant failed: MPT authorize "
                                   "succeeded but deleted issuances";
                return false;
            }
            if (mptokensDeleted_ > 0)
            {
                JLOG(j.fatal()) << "Invariant failed: MPT authorize "
                                   "succeeded but deleted MPTokens";
                return false;
            }
            // AMMCreate may auto-create up to two MPT objects:
            //   - one per asset side in an MPT/MPT AMM, or one in an IOU/MPT AMM.
            // CheckCash may auto-create at most one MPT object for the receiver.
            if ((txnType == ttAMM_CREATE && mptokensCreated_ > 2) ||
                (txnType == ttCHECK_CASH && mptokensCreated_ > 1))
            {
                JLOG(j.fatal()) << "Invariant failed: MPT authorize "
                                   "succeeded but created bad number of mptokens";
                return false;
            }
            if (submittedByIssuer)
            {
                JLOG(j.fatal()) << "Invariant failed: MPT authorize submitted by issuer "
                                   "succeeded but created mptokens";
                return false;
            }

            // Offer crossing or payment may consume multiple offers
            // where takerPays is MPT amount. If the offer owner doesn't
            // own MPT then MPT is created automatically.
            return true;
        }

        if (txnType == ttESCROW_FINISH)
        {
            // ttESCROW_FINISH may authorize an MPT, but it can't have the
            // mayAuthorizeMPT privilege, because that may cause
            // non-amendment-gated side effects.
            XRPL_ASSERT_PARTS(
                !enforceEscrowFinish, "xrpl::ValidMPTIssuance::finalize", "not escrow finish tx");
            return true;
        }

        if (hasPrivilege(tx, MayDeleteMpt) &&
            ((txnType == ttAMM_DELETE && mptokensDeleted_ <= 2) || mptokensDeleted_ == 1) &&
            mptokensCreated_ == 0 && mptIssuancesCreated_ == 0 && mptIssuancesDeleted_ == 0)
            return true;
    }

    if (mptIssuancesCreated_ != 0)
    {
        JLOG(j.fatal()) << "Invariant failed: a MPT issuance was created";
    }
    else if (mptIssuancesDeleted_ != 0)
    {
        JLOG(j.fatal()) << "Invariant failed: a MPT issuance was deleted";
    }
    else if (mptokensCreated_ != 0)
    {
        JLOG(j.fatal()) << "Invariant failed: a MPToken was created";
    }
    else if (mptokensDeleted_ != 0)
    {
        JLOG(j.fatal()) << "Invariant failed: a MPToken was deleted";
    }

    return mptIssuancesCreated_ == 0 && mptIssuancesDeleted_ == 0 && mptokensCreated_ == 0 &&
        mptokensDeleted_ == 0;
}

/** Accumulate outstanding-amount snapshots and holder-balance deltas for a
 *  single modified ledger entry.
 *
 *  Two SLE types are relevant:
 *  - **`ltMPTOKEN_ISSUANCE`**: records the `sfOutstandingAmount` for the
 *      `Order::Before` and `Order::After` slots of the corresponding `MPTData`
 *      entry.  Also performs an `sfMaximumAmount`-bounded overflow check on the
 *      post-apply value to catch issuances where outstanding exceeds the cap.
 *  - **`ltMPTOKEN`**: accumulates the signed net delta (`mptAmount`) across all
 *      holder entries.  The contribution of each token is
 *      `sfMPTAmount + sfLockedAmount`; locked amounts remain part of the
 *      outstanding supply.  Before-entries are subtracted, after-entries added.
 *
 *  If any individual value exceeds `kMAX_MP_TOKEN_AMOUNT`, or if
 *  `mptAmt + lockedAmt` would overflow 64-bit arithmetic, `overflow_` is set
 *  and all further processing for this transaction is skipped.
 *
 *  @note The `isDelete` parameter is unnamed/unused because deletion is already
 *      signalled by `after == nullptr`; presence of `before` is sufficient to
 *      handle the Before contribution.
 *
 *  @param before Snapshot of the SLE before the transaction; null for inserts.
 *  @param after  Snapshot of the SLE after the transaction; null for deletes.
 */
void
ValidMPTPayment::visitEntry(
    bool,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (overflow_)
        return;

    auto makeKey = [](SLE const& sle) {
        if (sle.getType() == ltMPTOKEN_ISSUANCE)
            return makeMptID(sle[sfSequence], sle[sfIssuer]);
        return sle[sfMPTokenIssuanceID];
    };

    auto update = [&](SLE const& sle, Order order) -> bool {
        auto const type = sle.getType();
        if (type == ltMPTOKEN_ISSUANCE)
        {
            auto const outstanding = sle[sfOutstandingAmount];
            if (outstanding > kMAX_MP_TOKEN_AMOUNT)
            {
                overflow_ = true;
                return false;
            }
            data_[makeKey(sle)].outstanding[static_cast<std::size_t>(order)] = outstanding;
        }
        else if (type == ltMPTOKEN)
        {
            auto const mptAmt = sle[sfMPTAmount];
            auto const lockedAmt = sle[~sfLockedAmount].value_or(0);
            if (mptAmt > kMAX_MP_TOKEN_AMOUNT || lockedAmt > kMAX_MP_TOKEN_AMOUNT ||
                lockedAmt > (kMAX_MP_TOKEN_AMOUNT - mptAmt))
            {
                overflow_ = true;
                return false;
            }
            auto const res = static_cast<std::int64_t>(mptAmt + lockedAmt);
            // subtract before from after
            if (order == Order::Before)
            {
                data_[makeKey(sle)].mptAmount -= res;
            }
            else
            {
                data_[makeKey(sle)].mptAmount += res;
            }
        }
        return true;
    };

    if (before && !update(*before, Order::Before))
        return;

    if (after)
    {
        if (after->getType() == ltMPTOKEN_ISSUANCE)
        {
            overflow_ = (*after)[sfOutstandingAmount] > maxMPTAmount(*after);
        }
        if (!update(*after, Order::After))
            return;
    }
}

/** Verify the `OutstandingAmount` conservation invariant for all touched MPTs.
 *
 *  For each MPT ID recorded during `visitEntry()`, checks:
 *
 *  ```
 *  OutstandingAmount[After] == OutstandingAmount[Before] + Σ(MPTAmount[After] − MPTAmount[Before])
 *  ```
 *
 *  where the sum includes `sfLockedAmount` in each holder's contribution, since
 *  locked amounts remain part of the outstanding supply.
 *
 *  Two layers of overflow protection are applied:
 *  1. If `overflow_` was set during `visitEntry()` (individual value exceeded
 *     `kMAX_MP_TOKEN_AMOUNT`), the check fails immediately.
 *  2. The signed delta arithmetic is checked for wrap-around before the final
 *     equality comparison, because the accumulated `mptAmount` can be large
 *     enough to overflow a 64-bit signed integer.
 *
 *  Enforcement is amendment-gated: the invariant hard-fails (returns `false`)
 *  only when `featureMPTokensV2` is active.  Before activation, a violation
 *  logs at fatal severity but returns `true` — preserving backward compatibility
 *  while surfacing problems to operators.
 *
 *  @note Unlike `ValidMPTIssuance::finalize()`, this method is non-`const`
 *      because the `data_` hash-map accumulator can in principle be lazily
 *      mutated; `finalize()` is the single point where all data is final.
 *
 *  @param tx     The transaction that was applied (unused by this checker).
 *  @param result The `TER` result; only `tesSUCCESS` triggers the check.
 *  @param view   Read-only view used to query active amendment rules.
 *  @param j      Journal for fatal-level diagnostics on violation.
 *  @return `true` if outstanding-amount conservation holds for all touched MPTs.
 */
bool
ValidMPTPayment::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    if (isTesSuccess(result))
    {
        bool const enforce = view.rules().enabled(featureMPTokensV2);
        if (overflow_)
        {
            JLOG(j.fatal()) << "Invariant failed: OutstandingAmount overflow";
            return !enforce;
        }

        auto const signedMax = static_cast<std::int64_t>(kMAX_MP_TOKEN_AMOUNT);
        for (auto const& [id, data] : data_)
        {
            (void)id;
            constexpr auto kI_BEFORE = static_cast<std::size_t>(Order::Before);
            constexpr auto kI_AFTER = static_cast<std::size_t>(Order::After);
            bool const addOverflows =
                (data.mptAmount > 0 &&
                 data.outstanding[kI_BEFORE] > (signedMax - data.mptAmount)) ||
                (data.mptAmount < 0 && data.outstanding[kI_BEFORE] < (-signedMax - data.mptAmount));
            if (addOverflows ||
                data.outstanding[kI_AFTER] != (data.outstanding[kI_BEFORE] + data.mptAmount))
            {
                JLOG(j.fatal()) << "Invariant failed: invalid OutstandingAmount balance "
                                << data.outstanding[kI_BEFORE] << " " << data.outstanding[kI_AFTER]
                                << " " << data.mptAmount;
                return !enforce;
            }
        }
    }

    return true;
}

}  // namespace xrpl
