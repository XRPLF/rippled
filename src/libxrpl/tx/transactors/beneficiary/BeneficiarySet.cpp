#include <xrpl/tx/transactors/beneficiary/BeneficiarySet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

#include <memory>

namespace xrpl {

NotTEC
BeneficiarySet::preflight(PreflightContext const& ctx)
{
    auto const beneficiary = ctx.tx[~sfBeneficiary];
    auto const timeLock = ctx.tx[~sfTimeLock];

    // The two fields describe one designation, so they arrive together or not
    // at all; neither alone is a meaningful instruction.
    if (beneficiary.has_value() != timeLock.has_value())
    {
        JLOG(ctx.j.trace()) << "BeneficiarySet: Beneficiary and TimeLock disagree";
        return temMALFORMED;
    }

    if (!beneficiary)
        return tesSUCCESS;

    if (*beneficiary == ctx.tx[sfAccount])
    {
        JLOG(ctx.j.trace()) << "BeneficiarySet: the beneficiary is the account";
        return temMALFORMED;
    }

    if (*beneficiary == beast::kZero)
    {
        JLOG(ctx.j.trace()) << "BeneficiarySet: the beneficiary is the zero account";
        return temMALFORMED;
    }

    // Zero would make the designation invocable in the ledger that set it.
    if (*timeLock == 0 || *timeLock > kMaxBeneficiaryTimeLock)
    {
        JLOG(ctx.j.trace()) << "BeneficiarySet: the time lock is out of range";
        return temMALFORMED;
    }

    return tesSUCCESS;
}

TER
BeneficiarySet::preclaim(PreclaimContext const& ctx)
{
    auto const beneficiary = ctx.tx[~sfBeneficiary];

    if (!beneficiary)
    {
        if (!ctx.view.exists(keylet::beneficiary(ctx.tx[sfAccount])))
            return tecNO_ENTRY;

        return tesSUCCESS;
    }

    if (!ctx.view.exists(keylet::account(*beneficiary)))
    {
        JLOG(ctx.j.trace()) << "BeneficiarySet: the beneficiary does not exist";
        return tecNO_TARGET;
    }

    return tesSUCCESS;
}

TER
BeneficiarySet::doApply()
{
    auto const sleAccount = view().peek(keylet::account(accountID_));
    if (!sleAccount)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    Keylet const beneficiaryKeylet = keylet::beneficiary(accountID_);
    auto const sle = view().peek(beneficiaryKeylet);

    // No Beneficiary field means clear: the entry goes, and so does the
    // timestamp, leaving the account as it was before any designation.
    if (!ctx_.tx.isFieldPresent(sfBeneficiary))
    {
        if (!sle)
            return tecNO_ENTRY;  // LCOV_EXCL_LINE

        if (!view().dirRemove(keylet::ownerDir(accountID_), (*sle)[sfOwnerNode], sle->key(), true))
        {
            // LCOV_EXCL_START
            JLOG(j_.fatal()) << "BeneficiarySet: cannot remove the entry from the owner directory";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }

        decreaseOwnerCountForObject(view(), sleAccount, sle, 1, j_);
        view().erase(sle);

        sleAccount->makeFieldAbsent(sfLastInteraction);
        view().update(sleAccount);
        return tesSUCCESS;
    }

    if (sle)
    {
        (*sle)[sfBeneficiary] = ctx_.tx[sfBeneficiary];
        (*sle)[sfTimeLock] = ctx_.tx[sfTimeLock];
        view().update(sle);
        return tesSUCCESS;
    }

    {
        auto const balance = STAmount((*sleAccount)[sfBalance]).xrp();
        auto const reserve = accountReserve(view(), sleAccount, j_, {.ownerCountDelta = 1});
        if (balance < reserve)
            return tecINSUFFICIENT_RESERVE;
    }

    auto const sleNew = std::make_shared<SLE>(beneficiaryKeylet);
    (*sleNew)[sfAccount] = accountID_;
    (*sleNew)[sfBeneficiary] = ctx_.tx[sfBeneficiary];
    (*sleNew)[sfTimeLock] = ctx_.tx[sfTimeLock];

    view().insert(sleNew);

    auto const page =
        view().dirInsert(keylet::ownerDir(accountID_), sleNew->key(), describeOwnerDir(accountID_));
    if (!page)
        return tecDIR_FULL;  // LCOV_EXCL_LINE
    (*sleNew)[sfOwnerNode] = *page;

    increaseOwnerCount(view(), sleAccount, {}, 1, j_);

    // Transactor::apply already stamped the field if it was present; this is the
    // first time it is not, so the timer starts here.
    sleAccount->setFieldU32(sfLastInteraction, view().parentCloseTime().time_since_epoch().count());
    view().update(sleAccount);

    return tesSUCCESS;
}

void
BeneficiarySet::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // The ValidBeneficiary check covers this transaction.
}

bool
BeneficiarySet::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // The ValidBeneficiary check covers this transaction.
    return true;
}

}  // namespace xrpl
