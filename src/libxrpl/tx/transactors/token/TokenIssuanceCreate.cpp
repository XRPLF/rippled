#include <xrpl/tx/transactors/token/TokenIssuanceCreate.h>

#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/ledger/helpers/TokenIssuanceHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STCurrency.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>

#include <memory>

namespace xrpl {

bool
TokenIssuanceCreate::checkExtraFeatures(PreflightContext const& ctx)
{
    // Binding to an MPT issuance requires the MPT amendment.
    return !ctx.tx.isFieldPresent(sfMPTokenIssuanceID) || ctx.rules.enabled(featureMPTokensV1);
}

std::uint32_t
TokenIssuanceCreate::getFlagsMask(PreflightContext const& ctx)
{
    return tfTokenIssuanceCreateMask;
}

NotTEC
TokenIssuanceCreate::preflight(PreflightContext const& ctx)
{
    Currency const currency = ctx.tx[sfCurrency];
    if (isXRP(currency) || currency == badCurrency())
        return temBAD_CURRENCY;

    if (auto const fee = ctx.tx[~sfTransferFee]; fee && *fee > kMaxTransferFee)
        return temBAD_TRANSFER_FEE;

    if (auto const metadata = ctx.tx[~sfMPTokenMetadata];
        metadata && (metadata->empty() || metadata->length() > kMaxMpTokenMetadataLength))
        return temMALFORMED;

    if (auto const maximum = ctx.tx[~sfMaximumAmount];
        maximum && (*maximum == 0 || *maximum > kMaxTokenIssuanceAmount))
        return temMALFORMED;

    if (auto const scale = ctx.tx[~sfTokenScale]; scale && *scale > kMaxTokenIssuanceScale)
        return temMALFORMED;

    // The base unit must be defined for anything that counts in base units.
    if ((ctx.tx.isFieldPresent(sfMaximumAmount) || ctx.tx.isFieldPresent(sfMPTokenIssuanceID)) &&
        !ctx.tx.isFieldPresent(sfTokenScale))
        return temMALFORMED;

    return tesSUCCESS;
}

TER
TokenIssuanceCreate::preclaim(PreclaimContext const& ctx)
{
    AccountID const account = ctx.tx[sfAccount];
    Currency const currency = ctx.tx[sfCurrency];

    if (ctx.view.exists(keylet::tokenIssuance(account, currency)))
        return tecDUPLICATE;

    if (auto const mptId = ctx.tx[~sfMPTokenIssuanceID])
        return validateTokenBinding(
            ctx.view, *mptId, account, ctx.tx[~sfMaximumAmount], ctx.tx[~sfTokenScale].value_or(0));

    return tesSUCCESS;
}

TER
TokenIssuanceCreate::doApply()
{
    auto ctx = ctx_.getApplyViewContext();
    auto const& tx = ctx_.tx;
    Currency const currency = tx[sfCurrency];

    auto const acct = ctx.view.peek(keylet::account(accountID_));
    if (!acct)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const sponsorExp = getEffectiveTxReserveSponsor(ctx, acct);
    if (!sponsorExp)
        return sponsorExp.error();  // LCOV_EXCL_LINE
    auto const sponsorSle = *sponsorExp;

    if (auto const ret =
            checkReserve(ctx, acct, preFeeBalance_, sponsorSle, {.ownerCountDelta = 1}, j_);
        !isTesSuccess(ret))
        return ret;

    auto const issuanceKeylet = keylet::tokenIssuance(accountID_, currency);

    auto const ownerNode = ctx.view.dirInsert(
        keylet::ownerDir(accountID_), issuanceKeylet, describeOwnerDir(accountID_));
    if (!ownerNode)
        return tecDIR_FULL;  // LCOV_EXCL_LINE

    auto issuance = std::make_shared<SLE>(issuanceKeylet);
    std::uint32_t flags = 0;
    if (tx.isFlag(tfTokenCannotLock))
        flags |= lsfTokenCannotLock;
    (*issuance)[sfFlags] = flags;
    (*issuance)[sfIssuer] = accountID_;
    issuance->setFieldCurrency(sfCurrency, STCurrency{sfCurrency, currency});
    (*issuance)[sfOwnerNode] = *ownerNode;

    if (auto const maximum = tx[~sfMaximumAmount])
        (*issuance)[sfMaximumAmount] = *maximum;

    if (auto const scale = tx[~sfTokenScale])
        issuance->at(sfTokenScale) = *scale;

    if (auto const fee = tx[~sfTransferFee])
        issuance->at(sfTransferFee) = *fee;

    if (auto const metadata = tx[~sfMPTokenMetadata])
        (*issuance)[sfMPTokenMetadata] = *metadata;

    if (auto const mptId = tx[~sfMPTokenIssuanceID])
    {
        (*issuance)[sfMPTokenIssuanceID] = *mptId;

        auto const sleMpt = ctx.view.peek(keylet::mptokenIssuance(*mptId));
        if (!sleMpt || sleMpt->isFieldPresent(sfTokenIssuanceID))
            return tecINTERNAL;  // LCOV_EXCL_LINE
        (*sleMpt)[sfTokenIssuanceID] = issuanceKeylet.key;
        ctx.view.update(sleMpt);
    }

    addSponsorToLedgerEntry(issuance, sponsorSle);

    ctx.view.insert(issuance);

    increaseOwnerCount(ctx.view, acct, sponsorSle, 1, j_);

    return tesSUCCESS;
}

}  // namespace xrpl
