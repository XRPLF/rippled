#include <xrpl/tx/transactors/check/CheckCreate.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/SLEWrappers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace xrpl {

bool
CheckCreate::checkExtraFeatures(xrpl::PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureMPTokensV2) || !ctx.tx[sfSendMax].holds<MPTIssue>();
}

NotTEC
CheckCreate::preflight(PreflightContext const& ctx)
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

        if (badAsset() == sendMax.asset())
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
CheckCreate::preclaim(PreclaimContext const& ctx)
{
    AccountID const dstId{ctx.tx[sfDestination]};
    AccountID const srcId{ctx.tx[sfAccount]};
    AccountRootEntry<ReadView> sleDst{keylet::account(dstId), ctx.view};
    if (!sleDst)
    {
        JLOG(ctx.j.warn()) << "Destination account does not exist.";
        return tecNO_DST;
    }

    // Check if the destination has disallowed incoming checks
    if (sleDst->isFlag(lsfDisallowIncomingCheck))
        return tecNO_PERMISSION;

    // Pseudo-accounts cannot cash checks. Note, this is not amendment-gated
    // because all writes to pseudo-account discriminator fields **are**
    // amendment gated, hence the behaviour of this check will always match the
    // currently active amendments.
    if (isPseudoAccount(sleDst.sle()))
        return tecNO_PERMISSION;

    if (sleDst->isFlag(lsfRequireDestTag) && !ctx.tx.isFieldPresent(sfDestinationTag))
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
            if (auto const ter = checkGlobalFrozen(ctx.view, sendMax.asset()); !isTesSuccess(ter))
            {
                JLOG(ctx.j.warn()) << "Creating a check for frozen or locked asset";
                return ter;
            }
            auto const err = sendMax.asset().visit(
                [&](Issue const& issue) -> std::optional<TER> {
                    // If this account has a trustline for the currency,
                    // that trustline may not be frozen.
                    //
                    // Note that we DO allow create check for a currency
                    // that the account does not yet have a trustline to.
                    if (issuerId != srcId)
                    {
                        // Check if the issuer froze the line
                        RippleStateEntry<ReadView> sleTrust{
                            keylet::trustLine(srcId, issuerId, issue.currency), ctx.view};
                        if (sleTrust &&
                            sleTrust->isFlag((issuerId > srcId) ? lsfHighFreeze : lsfLowFreeze))
                        {
                            JLOG(ctx.j.warn()) << "Creating a check for frozen trustline.";
                            return tecFROZEN;
                        }
                    }
                    if (issuerId != dstId)
                    {
                        // Check if dst froze the line.
                        RippleStateEntry<ReadView> sleTrust{
                            keylet::trustLine(issuerId, dstId, issue.currency), ctx.view};
                        if (sleTrust &&
                            sleTrust->isFlag((dstId > issuerId) ? lsfHighFreeze : lsfLowFreeze))
                        {
                            JLOG(ctx.j.warn()) << "Creating a check for "
                                                  "destination frozen trustline.";
                            return tecFROZEN;
                        }
                    }

                    return std::nullopt;
                },
                [&](MPTIssue const& issue) -> std::optional<TER> {
                    if (srcId != issuerId && isFrozen(ctx.view, srcId, issue))
                    {
                        JLOG(ctx.j.warn()) << "Creating a check for locked MPT.";
                        return tecLOCKED;
                    }
                    if (dstId != issuerId && isFrozen(ctx.view, dstId, issue))
                    {
                        JLOG(ctx.j.warn()) << "Creating a check for locked MPT.";
                        return tecLOCKED;
                    }
                    if (auto const ter = canTransfer(ctx.view, issue, srcId, dstId);
                        !isTesSuccess(ter))
                    {
                        JLOG(ctx.j.warn()) << "MPT transfer is disabled.";
                        return ter;
                    }

                    return std::nullopt;
                });
            if (err)
                return *err;
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
CheckCreate::doApply()
{
    AccountRootEntry<ApplyView> sle{keylet::account(accountID_), view()};
    if (!sle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // Note that we use the value from the sequence or ticket as the
    // Check sequence.  For more explanation see comments in SeqProxy.h.
    std::uint32_t const seq = ctx_.tx.getSeqValue();
    Keylet const checkKeylet = keylet::check(accountID_, seq);
    // Build with the ApplyViewContext so create() honors reserve sponsorship.
    CheckEntry<ApplyView> sleCheck{checkKeylet, ctx_.getApplyViewContext()};
    sleCheck.newSLE();

    sleCheck->setAccountID(sfAccount, accountID_);
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

    // Reserve check (source's pre-fee balance, honoring any reserve sponsor) +
    // link into the source owner directory and the destination tracking
    // directory + bump the source's OwnerCount + stamp the reserve sponsor +
    // insert. See CheckEntry::ownerDirs() and SLEBase::create().
    return sleCheck.create(preFeeBalance_);
}

void
CheckCreate::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
CheckCreate::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
