#include <xrpl/tx/transactors/coupon/CouponClaim.h>

#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/helpers/CouponHelpers.h>
#include <xrpl/ledger/helpers/EscrowHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <algorithm>
#include <cstdint>
#include <type_traits>
#include <variant>

namespace xrpl {

std::uint32_t
CouponClaim::getFlagsMask(PreflightContext const&)
{
    return tfCouponClaimMask;
}

NotTEC
CouponClaim::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfMPTokenIssuanceID] == beast::kZero)
        return temMALFORMED;

    if (auto const amount = ctx.tx[~sfAmount]; amount && *amount <= beast::kZero)
        return temBAD_AMOUNT;

    // A forfeit gives up the whole accrual, so it names no amount.
    if (ctx.tx.isFlag(tfCouponForfeit) && ctx.tx.isFieldPresent(sfAmount))
        return temMALFORMED;

    return tesSUCCESS;
}

TER
CouponClaim::preclaim(PreclaimContext const& ctx)
{
    auto const issuanceID = ctx.tx[sfMPTokenIssuanceID];
    AccountID const holder = ctx.tx[sfAccount];

    auto const schedule = ctx.view.read(keylet::couponSchedule(issuanceID));
    if (!schedule)
        return tecNO_ENTRY;

    if (!ctx.view.exists(keylet::mptoken(issuanceID, holder)))
        return tecNO_ENTRY;

    auto const& couponAsset = schedule->at(sfCouponAsset);

    if (auto const amount = ctx.tx[~sfAmount]; amount && amount->asset() != couponAsset)
        return tecNO_PERMISSION;

    // Delivery eligibility on the holder's side only. The payer's side was
    // settled when CouponPay funded the pool. A forfeit delivers nothing.
    if (!ctx.tx.isFlag(tfCouponForfeit) && !couponAsset.native() &&
        holder != couponAsset.getIssuer())
    {
        if (auto const ter = requireAuth(ctx.view, couponAsset, holder); !isTesSuccess(ter))
            return ter;
        if (isFrozen(ctx.view, holder, couponAsset))
            return couponAsset.holds<MPTIssue>() ? tecLOCKED : tecFROZEN;
    }

    return tesSUCCESS;
}

TER
CouponClaim::doApply()
{
    auto const issuanceID = ctx_.tx[sfMPTokenIssuanceID];

    auto const schedule = view().peek(keylet::couponSchedule(issuanceID));
    if (!schedule)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const mptoken = view().peek(keylet::mptoken(issuanceID, accountID_));
    if (!mptoken)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // Bring the holder current before reading what they are owed.
    couponSettleMPToken(view(), schedule, mptoken, j_);

    STAmount const zero{schedule->at(sfCouponAsset), 0};
    auto const accruedField = mptoken->at(~sfCouponAccrued);
    STAmount const accrued = accruedField ? *accruedField : zero;
    if (accrued <= beast::kZero)
        return tecNO_PERMISSION;

    // A forfeited accrual stays in the pool, which returns to the payer when the schedule goes.
    if (ctx_.tx.isFlag(tfCouponForfeit))
    {
        mptoken->makeFieldAbsent(sfCouponAccrued);
        view().update(mptoken);
        schedule->at(sfClaimantCount) = schedule->at(sfClaimantCount) - 1;
        view().update(schedule);
        return tesSUCCESS;
    }

    auto const requested = ctx_.tx[~sfAmount];
    STAmount const pay = std::min(requested ? *requested : accrued, accrued);
    STAmount const pool = schedule->at(sfPoolAmount);
    if (pool < pay)
        return tefINTERNAL;  // LCOV_EXCL_LINE — the pool always covers accruals.

    // The accrual and the pool fall by exactly what is delivered, or nothing is delivered.
    STAmount const remaining = accrued - pay;
    STAmount const newPool = pool - pay;
    if (accrued - remaining != pay || pool - newPool != pay)
        return tecPRECISION_LOSS;

    AccountID const payer = schedule->at(sfAccount);
    auto const& couponAsset = pay.asset();
    STAmount delivered = pay;

    // Release from the pool. XRP is credited directly; an IOU comes back off
    // its issuer's trust line with the transfer fee deducted and an MPT is
    // unlocked, the same release the token escrow performs.
    if (couponAsset.native())
    {
        auto const sle = view().peek(keylet::account(accountID_));
        if (!sle)
            return tefINTERNAL;  // LCOV_EXCL_LINE
        sle->at(sfBalance) = sle->at(sfBalance) + pay;
        view().update(sle);
    }
    else
    {
        auto const issuerID = couponAsset.getIssuer();
        auto sleDest = view().peek(keylet::account(accountID_));
        if (!sleDest)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        // The rate fixed when the schedule was created; the helper takes the lower of it and
        // the current one.
        auto const fixedRate = schedule->at(~sfTransferRate);
        Rate const lockedRate = fixedRate ? Rate{*fixedRate} : kParityRate;

        // The holder's balance in the coupon asset, to report what the release delivers net
        // of the transfer fee. The coupon asset's issuer redeems the whole amount.
        auto const holderBalance = [&] {
            return std::visit(
                [&]<typename T>(T const& issue) {
                    if constexpr (std::is_same_v<T, Issue>)
                    {
                        return accountHolds(
                            view(),
                            accountID_,
                            issue.currency,
                            issue.account,
                            FreezeHandling::IgnoreFreeze,
                            j_);
                    }
                    else
                    {
                        return accountHolds(
                            view(),
                            accountID_,
                            issue,
                            FreezeHandling::IgnoreFreeze,
                            AuthHandling::IgnoreAuth,
                            j_);
                    }
                },
                couponAsset.value());
        };
        bool const holderIssues = accountID_ == issuerID;
        STAmount const before = holderIssues ? pay : holderBalance();

        if (auto const ret = std::visit(
                [&]<typename T>(T const&) {
                    return escrowUnlockApplyHelper<T>(
                        ctx_.getApplyViewContext(),
                        lockedRate,
                        sleDest,
                        preFeeBalance_,
                        pay,
                        issuerID,
                        payer,
                        accountID_,
                        true,
                        j_);
                },
                couponAsset.value());
            !isTesSuccess(ret))
            return ret;

        if (!holderIssues)
            delivered = holderBalance() - before;
    }

    if (remaining <= beast::kZero)
    {
        mptoken->makeFieldAbsent(sfCouponAccrued);
        schedule->at(sfClaimantCount) = schedule->at(sfClaimantCount) - 1;
    }
    else
    {
        mptoken->at(sfCouponAccrued) = remaining;
    }
    view().update(mptoken);

    schedule->at(sfPoolAmount) = newPool;
    view().update(schedule);

    ctx_.deliver(delivered);

    return tesSUCCESS;
}

void
CouponClaim::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
    // No transaction-specific invariants yet (future work).
}

bool
CouponClaim::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
