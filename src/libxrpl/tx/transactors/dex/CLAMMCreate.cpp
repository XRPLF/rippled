#include <xrpl/ledger/View.h>
#include <xrpl/protocol/CLAMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/transactors/dex/CLAMMCreate.h>
#include <xrpl/tx/transactors/dex/CLAMMHelpers.h>

namespace xrpl {

bool
CLAMMCreate::checkExtraFeatures(PreflightContext const& ctx)
{
    return clammEnabled(ctx.rules);
}

NotTEC
CLAMMCreate::preflight(PreflightContext const& ctx)
{
    if (ctx.tx.getFlags() & tfUniversalMask)
    {
        JLOG(ctx.j.debug()) << "CLAMM Create: invalid flags.";
        return temINVALID_FLAG;
    }

    auto const asset = ctx.tx[sfAsset];
    auto const asset2 = ctx.tx[sfAsset2];

    if (asset == asset2)
    {
        JLOG(ctx.j.debug())
            << "CLAMM Create: tokens cannot have same currency/issuer.";
        return temBAD_AMM_TOKENS;
    }

    auto const feeTier = ctx.tx[sfFeeTier];
    if (!isValidCLAMMFeeTier(feeTier))
    {
        JLOG(ctx.j.debug()) << "CLAMM Create: invalid fee tier.";
        return temBAD_FEE;
    }

    auto const sqrtPrice =
        clamm::fromSLEField(ctx.tx.getFieldH128(sfInitialSqrtPrice));
    if (sqrtPrice < clamm::minSqrtRatio() ||
        sqrtPrice >= clamm::maxSqrtRatio())
    {
        JLOG(ctx.j.debug())
            << "CLAMM Create: initial sqrt price out of valid range.";
        return temBAD_AMOUNT;
    }

    return tesSUCCESS;
}

XRPAmount
CLAMMCreate::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    return calculateOwnerReserveFee(view, tx);
}

TER
CLAMMCreate::preclaim(PreclaimContext const& ctx)
{
    auto const accountID = ctx.tx[sfAccount];
    auto const asset = ctx.tx[sfAsset];
    auto const asset2 = ctx.tx[sfAsset2];
    auto const feeTier = ctx.tx[sfFeeTier];

    auto const clammKeylet = keylet::clamm(asset, asset2, feeTier);
    if (ctx.view.read(clammKeylet))
    {
        JLOG(ctx.j.debug()) << "CLAMM Create: pool already exists.";
        return tecDUPLICATE;
    }

    // Check trust lines and freeze status for IOU assets
    for (auto const& issue :
         {asset.get<Issue>(), asset2.get<Issue>()})
    {
        if (isXRP(issue))
            continue;

        // Issuer doesn't need a trust line to themselves
        if (issue.account == accountID)
            continue;

        // Creator must have a trust line to the issuer
        if (!ctx.view.read(
                keylet::line(accountID, issue.account, issue.currency)))
        {
            JLOG(ctx.j.debug())
                << "CLAMM Create: no trust line for " << issue;
            return tecNO_LINE;
        }

        // Asset must not be frozen (global or individual)
        if (isFrozen(ctx.view, accountID, issue))
        {
            JLOG(ctx.j.debug())
                << "CLAMM Create: asset is frozen, " << issue;
            return tecFROZEN;
        }
    }

    // Check reserve requirement
    auto const sleAccount = ctx.view.read(keylet::account(accountID));
    if (!sleAccount)
        return tecINTERNAL;
    auto const balance = (*sleAccount)[sfBalance].xrp();
    auto const ownerCount = sleAccount->at(sfOwnerCount);
    auto const reserve = ctx.view.fees().accountReserve(ownerCount + 1);
    if (balance < reserve)
    {
        JLOG(ctx.j.debug())
            << "CLAMM Create: insufficient reserve.";
        return tecINSUFFICIENT_RESERVE;
    }

    return tesSUCCESS;
}

TER
CLAMMCreate::doApply()
{
    auto const asset = ctx_.tx[sfAsset];
    auto const asset2 = ctx_.tx[sfAsset2];
    auto const feeTier = ctx_.tx[sfFeeTier];
    auto const tickSpacing = clammTickSpacing(feeTier);
    auto const tradingFee = clammTradingFee(feeTier);

    auto initialSqrtPrice =
        clamm::fromSLEField(ctx_.tx.getFieldH128(sfInitialSqrtPrice));

    auto const clammKeylet = keylet::clamm(asset, asset2, feeTier);

    // Create pseudo-account for the pool
    auto const pseudoAcctResult =
        createPseudoAccount(view(), clammKeylet.key, sfCLAMMID);
    if (!pseudoAcctResult)
        return pseudoAcctResult.error();

    // Enable default ripple on pseudo-account for IOU trust lines
    (*pseudoAcctResult)->setFieldU32(
        sfFlags,
        (*pseudoAcctResult)->getFieldU32(sfFlags) | lsfDefaultRipple);

    auto const ammAccountID = (*pseudoAcctResult)->getAccountID(sfAccount);

    // Create the CLAMM ledger entry
    auto sleClamm = std::make_shared<SLE>(clammKeylet);
    sleClamm->setAccountID(sfAccount, ammAccountID);
    auto const& [issue1, issue2] =
        std::minmax(asset.get<Issue>(), asset2.get<Issue>());

    // If canonical ordering differs from user-specified ordering,
    // invert the sqrt price: sqrtPrice' = 2^192 / sqrtPrice.
    // This ensures the price correctly reflects the canonical pair.
    if (issue1 != asset.get<Issue>())
    {
        auto const q192 = clamm::uint256(1) << 192;
        initialSqrtPrice = static_cast<clamm::uint128>(
            q192 / clamm::uint256(initialSqrtPrice));

        // Re-validate after inversion: the inverted price must still
        // fall within [minSqrtRatio, maxSqrtRatio).
        if (initialSqrtPrice < clamm::minSqrtRatio() ||
            initialSqrtPrice >= clamm::maxSqrtRatio())
        {
            JLOG(j_.warn())
                << "CLAMM Create: inverted sqrt price out of valid range.";
            return tecINTERNAL;
        }
    }

    auto const initialTick = clamm::sqrtPriceToTick(initialSqrtPrice);
    sleClamm->setFieldIssue(sfAsset, STIssue{sfAsset, issue1});
    sleClamm->setFieldIssue(sfAsset2, STIssue{sfAsset2, issue2});
    sleClamm->setFieldU8(sfFeeTier, feeTier);
    sleClamm->setFieldU16(sfTickSpacing, tickSpacing);
    sleClamm->setFieldU16(sfTradingFee, tradingFee);
    sleClamm->setFieldI32(sfCurrentTick, initialTick);
    sleClamm->setFieldH128(
        sfSqrtPrice, clamm::toSLEField(initialSqrtPrice));

    // Insert into pseudo-account's owner directory (matches AMM pattern).
    // The pool SLE lives in the pseudo-account's directory, not the
    // creator's. CLAMMDelete and CLAMMWithdraw auto-delete both clean up
    // from the pseudo-account's directory.
    auto page = view().dirInsert(
        keylet::ownerDir(ammAccountID),
        clammKeylet,
        describeOwnerDir(ammAccountID));
    if (!page)
        return tecDIR_FULL;
    sleClamm->setFieldU64(sfOwnerNode, *page);

    view().insert(sleClamm);

    return tesSUCCESS;
}

}  // namespace xrpl
