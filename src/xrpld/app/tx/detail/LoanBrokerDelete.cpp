#include <xrpld/app/tx/detail/LoanBrokerDelete.h>
//
#include <xrpld/app/misc/LendingHelpers.h>

namespace ripple {

bool
LoanBrokerDelete::checkExtraFeatures(PreflightContext const& ctx)
{
    return checkLendingProtocolDependencies(ctx);
}

NotTEC
LoanBrokerDelete::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfLoanBrokerID] == beast::zero)
        return temINVALID;

    return tesSUCCESS;
}

TER
LoanBrokerDelete::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;

    auto const account = tx[sfAccount];
    auto const brokerID = tx[sfLoanBrokerID];

    auto const sleBroker = ctx.view.read(keylet::loanbroker(brokerID));
    if (!sleBroker)
    {
        JLOG(ctx.j.warn()) << "LoanBroker does not exist.";
        return tecNO_ENTRY;
    }

    auto const brokerOwner = sleBroker->at(sfOwner);

    if (account != brokerOwner)
    {
        JLOG(ctx.j.warn()) << "Account is not the owner of the LoanBroker.";
        return tecNO_PERMISSION;
    }
    if (auto const ownerCount = sleBroker->at(sfOwnerCount); ownerCount != 0)
    {
        JLOG(ctx.j.warn()) << "LoanBrokerDelete: Owner count is " << ownerCount;
        return tecHAS_OBLIGATIONS;
    }
    if (auto const debtTotal = sleBroker->at(sfDebtTotal);
        debtTotal != beast::zero)
    {
        // Any remaining debt should have been wiped out by the last Loan
        // Delete. This check is purely defensive.
        auto const vault =
            ctx.view.read(keylet::vault(sleBroker->at(sfVaultID)));
        if (!vault)
            return tefINTERNAL;  // LCOV_EXCL_LINE
        auto const asset = vault->at(sfAsset);
        auto const scale = getVaultScale(vault);

        auto const rounded =
            roundToAsset(asset, debtTotal, scale, Number::towards_zero);

        if (rounded != beast::zero)
        {
            // LCOV_EXCL_START
            JLOG(ctx.j.warn()) << "LoanBrokerDelete: Debt total is "
                               << debtTotal << ", which rounds to " << rounded;
            return tecHAS_OBLIGATIONS;
            // LCOV_EXCL_START
        }
    }

    auto const vault = ctx.view.read(keylet::vault(sleBroker->at(sfVaultID)));
    if (!vault)
    {
        // LCOV_EXCL_START
        JLOG(ctx.j.fatal()) << "Vault is missing for Broker " << brokerID;
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    Asset const asset = vault->at(sfAsset);

    auto const coverAvailable =
        STAmount{asset, sleBroker->at(sfCoverAvailable)};
    // If there are assets in the cover, broker will receive them on deletion.
    // So we need to check if the broker owner is deep frozen for that asset.
    if (coverAvailable > beast::zero)
    {
        if (auto const ret = checkDeepFrozen(ctx.view, brokerOwner, asset))
        {
            JLOG(ctx.j.warn()) << "Broker owner account is frozen.";
            return ret;
        }
    }

    return tesSUCCESS;
}

TER
LoanBrokerDelete::doApply()
{
    auto const& tx = ctx_.tx;

    auto const brokerID = tx[sfLoanBrokerID];

    // Delete the loan broker
    auto broker = view().peek(keylet::loanbroker(brokerID));
    if (!broker)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    auto const vaultID = broker->at(sfVaultID);
    auto const sleVault = view().read(keylet::vault(vaultID));
    if (!sleVault)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    auto const vaultPseudoID = sleVault->at(sfAccount);
    auto const vaultAsset = sleVault->at(sfAsset);

    auto const brokerPseudoID = broker->at(sfAccount);

    if (!view().dirRemove(
            keylet::ownerDir(account_),
            broker->at(sfOwnerNode),
            broker->key(),
            false))
    {
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    }
    if (!view().dirRemove(
            keylet::ownerDir(vaultPseudoID),
            broker->at(sfVaultNode),
            broker->key(),
            false))
    {
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    }

    {
        auto const coverAvailable =
            STAmount{vaultAsset, broker->at(sfCoverAvailable)};
        if (auto const ter = accountSend(
                view(),
                brokerPseudoID,
                account_,
                coverAvailable,
                j_,
                WaiveTransferFee::Yes))
            return ter;
    }

    if (auto ter = removeEmptyHolding(view(), brokerPseudoID, vaultAsset, j_))
        return ter;

    auto brokerPseudoSLE = view().peek(keylet::account(brokerPseudoID));
    if (!brokerPseudoSLE)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    // Making the payment and removing the empty holding should have deleted any
    // obligations associated with the broker or broker pseudo-account.
    if (*brokerPseudoSLE->at(sfBalance))
    {
        JLOG(j_.warn()) << "LoanBrokerDelete: Pseudo-account has a balance";
        return tecHAS_OBLIGATIONS;  // LCOV_EXCL_LINE
    }
    if (brokerPseudoSLE->at(sfOwnerCount) != 0)
    {
        JLOG(j_.warn())
            << "LoanBrokerDelete: Pseudo-account still owns objects";
        return tecHAS_OBLIGATIONS;  // LCOV_EXCL_LINE
    }
    if (auto const directory = keylet::ownerDir(brokerPseudoID);
        view().read(directory))
    {
        JLOG(j_.warn()) << "LoanBrokerDelete: Pseudo-account has a directory";
        return tecHAS_OBLIGATIONS;  // LCOV_EXCL_LINE
    }

    view().erase(brokerPseudoSLE);

    view().erase(broker);

    {
        auto owner = view().peek(keylet::account(account_));
        if (!owner)
            return tefBAD_LEDGER;  // LCOV_EXCL_LINE

        // Decreases the owner count by two: one for the LoanBroker object, and
        // one for the pseudo-account.
        adjustOwnerCount(view(), owner, -2, j_);
    }

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

}  // namespace ripple
