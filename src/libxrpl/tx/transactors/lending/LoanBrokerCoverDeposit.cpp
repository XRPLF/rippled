#include <xrpl/tx/transactors/lending/LoanBrokerCoverDeposit.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
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
    if (ctx.tx[sfLoanBrokerID] == beast::kZERO)
        return temINVALID;

    auto const dstAmount = ctx.tx[sfAmount];
    if (dstAmount <= beast::kZERO)
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

    // Post-fixCleanup3_2_0: reject deposits whose amount is sub-ULP at
    // sfCoverAvailable's scale (which mirrors the broker pseudo-account trust
    // line). Without this check, a sub-ULP cover deposit silently succeeds:
    // depositor's trust line debits cleanly but broker pseudo balance and
    // sfCoverAvailable both stay unchanged (canonicalization rounds back to
    // zero), the two rails remain consistent so no broker invariant catches
    // the loss, and accountSendExact's two-sided check tolerates it as
    // sub-ULP-of-coarser-side noise.
    if (ctx.view.rules().enabled(fixCleanup3_2_0))
    {
        int const coverScale = scale(sleBroker->at(sfCoverAvailable), vaultAsset);
        if (roundsToZeroAtScale(vaultAsset, amount, coverScale))
        {
            JLOG(ctx.j.warn()) << "LoanBrokerCoverDeposit: amount " << amount.getFullText()
                               << " is sub-ULP at cover scale " << coverScale;
            return tecPRECISION_LOSS;
        }
    }

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

    return depositToBrokerCover(view(), broker, account_, amount, j_);
}

void
LoanBrokerCoverDeposit::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
}

bool
LoanBrokerCoverDeposit::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

//------------------------------------------------------------------------------

}  // namespace xrpl
