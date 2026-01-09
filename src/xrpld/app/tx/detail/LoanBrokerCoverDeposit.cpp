#include <xrpld/app/tx/detail/LoanBrokerCoverDeposit.h>
//
#include <xrpld/app/misc/LendingHelpers.h>

namespace xrpl {

bool
LoanBrokerCoverDeposit::checkExtraFeatures(PreflightContext const& ctx)
{
    return checkLendingProtocolDependencies(ctx);
}

NotTEC
LoanBrokerCoverDeposit::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfLoanBrokerID] == beast::zero)
        return temINVALID;

    auto const dstAmount = ctx.tx[sfAmount];
    if (dstAmount <= beast::zero)
        return temBAD_AMOUNT;

    if (!isLegalNet(dstAmount))
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

TER
LoanBrokerCoverDeposit::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;

    auto const account = tx[sfAccount];
    auto const brokerID = tx[sfLoanBrokerID];
    auto const amount = tx[sfAmount];

    auto const sleBroker = ctx.view.read(keylet::loanbroker(brokerID));
    if (!sleBroker)
    {
        JLOG(ctx.j.warn()) << "LoanBroker does not exist.";
        return tecNO_ENTRY;
    }
    auto const vault = ctx.view.read(keylet::vault(sleBroker->at(sfVaultID)));
    if (!vault)
    {
        // LCOV_EXCL_START
        JLOG(ctx.j.fatal()) << "Vault is missing for Broker " << brokerID;
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    auto const vaultAsset = vault->at(sfAsset);
    if (amount.asset() != vaultAsset)
        return tecWRONG_ASSET;

    // Only the broker owner can deposit cover
    if (account != sleBroker->at(sfOwner))
    {
        JLOG(ctx.j.warn()) << "Account is not the owner of the LoanBroker.";
        return tecNO_PERMISSION;
    }

    auto const pseudoAccountID = sleBroker->at(sfAccount);
    // Cannot transfer a non-transferable Asset
    if (auto const ret =
            canTransfer(ctx.view, vaultAsset, account, pseudoAccountID))
        return ret;
    // Cannot deposit if Asset is individually frozen for the depositor.
    // Global freeze does NOT prevent owner from depositing to their own broker
    // cover.
    if (isIndividualFrozen(ctx.view, account, vaultAsset))
        return vaultAsset.holds<Issue>() ? tecFROZEN : tecLOCKED;
    // Pseudo-account cannot receive if asset is deep frozen
    if (auto const ret = checkDeepFrozen(ctx.view, pseudoAccountID, vaultAsset))
        return ret;
    // Cannot transfer unauthorized asset
    if (auto const ret =
            requireAuth(ctx.view, vaultAsset, account, AuthType::StrongAuth))
        return ret;

    // We already checked individual freeze above, so use fhIGNORE_FREEZE here
    // to allow deposits under global freeze (individual freeze was checked
    // explicitly).
    if (accountSpendable(
            ctx.view,
            account,
            vaultAsset,
            FreezeHandling::fhIGNORE_FREEZE,
            AuthHandling::ahZERO_IF_UNAUTHORIZED,
            ctx.j) < amount)
        return tecINSUFFICIENT_FUNDS;

    return tesSUCCESS;
}

TER
LoanBrokerCoverDeposit::doApply()
{
    auto const& tx = ctx_.tx;

    auto const brokerID = tx[sfLoanBrokerID];
    auto const amount = tx[sfAmount];

    auto broker = view().peek(keylet::loanbroker(brokerID));
    if (!broker)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const brokerPseudoID = broker->at(sfAccount);

    // Transfer assets from depositor to pseudo-account.
    if (auto ter = accountSend(
            view(),
            account_,
            brokerPseudoID,
            amount,
            j_,
            WaiveTransferFee::Yes))
        return ter;

    // Increase the LoanBroker's CoverAvailable by Amount
    broker->at(sfCoverAvailable) += amount;
    view().update(broker);

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

}  // namespace xrpl
