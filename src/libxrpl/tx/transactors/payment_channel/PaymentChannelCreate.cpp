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

/*
    PaymentChannel

        Payment channels permit off-ledger checkpoints of XRP payments flowing
        in a single direction. A channel sequesters the owner's XRP in its own
        ledger entry. The owner can authorize the recipient to claim up to a
        given balance by giving the receiver a signed message (off-ledger). The
        recipient can use this signed message to claim any unpaid balance while
        the channel remains open. The owner can top off the line as needed. If
        the channel has not paid out all its funds, the owner must wait out a
        delay to close the channel to give the recipient a chance to supply any
        claims. The recipient can close the channel at any time. Any transaction
        that touches the channel after the expiration time will close the
        channel. The total amount paid increases monotonically as newer claims
        are issued. When the channel is closed any remaining balance is returned
        to the owner. Channels are intended to permit intermittent off-ledger
        settlement of ILP trust lines as balances get substantial. For
        bidirectional channels, a payment channel can be used in each direction.
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
    AccountRoot const acctSrc(account, ctx.view);
    if (!acctSrc)
        return terNO_ACCOUNT;

    // Check reserve and funds availability
    {
        auto const balance = acctSrc->at(sfBalance);
        auto const reserve = ctx.view.fees().accountReserve(acctSrc->getFieldU32(sfOwnerCount) + 1);

        if (balance < reserve)
            return tecINSUFFICIENT_RESERVE;

        if (balance < reserve + ctx.tx[sfAmount])
            return tecUNFUNDED;
    }

    auto const dst = ctx.tx[sfDestination];

    {
        // Check destination account
        AccountRoot const acctDst(dst, ctx.view);
        if (!acctDst)
            return tecNO_DST;

        auto const flags = acctDst->getFlags();

        // Check if they have disallowed incoming payment channels
        if ((flags & lsfDisallowIncomingPayChan) != 0u)
            return tecNO_PERMISSION;

        if (((flags & lsfRequireDestTag) != 0u) && !ctx.tx[~sfDestinationTag])
            return tecDST_TAG_NEEDED;

        // Pseudo-accounts cannot receive payment channels, other than native
        // to their underlying ledger object - implemented in their respective
        // transaction types. Note, this is not amendment-gated because all
        // writes to pseudo-account discriminator fields **are** amendment
        // gated, hence the behaviour of this check will always match the
        // currently active amendments.
        if (acctDst.isPseudoAccount())
            return tecNO_PERMISSION;
    }

    return tesSUCCESS;
}

TER
PaymentChannelCreate::doApply()
{
    WAccountRoot wrappedOwner(accountID_, ctx_.view(), j_);
    if (!wrappedOwner)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    if (ctx_.view().rules().enabled(fixPayChanCancelAfter))
    {
        auto const closeTime = ctx_.view().header().parentCloseTime;
        if (ctx_.tx[~sfCancelAfter] && after(closeTime, ctx_.tx[sfCancelAfter]))
            return tecEXPIRED;
    }

    auto const dst = ctx_.tx[sfDestination];

    // Create PayChan in ledger.
    //
    // Note that we use the value from the sequence or ticket as the
    // payChan sequence.  For more explanation see comments in SeqProxy.h.
    Keylet const payChanKeylet = keylet::payChan(accountID_, dst, ctx_.tx.getSeqValue());
    auto const slep = std::make_shared<SLE>(payChanKeylet);

    // Funds held in this channel
    (*slep)[sfAmount] = ctx_.tx[sfAmount];
    // Amount channel has already paid
    (*slep)[sfBalance] = ctx_.tx[sfAmount].zeroed();
    (*slep)[sfAccount] = accountID_;
    (*slep)[sfDestination] = dst;
    (*slep)[sfSettleDelay] = ctx_.tx[sfSettleDelay];
    (*slep)[sfPublicKey] = ctx_.tx[sfPublicKey];
    (*slep)[~sfCancelAfter] = ctx_.tx[~sfCancelAfter];
    (*slep)[~sfSourceTag] = ctx_.tx[~sfSourceTag];
    (*slep)[~sfDestinationTag] = ctx_.tx[~sfDestinationTag];
    if (ctx_.view().rules().enabled(fixIncludeKeyletFields))
    {
        (*slep)[sfSequence] = ctx_.tx.getSeqValue();
    }

    ctx_.view().insert(slep);

    // Add PayChan to owner directory
    {
        auto const page = ctx_.view().dirInsert(
            keylet::ownerDir(accountID_), payChanKeylet, describeOwnerDir(accountID_));
        if (!page)
            return tecDIR_FULL;  // LCOV_EXCL_LINE
        (*slep)[sfOwnerNode] = *page;
    }

    // Add PayChan to the recipient's owner directory
    {
        auto const page =
            ctx_.view().dirInsert(keylet::ownerDir(dst), payChanKeylet, describeOwnerDir(dst));
        if (!page)
            return tecDIR_FULL;  // LCOV_EXCL_LINE
        (*slep)[sfDestinationNode] = *page;
    }

    // Deduct owner's balance, increment owner count
    (*wrappedOwner)[sfBalance] = (*wrappedOwner)[sfBalance] - ctx_.tx[sfAmount];
    wrappedOwner.adjustOwnerCount(1);
    wrappedOwner.update();

    return tesSUCCESS;
}

void
PaymentChannelCreate::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
}

bool
PaymentChannelCreate::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}
}  // namespace xrpl
