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

bool
ValidMPTIssuance::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const _fee,
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
            // The comment above starting with "assert(enforce)" explains this
            // assert.
            XRPL_ASSERT_PARTS(
                enforceCreatedByIssuer, "xrpl::ValidMPTIssuance::finalize", "no issuer MPToken");
            if (enforceCreatedByIssuer)
                return false;
        }

        auto const txnType = tx.getTxnType();
        if (hasPrivilege(tx, createMPTIssuance))
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

        if (hasPrivilege(tx, destroyMPTIssuance))
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
        if (hasPrivilege(tx, mustAuthorizeMPT | mayAuthorizeMPT) || enforceEscrowFinish)
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
            if (mptV2Enabled && hasPrivilege(tx, mayAuthorizeMPT) &&
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
                !submittedByIssuer && hasPrivilege(tx, mustAuthorizeMPT) &&
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

        if (hasPrivilege(tx, mayCreateMPT))
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

        if (hasPrivilege(tx, mayDeleteMPT) &&
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
            if (outstanding > maxMPTokenAmount)
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
            if (mptAmt > maxMPTokenAmount || lockedAmt > maxMPTokenAmount ||
                lockedAmt > (maxMPTokenAmount - mptAmt))
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

        auto const signedMax = static_cast<std::int64_t>(maxMPTokenAmount);
        for (auto const& [id, data] : data_)
        {
            (void)id;
            constexpr auto iBefore = static_cast<std::size_t>(Order::Before);
            constexpr auto iAfter = static_cast<std::size_t>(Order::After);
            bool const addOverflows =
                (data.mptAmount > 0 && data.outstanding[iBefore] > (signedMax - data.mptAmount)) ||
                (data.mptAmount < 0 && data.outstanding[iBefore] < (-signedMax - data.mptAmount));
            if (addOverflows ||
                data.outstanding[iAfter] != (data.outstanding[iBefore] + data.mptAmount))
            {
                JLOG(j.fatal()) << "Invariant failed: invalid OutstandingAmount balance "
                                << data.outstanding[iBefore] << " " << data.outstanding[iAfter]
                                << " " << data.mptAmount;
                return !enforce;
            }
        }
    }

    return true;
}

}  // namespace xrpl
