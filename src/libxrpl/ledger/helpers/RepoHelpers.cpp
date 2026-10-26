#include <xrpl/ledger/helpers/RepoHelpers.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/EscrowHelpers.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

#include <algorithm>
#include <cstdint>
#include <variant>

namespace xrpl::repo {

namespace {

// The rate is annualized in 1/10 basis points, matching sfInterestRate on
// ltLOAN, so kTenthBipsPerUnity of it is the whole of the purchase price.
Number
accruedInterest(STAmount const& price, std::uint32_t rate, std::uint32_t elapsed)
{
    return tenthBipsOfValue(Number(price) * Number(elapsed), TenthBips32{rate}) /
        Number(kSecondsInYear);
}

}  // namespace

STAmount
repurchaseAmount(SLE::ConstRef sleRepo, std::uint32_t closeTime)
{
    STAmount price = (*sleRepo)[sfPurchasePrice];
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

    Number const interest = accruedInterest(price, rate, elapsed);

    // Round up, so rounding never favours the seller.
    NumberRoundModeGuard const guard(Number::RoundingMode::Upward);
    return price + STAmount(price.asset(), interest);
}

bool
repurchaseFits(STAmount const& price, std::uint32_t rate, std::uint32_t elapsed)
{
    // An IOU amount is floating point and cannot leave its range this way.
    if (!price.native() && !price.holds<MPTIssue>())
        return true;

    // With no interest the repurchase is the price itself, which is already a
    // legal amount.
    if (rate == 0 || elapsed == 0)
        return true;

    Number const limit{
        static_cast<std::int64_t>(price.native() ? STAmount::kMaxNativeN : kMaxMpTokenAmount)};
    Number const interest = accruedInterest(price, rate, elapsed);

    NumberRoundModeGuard const guard(Number::RoundingMode::Upward);
    return Number(price) + interest <= limit;
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

    // RepoCreate's preclaim has already checked the issuer exists.
    auto const sleIssuer = view.read(keylet::account(issuer));
    if (!sleIssuer)
        return tecNO_ISSUER;  // LCOV_EXCL_LINE

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
checkPayment(
    ReadView const& view,
    AccountID const& payer,
    AccountID const& payee,
    STAmount const& amount,
    XRPAmount fee,
    AuthType payeeAuth,
    beast::Journal journal)
{
    Asset const& asset = amount.asset();

    if (auto const ter = checkFrozen(view, payer, asset); !isTesSuccess(ter))
        return ter;

    if (auto const ter = checkDeepFrozen(view, payee, asset); !isTesSuccess(ter))
        return ter;

    if (auto const ter = requireAuth(view, asset, payer); !isTesSuccess(ter))
        return ter;

    if (auto const ter = requireAuth(view, asset, payee, payeeAuth); !isTesSuccess(ter))
        return ter;

    if (auto const ter = canTransfer(view, asset, payer, payee); !isTesSuccess(ter))
        return ter;

    // The issuer of the cash asset can always pay in it.
    STAmount held = accountFunds(
        view,
        payer,
        amount,
        FreezeHandling::ZeroIfFrozen,
        AuthHandling::ZeroIfUnauthorized,
        journal);
    if (isXRP(amount))
        held -= STAmount{fee};

    // Between two holders the payer is also debited the issuer's transfer fee,
    // computed as the send computes it: an MPT fee rounds up under
    // fixCleanup3_5_0.
    STAmount owed = amount;
    if (!isXRP(amount) && payer != amount.getIssuer() && payee != amount.getIssuer())
    {
        Rate const rate = transferRate(view, amount);
        owed = amount.holds<MPTIssue>() && view.rules().enabled(fixCleanup3_5_0)
            ? addTransferFee(amount, rate)
            : multiply(amount, rate);
    }

    if (held < owed)
        return tecINSUFFICIENT_FUNDS;

    return tesSUCCESS;
}

TER
ensureHolding(
    ApplyViewContext ctx,
    AccountID const& account,
    Asset const& asset,
    beast::Journal journal)
{
    if (asset.native() || asset.getIssuer() == account)
        return tesSUCCESS;

    auto const sleAccount = ctx.view.peek(keylet::account(account));
    if (!sleAccount)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const sponsorSle = getEffectiveTxReserveSponsor(ctx, sleAccount);
    if (!sponsorSle)
        return sponsorSle.error();  // LCOV_EXCL_LINE

    if (asset.holds<MPTIssue>())
    {
        MPTID const& mptID = asset.get<MPTIssue>().getMptID();
        if (ctx.view.exists(keylet::mptoken(mptID, account)))
            return tesSUCCESS;

        if (auto const ter = createMPToken(ctx.view, mptID, account, *sponsorSle, 0);
            !isTesSuccess(ter))
            return ter;  // LCOV_EXCL_LINE
        increaseOwnerCount(ctx.view, sleAccount, *sponsorSle, 1, journal);
        return tesSUCCESS;
    }

    auto const& issue = asset.get<Issue>();
    Keylet const lineKey = keylet::trustLine(account, issue);
    if (ctx.view.exists(lineKey))
        return tesSUCCESS;

    STAmount initialBalance(issue);
    initialBalance.get<Issue>().account = noAccount();
    if (auto const ter = trustCreate(
            ctx.view,
            issue.account > account,
            issue.account,
            account,
            lineKey.key,
            sleAccount,
            false,
            !sleAccount->isFlag(lsfDefaultRipple),
            false,
            false,
            initialBalance,
            Issue(issue.currency, account),
            0,
            0,
            *sponsorSle,
            journal);
        !isTesSuccess(ter))
        return ter;  // LCOV_EXCL_LINE
    ctx.view.update(sleAccount);
    return tesSUCCESS;
}

TER
checkRelease(ReadView const& view, AccountID const& receiver, STAmount const& collateral)
{
    if (isXRP(collateral))
        return tesSUCCESS;

    return std::visit(
        [&]<typename T>(T const&) {
            return escrowUnlockPreclaimHelper<T>(view, receiver, collateral, true);
        },
        collateral.asset().value());
}

namespace {

// Moves a locked amount from the seller to the receiver: XRP back into the
// balance it left, an issued asset through the XLS-85 unlock at the given rate.
TER
unlockTo(
    ApplyViewContext ctx,
    STAmount const& amount,
    AccountID const& seller,
    AccountID const& receiver,
    Rate rate,
    beast::Journal journal)
{
    auto& view = ctx.view;
    if (isXRP(amount))
    {
        auto const sleReceiver = view.peek(keylet::account(receiver));
        if (!sleReceiver)
            return tefINTERNAL;  // LCOV_EXCL_LINE
        (*sleReceiver)[sfBalance] = (*sleReceiver)[sfBalance] + amount;
        view.update(sleReceiver);
        return tesSUCCESS;
    }

    // The receiver agreed to this asset when the repo was struck, so a missing
    // holding is opened without its reserve rather than blocking the release.
    if (auto const ter = ensureHolding(ctx, receiver, amount.asset(), journal); !isTesSuccess(ter))
        return ter;  // LCOV_EXCL_LINE

    auto const sleReceiver = view.peek(keylet::account(receiver));
    if (!sleReceiver)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const ter = std::visit(
        [&]<typename T>(T const&) {
            return escrowUnlockApplyHelper<T>(
                ctx,
                rate,
                sleReceiver,
                STAmount((*sleReceiver)[sfBalance]).xrp(),
                amount,
                amount.getIssuer(),
                seller,
                receiver,
                true,
                journal);
        },
        amount.asset().value());
    if (!isTesSuccess(ter))
        return ter;  // LCOV_EXCL_LINE
    return tesSUCCESS;
}

// Returns the collateral to the receiver. The captured fee is charged only on
// delivery to the buyer; collateral going back to the seller returns in full.
TER
releaseCollateral(
    ApplyViewContext ctx,
    SLE::ConstRef sleRepo,
    AccountID const& receiver,
    beast::Journal journal)
{
    AccountID const seller = (*sleRepo)[sfAccount];
    Rate const rate = receiver != seller && sleRepo->isFieldPresent(sfTransferRate)
        ? Rate{(*sleRepo)[sfTransferRate]}
        : kParityRate;
    return unlockTo(ctx, (*sleRepo)[sfCollateralAmount], seller, receiver, rate, journal);
}

// Removes the entry from every directory it is in and releases the seller's
// reserve. The issuer directory is the collateral issuer's while the repo is
// open and the cash issuer's once it is closed.
TER
deleteRepo(ApplyViewContext ctx, SLE::Ref sleRepo, beast::Journal journal)
{
    auto& view = ctx.view;
    AccountID const seller = (*sleRepo)[sfAccount];
    auto const sleSeller = view.peek(keylet::account(seller));
    if (!sleSeller)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    if (!view.dirRemove(keylet::ownerDir(seller), (*sleRepo)[sfOwnerNode], sleRepo->key(), false))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    if (!view.dirRemove(
            keylet::ownerDir((*sleRepo)[sfCounterparty]),
            (*sleRepo)[sfDestinationNode],
            sleRepo->key(),
            false))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    if (sleRepo->isFieldPresent(sfIssuerNode))
    {
        AccountID const issuer = isClosed(sleRepo) ? (*sleRepo)[sfAmount]->getIssuer()
                                                   : (*sleRepo)[sfCollateralAmount]->getIssuer();
        if (!view.dirRemove(
                keylet::ownerDir(issuer), (*sleRepo)[sfIssuerNode], sleRepo->key(), false))
            return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    }

    decreaseOwnerCountForObject(view, sleSeller, sleRepo, 1, journal);
    view.erase(sleRepo);
    return tesSUCCESS;
}

}  // namespace

bool
canReceive(ReadView const& view, AccountID const& account, Asset const& asset)
{
    return isTesSuccess(requireAuth(view, asset, account, AuthType::WeakAuth));
}

TER
releaseAndDelete(
    ApplyViewContext ctx,
    SLE::Ref sleRepo,
    AccountID const& receiver,
    beast::Journal journal)
{
    if (auto const ter = releaseCollateral(ctx, sleRepo, receiver, journal); !isTesSuccess(ter))
        return ter;  // LCOV_EXCL_LINE
    return deleteRepo(ctx, sleRepo, journal);
}

TER
closeForBuyer(ApplyViewContext ctx, SLE::Ref sleRepo, STAmount const& owed, beast::Journal journal)
{
    auto& view = ctx.view;
    AccountID const seller = (*sleRepo)[sfAccount];
    AccountID const cashIssuer = owed.getIssuer();

    // The issuer of the cash has nothing to lock: it issues the amount when the
    // buyer collects.
    if (cashIssuer != seller)
    {
        if (auto const ter = lockCollateral(view, cashIssuer, seller, owed, journal);
            !isTesSuccess(ter))
            return ter;  // LCOV_EXCL_LINE
    }

    if (auto const ter = releaseCollateral(ctx, sleRepo, seller, journal); !isTesSuccess(ter))
        return ter;  // LCOV_EXCL_LINE

    // The collateral issuer no longer tracks this entry; an IOU amount held for
    // the buyer is tracked by its own issuer instead.
    if (sleRepo->isFieldPresent(sfIssuerNode))
    {
        if (!view.dirRemove(
                keylet::ownerDir((*sleRepo)[sfCollateralAmount]->getIssuer()),
                (*sleRepo)[sfIssuerNode],
                sleRepo->key(),
                false))
            return tefBAD_LEDGER;  // LCOV_EXCL_LINE
        sleRepo->makeFieldAbsent(sfIssuerNode);
    }

    if (owed.holds<Issue>() && cashIssuer != seller)
    {
        auto const page = view.dirInsert(
            keylet::ownerDir(cashIssuer), sleRepo->key(), describeOwnerDir(cashIssuer));
        if (!page)
            return tecDIR_FULL;  // LCOV_EXCL_LINE
        (*sleRepo)[sfIssuerNode] = *page;
    }

    (*sleRepo)[sfAmount] = owed;
    view.update(sleRepo);
    return tesSUCCESS;
}

TER
collect(ApplyViewContext ctx, SLE::Ref sleRepo, beast::Journal journal)
{
    STAmount const amount = (*sleRepo)[sfAmount];
    AccountID const seller = (*sleRepo)[sfAccount];
    AccountID const buyer = (*sleRepo)[sfCounterparty];

    if (amount.getIssuer() == seller)
    {
        // The seller is the cash issuer and locked nothing; it issues now.
        if (auto const ter = ensureHolding(ctx, buyer, amount.asset(), journal); !isTesSuccess(ter))
            return ter;  // LCOV_EXCL_LINE
        // An issuance without room for the amount fails here (tecPATH_DRY) and
        // the buyer retries once holders redeem.
        if (auto const ter = accountSend(ctx.view, seller, buyer, amount, journal);
            !isTesSuccess(ter))
            return ter;
    }
    else if (
        auto const ter = unlockTo(ctx, amount, seller, buyer, kParityRate, journal);
        !isTesSuccess(ter))
    {
        return ter;  // LCOV_EXCL_LINE
    }

    return deleteRepo(ctx, sleRepo, journal);
}

}  // namespace xrpl::repo
