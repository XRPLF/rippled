#include <xrpl/tx/transactors/token/MPTokenIssuanceCreate.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <expected>
#include <memory>

namespace xrpl {

bool
MPTokenIssuanceCreate::checkExtraFeatures(PreflightContext const& ctx)
{
    if (ctx.tx.isFieldPresent(sfDomainID) &&
        !(ctx.rules.enabled(featurePermissionedDomains) &&
          ctx.rules.enabled(featureSingleAssetVault)))
        return false;

    if (ctx.tx.isFieldPresent(sfMutableFlags) && !ctx.rules.enabled(featureDynamicMPT))
        return false;

    return true;
}

std::uint32_t
MPTokenIssuanceCreate::getFlagsMask(PreflightContext const& ctx)
{
    // This mask is only compared against sfFlags
    return tfMPTokenIssuanceCreateMask;
}

NotTEC
MPTokenIssuanceCreate::preflight(PreflightContext const& ctx)
{
    // sfReferenceHolding is set only internally by VaultCreate. Reject
    // any user-submitted MPTokenIssuanceCreate that attempts to carry it.
    if (ctx.rules.enabled(fixCleanup3_2_0) && ctx.tx.isFieldPresent(sfReferenceHolding))
        return temMALFORMED;

    // If the mutable flags field is included, at least one flag must be
    // specified.
    if (auto const mutableFlags = ctx.tx[~sfMutableFlags]; mutableFlags &&
        ((*mutableFlags == 0u) || ((*mutableFlags & tmfMPTokenIssuanceCreateMutableMask) != 0u)))
        return temINVALID_FLAG;

    if (auto const fee = ctx.tx[~sfTransferFee])
    {
        if (fee > kMaxTransferFee)
            return temBAD_TRANSFER_FEE;

        // If a non-zero TransferFee is set then the tfTransferable flag
        // must also be set.
        if (fee > 0u && !ctx.tx.isFlag(tfMPTCanTransfer))
            return temMALFORMED;
    }

    if (auto const domain = ctx.tx[~sfDomainID])
    {
        if (*domain == beast::kZero)
            return temMALFORMED;

        // Domain present implies that MPTokenIssuance is not public
        if (!ctx.tx.isFlag(tfMPTRequireAuth))
            return temMALFORMED;
    }

    if (auto const metadata = ctx.tx[~sfMPTokenMetadata])
    {
        if (metadata->empty() || metadata->length() > kMaxMpTokenMetadataLength)
            return temMALFORMED;
    }

    // Check if maximumAmount is within unsigned 63 bit range
    if (auto const maxAmt = ctx.tx[~sfMaximumAmount])
    {
        if (maxAmt == 0)
            return temMALFORMED;

        if (maxAmt > kMaxMpTokenAmount)
            return temMALFORMED;
    }
    return tesSUCCESS;
}

std::expected<MPTID, TER>
MPTokenIssuanceCreate::create(ApplyView& view, beast::Journal journal, MPTCreateArgs const& args)
{
    auto const acct = view.peek(keylet::account(args.account));
    if (!acct)
        return std::unexpected(tecINTERNAL);  // LCOV_EXCL_LINE

    if (args.priorBalance &&
        *(args.priorBalance) < view.fees().accountReserve((*acct)[sfOwnerCount] + 1))
        return std::unexpected(tecINSUFFICIENT_RESERVE);

    auto const mptId = makeMptID(args.sequence, args.account);
    auto const mptIssuanceKeylet = keylet::mptIssuance(mptId);

    // create the MPTokenIssuance
    {
        auto const ownerNode = view.dirInsert(
            keylet::ownerDir(args.account), mptIssuanceKeylet, describeOwnerDir(args.account));

        if (!ownerNode)
            return std::unexpected(tecDIR_FULL);  // LCOV_EXCL_LINE

        auto mptIssuance = std::make_shared<SLE>(mptIssuanceKeylet);
        (*mptIssuance)[sfFlags] = args.flags & ~tfUniversal;
        (*mptIssuance)[sfIssuer] = args.account;
        (*mptIssuance)[sfOutstandingAmount] = 0;
        (*mptIssuance)[sfOwnerNode] = *ownerNode;
        (*mptIssuance)[sfSequence] = args.sequence;

        if (args.maxAmount)
            (*mptIssuance)[sfMaximumAmount] = *args.maxAmount;

        if (args.assetScale)
            (*mptIssuance)[sfAssetScale] = *args.assetScale;

        if (args.transferFee)
            (*mptIssuance)[sfTransferFee] = *args.transferFee;

        if (args.metadata)
            (*mptIssuance)[sfMPTokenMetadata] = *args.metadata;

        if (args.domainId)
            (*mptIssuance)[sfDomainID] = *args.domainId;

        if (args.mutableFlags)
            (*mptIssuance)[sfMutableFlags] = *args.mutableFlags;

        if (args.referenceHolding)
        {
            // Defensive: the holding must already exist and be of an
            // expected type. Callers (currently only VaultCreate)
            // populate this after the pseudo-account's MPToken /
            // RippleState has been installed. A missing holding here
            // would dangle the pointer and is a programmer error.
            auto const sleHolding = view.read(keylet::unchecked(*args.referenceHolding));
            if (!sleHolding)
                return std::unexpected(tecINTERNAL);  // LCOV_EXCL_LINE
            auto const type = sleHolding->getType();
            if (type != ltMPTOKEN && type != ltRIPPLE_STATE)
                return std::unexpected(tecINTERNAL);  // LCOV_EXCL_LINE
            (*mptIssuance)[sfReferenceHolding] = *args.referenceHolding;
        }

        view.insert(mptIssuance);
    }

    // Update owner count.
    adjustOwnerCount(view, acct, 1, journal);

    return mptId;
}

TER
MPTokenIssuanceCreate::doApply()
{
    auto const& tx = ctx_.tx;
    auto const result = create(
        view(),
        j_,
        {
            .priorBalance = preFeeBalance_,
            .account = accountID_,
            .sequence = tx.getSeqValue(),
            .flags = tx.getFlags(),
            .maxAmount = tx[~sfMaximumAmount],
            .assetScale = tx[~sfAssetScale],
            .transferFee = tx[~sfTransferFee],
            .metadata = tx[~sfMPTokenMetadata],
            .domainId = tx[~sfDomainID],
            .mutableFlags = tx[~sfMutableFlags],
        });
    return result ? tesSUCCESS : result.error();
}

void
MPTokenIssuanceCreate::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
MPTokenIssuanceCreate::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
