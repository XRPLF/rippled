#include <xrpl/tx/transactors/repo/RepoCreate.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/RepoHelpers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

namespace xrpl {

/**
 * The Data field carries a reference to the governing legal annex.
 */
static constexpr std::size_t kMaxRepoDataBytes = 256;

std::uint32_t
RepoCreate::getFlagsMask(PreflightContext const& ctx)
{
    return tfUniversalMask;
}

NotTEC
RepoCreate::preflight(PreflightContext const& ctx)
{
    AccountID const account = ctx.tx[sfAccount];
    AccountID const counterparty = ctx.tx[sfCounterparty];

    if (account == counterparty)
    {
        JLOG(ctx.j.trace()) << "RepoCreate: the counterparty is the account";
        return temMALFORMED;
    }

    STAmount const collateral = ctx.tx[sfCollateralAmount];
    STAmount const price = ctx.tx[sfPurchasePrice];

    if (collateral <= beast::kZero || !isLegalNet(collateral))
    {
        JLOG(ctx.j.trace()) << "RepoCreate: the collateral amount is not positive";
        return temBAD_AMOUNT;
    }

    if (price <= beast::kZero || !isLegalNet(price))
    {
        JLOG(ctx.j.trace()) << "RepoCreate: the purchase price is not positive";
        return temBAD_AMOUNT;
    }

    // Settling the repurchase in the collateral itself would let the seller
    // repay with the very asset that is locked.
    if (collateral.asset() == price.asset())
    {
        JLOG(ctx.j.trace()) << "RepoCreate: the collateral and the cash are the same asset";
        return temBAD_AMOUNT;
    }

    // The repurchase deadline has to fall after the offer stops being
    // acceptable, or the repo could mature before it could be accepted.
    if (ctx.tx[sfMaturityDate] <= ctx.tx[sfExpiration])
    {
        JLOG(ctx.j.trace()) << "RepoCreate: maturity is not after expiration";
        return temBAD_EXPIRATION;
    }

    // MaturityDate plus GracePeriod is the default time, so it must fit the
    // 32-bit date fields.
    if (ctx.tx[sfGracePeriod] > std::numeric_limits<std::uint32_t>::max() - ctx.tx[sfMaturityDate])
    {
        JLOG(ctx.j.trace()) << "RepoCreate: maturity plus grace period overflows";
        return temBAD_EXPIRATION;
    }

    // The same ceiling LoanSet puts on sfInterestRate.
    if (ctx.tx[sfInterestRate] > lending::kMaxInterestRate.value())
    {
        JLOG(ctx.j.trace()) << "RepoCreate: the interest rate is above the maximum";
        return temBAD_AMOUNT;
    }

    if (auto const data = ctx.tx[~sfData]; data && data->size() > kMaxRepoDataBytes)
    {
        JLOG(ctx.j.trace()) << "RepoCreate: sfData is too large";
        return temMALFORMED;
    }

    return tesSUCCESS;
}

TER
RepoCreate::preclaim(PreclaimContext const& ctx)
{
    AccountID const account = ctx.tx[sfAccount];
    AccountID const counterparty = ctx.tx[sfCounterparty];

    if (!ctx.view.exists(keylet::account(counterparty)))
    {
        JLOG(ctx.j.trace()) << "RepoCreate: the counterparty account does not exist";
        return tecNO_DST;
    }

    // An integral cash asset must hold the repurchase amount at maturity.
    // Interest accrues from acceptance, which is no earlier than this ledger.
    auto const closeTime = ctx.view.parentCloseTime().time_since_epoch().count();
    std::uint32_t const maturity = ctx.tx[sfMaturityDate];
    std::uint32_t const elapsed = maturity > closeTime ? maturity - closeTime : 0;
    if (!repo::repurchaseFits(ctx.tx[sfPurchasePrice], ctx.tx[sfInterestRate], elapsed))
    {
        JLOG(ctx.j.trace()) << "RepoCreate: the repurchase amount leaves the cash asset's range";
        return tecPRECISION_LOSS;
    }

    STAmount const collateral = ctx.tx[sfCollateralAmount];
    if (!isXRP(collateral))
    {
        // An issuer holding its own obligation as collateral has nothing
        // locked, so neither party may be the issuer.
        AccountID const issuer = collateral.getIssuer();
        if (issuer == account || issuer == counterparty)
        {
            JLOG(ctx.j.trace()) << "RepoCreate: the collateral issuer is a party";
            return tecNO_PERMISSION;
        }

        if (!ctx.view.exists(keylet::account(issuer)))
            return tecNO_ISSUER;
    }

    // The XLS-85 table: the asset must be lockable, both parties authorized
    // and unfrozen, and the seller actually holding enough to lock.
    if (auto const ter = repo::checkCollateral(ctx.view, account, counterparty, collateral, ctx.j);
        !isTesSuccess(ter))
        return ter;

    return tesSUCCESS;
}

TER
RepoCreate::doApply()
{
    auto const applyViewContext = ctx_.getApplyViewContext();

    auto const sle = view().peek(keylet::account(accountID_));
    if (!sle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    STAmount const collateral = ctx_.tx[sfCollateralAmount];
    AccountID const counterparty = ctx_.tx[sfCounterparty];

    if (auto const ret =
            checkReserve(applyViewContext, sle, preFeeBalance_, {.ownerCountDelta = 1}, j_);
        !isTesSuccess(ret))
        return ret;

    // Locking XRP must leave the seller above its own reserve floor.
    if (isXRP(collateral))
    {
        auto const reserve = accountReserve(view(), sle, j_, {.ownerCountDelta = 1});
        if (preFeeBalance_ - collateral.xrp() < reserve)
            return tecUNFUNDED;
    }

    Keylet const repoKeylet = keylet::repo(accountID_, ctx_.tx.getSeqProxy());
    auto const sleRepo = std::make_shared<SLE>(repoKeylet);
    (*sleRepo)[sfAccount] = accountID_;
    (*sleRepo)[sfCounterparty] = counterparty;
    (*sleRepo)[sfCollateralAmount] = collateral;
    (*sleRepo)[sfPurchasePrice] = ctx_.tx[sfPurchasePrice];
    (*sleRepo)[sfInterestRate] = ctx_.tx[sfInterestRate];
    (*sleRepo)[sfExpiration] = ctx_.tx[sfExpiration];
    (*sleRepo)[sfMaturityDate] = ctx_.tx[sfMaturityDate];
    (*sleRepo)[sfGracePeriod] = ctx_.tx[sfGracePeriod];
    (*sleRepo)[~sfData] = ctx_.tx[~sfData];

    // The issuer's rate is captured now so a later change cannot alter the
    // economics of a trade already struck.
    if (!isXRP(collateral))
    {
        auto const xferRate = transferRate(view(), collateral);
        if (xferRate != kParityRate)
            (*sleRepo)[sfTransferRate] = xferRate.value;
    }

    view().insert(sleRepo);

    auto const ownerPage =
        view().dirInsert(keylet::ownerDir(accountID_), repoKeylet, describeOwnerDir(accountID_));
    if (!ownerPage)
        return tecDIR_FULL;  // LCOV_EXCL_LINE
    (*sleRepo)[sfOwnerNode] = *ownerPage;

    auto const destinationPage = view().dirInsert(
        keylet::ownerDir(counterparty), repoKeylet, describeOwnerDir(counterparty));
    if (!destinationPage)
        return tecDIR_FULL;  // LCOV_EXCL_LINE
    (*sleRepo)[sfDestinationNode] = *destinationPage;

    // An IOU issuer tracks locked balances through its own directory; an MPT
    // issuance records the locked amount on the issuance itself.
    if (!isXRP(collateral) && !collateral.holds<MPTIssue>())
    {
        AccountID const issuer = collateral.getIssuer();
        auto const issuerPage =
            view().dirInsert(keylet::ownerDir(issuer), repoKeylet, describeOwnerDir(issuer));
        if (!issuerPage)
            return tecDIR_FULL;  // LCOV_EXCL_LINE
        (*sleRepo)[sfIssuerNode] = *issuerPage;
    }

    if (isXRP(collateral))
    {
        (*sle)[sfBalance] = (*sle)[sfBalance] - collateral;
        view().update(sle);
    }
    else if (
        auto const ret =
            repo::lockCollateral(view(), collateral.getIssuer(), accountID_, collateral, j_);
        !isTesSuccess(ret))
    {
        // Preclaim's checkCollateral has already shown the lock succeeds.
        return ret;  // LCOV_EXCL_LINE
    }

    increaseOwnerCount(applyViewContext, sle, 1, j_);
    addSponsorToLedgerEntry(applyViewContext, sleRepo);

    return tesSUCCESS;
}

void
RepoCreate::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
}

bool
RepoCreate::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
