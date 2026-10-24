#include <xrpl/tx/transactors/coupon/CouponPay.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <optional>
#include <variant>

namespace xrpl {

namespace {

// One coupon's effect on the schedule: the accumulator after it, and the pool
// funding for the step the accumulator actually stores.
struct CouponStep
{
    STAmount accruedPerUnit;
    STAmount total;
};

// The accumulator rounds up so no declared coupon is lost, and the pool is
// funded for the stored step times the units outstanding, rounded up, so the
// pool always covers what holders settle. A coupon smaller than the rounding
// it would need is refused.
std::optional<CouponStep>
couponStep(STAmount const& accruedPerUnit, STAmount const& perUnit, std::uint64_t outstanding)
{
    NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
    STAmount const next = accruedPerUnit + perUnit;
    Number const step = Number(next) - Number(accruedPerUnit);
    if (step - Number(perUnit) > Number(perUnit))
        return std::nullopt;
    return CouponStep{
        .accruedPerUnit = next,
        .total = STAmount{perUnit.asset(), Number(outstanding) * step},
    };
}

// The pool after adding total, or nothing when the sum is not exact.
std::optional<STAmount>
poolAfter(STAmount const& pool, STAmount const& total)
{
    STAmount next = pool + total;
    if (next - pool != total)
        return std::nullopt;
    return next;
}

// The vault whose pseudo-account pays a share issuance's coupons, if the payer is one.
SLE::const_pointer
payerVault(ReadView const& view, AccountID const& payer)
{
    auto const sleAccount = view.read(keylet::account(payer));
    if (!sleAccount)
        return nullptr;  // LCOV_EXCL_LINE
    auto const vaultID = sleAccount->at(~sfVaultID);
    if (!vaultID)
        return nullptr;
    return view.read(keylet::vault(*vaultID));
}

}  // namespace

NotTEC
CouponPay::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfMPTokenIssuanceID] == beast::kZero)
        return temMALFORMED;

    auto const amount = ctx.tx[sfAmount];
    if (amount <= beast::kZero)
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

TER
CouponPay::preclaim(PreclaimContext const& ctx)
{
    auto const issuanceID = ctx.tx[sfMPTokenIssuanceID];

    auto const schedule = ctx.view.read(keylet::couponSchedule(issuanceID));
    if (!schedule)
        return tecNO_ENTRY;

    // The administrator declares coupons; for a share issuance that is the
    // vault owner, while the vault pseudo-account named as Account pays.
    if (ctx.tx[sfAccount] != schedule->at(sfOwner))
        return tecNO_PERMISSION;

    auto const amount = ctx.tx[sfAmount];
    if (amount.asset() != schedule->at(sfCouponAsset))
        return tecWRONG_ASSET;

    auto const closeTime = ctx.view.header().parentCloseTime.time_since_epoch().count();
    if (auto const expiration = schedule->at(~sfExpiration); expiration && closeTime >= *expiration)
        return tecEXPIRED;

    auto const issuance = ctx.view.read(keylet::mptokenIssuance(issuanceID));
    if (!issuance)
        return tecOBJECT_NOT_FOUND;

    std::uint64_t const outstanding = issuance->at(sfOutstandingAmount);
    if (outstanding == 0)
        return tecNO_PERMISSION;

    auto const step = couponStep(schedule->at(sfAccruedPerUnit), amount, outstanding);
    if (!step || !poolAfter(schedule->at(sfPoolAmount), step->total))
        return tecPRECISION_LOSS;

    // The pool must be funded in full at declaration, so a claim can never
    // fail for want of funds.
    STAmount const& total = step->total;
    AccountID const payer = schedule->at(sfAccount);
    if (auto const vault = payerVault(ctx.view, payer))
    {
        if (vault->getFieldNumber(sfAssetsAvailable).value() < Number(total))
            return tecINSUFFICIENT_FUNDS;
    }
    else if (total.native())
    {
        if (xrpLiquid(ctx.view, payer, 0, ctx.j) < total.xrp())
            return tecINSUFFICIENT_FUNDS;
    }
    else if (payer == total.getIssuer())
    {
        // The pool holds the payer's own holdings, which the asset's issuer does not have.
        return tecNO_PERMISSION;
    }
    else
    {
        STAmount const spendable = std::visit(
            [&]<typename T>(T const& issue) {
                if constexpr (std::is_same_v<T, Issue>)
                {
                    return accountHolds(
                        ctx.view,
                        payer,
                        issue.currency,
                        issue.account,
                        FreezeHandling::ZeroIfFrozen,
                        ctx.j);
                }
                else
                {
                    return accountHolds(
                        ctx.view,
                        payer,
                        issue,
                        FreezeHandling::ZeroIfFrozen,
                        AuthHandling::IgnoreAuth,
                        ctx.j);
                }
            },
            total.asset().value());
        if (spendable < total)
            return tecINSUFFICIENT_FUNDS;
    }

    return tesSUCCESS;
}

TER
CouponPay::doApply()
{
    auto const issuanceID = ctx_.tx[sfMPTokenIssuanceID];
    auto const schedule = view().peek(keylet::couponSchedule(issuanceID));
    if (!schedule)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const issuance = view().read(keylet::mptokenIssuance(issuanceID));
    if (!issuance)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const step = couponStep(
        schedule->at(sfAccruedPerUnit), ctx_.tx[sfAmount], issuance->at(sfOutstandingAmount));
    if (!step)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    auto const pool = poolAfter(schedule->at(sfPoolAmount), step->total);
    if (!pool)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    STAmount const& total = step->total;
    AccountID const payer = schedule->at(sfAccount);

    // Move the coupon out of the payer's spendable balance. XRP is debited
    // directly; an IOU parks on its issuer's trust line and an MPT moves into
    // LockedAmount, the same custody the token escrow uses.
    if (total.native())
    {
        auto const sle = view().peek(keylet::account(payer));
        if (!sle)
            return tefINTERNAL;  // LCOV_EXCL_LINE
        auto const balance = sle->at(sfBalance);
        if (balance < total)
            return tecINSUFFICIENT_FUNDS;
        sle->at(sfBalance) = balance - total;
        view().update(sle);
    }
    else if (total.holds<MPTIssue>())
    {
        if (auto const ter = lockEscrowMPT(view(), payer, total, j_); !isTesSuccess(ter))
            return ter;
    }
    else
    {
        if (auto const ter = directSendNoFee(view(), payer, total.getIssuer(), total, true, j_);
            !isTesSuccess(ter))
            return ter;
    }

    // A share issuance's coupon leaves the vault's assets at declaration.
    if (auto const sleAccount = view().read(keylet::account(payer));
        sleAccount && sleAccount->isFieldPresent(sfVaultID))
    {
        auto const vault = view().peek(keylet::vault(sleAccount->at(sfVaultID)));
        if (!vault)
            return tefINTERNAL;  // LCOV_EXCL_LINE
        if (auto const ter = adjustVaultCash(vault, -total, j_); !isTesSuccess(ter))
            return ter;  // LCOV_EXCL_LINE
        view().update(vault);
    }

    schedule->at(sfAccruedPerUnit) = step->accruedPerUnit;
    schedule->at(sfPoolAmount) = *pool;
    schedule->at(sfCouponCount) = schedule->at(sfCouponCount) + 1;
    schedule->at(sfLastCouponTime) =
        static_cast<std::uint32_t>(view().header().parentCloseTime.time_since_epoch().count());
    view().update(schedule);

    return tesSUCCESS;
}

void
CouponPay::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
    // No transaction-specific invariants yet (future work).
}

bool
CouponPay::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
