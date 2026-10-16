#include <xrpl/ledger/helpers/RepoHelpers.h>

#include <xrpl/basics/Number.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/EscrowHelpers.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>

#include <algorithm>
#include <variant>

namespace xrpl {
namespace repo {

STAmount
repurchaseAmount(SLE::const_ref sleRepo, std::uint32_t closeTime)
{
    STAmount const price = (*sleRepo)[sfPurchasePrice];
    std::uint32_t const rate = (*sleRepo)[sfInterestRate];

    // A pending repo has no start date and cannot be closed, so the caller is
    // expected to have rejected that already.
    std::uint32_t const start =
        sleRepo->isFieldPresent(sfStartDate) ? sleRepo->getFieldU32(sfStartDate) : closeTime;
    std::uint32_t const maturity = (*sleRepo)[sfMaturityDate];

    // Time past maturity is not charged: the seller owes interest to maturity
    // and no further.
    std::uint32_t const end = std::min(closeTime, maturity);
    std::uint32_t const elapsed = end > start ? end - start : 0;

    if (rate == 0 || elapsed == 0)
        return price;

    // The rate is annualized in 1/10 basis points, matching sfInterestRate on
    // ltLOAN, so kTenthBipsPerUnity of it is the whole of the purchase price.
    Number const interest = tenthBipsOfValue(Number(price) * Number(elapsed), TenthBips32{rate}) /
        Number(kSecondsInYear);

    // Round up, so rounding never favours the seller.
    NumberRoundModeGuard const guard(Number::RoundingMode::Upward);
    return price + STAmount(price.asset(), interest);
}

TER
lockCollateral(
    ApplyView& view,
    AccountID const& issuer,
    AccountID const& seller,
    STAmount const& amount,
    beast::Journal journal)
{
    // The issuer cannot be a party, checked at preflight, so this is defensive.
    if (issuer == seller)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (amount.holds<MPTIssue>())
        return lockEscrowMPT(view, seller, amount, journal);

    return directSendNoFee(view, seller, issuer, amount, true, journal);
}

namespace {

TER
checkIouCollateral(
    ReadView const& view,
    AccountID const& seller,
    AccountID const& buyer,
    STAmount const& collateral,
    beast::Journal journal)
{
    auto const& issue = collateral.get<Issue>();
    AccountID const issuer = collateral.getIssuer();

    auto const sleIssuer = view.read(keylet::account(issuer));
    if (!sleIssuer)
        return tecNO_ISSUER;

    // The issuer has to have opted in to having its obligations locked.
    if (!sleIssuer->isFlag(lsfAllowTrustLineLocking))
        return tecNO_PERMISSION;

    auto const sleLine = view.read(keylet::trustLine(seller, issuer, issue.currency));
    if (!sleLine)
        return tecNO_LINE;

    if (auto const ter = requireAuth(view, issue, seller); !isTesSuccess(ter))
        return ter;

    if (auto const ter = requireAuth(view, issue, buyer); !isTesSuccess(ter))
        return ter;

    if (isFrozen(view, seller, issue) || isFrozen(view, buyer, issue))
        return tecFROZEN;

    STAmount const spendable =
        accountHolds(view, seller, issue.currency, issuer, FreezeHandling::IgnoreFreeze, journal);

    if (spendable < collateral || spendable <= beast::kZero)
        return tecINSUFFICIENT_FUNDS;

    if (!canAdd(spendable, collateral))
        return tecPRECISION_LOSS;

    return tesSUCCESS;
}

TER
checkMptCollateral(
    ReadView const& view,
    AccountID const& seller,
    AccountID const& buyer,
    STAmount const& collateral,
    beast::Journal journal)
{
    auto const& mptIssue = collateral.get<MPTIssue>();
    auto const issuanceKey = keylet::mptokenIssuance(mptIssue.getMptID());
    auto const sleIssuance = view.read(issuanceKey);
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    // The issuance has to permit locking, the same opt-in an escrow needs.
    if (!sleIssuance->isFlag(lsfMPTCanEscrow))
        return tecNO_PERMISSION;

    if (!view.exists(keylet::mptoken(issuanceKey.key, seller)))
        return tecOBJECT_NOT_FOUND;

    if (auto const ter = requireAuth(view, mptIssue, seller, AuthType::WeakAuth);
        !isTesSuccess(ter))
        return ter;

    if (auto const ter = requireAuth(view, mptIssue, buyer, AuthType::WeakAuth); !isTesSuccess(ter))
        return ter;

    if (isFrozen(view, seller, *sleIssuance) || isFrozen(view, buyer, *sleIssuance))
        return tecLOCKED;

    if (auto const ter = canTransfer(view, mptIssue, seller, buyer); !isTesSuccess(ter))
        return ter;

    STAmount const spendable = accountHolds(
        view, seller, mptIssue, FreezeHandling::IgnoreFreeze, AuthHandling::IgnoreAuth, journal);

    if (spendable < collateral || spendable <= beast::kZero)
        return tecINSUFFICIENT_FUNDS;

    return tesSUCCESS;
}

}  // namespace

TER
checkCollateral(
    ReadView const& view,
    AccountID const& seller,
    AccountID const& buyer,
    STAmount const& collateral,
    beast::Journal journal)
{
    if (isXRP(collateral))
        return tesSUCCESS;

    if (collateral.holds<MPTIssue>())
        return checkMptCollateral(view, seller, buyer, collateral, journal);

    return checkIouCollateral(view, seller, buyer, collateral, journal);
}

TER
releaseAndDelete(
    ApplyViewContext ctx,
    SLE::ref sleRepo,
    AccountID const& receiver,
    beast::Journal journal)
{
    auto& view = ctx.view;
    STAmount const collateral = (*sleRepo)[sfCollateralAmount];
    AccountID const seller = (*sleRepo)[sfAccount];
    AccountID const counterparty = (*sleRepo)[sfCounterparty];

    auto const sleSeller = view.peek(keylet::account(seller));
    if (!sleSeller)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // Return the collateral. XRP went out of the balance directly at create,
    // so it comes back the same way.
    if (isXRP(collateral))
    {
        auto const sleReceiver =
            receiver == seller ? sleSeller : view.peek(keylet::account(receiver));
        if (!sleReceiver)
            return tefINTERNAL;  // LCOV_EXCL_LINE
        (*sleReceiver)[sfBalance] = (*sleReceiver)[sfBalance] + collateral;
        view.update(sleReceiver);
    }
    else
    {
        AccountID const issuer = collateral.getIssuer();
        Rate const lockedRate = sleRepo->isFieldPresent(sfTransferRate)
            ? Rate{(*sleRepo)[sfTransferRate]}
            : kParityRate;
        auto const sleReceiver = view.peek(keylet::account(receiver));
        if (!sleReceiver)
            return tefINTERNAL;  // LCOV_EXCL_LINE

        auto const ter = std::visit(
            [&]<typename T>(T const&) {
                return escrowUnlockApplyHelper<T>(
                    ctx,
                    lockedRate,
                    sleReceiver,
                    STAmount((*sleReceiver)[sfBalance]).xrp(),
                    collateral,
                    issuer,
                    seller,
                    receiver,
                    true,
                    journal);
            },
            collateral.asset().value());
        if (!isTesSuccess(ter))
            return ter;
    }

    // Remove from both parties' directories, and the issuer's for an IOU.
    if (!view.dirRemove(keylet::ownerDir(seller), (*sleRepo)[sfOwnerNode], sleRepo->key(), false))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    if (!view.dirRemove(
            keylet::ownerDir(counterparty), (*sleRepo)[sfDestinationNode], sleRepo->key(), false))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    if (sleRepo->isFieldPresent(sfIssuerNode) &&
        !view.dirRemove(
            keylet::ownerDir(collateral.getIssuer()),
            (*sleRepo)[sfIssuerNode],
            sleRepo->key(),
            false))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    decreaseOwnerCountForObject(view, sleSeller, sleRepo, 1, journal);
    view.erase(sleRepo);

    return tesSUCCESS;
}

}  // namespace repo
}  // namespace xrpl
