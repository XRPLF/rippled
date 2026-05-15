#include <xrpl/tx/transactors/lending/LoanBrokerCoverDeposit.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <memory>

namespace xrpl {

bool
LoanBrokerCoverDeposit::checkExtraFeatures(PreflightContext const& ctx)
{
    return checkLendingProtocolDependencies(ctx.rules, ctx.tx);
}

NotTEC
LoanBrokerCoverDeposit::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfLoanBrokerID] == beast::kZero)
        return temINVALID;

    auto const dstAmount = ctx.tx[sfAmount];
    if (dstAmount <= beast::kZero)
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
    if (account != sleBroker->at(sfOwner))
    {
        JLOG(ctx.j.warn()) << "Account is not the owner of the LoanBroker.";
        return tecNO_PERMISSION;
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

    auto const pseudoAccountID = sleBroker->at(sfAccount);
    // Cannot transfer a non-transferable Asset
    if (auto const ret = canTransfer(ctx.view, vaultAsset, account, pseudoAccountID))
        return ret;
    // Cannot transfer a frozen Asset
    if (auto const ret = checkFrozen(ctx.view, account, vaultAsset))
        return ret;
    // Pseudo-account cannot receive if asset is deep frozen
    if (auto const ret = checkDeepFrozen(ctx.view, pseudoAccountID, vaultAsset))
        return ret;
    // Cannot transfer unauthorized asset
    if (auto const ret = requireAuth(ctx.view, vaultAsset, account, AuthType::StrongAuth))
        return ret;

    if (accountHolds(
            ctx.view,
            account,
            vaultAsset,
            FreezeHandling::ZeroIfFrozen,
            AuthHandling::ZeroIfUnauthorized,
            ctx.j,
            SpendableHandling::FullBalance) < amount)
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

    auto const vault = view().read(keylet::vault(broker->at(sfVaultID)));
    if (!vault)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const vaultAsset = vault->at(sfAsset);

    auto const brokerPseudoID = broker->at(sfAccount);

    // Transfer assets from depositor to pseudo-account.
    if (auto ter = accountSend(view(), account_, brokerPseudoID, amount, j_, WaiveTransferFee::Yes))
        return ter;

    // Increase the LoanBroker's CoverAvailable by Amount
    broker->at(sfCoverAvailable) += amount;
    view().update(broker);

    associateAsset(*broker, vaultAsset);

    return tesSUCCESS;
}

void
LoanBrokerCoverDeposit::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
LoanBrokerCoverDeposit::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

//------------------------------------------------------------------------------

}  // namespace xrpl
