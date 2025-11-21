#include <xrpld/app/misc/DelegateUtils.h>
#include <xrpld/app/tx/detail/SponsorshipSet.h>

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

std::uint32_t
SponsorshipSet::getFlagsMask(PreflightContext const& ctx)
{
    return tfSponsorshipSetMask;
}

NotTEC
SponsorshipSet::preflight(PreflightContext const& ctx)
{
    // check Flags
    {
        if (ctx.tx.isFlag(tfSponsorshipSetRequireSignForFee) &&
            ctx.tx.isFlag(tfSponsorshipClearRequireSignForFee))
            return temINVALID_FLAG;

        if (ctx.tx.isFlag(tfSponsorshipSetRequireSignForReserve) &&
            ctx.tx.isFlag(tfSponsorshipClearRequireSignForReserve))
            return temINVALID_FLAG;

        if (ctx.tx.isFlag(tfDeleteObject))
        {
            // check Flags
            if (ctx.tx.getFlags() &
                (tfSponsorshipSetRequireSignForFee |
                 tfSponsorshipSetRequireSignForReserve |
                 tfSponsorshipClearRequireSignForFee |
                 tfSponsorshipClearRequireSignForReserve))
                return temINVALID_FLAG;
        }
    }

    if ((ctx.tx.isFieldPresent(sfSponsorAccount) &&
         ctx.tx.isFieldPresent(sfSponsee)) ||
        (!ctx.tx.isFieldPresent(sfSponsorAccount) &&
         !ctx.tx.isFieldPresent(sfSponsee)))
        return temMALFORMED;

    auto const sponsor = ctx.tx.isFieldPresent(sfSponsorAccount)
        ? ctx.tx.getAccountID(sfSponsorAccount)
        : ctx.tx.getAccountID(sfAccount);
    auto const sponsee = ctx.tx.isFieldPresent(sfSponsee)
        ? ctx.tx.getAccountID(sfSponsee)
        : ctx.tx.getAccountID(sfAccount);

    if (sponsee == sponsor)
        return temMALFORMED;

    auto const checkOptionalAmountField = [&](SField const& field) -> NotTEC {
        if (!ctx.tx.isFieldPresent(field))
            return tesSUCCESS;

        auto const amount = ctx.tx.getFieldAmount(field);

        if (!isXRP(amount))
            return temBAD_AMOUNT;

        if (amount.xrp().drops() <= 0)
            return temBAD_AMOUNT;

        return tesSUCCESS;
    };

    if (auto const ret = checkOptionalAmountField(sfFeeAmount);
        !isTesSuccess(ret))
        return ret;

    if (auto const ret = checkOptionalAmountField(sfMaxFee); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.isFieldPresent(sfReserveCount))
    {
        auto const reserveCount = ctx.tx.getFieldU32(sfReserveCount);
        if (reserveCount < 1)
            return temMALFORMED;
    }

    if (ctx.tx.isFlag(tfDeleteObject))
    {
        if (ctx.tx.isFieldPresent(sfFeeAmount) ||
            ctx.tx.isFieldPresent(sfReserveCount) ||
            ctx.tx.isFieldPresent(sfMaxFee))
            return temMALFORMED;
    }
    else
    {
        if (!ctx.tx.isFieldPresent(sfSponsee))
            return temMALFORMED;
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

    std::unordered_set<GranularPermissionType> granularPermissions;
    loadGranularPermission(sle, ttSPONSORSHIP_SET, granularPermissions);

    auto const sponsoringFee = tx.isFieldPresent(sfFeeAmount) ||
        tx.isFieldPresent(sfMaxFee) ||
        tx.isFlag(tfSponsorshipSetRequireSignForFee);
    auto const sponsoringReserve = tx.isFieldPresent(sfReserveCount) ||
        tx.isFlag(tfSponsorshipSetRequireSignForReserve);

    if (sponsoringFee && !granularPermissions.contains(SponsorFee))
        return terNO_DELEGATE_PERMISSION;

    if (sponsoringReserve && !granularPermissions.contains(SponsorReserve))
        return terNO_DELEGATE_PERMISSION;

    return tesSUCCESS;
}

TER
SponsorshipSet::preclaim(PreclaimContext const& ctx)
{
    auto const sponsor = ctx.tx.isFieldPresent(sfSponsorAccount)
        ? ctx.tx.getAccountID(sfSponsorAccount)
        : ctx.tx.getAccountID(sfAccount);
    auto const sponsee = ctx.tx.isFieldPresent(sfSponsee)
        ? ctx.tx.getAccountID(sfSponsee)
        : ctx.tx.getAccountID(sfAccount);

    if (sponsee == sponsor)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // check Sponsee
    auto const sponseeSle = ctx.view.read(keylet::account(sponsee));
    if (!sponseeSle)
        return tecNO_DST;

    // check if object exists
    auto const sponsorObjSle = ctx.view.read(keylet::sponsor(sponsor, sponsee));

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
    auto const sponseeAcc = ctx_.tx.isFieldPresent(sfSponsee)
        ? ctx_.tx.getAccountID(sfSponsee)
        : ctx_.tx.getAccountID(sfAccount);

    auto const sponsorAcc = ctx_.tx.isFieldPresent(sfSponsorAccount)
        ? ctx_.tx.getAccountID(sfSponsorAccount)
        : account_;

    if (sponseeAcc == sponsorAcc)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const keylet = keylet::sponsor(sponsorAcc, sponseeAcc);

    auto const sponsorAccSle = ctx_.view().peek(keylet::account(sponsorAcc));
    if (!sponsorAccSle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const sponsorObjSle = ctx_.view().peek(keylet);

    if (ctx_.tx.isFlag(tfDeleteObject))
    {
        // Delete
        if (!sponsorObjSle)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        auto const sponsor =
            getLedgerEntryReserveSponsor(ctx_.view(), sponsorObjSle);
        adjustOwnerCount(ctx_.view(), sponsorAccSle, sponsor, -1, ctx_.journal);

        ctx_.view().dirRemove(
            keylet::ownerDir(sponsorAcc),
            (*sponsorObjSle)[sfOwnerNode],
            sponsorObjSle->key(),
            false);
        ctx_.view().dirRemove(
            keylet::ownerDir(sponseeAcc),
            (*sponsorObjSle)[sfSponseeNode],
            sponsorObjSle->key(),
            false);

        // transfer feeAmount from ledger entry
        auto const feeAmount = sponsorObjSle->getFieldAmount(sfFeeAmount);
        (*sponsorAccSle)[sfBalance] += feeAmount;

        ctx_.view().erase(sponsorObjSle);

        return tesSUCCESS;
    }

    auto const feeAmount = ctx_.tx[~sfFeeAmount];
    auto const maxFee = ctx_.tx[~sfMaxFee];
    auto const reserveCount = ctx_.tx[~sfReserveCount];

    auto reserveSponsorAccSle = getTxReserveSponsor(view(), ctx_.tx);

    if (!sponsorObjSle)
    {
        // Create
        auto newSle = std::make_shared<SLE>(keylet);

        if (auto const ret = checkInsufficientReserve(
                ctx_.view(),
                ctx_.tx,
                sponsorAccSle,
                mPriorBalance,
                reserveSponsorAccSle,
                1);
            !isTesSuccess(ret))
            return tecUNFUNDED;

        (*newSle)[sfOwner] = sponsorAcc;
        (*newSle)[sfSponsee] = sponseeAcc;
        if (feeAmount)
        {
            (*sponsorAccSle)[sfBalance] -= *feeAmount;
            (*newSle)[sfFeeAmount] = *feeAmount;
        }
        if (maxFee)
        {
            (*newSle)[sfMaxFee] = *maxFee;
        }
        if (reserveCount)
        {
            (*newSle)[sfReserveCount] = *reserveCount;
        }

        auto flags = 0;
        if (ctx_.tx.isFlag(tfSponsorshipSetRequireSignForFee))
            flags |= lsfSponsorshipRequireSignForFee;

        if (ctx_.tx.isFlag(tfSponsorshipSetRequireSignForReserve))
            flags |= lsfSponsorshipRequireSignForReserve;

        (*newSle)[sfFlags] = flags;

        auto const sponsorPage = view().dirInsert(
            keylet::ownerDir(sponsorAcc), keylet, describeOwnerDir(sponsorAcc));
        (*newSle)[sfOwnerNode] = *sponsorPage;

        auto const sponseePage = view().dirInsert(
            keylet::ownerDir(sponseeAcc), keylet, describeOwnerDir(sponseeAcc));
        (*newSle)[sfSponseeNode] = *sponseePage;

        auto viewJ = ctx_.app.journal("View");

        adjustOwnerCount(
            view(), ctx_.tx, sponsorAccSle, reserveSponsorAccSle, 1, viewJ);
        addSponsorToLedgerEntry(newSle, reserveSponsorAccSle);

        ctx_.view().insert(newSle);
        return tesSUCCESS;
    }

    // Update
    if (feeAmount)
    {
        // transfer feeAmount to ledger entry
        (*sponsorAccSle)[sfBalance] -= *feeAmount;
        if ((*sponsorObjSle).isFieldPresent(sfFeeAmount))
        {
            auto const oldFeeAmount =
                (*sponsorObjSle).getFieldAmount(sfFeeAmount);
            auto const newFeeAmount = oldFeeAmount + *feeAmount;
            (*sponsorObjSle).setFieldAmount(sfFeeAmount, newFeeAmount);
        }
        else
        {
            (*sponsorObjSle).setFieldAmount(sfFeeAmount, *feeAmount);
        }
    }

    if (maxFee)
    {
        (*sponsorObjSle)[sfMaxFee] = *maxFee;
    }

    if (reserveCount)
        (*sponsorObjSle)[sfReserveCount] =
            (*sponsorObjSle).getFieldU32(sfReserveCount) + *reserveCount;

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

    ctx_.view().update(sponsorObjSle);

    return tesSUCCESS;
}

TER
SponsorshipSet::deleteSponsorship(
    ApplyView& view,
    std::shared_ptr<SLE> const& sle,
    beast::Journal j)
{
    auto const sponsor = sle->getAccountID(sfOwner);
    auto const sponsee = sle->getAccountID(sfSponsee);

    // adjust balance
    auto const sponsorAccSle = view.peek(keylet::account(sponsor));
    if (!sponsorAccSle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const feeAmount = sle->getFieldAmount(sfFeeAmount);
    (*sponsorAccSle)[sfBalance] += feeAmount;

    auto const reserveSponsor = getLedgerEntryReserveSponsor(view, sle);
    adjustOwnerCount(view, sponsorAccSle, reserveSponsor, -1, j);

    view.update(sponsorAccSle);

    // delete sponsor node
    view.dirRemove(
        keylet::ownerDir(sponsor), (*sle)[sfOwnerNode], sle->key(), false);
    // delete sponsee node
    view.dirRemove(
        keylet::ownerDir(sponsee), (*sle)[sfSponseeNode], sle->key(), false);

    view.erase(sle);

    return tesSUCCESS;
}

}  // namespace ripple
