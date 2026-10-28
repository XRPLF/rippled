#include <xrpl/tx/transactors/lending/LoanTransfer.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstddef>
#include <optional>

namespace xrpl {

namespace {

// The owner of the LoanBroker that the submitter does not own: the seller when the buyer
// submits, the buyer otherwise.
std::optional<AccountID>
otherOwner(ReadView const& view, STTx const& tx)
{
    auto const loan = view.read(keylet::loan(tx[sfLoanID]));
    auto const buying = view.read(keylet::loanBroker(tx[sfLoanBrokerID]));
    if (!loan || !buying)
        return std::nullopt;
    auto const selling = view.read(keylet::loanBroker(loan->at(sfLoanBrokerID)));
    if (!selling)
        return std::nullopt;
    return tx[sfAccount] == selling->at(sfOwner) ? buying->at(sfOwner) : selling->at(sfOwner);
}

}  // namespace

NotTEC
LoanTransfer::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfLoanID] == beast::kZero || ctx.tx[sfLoanBrokerID] == beast::kZero)
        return temINVALID;

    return tesSUCCESS;
}

NotTEC
LoanTransfer::checkSign(PreclaimContext const& ctx)
{
    if (auto ret = Transactor::checkSign(ctx))
        return ret;

    // Whether a signature is required is known only from the ledger, so preclaim checks
    // its presence; here a present signature must verify for the other owner.
    if (!ctx.tx.isFieldPresent(sfCounterpartySignature))
        return tesSUCCESS;
    auto const counterSigner =
        ctx.tx[~sfCounterparty] ? ctx.tx[~sfCounterparty] : otherOwner(ctx.view, ctx.tx);
    if (!counterSigner)
        return temBAD_SIGNER;
    auto const counterSig = ctx.tx.getFieldObject(sfCounterpartySignature);
    return Transactor::checkSign(
        ctx.view, ctx.flags, ctx.parentBatchId, *counterSigner, counterSig, ctx.j);
}

XRPAmount
LoanTransfer::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    auto const normalCost = Transactor::calculateBaseFee(view, tx);

    // Each signer of the CounterpartySignature adds one base fee, as for LoanSet.
    XRPAmount const baseFee = view.fees().base;
    auto const counterSig = tx.getFieldObject(sfCounterpartySignature);
    std::size_t const signerCount = [&counterSig]() -> std::size_t {
        if (counterSig.isFieldPresent(sfSigners))
            return counterSig.getFieldArray(sfSigners).size();
        return counterSig.isFieldPresent(sfTxnSignature) ? 1 : 0;
    }();

    return normalCost + (signerCount * baseFee);
}

