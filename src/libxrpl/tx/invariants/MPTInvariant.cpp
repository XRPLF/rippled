#include <xrpl/tx/invariants/MPTInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/invariants/InvariantCheckPrivilege.h>

#include <cstddef>
#include <cstdint>
#include <memory>
namespace xrpl {

void
ValidMPTIssuance::visitEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after)
{
    // The sfReferenceHolding tracking and the deleted-holding capture are
    // only meaningful post-fixCleanup3_2_0 (the field is never set
    // pre-amendment, and the holding-deletion rule does not apply).
    // Skip both blocks when the amendment is off so we avoid wasted work
    // on the hot path.
    bool const fix320Enabled = isFeatureEnabled(fixCleanup3_2_0);

    if (after && after->getType() == ltMPTOKEN_ISSUANCE)
    {
        if (isDelete)
        {
            mptIssuancesDeleted_++;
        }
        else if (!before)
        {
            mptIssuancesCreated_++;
            if (fix320Enabled && after->isFieldPresent(sfReferenceHolding))
                referenceHoldingSetOnCreate_ = true;
        }
        else if (fix320Enabled)
        {
            // Modified issuance: detect any change to sfReferenceHolding.
            bool const beforePresent = before->isFieldPresent(sfReferenceHolding);
            bool const afterPresent = after->isFieldPresent(sfReferenceHolding);
            if (beforePresent != afterPresent ||
                (afterPresent &&
                 before->getFieldH256(sfReferenceHolding) !=
                     after->getFieldH256(sfReferenceHolding)))
            {
                referenceHoldingMutated_ = true;
            }
        }
    }

    if (after && after->getType() == ltMPTOKEN)
    {
        if (isDelete)
        {
            mptokensDeleted_++;
            if (fix320Enabled)
                deletedHoldings_.push_back(after);
        }
        else if (!before)
        {
            mptokensCreated_++;
            MPTIssue const mptIssue{after->at(sfMPTokenIssuanceID)};
            if (mptIssue.getIssuer() == after->at(sfAccount))
                mptCreatedByIssuer_ = true;
        }
    }

    // Capture deleted RippleState SLEs so finalize() can verify none of
    // them were owned by a vault pseudo-account outside VaultDelete.
    if (fix320Enabled && isDelete && after && after->getType() == ltRIPPLE_STATE)
        deletedHoldings_.push_back(after);
}

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

    // Post-fixCleanup3_2_0:
    //   - sfReferenceHolding is set only by VaultCreate at share-issuance
    //     creation, and is immutable thereafter.
    //   - A vault pseudo-account's MPToken or RippleState may only be
    //     deleted by VaultDelete; the share's sfReferenceHolding pointer
    //     must not dangle outside that controlled lifecycle.
    if (rules.enabled(fixCleanup3_2_0))
    {
        bool invariantPasses = true;
        if (referenceHoldingMutated_)
        {
            JLOG(j.fatal()) << "Invariant failed: sfReferenceHolding was modified "
                               "on an existing MPTokenIssuance";
            invariantPasses = false;
        }
        if (referenceHoldingSetOnCreate_ && tx.getTxnType() != ttVAULT_CREATE)
        {
            JLOG(j.fatal()) << "Invariant failed: sfReferenceHolding set on a new "
                               "MPTokenIssuance by a non-VaultCreate transaction";
            invariantPasses = false;
        }
        if (!deletedHoldings_.empty() && tx.getTxnType() != ttVAULT_DELETE)
        {
            auto const isVaultPseudo = [&](AccountID const& acct) {
                auto const sle = view.read(keylet::account(acct));
                return sle && sle->isFieldPresent(sfVaultID);
            };
            for (auto const& sleHolding : deletedHoldings_)
            {
                bool offending = false;
                if (sleHolding->getType() == ltMPTOKEN)
                {
                    offending = isVaultPseudo(sleHolding->at(sfAccount));
                }
                else  // ltRIPPLE_STATE
                {
                    auto const lowLimit = sleHolding->getFieldAmount(sfLowLimit);
                    auto const highLimit = sleHolding->getFieldAmount(sfHighLimit);
                    // Each limit's STAmount.issuer is the COUNTERPARTY of
                    // that side's owner: lowLimit's issuer is the high
                    // account, highLimit's issuer is the low account.
                    offending =
                        isVaultPseudo(lowLimit.getIssuer()) || isVaultPseudo(highLimit.getIssuer());
                }
                if (offending)
                {
                    JLOG(j.fatal()) << "Invariant failed: vault pseudo-account holding "
                                       "deleted by a non-VaultDelete transaction";
                    invariantPasses = false;
                }
            }
        }
        if (!invariantPasses)
            return false;
    }

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

void
ValidMPTPayment::visitEntry(bool, SLE::const_ref before, SLE::const_ref after)
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
            if (outstanding > kMaxMpTokenAmount)
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
            if (mptAmt > kMaxMpTokenAmount || lockedAmt > kMaxMpTokenAmount ||
                lockedAmt > (kMaxMpTokenAmount - mptAmt))
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
        bool const invariantPasses = !view.rules().enabled(featureMPTokensV2);
        if (overflow_)
        {
            JLOG(j.fatal()) << "Invariant failed: OutstandingAmount overflow";
            return invariantPasses;
        }

        auto const signedMax = static_cast<std::int64_t>(kMaxMpTokenAmount);
        for (auto const& [id, data] : data_)
        {
            (void)id;
            static constexpr auto kIBefore = static_cast<std::size_t>(Order::Before);
            static constexpr auto kIAfter = static_cast<std::size_t>(Order::After);
            bool const addOverflows =
                (data.mptAmount > 0 && data.outstanding[kIBefore] > (signedMax - data.mptAmount)) ||
                (data.mptAmount < 0 && data.outstanding[kIBefore] < (-signedMax - data.mptAmount));
            if (addOverflows ||
                data.outstanding[kIAfter] != (data.outstanding[kIBefore] + data.mptAmount))
            {
                JLOG(j.fatal()) << "Invariant failed: invalid OutstandingAmount balance "
                                << data.outstanding[kIBefore] << " " << data.outstanding[kIAfter]
                                << " " << data.mptAmount;
                return invariantPasses;
            }
        }
    }

    return true;
}

