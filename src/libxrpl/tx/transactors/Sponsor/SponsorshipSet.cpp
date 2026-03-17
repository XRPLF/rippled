#include <xrpl/ledger/View.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/transactors/delegate/DelegateUtils.h>
#include <xrpl/tx/transactors/sponsor/SponsorshipSet.h>

namespace xrpl {

std::uint32_t
SponsorshipSet::getFlagsMask(PreflightContext const& ctx)
{
    return tfSponsorshipSetMask;
}

NotTEC
SponsorshipSet::preflight(PreflightContext const& ctx)
{
    auto const flags = ctx.tx.getFlags();

    if ((flags & tfSponsorshipSetRequireSignForFee) &&
        (flags & tfSponsorshipClearRequireSignForFee))
        return temINVALID_FLAG;
    if ((flags & tfSponsorshipSetRequireSignForReserve) &&
        (flags & tfSponsorshipClearRequireSignForReserve))
        return temINVALID_FLAG;

    auto const account = ctx.tx.getAccountID(sfAccount);
    bool const hasSponsor = ctx.tx.isFieldPresent(sfCounterpartySponsor);
    bool const hasSponsee = ctx.tx.isFieldPresent(sfSponsee);

    //  The transaction must specify either Sponsor or Sponsee, but not both.
    if (hasSponsor == hasSponsee)
        return temMALFORMED;

    auto const sponsorAccountID = ctx.tx[~sfCounterpartySponsor].value_or(account);
    auto const sponseeAccountID = ctx.tx[~sfSponsee].value_or(account);

    if (sponsorAccountID == sponseeAccountID)
        return temMALFORMED;

    if (flags & tfDeleteObject)
    {
        // can not combine with any modification flags when deleting
        constexpr std::uint32_t modifyFlags = tfSponsorshipSetRequireSignForFee |
            tfSponsorshipSetRequireSignForReserve | tfSponsorshipClearRequireSignForFee |
            tfSponsorshipClearRequireSignForReserve;

        if (flags & modifyFlags)
            return temINVALID_FLAG;

        // can not include these fields when deleting
        if (ctx.tx.isFieldPresent(sfFeeAmount) || ctx.tx.isFieldPresent(sfReserveCount) ||
            ctx.tx.isFieldPresent(sfMaxFee))
            return temMALFORMED;
    }
    else
    {
        // although both Sponsor and Sponsee can delete,
        // only the Sponsor can create or update sponsorship.
        if (account != sponsorAccountID)
            return temMALFORMED;

        // Check FeeAmount and MaxFee
        auto const checkOptionalAmountField = [&](SField const& field) -> NotTEC {
            if (!ctx.tx.isFieldPresent(field))
                return tesSUCCESS;

            auto const amount = ctx.tx.getFieldAmount(field);

            if (!isXRP(amount))
                return temBAD_AMOUNT;

            if (amount.xrp() < XRPAmount{0})
                return temBAD_AMOUNT;

            return tesSUCCESS;
        };

        if (auto const ret = checkOptionalAmountField(sfFeeAmount); !isTesSuccess(ret))
            return ret;

        if (auto const ret = checkOptionalAmountField(sfMaxFee); !isTesSuccess(ret))
            return ret;
    }

    return tesSUCCESS;
}

NotTEC
SponsorshipSet::checkPermission(ReadView const& view, STTx const& tx)
{
    auto const delegate = tx[~sfDelegate];
    if (!delegate)
        return tesSUCCESS;

    auto const delegateKey = keylet::delegate(tx[sfAccount], *delegate);
    auto const sle = view.read(delegateKey);

    if (!sle)
        return terNO_DELEGATE_PERMISSION;

    if (checkTxPermission(sle, tx) == tesSUCCESS)
        return tesSUCCESS;

    auto const txFlags = tx.getFlags();

    // this is added in case more flags will be added for SponsorshipSet
    // in the future. Currently unreachable.
    if (txFlags & tfSponsorshipSetPermissionMask)
        return terNO_DELEGATE_PERMISSION;

    std::unordered_set<GranularPermissionType> granularPermissions;
    loadGranularPermission(sle, ttSPONSORSHIP_SET, granularPermissions);

    auto const sponsoringFee = tx.isFieldPresent(sfFeeAmount) || tx.isFieldPresent(sfMaxFee) ||
        txFlags & tfSponsorshipSetRequireSignForFee;
    auto const sponsoringReserve =
        tx.isFieldPresent(sfReserveCount) || txFlags & tfSponsorshipSetRequireSignForReserve;

    if (sponsoringFee && !granularPermissions.contains(SponsorFee))
        return terNO_DELEGATE_PERMISSION;

    if (sponsoringReserve && !granularPermissions.contains(SponsorReserve))
        return terNO_DELEGATE_PERMISSION;

    return tesSUCCESS;
}

TER
SponsorshipSet::preclaim(PreclaimContext const& ctx)
{
    auto const sponsorAccountID = ctx.tx[~sfCounterpartySponsor].value_or(ctx.tx[sfAccount]);
    auto const sponseeAccountID = ctx.tx[~sfSponsee].value_or(ctx.tx[sfAccount]);

    if (sponseeAccountID == sponsorAccountID)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // check Sponsor
    auto const sponsorAccSle = ctx.view.read(keylet::account(sponsorAccountID));
    if (!sponsorAccSle)
        return tecNO_DST;

    // check Sponsee
    auto const sponseeSle = ctx.view.read(keylet::account(sponseeAccountID));
    if (!sponseeSle)
        return tecNO_DST;

    // check if object exists
    auto const sponsorObjSle = ctx.view.read(keylet::sponsor(sponsorAccountID, sponseeAccountID));

    if (ctx.tx.isFlag(tfDeleteObject) && !sponsorObjSle)
        return tecNO_ENTRY;

    if (sponseeSle->isFlag(lsfDisallowIncomingSponsor) && !sponsorObjSle)
        // new sponsor creation is not allowed by disallowIncomingSponsor flag
        return tecNO_PERMISSION;

    return tesSUCCESS;
}

TER
SponsorshipSet::doApply()
{
    auto const sponsorAccountID = ctx_.tx[~sfCounterpartySponsor].value_or(account_);
    auto const sponseeAccountID = ctx_.tx[~sfSponsee].value_or(account_);

    if (sponseeAccountID == sponsorAccountID)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const sponsorAccSle = ctx_.view().peek(keylet::account(sponsorAccountID));
    if (!sponsorAccSle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (!ctx_.view().exists(keylet::account(sponseeAccountID)))
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const sponsorKeylet = keylet::sponsor(sponsorAccountID, sponseeAccountID);
    auto const sponsorObjSle = ctx_.view().peek(sponsorKeylet);

    if (ctx_.tx.isFlag(tfDeleteObject))
    {
        // Delete
        if (!sponsorObjSle)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        auto const sponsor = getLedgerEntryReserveSponsor(ctx_.view(), sponsorObjSle);
        adjustOwnerCount(ctx_.view(), sponsorAccSle, sponsor, -1, ctx_.journal);

        ctx_.view().dirRemove(
            keylet::ownerDir(sponsorAccountID),
            (*sponsorObjSle)[sfOwnerNode],
            sponsorObjSle->key(),
            false);
        ctx_.view().dirRemove(
            keylet::ownerDir(sponseeAccountID),
            (*sponsorObjSle)[sfSponseeNode],
            sponsorObjSle->key(),
            false);

        // transfer feeAmount from ledger entry
        if (sponsorObjSle->isFieldPresent(sfFeeAmount))
        {
            auto const feeAmount = sponsorObjSle->getFieldAmount(sfFeeAmount);
            (*sponsorAccSle)[sfBalance] += feeAmount;
        }

        ctx_.view().erase(sponsorObjSle);

        return tesSUCCESS;
    }

    auto const feeAmount = ctx_.tx[~sfFeeAmount];
    auto const maxFee = ctx_.tx[~sfMaxFee];
    auto const reserveCount = ctx_.tx[~sfReserveCount];

    auto reserveSponsorAccSle = getTxReserveSponsor(view(), ctx_.tx);

    if (feeAmount && (*feeAmount).xrp() > (*sponsorAccSle)[sfBalance])
        return tecUNFUNDED;

    if (!sponsorObjSle)
    {
        // Create
        auto newSle = std::make_shared<SLE>(sponsorKeylet);

        if (auto const ret = checkInsufficientReserve(
                ctx_.view(), ctx_.tx, sponsorAccSle, mPriorBalance, reserveSponsorAccSle, 1);
            !isTesSuccess(ret))
            return tecUNFUNDED;

        (*newSle)[sfOwner] = sponsorAccountID;
        (*newSle)[sfSponsee] = sponseeAccountID;
        if (feeAmount && *feeAmount > XRPAmount(0))
        {
            (*sponsorAccSle)[sfBalance] -= *feeAmount;
            (*newSle)[sfFeeAmount] = *feeAmount;
        }
        if (maxFee && *maxFee > XRPAmount(0))
            (*newSle)[sfMaxFee] = *maxFee;
        if (reserveCount && *reserveCount > 0)
            (*newSle)[sfReserveCount] = *reserveCount;

        auto flags = 0;
        if (ctx_.tx.isFlag(tfSponsorshipSetRequireSignForFee))
            flags |= lsfSponsorshipRequireSignForFee;

        if (ctx_.tx.isFlag(tfSponsorshipSetRequireSignForReserve))
            flags |= lsfSponsorshipRequireSignForReserve;

        (*newSle)[sfFlags] = flags;

        auto const sponsorPage = view().dirInsert(
            keylet::ownerDir(sponsorAccountID), sponsorKeylet, describeOwnerDir(sponsorAccountID));
        (*newSle)[sfOwnerNode] = *sponsorPage;

        auto const sponseePage = view().dirInsert(
            keylet::ownerDir(sponseeAccountID), sponsorKeylet, describeOwnerDir(sponseeAccountID));
        (*newSle)[sfSponseeNode] = *sponseePage;

        auto viewJ = ctx_.registry.journal("View");

        adjustOwnerCount(view(), sponsorAccSle, reserveSponsorAccSle, 1, viewJ);
        addSponsorToLedgerEntry(newSle, reserveSponsorAccSle);

        ctx_.view().insert(newSle);
        return tesSUCCESS;
    }

    // Update
    if (feeAmount)
    {
        auto const currentFeeAmount = (*sponsorObjSle)[~sfFeeAmount].value_or(XRPAmount(0));
        auto feeAmountDelta = XRPAmount(*feeAmount - currentFeeAmount);

        // transfer feeAmount to ledger entry
        if (feeAmountDelta != beast::zero)
        {
            (*sponsorAccSle)[sfBalance] -= feeAmountDelta;

            if (*feeAmount == XRPAmount(0))
                (*sponsorObjSle).makeFieldAbsent(sfFeeAmount);
            else
                (*sponsorObjSle).setFieldAmount(sfFeeAmount, *feeAmount);
        }
    }

    if (maxFee)
    {
        if (*maxFee == XRPAmount(0))
            (*sponsorObjSle).makeFieldAbsent(sfMaxFee);
        else
            (*sponsorObjSle)[sfMaxFee] = *maxFee;
    }

    if (reserveCount)
    {
        if (*reserveCount == 0)
            (*sponsorObjSle).makeFieldAbsent(sfReserveCount);
        else
            (*sponsorObjSle)[sfReserveCount] = *reserveCount;
    }

    // update Flags
    auto flags = sponsorObjSle->getFieldU32(sfFlags);
    if (ctx_.tx.isFlag(tfSponsorshipSetRequireSignForFee))
        flags |= lsfSponsorshipRequireSignForFee;

    if (ctx_.tx.isFlag(tfSponsorshipClearRequireSignForFee))
        flags &= ~lsfSponsorshipRequireSignForFee;

    if (ctx_.tx.isFlag(tfSponsorshipSetRequireSignForReserve))
        flags |= lsfSponsorshipRequireSignForReserve;

    if (ctx_.tx.isFlag(tfSponsorshipClearRequireSignForReserve))
        flags &= ~lsfSponsorshipRequireSignForReserve;

    if (flags != (*sponsorObjSle)[sfFlags])
        (*sponsorObjSle)[sfFlags] = flags;

    view().update(sponsorObjSle);

    return tesSUCCESS;
}

TER
SponsorshipSet::deleteSponsorship(
    ApplyView& view,
    std::shared_ptr<SLE> const& sle,
    beast::Journal j)
{
    auto const sponsorAccountID = sle->getAccountID(sfOwner);
    auto const sponseeAccountID = sle->getAccountID(sfSponsee);

    // adjust balance
    auto const sponsorAccSle = view.peek(keylet::account(sponsorAccountID));
    if (!sponsorAccSle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (sle->isFieldPresent(sfFeeAmount))
    {
        auto const feeAmount = sle->getFieldAmount(sfFeeAmount);
        (*sponsorAccSle)[sfBalance] += feeAmount;
    }

    auto const reserveSponsor = getLedgerEntryReserveSponsor(view, sle);
    adjustOwnerCount(view, sponsorAccSle, reserveSponsor, -1, j);

    view.update(sponsorAccSle);

    // delete sponsor node
    view.dirRemove(keylet::ownerDir(sponsorAccountID), (*sle)[sfOwnerNode], sle->key(), false);
    // delete sponsee node
    view.dirRemove(keylet::ownerDir(sponseeAccountID), (*sle)[sfSponseeNode], sle->key(), false);

    view.erase(sle);

    return tesSUCCESS;
}

}  // namespace xrpl
