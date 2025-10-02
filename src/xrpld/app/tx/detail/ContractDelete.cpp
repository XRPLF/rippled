//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <xrpld/app/misc/DeleteUtils.h>
#include <xrpld/app/tx/detail/ContractDelete.h>

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
ContractDelete::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureSmartContract))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto const flags = ctx.tx.getFlags();
    if (flags & tfUniversalMask)
    {
        JLOG(ctx.j.error())
            << "ContractDelete: only tfUniversalMask is allowed.";
        return temINVALID_FLAG;
    }

    return preflight2(ctx);
}

TER
ContractDelete::preclaim(PreclaimContext const& ctx)
{
    AccountID const account = ctx.tx.getAccountID(sfAccount);
    AccountID const contractAccount = ctx.tx.isFieldPresent(sfContractAccount)
        ? ctx.tx.getAccountID(sfContractAccount)
        : account;

    auto const caSle = ctx.view.read(keylet::account(contractAccount));
    if (!caSle)
    {
        JLOG(ctx.j.error()) << "ContractDelete: Account does not exist.";
        return terNO_ACCOUNT;
    }

    if (!caSle->isFieldPresent(sfContractID))
    {
        JLOG(ctx.j.error()) << "ContractDelete: Account is not a smart "
                               "contract pseudo-account.";
        return tecNO_PERMISSION;
    }

    uint256 const contractID = caSle->getFieldH256(sfContractID);
    auto const contractSle = ctx.view.read(keylet::contract(contractID));
    if (!contractSle)
    {
        JLOG(ctx.j.error()) << "ContractDelete: Contract does not exist.";
        return tecNO_TARGET;
    }

    if (contractSle->getAccountID(sfAccount) != account)
    {
        JLOG(ctx.j.error()) << "ContractDelete: Cannot delete a contract that "
                               "does not belong to the account.";
        return tecNO_PERMISSION;
    }

    std::uint32_t flags = contractSle->getFlags();

    // Check if the contract is undeletable.
    if (flags & tfUndeletable)
    {
        JLOG(ctx.j.error()) << "ContractDelete: Contract is undeletable.";
        return tecNO_PERMISSION;
    }

    AccountID const owner = contractSle->getAccountID(sfOwner);
    if (auto const res = deletePreclaim(ctx, 0, account, owner, true);
        !isTesSuccess(res))
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

    if (!view.dirRemove(
            keylet::ownerDir(account), (*sle)[sfOwnerNode], sle->key(), false))
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Unable to delete Delegate from owner.";
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
        JLOG(j_.error()) << "ContractModify: Account does not exist.";
        return tefBAD_LEDGER;
    }

    uint256 const contractID = caSle->getFieldH256(sfContractID);
    auto const contractSle = ctx_.view().peek(keylet::contract(contractID));
    if (!contractSle)
    {
        JLOG(j_.error()) << "ContractDelete: Contract does not exist.";
        return tecNO_TARGET;
    }

    // Lower the reference count of the ContractSource or remove the source from
    // the ledger.
    uint256 const contractHash = contractSle->getFieldH256(sfContractHash);
    auto oldSourceSle = ctx_.view().peek(keylet::contractSource(contractHash));
    if (oldSourceSle->getFieldU64(sfReferenceCount) == 1)
    {
        ctx_.view().erase(oldSourceSle);
    }
    else
    {
        oldSourceSle->setFieldU64(
            sfReferenceCount, oldSourceSle->getFieldU64(sfReferenceCount) - 1);
        ctx_.view().update(oldSourceSle);
    }

    AccountID const owner = contractSle->getAccountID(sfOwner);
    if (auto const res =
            deleteDoApply(ctx_, mSourceBalance, contractAccount, owner);
        !isTesSuccess(res))
        return res;

    return tesSUCCESS;
}

}  // namespace ripple
