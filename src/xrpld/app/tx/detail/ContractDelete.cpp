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

#include <xrpld/app/tx/detail/ContractDelete.h>
#include <xrpld/ledger/View.h>

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
    AccountID const contractAccount = ctx.tx.getAccountID(sfContractAccount);

    auto const accountSle = ctx.view.read(keylet::account(account));
    if (!accountSle)
    {
        JLOG(ctx.j.error()) << "ContractDelete: Account does not exist.";
        return terNO_ACCOUNT;
    }

    if (!accountSle->isFieldPresent(sfContractID))
    {
        JLOG(ctx.j.error()) << "ContractDelete: Account is not a smart "
                               "contract pseudo-account.";
        return tecNO_PERMISSION;
    }

    auto const contractSle = ctx.view.read(keylet::contract(contractAccount));
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
    return tesSUCCESS;
}

TER
ContractDelete::doApply()
{
    return tesSUCCESS;
}

}  // namespace ripple
