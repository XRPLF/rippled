#include <xrpld/app/tx/detail/CreateCheck.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
CreateCheck::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfAccount] == ctx.tx[sfDestination])
    {
        // They wrote a check to themselves.
        JLOG(ctx.j.warn()) << "Malformed transaction: Check to self.";
        return temREDUNDANT;
    }

    {
        STAmount const sendMax{ctx.tx.getFieldAmount(sfSendMax)};
        if (!isLegalNet(sendMax) || sendMax.signum() <= 0)
        {
            JLOG(ctx.j.warn()) << "Malformed transaction: bad sendMax amount: "
                               << sendMax.getFullText();
            return temBAD_AMOUNT;
        }

        if (badCurrency() == sendMax.getCurrency())
        {
            JLOG(ctx.j.warn()) << "Malformed transaction: Bad currency.";
            return temBAD_CURRENCY;
        }
    }

    if (auto const optExpiry = ctx.tx[~sfExpiration])
    {
        if (*optExpiry == 0)
        {
            JLOG(ctx.j.warn()) << "Malformed transaction: bad expiration";
            return temBAD_EXPIRATION;
        }
    }

    return tesSUCCESS;
}

TER
CreateCheck::preclaim(PreclaimContext const& ctx)
{
    AccountID const dstId{ctx.tx[sfDestination]};
    auto const sleDst = ctx.view.read(keylet::account(dstId));
    if (!sleDst)
    {
        JLOG(ctx.j.warn()) << "Destination account does not exist.";
        return tecNO_DST;
    }

    auto const flags = sleDst->getFlags();

    // Check if the destination has disallowed incoming checks
    if (ctx.view.rules().enabled(featureDisallowIncoming) &&
        (flags & lsfDisallowIncomingCheck))
        return tecNO_PERMISSION;

    // Pseudo-accounts cannot cash checks. Note, this is not amendment-gated
    // because all writes to pseudo-account discriminator fields **are**
    // amendment gated, hence the behaviour of this check will always match the
    // currently active amendments.
    if (isPseudoAccount(sleDst))
        return tecNO_PERMISSION;

    if ((flags & lsfRequireDestTag) && !ctx.tx.isFieldPresent(sfDestinationTag))
    {
        // The tag is basically account-specific information we don't
        // understand, but we can require someone to fill it in.
        JLOG(ctx.j.warn()) << "Malformed transaction: DestinationTag required.";
        return tecDST_TAG_NEEDED;
    }

    {
        STAmount const sendMax{ctx.tx[sfSendMax]};
        if (!sendMax.native())
        {
            // The currency may not be globally frozen
            AccountID const& issuerId{sendMax.getIssuer()};
            if (isGlobalFrozen(ctx.view, issuerId))
            {
                JLOG(ctx.j.warn()) << "Creating a check for frozen asset";
                return tecFROZEN;
            }
            // If this account has a trustline for the currency, that
            // trustline may not be frozen.
            //
            // Note that we DO allow create check for a currency that the
            // account does not yet have a trustline to.
            AccountID const srcId{ctx.tx.getAccountID(sfAccount)};
            if (issuerId != srcId)
            {
                // Check if the issuer froze the line
                auto const sleTrust = ctx.view.read(
                    keylet::line(srcId, issuerId, sendMax.getCurrency()));
                if (sleTrust &&
                    sleTrust->isFlag(
                        (issuerId > srcId) ? lsfHighFreeze : lsfLowFreeze))
                {
                    JLOG(ctx.j.warn())
                        << "Creating a check for frozen trustline.";
                    return tecFROZEN;
                }
            }
            if (issuerId != dstId)
            {
                // Check if dst froze the line.
                auto const sleTrust = ctx.view.read(
                    keylet::line(issuerId, dstId, sendMax.getCurrency()));
                if (sleTrust &&
                    sleTrust->isFlag(
                        (dstId > issuerId) ? lsfHighFreeze : lsfLowFreeze))
                {
                    JLOG(ctx.j.warn())
                        << "Creating a check for destination frozen trustline.";
                    return tecFROZEN;
                }
            }
        }
    }
    if (hasExpired(ctx.view, ctx.tx[~sfExpiration]))
    {
        JLOG(ctx.j.warn()) << "Creating a check that has already expired.";
        return tecEXPIRED;
    }
    return tesSUCCESS;
}

TER
CreateCheck::doApply()
{
    auto const sle = view().peek(keylet::account(account_));
    if (!sle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // A check counts against the reserve of the issuing account, but we
    // check the starting balance because we want to allow dipping into the
    // reserve to pay fees.
    {
        STAmount const reserve{
            view().fees().accountReserve(sle->getFieldU32(sfOwnerCount) + 1)};

        if (mPriorBalance < reserve)
            return tecINSUFFICIENT_RESERVE;
    }

    // Note that we use the value from the sequence or ticket as the
    // Check sequence.  For more explanation see comments in SeqProxy.h.
    std::uint32_t const seq = ctx_.tx.getSeqValue();
    Keylet const checkKeylet = keylet::check(account_, seq);
    auto sleCheck = std::make_shared<SLE>(checkKeylet);

    sleCheck->setAccountID(sfAccount, account_);
    AccountID const dstAccountId = ctx_.tx[sfDestination];
    sleCheck->setAccountID(sfDestination, dstAccountId);
    sleCheck->setFieldU32(sfSequence, seq);
    sleCheck->setFieldAmount(sfSendMax, ctx_.tx[sfSendMax]);
    if (auto const srcTag = ctx_.tx[~sfSourceTag])
        sleCheck->setFieldU32(sfSourceTag, *srcTag);
    if (auto const dstTag = ctx_.tx[~sfDestinationTag])
        sleCheck->setFieldU32(sfDestinationTag, *dstTag);
    if (auto const invoiceId = ctx_.tx[~sfInvoiceID])
        sleCheck->setFieldH256(sfInvoiceID, *invoiceId);
    if (auto const expiry = ctx_.tx[~sfExpiration])
        sleCheck->setFieldU32(sfExpiration, *expiry);

    view().insert(sleCheck);

    auto viewJ = ctx_.app.journal("View");
    // If it's not a self-send (and it shouldn't be), add Check to the
    // destination's owner directory.
    if (dstAccountId != account_)
    {
        auto const page = view().dirInsert(
            keylet::ownerDir(dstAccountId),
            checkKeylet,
            describeOwnerDir(dstAccountId));

        JLOG(j_.trace()) << "Adding Check to destination directory "
                         << to_string(checkKeylet.key) << ": "
                         << (page ? "success" : "failure");

        if (!page)
            return tecDIR_FULL;  // LCOV_EXCL_LINE

        sleCheck->setFieldU64(sfDestinationNode, *page);
    }

    {
        auto const page = view().dirInsert(
            keylet::ownerDir(account_),
            checkKeylet,
            describeOwnerDir(account_));

        JLOG(j_.trace()) << "Adding Check to owner directory "
                         << to_string(checkKeylet.key) << ": "
                         << (page ? "success" : "failure");

        if (!page)
            return tecDIR_FULL;  // LCOV_EXCL_LINE

        sleCheck->setFieldU64(sfOwnerNode, *page);
    }
    // If we succeeded, the new entry counts against the creator's reserve.
    adjustOwnerCount(view(), sle, 1, viewJ);
    return tesSUCCESS;
}

}  // namespace ripple
