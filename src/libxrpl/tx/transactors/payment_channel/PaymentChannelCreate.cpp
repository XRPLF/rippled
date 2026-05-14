#include <xrpl/tx/transactors/payment_channel/PaymentChannelCreate.h>

#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/applySteps.h>

#include <memory>

namespace xrpl {

/** @file
 *  Implements `PaymentChannelCreate` — the transactor that opens a
 *  unidirectional XRP payment channel.
 *
 *  Payment channels sequesters the owner's XRP in a dedicated `PayChannel`
 *  SLE so the owner can issue signed off-ledger claims to a recipient without
 *  touching the ledger for every payment. The recipient presents claims
 *  on-chain to settle; the paid total increases monotonically. When the channel
 *  closes, any remaining balance is returned to the owner. For bidirectional
 *  flow, two channels (one in each direction) are used.
 *
 *  This file covers only channel construction. Funding additions, claim
 *  settlement, and closure are handled by `PaymentChannelFund.cpp` and
 *  `PaymentChannelClaim.cpp` respectively. Crucially, `PaymentChannelCreate`
 *  is the only transactor that allocates the `PayChannel` SLE and inserts
 *  both directory entries; the sibling transactors always assume those
 *  structures already exist.
 */

//------------------------------------------------------------------------------

TxConsequences
PaymentChannelCreate::makeTxConsequences(PreflightContext const& ctx)
{
    return TxConsequences{ctx.tx, ctx.tx[sfAmount].xrp()};
}

NotTEC
PaymentChannelCreate::preflight(PreflightContext const& ctx)
{
    if (!isXRP(ctx.tx[sfAmount]) || (ctx.tx[sfAmount] <= beast::kZERO))
        return temBAD_AMOUNT;

    if (ctx.tx[sfAccount] == ctx.tx[sfDestination])
        return temDST_IS_SRC;

    if (!publicKeyType(ctx.tx[sfPublicKey]))
        return temMALFORMED;

    return tesSUCCESS;
}

TER
PaymentChannelCreate::preclaim(PreclaimContext const& ctx)
{
    auto const account = ctx.tx[sfAccount];
    auto const sle = ctx.view.read(keylet::account(account));
    if (!sle)
        return terNO_ACCOUNT;

    // Two-step reserve check: first ensure the account can hold one more owned
    // object (tecINSUFFICIENT_RESERVE), then that it can also fund the channel
    // at the requested level (tecUNFUNDED). The distinction is intentional —
    // callers can use it to tell whether the problem is reserve capacity or
    // channel size.
    {
        auto const balance = (*sle)[sfBalance];
        auto const reserve = ctx.view.fees().accountReserve((*sle)[sfOwnerCount] + 1);

        if (balance < reserve)
            return tecINSUFFICIENT_RESERVE;

        if (balance < reserve + ctx.tx[sfAmount])
            return tecUNFUNDED;
    }

    auto const dst = ctx.tx[sfDestination];

    {
        auto const sled = ctx.view.read(keylet::account(dst));
        if (!sled)
            // Deliberate design choice: channels to non-existent accounts are
            // rejected even though XRP would be held in escrow. Allowing it
            // would reserve funds to an address that may never be activated,
            // effectively burning the sender's XRP in a dead-end channel.
            return tecNO_DST;

        auto const flags = sled->getFlags();

        if ((flags & lsfDisallowIncomingPayChan) != 0u)
            return tecNO_PERMISSION;

        if (((flags & lsfRequireDestTag) != 0u) && !ctx.tx[~sfDestinationTag])
            return tecDST_TAG_NEEDED;

        // Not amendment-gated: pseudo-account discriminator fields are only
        // written under their own amendment guards, so this check automatically
        // tracks the active amendment set without additional gating here.
        if (isPseudoAccount(sled))
            return tecNO_PERMISSION;
    }

    return tesSUCCESS;
}

TER
PaymentChannelCreate::doApply()
{
    auto const account = ctx_.tx[sfAccount];
    auto const sle = ctx_.view().peek(keylet::account(account));
    if (!sle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    if (ctx_.view().rules().enabled(fixPayChanCancelAfter))
    {
        auto const closeTime = ctx_.view().header().parentCloseTime;
        if (ctx_.tx[~sfCancelAfter] && after(closeTime, ctx_.tx[sfCancelAfter]))
            return tecEXPIRED;
    }

    auto const dst = ctx_.tx[sfDestination];

    // The keylet hashes account, destination, and sequence (or ticket) number.
    // Including the sequence ensures that two channels between the same pair of
    // accounts are always addressable distinctly — each channel open has a
    // unique sequence value, so their keylets never collide. getSeqValue()
    // transparently returns either the transaction sequence or the ticket
    // number, so the keylet is deterministic for both regular and ticket-based
    // transactions. See SeqProxy.h for details.
    Keylet const payChanKeylet = keylet::payChan(account, dst, ctx_.tx.getSeqValue());
    auto const slep = std::make_shared<SLE>(payChanKeylet);

    (*slep)[sfAmount] = ctx_.tx[sfAmount];
    // zeroed() preserves the XRPAmount type, keeping sfBalance type-consistent
    // with sfAmount from the start (as opposed to a bare integer zero).
    (*slep)[sfBalance] = ctx_.tx[sfAmount].zeroed();
    (*slep)[sfAccount] = account;
    (*slep)[sfDestination] = dst;
    (*slep)[sfSettleDelay] = ctx_.tx[sfSettleDelay];
    (*slep)[sfPublicKey] = ctx_.tx[sfPublicKey];
    (*slep)[~sfCancelAfter] = ctx_.tx[~sfCancelAfter];
    (*slep)[~sfSourceTag] = ctx_.tx[~sfSourceTag];
    (*slep)[~sfDestinationTag] = ctx_.tx[~sfDestinationTag];
    // fixIncludeKeyletFields is a bug-fix amendment: channels created before it
    // was activated lack sfSequence in the SLE, so callers must fetch the
    // originating transaction to recompute the keylet. When active, storing
    // sfSequence directly in the SLE lets any tool reconstruct the channel's
    // keylet from the object alone, without additional transaction lookup.
    if (ctx_.view().rules().enabled(fixIncludeKeyletFields))
    {
        (*slep)[sfSequence] = ctx_.tx.getSeqValue();
    }

    ctx_.view().insert(slep);

    // Dual directory insertion: both owner and recipient can enumerate this
    // channel, and the stored page indices make O(1) removal possible on close.
    {
        auto const page = ctx_.view().dirInsert(
            keylet::ownerDir(account), payChanKeylet, describeOwnerDir(account));
        if (!page)
            return tecDIR_FULL;  // LCOV_EXCL_LINE
        (*slep)[sfOwnerNode] = *page;
    }

    {
        auto const page =
            ctx_.view().dirInsert(keylet::ownerDir(dst), payChanKeylet, describeOwnerDir(dst));
        if (!page)
            return tecDIR_FULL;  // LCOV_EXCL_LINE
        (*slep)[sfDestinationNode] = *page;
    }

    (*sle)[sfBalance] = (*sle)[sfBalance] - ctx_.tx[sfAmount];
    adjustOwnerCount(ctx_.view(), sle, 1, ctx_.journal);
    ctx_.view().update(sle);

    return tesSUCCESS;
}

void
PaymentChannelCreate::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
PaymentChannelCreate::finalizeInvariants(
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
