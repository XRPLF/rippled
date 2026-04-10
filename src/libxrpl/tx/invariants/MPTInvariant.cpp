#include <xrpl/tx/invariants/MPTInvariant.h>
//
#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/tx/invariants/InvariantCheckPrivilege.h>

#include <algorithm>
#include <array>

namespace xrpl {

static constexpr auto confidentialMPTTxTypes = std::to_array<TxType>({
    ttCONFIDENTIAL_MPT_SEND,
    ttCONFIDENTIAL_MPT_CONVERT,
    ttCONFIDENTIAL_MPT_CONVERT_BACK,
    ttCONFIDENTIAL_MPT_MERGE_INBOX,
    ttCONFIDENTIAL_MPT_CLAWBACK,
});

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
            data_[makeKey(sle)].outstanding[order] = outstanding;
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
            if (order == Before)
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

    if (before && !update(*before, Before))
        return;

    if (after)
    {
        if (after->getType() == ltMPTOKEN_ISSUANCE)
        {
            overflow_ = (*after)[sfOutstandingAmount] > maxMPTAmount(*after);
        }
        if (!update(*after, After))
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
        // Confidential transactions are validated by ValidConfidentialMPToken.
        // They modify encrypted fields and sfConfidentialOutstandingAmount
        // rather than sfMPTAmount/sfOutstandingAmount in the standard way,
        // so ValidMPTPayment's accounting does not apply to them.
        if (std::ranges::find(confidentialMPTTxTypes, tx.getTxnType()) !=
            confidentialMPTTxTypes.end())
            return true;

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
            bool const addOverflows =
                (data.mptAmount > 0 && data.outstanding[Before] > (signedMax - data.mptAmount)) ||
                (data.mptAmount < 0 && data.outstanding[Before] < (-signedMax - data.mptAmount));
            if (addOverflows ||
                data.outstanding[After] != (data.outstanding[Before] + data.mptAmount))
            {
                JLOG(j.fatal()) << "Invariant failed: invalid OutstandingAmount balance "
                                << data.outstanding[Before] << " " << data.outstanding[After] << " "
                                << data.mptAmount;
                return !enforce;
            }
        }
    }

    return true;
}

void
ValidConfidentialMPToken::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    // Helper to get MPToken Issuance ID safely
    auto const getMptID = [](std::shared_ptr<SLE const> const& sle) -> uint192 {
        if (!sle)
            return beast::zero;
        if (sle->getType() == ltMPTOKEN)
            return sle->getFieldH192(sfMPTokenIssuanceID);
        if (sle->getType() == ltMPTOKEN_ISSUANCE)
            return makeMptID(sle->getFieldU32(sfSequence), sle->getAccountID(sfIssuer));
        return beast::zero;
    };

    if (before && before->getType() == ltMPTOKEN)
    {
        uint192 const id = getMptID(before);
        changes_[id].mptAmountDelta -= before->getFieldU64(sfMPTAmount);

        // Cannot delete MPToken with non-zero confidential state or non-zero public amount
        if (isDelete)
        {
            bool const hasPublicBalance = before->getFieldU64(sfMPTAmount) > 0;
            bool const hasEncryptedFields = before->isFieldPresent(sfConfidentialBalanceSpending) ||
                before->isFieldPresent(sfConfidentialBalanceInbox) ||
                before->isFieldPresent(sfIssuerEncryptedBalance);

            if (hasPublicBalance || hasEncryptedFields)
                changes_[id].deletedWithEncrypted = true;
        }
    }

    if (after && after->getType() == ltMPTOKEN)
    {
        uint192 const id = getMptID(after);
        changes_[id].mptAmountDelta += after->getFieldU64(sfMPTAmount);

        // Encrypted field existence consistency
        bool const hasIssuerBalance = after->isFieldPresent(sfIssuerEncryptedBalance);
        bool const hasHolderInbox = after->isFieldPresent(sfConfidentialBalanceInbox);
        bool const hasHolderSpending = after->isFieldPresent(sfConfidentialBalanceSpending);

        bool const hasAnyHolder = hasHolderInbox || hasHolderSpending;

        if (hasAnyHolder != hasIssuerBalance)
        {
            changes_[id].badConsistency = true;
        }

        // Privacy flag consistency
        bool const hasEncrypted = hasAnyHolder || hasIssuerBalance;
        if (hasEncrypted)
            changes_[id].requiresPrivacyFlag = true;
    }

    if (before && before->getType() == ltMPTOKEN_ISSUANCE)
    {
        uint192 const id = getMptID(before);
        if (before->isFieldPresent(sfConfidentialOutstandingAmount))
            changes_[id].coaDelta -= before->getFieldU64(sfConfidentialOutstandingAmount);
        changes_[id].outstandingDelta -= before->getFieldU64(sfOutstandingAmount);
    }

    if (after && after->getType() == ltMPTOKEN_ISSUANCE)
    {
        uint192 const id = getMptID(after);
        auto& change = changes_[id];

        bool const hasCOA = after->isFieldPresent(sfConfidentialOutstandingAmount);
        std::uint64_t const coa = (*after)[~sfConfidentialOutstandingAmount].value_or(0);
        std::uint64_t const oa = after->getFieldU64(sfOutstandingAmount);

        if (hasCOA)
            change.coaDelta += coa;

        change.outstandingDelta += oa;
        change.issuance = after;

        // COA <= OutstandingAmount
        if (coa > oa)
            change.badCOA = true;
    }

    if (before && after && before->getType() == ltMPTOKEN && after->getType() == ltMPTOKEN)
    {
        uint192 const id = getMptID(after);

        // sfConfidentialBalanceVersion must change when spending changes
        auto const spendingBefore = (*before)[~sfConfidentialBalanceSpending];
        auto const spendingAfter = (*after)[~sfConfidentialBalanceSpending];
        auto const versionBefore = (*before)[~sfConfidentialBalanceVersion];
        auto const versionAfter = (*after)[~sfConfidentialBalanceVersion];

        if (spendingBefore.has_value() && spendingBefore != spendingAfter)
        {
            if (versionBefore == versionAfter)
            {
                changes_[id].badVersion = true;
            }
        }
    }
}

