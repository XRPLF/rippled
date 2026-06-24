#include <xrpl/tx/transactors/sponsor/SponsorshipSet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <memory>

namespace xrpl {

std::uint32_t
SponsorshipSet::getFlagsMask(PreflightContext const& ctx)
{
    return tfSponsorshipSetMask;
}

NotTEC
SponsorshipSet::preflight(PreflightContext const& ctx)
{
    if (ctx.tx.isFlag(tfSponsorshipSetRequireSignForFee) &&
        ctx.tx.isFlag(tfSponsorshipClearRequireSignForFee))
        return temINVALID_FLAG;
    if (ctx.tx.isFlag(tfSponsorshipSetRequireSignForReserve) &&
        ctx.tx.isFlag(tfSponsorshipClearRequireSignForReserve))
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

    if (ctx.tx.isFlag(tfDeleteObject))
    {
        // can not combine with any modification flags when deleting
        constexpr std::uint32_t kModifyFlags = tfSponsorshipSetRequireSignForFee |
            tfSponsorshipSetRequireSignForReserve | tfSponsorshipClearRequireSignForFee |
            tfSponsorshipClearRequireSignForReserve;

        if ((ctx.tx.getFlags() & kModifyFlags) != 0u)
            return temINVALID_FLAG;

        // can not include these fields when deleting
        if (ctx.tx.isFieldPresent(sfFeeAmount) || ctx.tx.isFieldPresent(sfRemainingOwnerCount) ||
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

TER
SponsorshipSet::preclaim(PreclaimContext const& ctx)
{
    auto const sponsorID = ctx.tx[~sfCounterpartySponsor].value_or(ctx.tx[sfAccount]);
    auto const sponseeID = ctx.tx[~sfSponsee].value_or(ctx.tx[sfAccount]);

    if (sponseeID == sponsorID)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // check Sponsor
    auto const sponsorSle = ctx.view.read(keylet::account(sponsorID));
    if (!sponsorSle)
        return tecNO_DST;

    // check Sponsee
    auto const sponseeSle = ctx.view.read(keylet::account(sponseeID));
    if (!sponseeSle)
        return tecNO_DST;

    // Pseudo accounts cannot be sponsors or sponsees
    if (isPseudoAccount(sponsorSle) || isPseudoAccount(sponseeSle))
        return tecNO_PERMISSION;

    // check if object exists
    auto const sponsorshipSle = ctx.view.read(keylet::sponsorship(sponsorID, sponseeID));

    if (ctx.tx.isFlag(tfDeleteObject) && !sponsorshipSle)
        return tecNO_ENTRY;

    return tesSUCCESS;
}

static TER
deleteSponsorshipHlp(
    ApplyView& view,
    SLE::ref sponsorSle,
    SLE::ref sponsorshipSle,
    beast::Journal j)
{
    if (!sponsorSle || !sponsorshipSle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const sponsorID = (*sponsorshipSle)[sfOwner];
    auto const sponseeID = (*sponsorshipSle)[sfSponsee];

    XRPL_ASSERT(
        sponsorID == sponsorSle->at(sfAccount),
        "deleteSponsorshipHlp: sponsorID == sponsorSle->at(sfAccount)");

    // The reserve for the Sponsorship object is held by the sponsor (Owner).

    if (!view.dirRemove(
            keylet::ownerDir(sponsorID),
            (*sponsorshipSle)[sfOwnerNode],
            sponsorshipSle->key(),
            false))
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Unable to delete Sponsorship from sponsor.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }
    if (!view.dirRemove(
            keylet::ownerDir(sponseeID),
            (*sponsorshipSle)[sfSponseeNode],
            sponsorshipSle->key(),
            false))
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Unable to delete Sponsorship from sponsee.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    adjustOwnerCountDeleteObj(view, sponsorSle, sponsorshipSle, -1, j);

    // transfer feeAmount back to the sponsor
    if (sponsorshipSle->isFieldPresent(sfFeeAmount))
        (*sponsorSle)[sfBalance] += sponsorshipSle->getFieldAmount(sfFeeAmount);

    view.erase(sponsorshipSle);

    return tesSUCCESS;
}

TER
SponsorshipSet::doApply()
{
    auto const sponsorID = ctx_.tx[~sfCounterpartySponsor].value_or(accountID_);
    auto const sponseeID = ctx_.tx[~sfSponsee].value_or(accountID_);

    if (sponseeID == sponsorID)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const sponsorSle = ctx_.view().peek(keylet::account(sponsorID));
    if (!sponsorSle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (!ctx_.view().exists(keylet::account(sponseeID)))
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const sponsorKeylet = keylet::sponsorship(sponsorID, sponseeID);
    auto const sponsorshipSle = ctx_.view().peek(sponsorKeylet);

    if (ctx_.tx.isFlag(tfDeleteObject))
    {
        // Delete
        if (!sponsorshipSle)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        return deleteSponsorshipHlp(ctx_.view(), sponsorSle, sponsorshipSle, ctx_.journal);
    }

    auto const feeAmount = ctx_.tx[~sfFeeAmount];
    auto const maxFee = ctx_.tx[~sfMaxFee];
    auto const remainingOwnerCount = ctx_.tx[~sfRemainingOwnerCount];

    auto txSponsorSle = getTxReserveSponsor(view(), ctx_.tx, sponsorID);
    if (!sponsorshipSle)
    {
        // Create
        auto newSle = std::make_shared<SLE>(sponsorKeylet);

        (*newSle)[sfOwner] = sponsorID;
        (*newSle)[sfSponsee] = sponseeID;

        STAmount const balanceAdj = feeAmount ? *feeAmount : STAmount();
        if (auto const ret = checkXrpBalance(
                ctx_.view(), ctx_.tx, sponsorSle, txSponsorSle, 1, -balanceAdj.xrp(), ctx_.journal);
            !isTesSuccess(ret))
            return tecUNFUNDED;

        if (feeAmount && *feeAmount > XRPAmount(0))
        {
            (*sponsorSle)[sfBalance] -= *feeAmount;
            (*newSle)[sfFeeAmount] = *feeAmount;
        }

        if (maxFee && *maxFee > XRPAmount(0))
            (*newSle)[sfMaxFee] = *maxFee;
        if (remainingOwnerCount && *remainingOwnerCount > 0)
            (*newSle)[sfRemainingOwnerCount] = *remainingOwnerCount;

        auto flags = 0;
        if (ctx_.tx.isFlag(tfSponsorshipSetRequireSignForFee))
            flags |= lsfSponsorshipRequireSignForFee;

        if (ctx_.tx.isFlag(tfSponsorshipSetRequireSignForReserve))
            flags |= lsfSponsorshipRequireSignForReserve;

        (*newSle)[sfFlags] = flags;

        auto const sponsorPage = view().dirInsert(
            keylet::ownerDir(sponsorID), sponsorKeylet, describeOwnerDir(sponsorID));
        if (!sponsorPage)
            return tecDIR_FULL;  // LCOV_EXCL_LINE
        (*newSle)[sfOwnerNode] = *sponsorPage;

        auto const sponseePage = view().dirInsert(
            keylet::ownerDir(sponseeID), sponsorKeylet, describeOwnerDir(sponseeID));
        if (!sponseePage)
            return tecDIR_FULL;  // LCOV_EXCL_LINE
        (*newSle)[sfSponseeNode] = *sponseePage;

        // NOLINTNEXTLINE(readability-suspicious-call-argument)
        adjustOwnerCount(view(), sponsorSle, txSponsorSle, 1, ctx_.journal);
        addSponsorToLedgerEntry(newSle, txSponsorSle);

        ctx_.view().insert(newSle);
        return tesSUCCESS;
    }

    // Update
    if (feeAmount)
    {
        auto const currentFeeAmount = (*sponsorshipSle)[~sfFeeAmount].valueOr(XRPAmount(0));
        auto const feeAmountDelta = XRPAmount(*feeAmount - currentFeeAmount);

        if (auto const ret = checkXrpBalance(
                ctx_.view(), ctx_.tx, sponsorSle, txSponsorSle, 0, -feeAmountDelta, ctx_.journal);
            !isTesSuccess(ret))
            return tecUNFUNDED;

        // transfer feeAmount to ledger entry
        if (feeAmountDelta != beast::kZero)
        {
            (*sponsorSle)[sfBalance] -= feeAmountDelta;

            if (*feeAmount == XRPAmount(0))
            {
                (*sponsorshipSle).makeFieldAbsent(sfFeeAmount);
            }
            else
            {
                (*sponsorshipSle).setFieldAmount(sfFeeAmount, *feeAmount);
            }
        }
    }

    if (maxFee)
    {
        if (*maxFee == XRPAmount(0))
        {
            (*sponsorshipSle).makeFieldAbsent(sfMaxFee);
        }
        else
        {
            (*sponsorshipSle)[sfMaxFee] = *maxFee;
        }
    }

    if (remainingOwnerCount)
        sponsorshipSle->at(sfRemainingOwnerCount) = *remainingOwnerCount;

    // update Flags
    auto flags = sponsorshipSle->getFieldU32(sfFlags);
    if (ctx_.tx.isFlag(tfSponsorshipSetRequireSignForFee))
        flags |= lsfSponsorshipRequireSignForFee;

    if (ctx_.tx.isFlag(tfSponsorshipClearRequireSignForFee))
        flags &= ~lsfSponsorshipRequireSignForFee;

    if (ctx_.tx.isFlag(tfSponsorshipSetRequireSignForReserve))
        flags |= lsfSponsorshipRequireSignForReserve;

    if (ctx_.tx.isFlag(tfSponsorshipClearRequireSignForReserve))
        flags &= ~lsfSponsorshipRequireSignForReserve;

    if (flags != (*sponsorshipSle)[sfFlags])
        (*sponsorshipSle)[sfFlags] = flags;

    view().update(sponsorshipSle);

    return tesSUCCESS;
}

void
SponsorshipSet::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
}

bool
SponsorshipSet::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
