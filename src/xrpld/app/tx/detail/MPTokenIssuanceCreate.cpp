#include <xrpld/app/tx/detail/MPTokenIssuanceCreate.h>

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

bool
MPTokenIssuanceCreate::checkExtraFeatures(PreflightContext const& ctx)
{
    if (ctx.tx.isFieldPresent(sfDomainID) &&
        !(ctx.rules.enabled(featurePermissionedDomains) &&
          ctx.rules.enabled(featureSingleAssetVault)))
        return false;

    if (ctx.tx.isFieldPresent(sfMutableFlags) &&
        !ctx.rules.enabled(featureDynamicMPT))
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
    // If the mutable flags field is included, at least one flag must be
    // specified.
    if (auto const mutableFlags = ctx.tx[~sfMutableFlags]; mutableFlags &&
        (!*mutableFlags || *mutableFlags & tmfMPTokenIssuanceCreateMutableMask))
        return temINVALID_FLAG;

    if (auto const fee = ctx.tx[~sfTransferFee])
    {
        if (fee > maxTransferFee)
            return temBAD_TRANSFER_FEE;

        // If a non-zero TransferFee is set then the tfTransferable flag
        // must also be set.
        if (fee > 0u && !ctx.tx.isFlag(tfMPTCanTransfer))
            return temMALFORMED;
    }

    if (auto const domain = ctx.tx[~sfDomainID])
    {
        if (*domain == beast::zero)
            return temMALFORMED;

        // Domain present implies that MPTokenIssuance is not public
        if ((ctx.tx.getFlags() & tfMPTRequireAuth) == 0)
            return temMALFORMED;
    }

    if (auto const metadata = ctx.tx[~sfMPTokenMetadata])
    {
        if (metadata->length() == 0 ||
            metadata->length() > maxMPTokenMetadataLength)
            return temMALFORMED;
    }

    // Check if maximumAmount is within unsigned 63 bit range
    if (auto const maxAmt = ctx.tx[~sfMaximumAmount])
    {
        if (maxAmt == 0)
            return temMALFORMED;

        if (maxAmt > maxMPTokenAmount)
            return temMALFORMED;
    }
    return tesSUCCESS;
}

Expected<MPTID, TER>
MPTokenIssuanceCreate::create(
    ApplyView& view,
    beast::Journal journal,
    MPTCreateArgs const& args)
{
    auto const acct = view.peek(keylet::account(args.account));
    if (!acct)
        return Unexpected(tecINTERNAL);  // LCOV_EXCL_LINE

    if (args.priorBalance &&
        *(args.priorBalance) <
            view.fees().accountReserve((*acct)[sfOwnerCount] + 1))
        return Unexpected(tecINSUFFICIENT_RESERVE);

    auto const mptId = makeMptID(args.sequence, args.account);
    auto const mptIssuanceKeylet = keylet::mptIssuance(mptId);

    // create the MPTokenIssuance
    {
        auto const ownerNode = view.dirInsert(
            keylet::ownerDir(args.account),
            mptIssuanceKeylet,
            describeOwnerDir(args.account));

        if (!ownerNode)
            return Unexpected(tecDIR_FULL);  // LCOV_EXCL_LINE

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
            .priorBalance = mPriorBalance,
            .account = account_,
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

}  // namespace ripple