TER
LoanTransfer::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;
    auto const account = tx[sfAccount];

    auto const loan = ctx.view.read(keylet::loan(tx[sfLoanID]));
    auto const buying = ctx.view.read(keylet::loanBroker(tx[sfLoanBrokerID]));
    if (!loan || !buying)
    {
        JLOG(ctx.j.debug()) << "LoanTransfer: loan or buying broker does not exist.";
        return tecNO_ENTRY;
    }
    auto const selling = ctx.view.read(keylet::loanBroker(loan->at(sfLoanBrokerID)));
    if (!selling)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    auto const sellingVault = ctx.view.read(keylet::vault(selling->at(sfVaultID)));
    auto const buyingVault = ctx.view.read(keylet::vault(buying->at(sfVaultID)));
    if (!sellingVault || !buyingVault)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    if (buying->key() == selling->key() || buyingVault->key() == sellingVault->key())
    {
        JLOG(ctx.j.debug()) << "LoanTransfer: the loan must move to another vault.";
        return tecNO_PERMISSION;
    }

    auto const sellingOwner = selling->at(sfOwner);
    auto const buyingOwner = buying->at(sfOwner);
    if (account != sellingOwner && account != buyingOwner)
    {
        JLOG(ctx.j.debug()) << "LoanTransfer: the submitter owns neither broker.";
        return tecNO_PERMISSION;
    }
    if (sellingOwner != buyingOwner)
    {
        auto const other = account == sellingOwner ? buyingOwner : sellingOwner;
        if (!tx.isFieldPresent(sfCounterpartySignature) ||
            tx[~sfCounterparty].value_or(other) != other)
        {
            JLOG(ctx.j.debug()) << "LoanTransfer: the other broker owner must sign.";
            return tecNO_PERMISSION;
        }
    }

    Asset const asset = sellingVault->at(sfAsset);
    if (buyingVault->at(sfAsset) != asset)
    {
        JLOG(ctx.j.debug()) << "LoanTransfer: the vaults hold different assets.";
        return tecWRONG_ASSET;
    }

    // The buyer must carry the loan at the value the seller removes, and split each payment
    // as sfManagementFeeOutstanding was computed at origination.
    if (getVaultVersion(buyingVault) != getVaultVersion(sellingVault) ||
        buying->at(sfManagementFeeRate) != selling->at(sfManagementFeeRate))
    {
        JLOG(ctx.j.debug()) << "LoanTransfer: the vaults or brokers do not match.";
        return tecNO_PERMISSION;
    }

    if (loan->isFlag(lsfLoanDefault) || loan->isFlag(lsfLoanImpaired) ||
        loan->at(sfPaymentRemaining) == 0 || isPaymentLate(ctx.view, loan))
    {
        JLOG(ctx.j.debug()) << "LoanTransfer: only a performing loan can move.";
        return tecNO_PERMISSION;
    }

    if (loan->at(sfBorrower) == buyingOwner)
    {
        JLOG(ctx.j.debug()) << "LoanTransfer: the borrower owns the buying broker.";
        return tecNO_PERMISSION;
    }

    if (auto const phase = getVaultPhase(ctx.view, buyingVault);
        phase != VaultPhase::NoPhase && phase != VaultPhase::Investment)
    {
        JLOG(ctx.j.debug()) << "LoanTransfer: the buying vault is not investing.";
        return tecNO_PERMISSION;
    }

    if (isFrozen(ctx.view, sellingVault->at(sfAccount), asset) ||
        isFrozen(ctx.view, buyingVault->at(sfAccount), asset))
        return asset.holds<Issue>() ? tecFROZEN : tecLOCKED;

    Number const price = loanVaultExposure(sellingVault, loan);
    if (buyingVault->at(sfAssetsAvailable) < price)
    {
        JLOG(ctx.j.debug()) << "LoanTransfer: the buying vault cannot pay the price.";
        return tecINSUFFICIENT_FUNDS;
    }

    Number const newDebt = buying->at(sfDebtTotal) + price;
    if (auto const debtMaximum = buying->at(sfDebtMaximum);
        debtMaximum != 0 && debtMaximum < newDebt)
    {
        JLOG(ctx.j.debug()) << "LoanTransfer: the buying broker would exceed its debt maximum.";
        return tecLIMIT_EXCEEDED;
    }
    TenthBips32 const coverRateMinimum{buying->at(sfCoverRateMinimum)};
    if (buying->at(sfCoverAvailable) < minimumBrokerCover(newDebt, coverRateMinimum, buyingVault))
    {
        JLOG(ctx.j.debug()) << "LoanTransfer: the buying broker lacks first-loss capital.";
        return tecINSUFFICIENT_FUNDS;
    }

    return tesSUCCESS;
}

TER
LoanTransfer::doApply()
{
    auto& view = ctx_.view();
    auto loan = view.peek(keylet::loan(ctx_.tx[sfLoanID]));
    if (!loan)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    auto const selling = view.peek(keylet::loanBroker(loan->at(sfLoanBrokerID)));
    auto const buying = view.peek(keylet::loanBroker(ctx_.tx[sfLoanBrokerID]));
    if (!selling || !buying)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    auto const sellingVault = view.peek(keylet::vault(selling->at(sfVaultID)));
    auto const buyingVault = view.peek(keylet::vault(buying->at(sfVaultID)));
    if (!sellingVault || !buyingVault)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    Asset const asset = sellingVault->at(sfAsset);
    Number const price = loanVaultExposure(sellingVault, loan);

    // The buying vault pays the carrying value, so neither vault's sfAssetsTotal moves.
    if (auto const ter = accountSend(
            view,
            buyingVault->at(sfAccount),
            sellingVault->at(sfAccount),
            STAmount{asset, price},
            j_,
            {},
            WaiveTransferFee::Yes))
        return ter;
    sellingVault->at(sfAssetsAvailable) += price;
    buyingVault->at(sfAssetsAvailable) -= price;
    view.update(sellingVault);
    view.update(buyingVault);

    adjustImpreciseNumber(
        selling->at(sfDebtTotal), -price, asset, getAssetsTotalScale(sellingVault));
    adjustImpreciseNumber(buying->at(sfDebtTotal), price, asset, getAssetsTotalScale(buyingVault));
    adjustLoanBrokerOwnerCount(view, selling, -1, j_);
    adjustLoanBrokerOwnerCount(view, buying, 1, j_);
    view.update(selling);
    view.update(buying);

    // The Loan keeps its ID and its borrower; only the broker it belongs to changes.
    if (!view.dirRemove(
            keylet::ownerDir(selling->at(sfAccount)),
            loan->at(sfLoanBrokerNode),
            loan->key(),
            false))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    loan->at(sfLoanBrokerID) = buying->key();
    if (auto const ter = dirLink(view, AccountID{buying->at(sfAccount)}, loan, sfLoanBrokerNode))
        return ter;
    view.update(loan);

    associateAsset(*sellingVault, asset);
    associateAsset(*buyingVault, asset);
    associateAsset(*selling, asset);
    associateAsset(*buying, asset);
    associateAsset(*loan, asset);

    return tesSUCCESS;
}

void
LoanTransfer::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
LoanTransfer::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