void
ValidMPTTransfer::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    // Record the before/after MPTAmount for each (issuanceID, account) pair
    // so finalize() can determine whether a transfer actually occurred.
    auto update = [&](SLE const& sle, bool isBefore) {
        if (sle.getType() == ltMPTOKEN)
        {
            auto const issuanceID = sle[sfMPTokenIssuanceID];
            auto const account = sle[sfAccount];
            auto const amount = sle[sfMPTAmount];
            if (isBefore)
            {
                amount_[issuanceID][account].amtBefore = amount;
            }
            else
            {
                amount_[issuanceID][account].amtAfter = amount;
            }
            if (isDelete && isBefore)
            {
                deletedAuthorized_[sle.key()] = sle.isFlag(lsfMPTAuthorized);
            }
        }
    };

    if (before)
        update(*before, true);

    if (after)
        update(*after, false);
}

bool
ValidMPTTransfer::isAuthorized(
    ReadView const& view,
    MPTID const& mptid,
    AccountID const& holder,
    bool reqAuth) const
{
    auto const key = keylet::mptoken(mptid, holder);
    auto const it = deletedAuthorized_.find(key.key);
    if (it != deletedAuthorized_.end())
        return !reqAuth || it->second;
    return isTesSuccess(requireAuth(view, MPTIssue{mptid}, holder));
}

bool
ValidMPTTransfer::finalize(
    STTx const& tx,
    TER const,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    if (hasPrivilege(tx, OverrideFreeze))
        return true;

    // DEX transactions (AMM[Create,Deposit], cross-currency payments, offer creates) are
    // subject to the MPTCanTrade flag in addition to the standard transfer rules.
    // A payment is only DEX if it is a cross-currency payment.
    auto const txnType = tx.getTxnType();
    auto const isDEX = [&] {
        if (txnType == ttPAYMENT)
        {
            // A payment is cross-currency (and thus DEX) only if SendMax is present
            // and its asset differs from the destination asset.
            auto const amount = tx[sfAmount];
            return tx[~sfSendMax].value_or(amount).asset() != amount.asset();
        }
        return txnType == ttAMM_CREATE || txnType == ttAMM_DEPOSIT || txnType == ttOFFER_CREATE;
    }();

    // Only enforce once MPTokensV2 is enabled to preserve consensus with non-V2 nodes.
    // Log invariant failure error even if MPTokensV2 is disabled.
    auto const invariantPasses = !view.rules().enabled(featureMPTokensV2);

    for (auto const& [mptID, values] : amount_)
    {
        std::uint16_t senders = 0;
        std::uint16_t receivers = 0;
        bool invalidTransfer = false;
        auto const sleIssuance = view.read(keylet::mptIssuance(mptID));
        if (!sleIssuance)
        {
            continue;
        }

        // These transactions are recovery/settlement paths. They may move an
        // existing MPT position even after the issuer clears CanTransfer, so
        // holders are not trapped in AMM, vault, or loan protocol accounts.
        auto const waivesCanTransfer = txnType == ttAMM_WITHDRAW ||
            (view.rules().enabled(fixCleanup3_2_0) &&
             (txnType == ttVAULT_WITHDRAW || txnType == ttLOAN_BROKER_COVER_WITHDRAW ||
              txnType == ttLOAN_PAY));
        auto const canTransfer = sleIssuance->isFlag(lsfMPTCanTransfer) || waivesCanTransfer;
        auto const canTrade = sleIssuance->isFlag(lsfMPTCanTrade);
        auto const reqAuth = sleIssuance->isFlag(lsfMPTRequireAuth);

        for (auto const& [account, value] : values)
        {
            // Classify each account as a sender or receiver based on whether their MPTAmount
            // decreased or increased. Count new MPToken holders (no amtBefore) as receivers.
            // Skip deleted MPToken holders (amtAfter is nullopt); deletion requires zero balance.
            if (value.amtAfter.has_value() && value.amtBefore.value_or(0) != *value.amtAfter)
            {
                if (!value.amtBefore.has_value() || *value.amtAfter > *value.amtBefore)
                {
                    ++receivers;
                }
                else
                {
                    ++senders;
                }

                // Check once: if any involved account is frozen, the whole
                // issuance transfer is considered frozen. Only need to check for
                // frozen if there is a transfer of funds.
                if (!invalidTransfer &&
                    (isFrozen(view, account, MPTIssue{mptID}) ||
                     !isAuthorized(view, mptID, account, reqAuth)))
                {
                    invalidTransfer = true;
                }
            }
        }
        // A transfer between holders has occurred (senders > 0 && receivers > 0).
        // Fail if the issuance is frozen, does not permit transfers, or — for
        // DEX transactions — does not permit trading.
        if ((invalidTransfer || !canTransfer || (isDEX && !canTrade)) && senders > 0 &&
            receivers > 0)
        {
            JLOG(j.fatal()) << "Invariant failed: invalid MPToken transfer between holders";
            return invariantPasses;
        }
    }

    return true;
}

}  // namespace xrpl