bool
ValidConfidentialMPToken::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    if (result != tesSUCCESS)
        return true;

    for (auto const& [id, checks] : changes_)
    {
        // Find the MPTokenIssuance
        auto const issuance = [&]() -> std::shared_ptr<SLE const> {
            if (checks.issuance)
                return checks.issuance;
            return view.read(keylet::mptIssuance(id));
        }();

        // Skip all invariance checks if issuance doesn't exist because that means the MPT has been
        // deleted
        if (!issuance)
            continue;

        // Cannot delete MPToken with non-zero confidential state
        if (checks.deletedWithEncrypted)
        {
            if ((*issuance)[~sfConfidentialOutstandingAmount].value_or(0) > 0)
            {
                JLOG(j.fatal())
                    << "Invariant failed: MPToken deleted with encrypted fields while COA > 0";
                return false;
            }
        }

        // Encrypted field existence consistency
        if (checks.badConsistency)
        {
            JLOG(j.fatal()) << "Invariant failed: MPToken encrypted field "
                               "existence inconsistency";
            return false;
        }

        // COA <= OutstandingAmount
        if (checks.badCOA)
        {
            JLOG(j.fatal()) << "Invariant failed: Confidential outstanding amount "
                               "exceeds total outstanding amount";
            return false;
        }

        // Privacy flag consistency
        if (checks.requiresPrivacyFlag)
        {
            if (!issuance->isFlag(lsfMPTCanConfidentialAmount))
            {
                JLOG(j.fatal()) << "Invariant failed: MPToken has encrypted "
                                   "fields but Issuance does not have "
                                   "lsfMPTCanConfidentialAmount set";
                return false;
            }
        }

        // We only enforce this when Confidential Outstanding Amount changes (Convert, ConvertBack,
        // ConfidentialClawback). This avoids falsely failing on Escrow or AMM operations that lock
        // public tokens outside of ltMPTOKEN. Convert / ConvertBack:
        // - COA and MPTAmount must have opposite deltas, which cancel each other out to zero.
        // - OA remains unchanged.
        // - Therefore, the net delta on both sides of the equation is zero.
        //
        // Clawback:
        // - MPTAmount remains unchanged.
        // - COA and OA must have identical deltas (mirrored on each side).
        // - The equation remains balanced as both sides have equal offsets.
        if (checks.coaDelta != 0)
        {
            if (checks.mptAmountDelta + checks.coaDelta != checks.outstandingDelta)
            {
                JLOG(j.fatal()) << "Invariant failed: Token conservation "
                                   "violation for MPT "
                                << to_string(id);
                return false;
            }
        }

        if (checks.badVersion)
        {
            JLOG(j.fatal())
                << "Invariant failed: MPToken sfConfidentialBalanceVersion not updated when "
                   "sfConfidentialBalanceSpending changed";
            return false;
        }
    }

    return true;
}

void
ValidMPTTransfer::visitEntry(
    bool,
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
        }
    };

    if (before)
        update(*before, true);

    if (after)
        update(*after, false);
}

bool
ValidMPTTransfer::finalize(
    STTx const& tx,
    TER const,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    // AMMClawback is called by the issuer, so freeze restrictions do not apply.
    auto const txnType = tx.getTxnType();
    if (txnType == ttAMM_CLAWBACK)
        return true;

    // DEX transactions (AMM[Create,Deposit,Withdraw], cross-currency payments, offer creates) are
    // subject to the MPTCanTrade flag in addition to the standard transfer rules.
    // A payment is only DEX if it is a cross-currency payment.
    auto const isDEX = [&tx, &txnType] {
        if (txnType == ttPAYMENT)
        {
            // A payment is cross-currency (and thus DEX) only if SendMax is present
            // and its asset differs from the destination asset.
            auto const amount = tx[sfAmount];
            return tx[~sfSendMax].value_or(amount).asset() != amount.asset();
        }
        return txnType == ttAMM_CREATE || txnType == ttAMM_DEPOSIT || txnType == ttAMM_WITHDRAW ||
            txnType == ttOFFER_CREATE;
    }();

    // Only enforce once MPTokensV2 is enabled to preserve consensus with non-V2 nodes.
    // Log invariant failure error even if MPTokensV2 is disabled.
    auto const enforce = !view.rules().enabled(featureMPTokensV2);

    for (auto const& [mptID, values] : amount_)
    {
        std::uint16_t senders = 0;
        std::uint16_t receivers = 0;
        bool frozen = false;
        auto const sleIssuance = view.read(keylet::mptIssuance(mptID));
        if (!sleIssuance)
        {
            continue;
        }

        auto const canTransfer = sleIssuance->isFlag(lsfMPTCanTransfer);
        auto const canTrade = sleIssuance->isFlag(lsfMPTCanTrade);

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
                if (!frozen && isFrozen(view, account, MPTIssue{mptID}))
                {
                    frozen = true;
                }
            }
        }
        // A transfer between holders has occurred (senders > 0 && receivers > 0).
        // Fail if the issuance is frozen, does not permit transfers, or — for
        // DEX transactions — does not permit trading.
        if ((frozen || !canTransfer || (isDEX && !canTrade)) && senders > 0 && receivers > 0)
        {
            JLOG(j.fatal()) << "Invariant failed: invalid MPToken transfer between holders";
            return enforce;
        }
    }

    return true;
}

}  // namespace xrpl
