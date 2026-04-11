#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/transactors/DeleteUtils.h>
#include <xrpl/tx/transactors/contract/ContractDelete.h>

namespace xrpl {

NotTEC
ContractDelete::preflight(PreflightContext const& ctx)
{
    auto const flags = ctx.tx.getFlags();
    if (flags & tfUniversalMask)
    {
        JLOG(ctx.j.trace()) << "ContractDelete: only tfUniversalMask is allowed.";
        return temINVALID_FLAG;
    }

    return tesSUCCESS;
}

TER
ContractDelete::preclaim(PreclaimContext const& ctx)
{
    AccountID const account = ctx.tx.getAccountID(sfAccount);
    AccountID const contractAccount =
        ctx.tx.isFieldPresent(sfContractAccount) ? ctx.tx.getAccountID(sfContractAccount) : account;

    auto const caSle = ctx.view.read(keylet::account(contractAccount));
    if (!caSle)
    {
        JLOG(ctx.j.trace()) << "ContractDelete: Account does not exist.";
        return terNO_ACCOUNT;
    }

    if (!caSle->isFieldPresent(sfContractID))
    {
        JLOG(ctx.j.trace()) << "ContractDelete: Account is not a smart "
                               "contract pseudo-account.";
        return tecNO_PERMISSION;
    }

    uint256 const contractID = caSle->getFieldH256(sfContractID);
    auto const contractSle = ctx.view.read(keylet::contract(contractID));
    if (!contractSle)
    {
        JLOG(ctx.j.trace()) << "ContractDelete: Contract does not exist.";
        return tecNO_TARGET;
    }

    if (contractSle->getAccountID(sfOwner) != account)
    {
        JLOG(ctx.j.trace()) << "ContractDelete: Cannot delete a contract that "
                               "does not belong to the account.";
        return tecNO_PERMISSION;
    }

    std::uint32_t const flags = contractSle->getFlags();

    // Check if the contract is undeletable.
    if (flags & tfUndeletable)
    {
        JLOG(ctx.j.trace()) << "ContractDelete: Contract is undeletable.";
        return tecNO_PERMISSION;
    }

    AccountID const owner = contractSle->getAccountID(sfOwner);
    if (auto const res = deletePreclaim(ctx, 0, account, owner, true); !isTesSuccess(res))
        return res;
    return tesSUCCESS;
}

TER
ContractDelete::deleteContract(
    ApplyView& view,
    std::shared_ptr<SLE> const& sle,
    AccountID const& account,
    beast::Journal j)
{
    if (!sle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (!view.dirRemove(keylet::ownerDir(account), (*sle)[sfOwnerNode], sle->key(), false))
    {
        // LCOV_EXCL_START
        JLOG(j.trace()) << "Unable to delete Delegate from owner.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    auto const sleOwner = view.peek(keylet::account(account));
    if (!sleOwner)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    adjustOwnerCount(view, sleOwner, -1, j);
    view.erase(sle);
    return tesSUCCESS;
}

TER
ContractDelete::doApply()
{
    AccountID const account = ctx_.tx.getAccountID(sfAccount);
    AccountID const contractAccount = ctx_.tx.isFieldPresent(sfContractAccount)
        ? ctx_.tx.getAccountID(sfContractAccount)
        : account;

    auto const caSle = ctx_.view().read(keylet::account(contractAccount));
    if (!caSle)
    {
        JLOG(j_.trace()) << "ContractModify: Account does not exist.";
        return tefBAD_LEDGER;
    }

    uint256 const contractID = caSle->getFieldH256(sfContractID);
    auto const contractSle = ctx_.view().read(keylet::contract(contractID));
    if (!contractSle)
    {
        JLOG(j_.trace()) << "ContractDelete: Contract does not exist.";
        return tecNO_TARGET;
    }

    // Lower the reference count of the ContractSource or remove the source from
    // the ledger.
    uint256 const contractHash = contractSle->getFieldH256(sfContractHash);
    auto oldSourceSle = ctx_.view().peek(keylet::contractSource(contractHash));
    if (oldSourceSle->getFieldU64(sfReferenceCount) == 1)
    {
        // NOTE: We dont adjust the owner count because ContractSource is an
        // unowned object.
        ctx_.view().erase(oldSourceSle);
    }
    else
    {
        oldSourceSle->setFieldU64(
            sfReferenceCount, oldSourceSle->getFieldU64(sfReferenceCount) - 1);
        ctx_.view().update(oldSourceSle);
    }

    AccountID const owner = contractSle->getAccountID(sfOwner);
    STAmount const contractBalance = (*caSle)[sfBalance];
    if (auto const res = deleteDoApply(ctx_, contractBalance, contractAccount, owner);
        !isTesSuccess(res))
        return res;

    return tesSUCCESS;
}

}  // namespace xrpl
