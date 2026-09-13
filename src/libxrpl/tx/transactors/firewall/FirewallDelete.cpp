#include <xrpl/tx/transactors/firewall/FirewallDelete.h>

#include <xrpl/basics/Log.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/transactors/firewall/WithdrawPreauth.h>

#include <cstdint>
#include <memory>
#include <utility>

namespace xrpl {

std::uint32_t
FirewallDelete::getFlagsMask(PreflightContext const& ctx)
{
    return tfUniversalMask;
}

XRPAmount
FirewallDelete::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    auto const normalCost = Transactor::calculateBaseFee(view, tx);

    auto const counterSig = tx.getFieldObject(sfCounterpartySignature);
    std::size_t const signerCount = [&counterSig]() -> std::size_t {
        if (counterSig.isFieldPresent(sfSigners))
            return counterSig.getFieldArray(sfSigners).size();
        return counterSig.isFieldPresent(sfTxnSignature) ? 1 : 0;
    }();

    return normalCost + (view.fees().base * signerCount);
}

NotTEC
FirewallDelete::preflight(PreflightContext const& ctx)
{
    auto const counterSig = ctx.tx.getFieldObject(sfCounterpartySignature);
    if (auto const ret = detail::preflightCheckSigningKey(counterSig, ctx.j))
        return ret;

    return tesSUCCESS;
}

TER
FirewallDelete::preclaim(PreclaimContext const& ctx)
{
    auto const sleFirewall = ctx.view.read(keylet::firewall(ctx.tx.getFieldH256(sfFirewallID)));
    if (!sleFirewall)
    {
        JLOG(ctx.j.trace()) << "FirewallDelete: the firewall was not found";
        return tecNO_TARGET;
    }

    if (sleFirewall->getAccountID(sfOwner) != ctx.tx.getAccountID(sfAccount))
    {
        JLOG(ctx.j.trace()) << "FirewallDelete: the account does not own the firewall";
        return tecNO_PERMISSION;
    }

    return Transactor::checkSign(
        ctx.view,
        ctx.flags,
        ctx.parentBatchId,
        sleFirewall->getAccountID(sfCounterparty),
        ctx.tx.getFieldObject(sfCounterpartySignature),
        ctx.j);
}

TER
FirewallDelete::doApply()
{
    auto applyViewContext = ctx_.getApplyViewContext();

    auto const sleOwner = view().peek(keylet::account(accountID_));
    if (!sleOwner)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    uint256 const firewallID = ctx_.tx.getFieldH256(sfFirewallID);
    auto const sleFirewall = view().peek(keylet::firewall(firewallID));
    if (!sleFirewall)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // The preauthorizations exist only to let value past the firewall, so they
    // go with it.
    auto const ter = cleanupOnAccountDelete(
        view(),
        keylet::ownerDir(accountID_),
        [&](LedgerEntryType nodeType,
            uint256 const& dirEntry,
            std::shared_ptr<SLE>& sleItem) -> std::pair<TER, SkipEntry> {
            if (nodeType == ltWITHDRAW_PREAUTH)
                return {WithdrawPreauth::removeFromLedger(view(), dirEntry, j_), SkipEntry::No};
            return {tesSUCCESS, SkipEntry::Yes};
        },
        j_);
    if (ter != tesSUCCESS)
        return ter;  // LCOV_EXCL_LINE

    std::uint64_t const page{(*sleFirewall)[sfOwnerNode]};
    if (!view().dirRemove(keylet::ownerDir(accountID_), page, firewallID, false))
    {
        // LCOV_EXCL_START
        JLOG(j_.fatal()) << "FirewallDelete: could not remove the firewall from the owner dir";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    decreaseOwnerCountForObject(view(), sleOwner, sleFirewall, 1, j_);
    view().erase(sleFirewall);
    return tesSUCCESS;
}

void
FirewallDelete::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
}

bool
FirewallDelete::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
