//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <BeastConfig.h>
#include <ripple/legacy/0.27/book/OfferStream.h>
#include <ripple/legacy/0.27/book/Taker.h>
#include <ripple/legacy/0.27/book/Types.h>
#include <ripple/legacy/0.27/book/Amounts.h>
#include <ripple/legacy/0.27/CreateOffer.h>
#include <ripple/legacy/0.27/Emulate027.h>
#include <ripple/basics/Log.h>
#include <ripple/core/Config.h>
#include <ripple/json/to_string.h>
#include <beast/cxx14/memory.h>

namespace ripple {

namespace legacy {

TER
CreateOffer::checkAcceptAsset(IssueRef issue) const
{
    /* Only valid for custom currencies */
    assert (!isXRP (issue.currency));

    SLE::pointer const issuerAccount = mEngine->view().entryCache (
        ltACCOUNT_ROOT, getAccountRootIndex (issue.account));

    if (!issuerAccount)
    {
        if (m_journal.warning) m_journal.warning <<
            "delay: can't receive IOUs from non-existent issuer: " <<
            to_string (issue.account);

        return (mParams & tapRETRY)
            ? terNO_ACCOUNT
            : tecNO_ISSUER;
    }

    if (issuerAccount->getFieldU32 (sfFlags) & lsfRequireAuth)
    {
        SLE::pointer const trustLine (mEngine->view().entryCache (
            ltRIPPLE_STATE, getRippleStateIndex (
                mTxnAccountID, issue.account, issue.currency)));

        if (!trustLine)
        {
            return (mParams & tapRETRY)
                ? terNO_LINE
                : tecNO_LINE;
        }

        // Entries have a canonical representation, determined by a
        // lexicographical "greater than" comparison employing strict weak
        // ordering. Determine which entry we need to access.
        bool const canonical_gt (mTxnAccountID > issue.account);

        bool const is_authorized (trustLine->getFieldU32 (sfFlags) &
            (canonical_gt ? lsfLowAuth : lsfHighAuth));

        if (!is_authorized)
        {
            if (m_journal.debug) m_journal.debug <<
                "delay: can't receive IOUs from issuer without auth.";

            return (mParams & tapRETRY)
                ? terNO_AUTH
                : tecNO_AUTH;
        }
    }

    return tesSUCCESS;
}

std::pair<TER, legacy::core::Amounts>
CreateOffer::crossOffers (legacy::core::LedgerView& view,
    legacy::core::Amounts const& taker_amount)
{
    legacy::core::Taker::Options const options (mTxn.getFlags());

    legacy::core::Clock::time_point const when (
        mEngine->getLedger ()->getParentCloseTimeNC ());

    legacy::core::LedgerView view_cancel (view.duplicate());
    legacy::core::OfferStream offers (
        view, view_cancel,
        Book (taker_amount.in.issue(), taker_amount.out.issue()),
        when, m_journal);
    legacy::core::Taker taker (offers.view(), mTxnAccountID, taker_amount, options);

    TER cross_result (tesSUCCESS);

    while (true)
    {
        // Modifying the order or logic of these
        // operations causes a protocol breaking change.

        // Checks which remove offers are performed early so we
        // can reduce the size of the order book as much as possible
        // before terminating the loop.

        if (taker.done())
        {
            m_journal.debug << "The taker reports he's done during crossing!";
            break;
        }

        if (! offers.step ())
        {
            // Place the order since there are no
            // more offers and the order has a balance.
            m_journal.debug << "No more offers to consider during crossing!";
            break;
        }

        auto const& offer (offers.tip());

        if (taker.reject (offer.quality()))
        {
            // Place the order since there are no more offers
            // at the desired quality, and the order has a balance.
            break;
        }

        if (offer.account() == taker.account())
        {
            // Skip offer from self. The offer will be considered expired and
            // will get deleted.
            continue;
        }

        if (m_journal.debug) m_journal.debug <<
                "  Offer: " << offer.entry()->getIndex() << std::endl <<
                "         " << offer.amount().in << " : " << offer.amount().out;

        cross_result = taker.cross (offer);

        if (cross_result != tesSUCCESS)
        {
            cross_result = tecFAILED_PROCESSING;
            break;
        }
    }

    return std::make_pair(cross_result, taker.remaining_offer ());
}

CreateOffer::CreateOffer (STTx const& txn,
    TransactionEngineParams params, TransactionEngine* engine)
    : Transactor (txn, params, engine, deprecatedLogs().journal("CreateOffer"))
{
}

TER
CreateOffer::doApply()
{
    if (m_journal.debug) m_journal.debug <<
        "OfferCreate> " << mTxn.getJson (0);

    std::uint32_t const uTxFlags = mTxn.getFlags ();

    bool const bPassive (uTxFlags & tfPassive);
    bool const bImmediateOrCancel (uTxFlags & tfImmediateOrCancel);
    bool const bFillOrKill (uTxFlags & tfFillOrKill);
    bool const bSell  (uTxFlags & tfSell);

    STAmount saTakerPays = mTxn.getFieldAmount (sfTakerPays);
    STAmount saTakerGets = mTxn.getFieldAmount (sfTakerGets);

    if (!isLegalNet (saTakerPays) || !isLegalNet (saTakerGets))
        return temBAD_AMOUNT;

    auto const& uPaysIssuerID = saTakerPays.getIssuer ();
    auto const& uPaysCurrency = saTakerPays.getCurrency ();

    auto const& uGetsIssuerID = saTakerGets.getIssuer ();
    auto const& uGetsCurrency = saTakerGets.getCurrency ();

    bool const bHaveExpiration (mTxn.isFieldPresent (sfExpiration));
    bool const bHaveCancel (mTxn.isFieldPresent (sfOfferSequence));

    std::uint32_t const uExpiration = mTxn.getFieldU32 (sfExpiration);
    std::uint32_t const uCancelSequence = mTxn.getFieldU32 (sfOfferSequence);

    // FIXME understand why we use SequenceNext instead of current transaction
    //       sequence to determine the transaction. Why is the offer seuqnce
    //       number insufficient?

    std::uint32_t const uAccountSequenceNext = mTxnAccount->getFieldU32 (sfSequence);
    std::uint32_t const uSequence = mTxn.getSequence ();

    const uint256 uLedgerIndex = getOfferIndex (mTxnAccountID, uSequence);

    if (m_journal.debug)
    {
        m_journal.debug <<
            "Creating offer node: " << to_string (uLedgerIndex) <<
            " uSequence=" << uSequence;

        if (bImmediateOrCancel)
            m_journal.debug << "Transaction: IoC set.";

        if (bFillOrKill)
            m_journal.debug << "Transaction: FoK set.";
    }

    // This is the original rate of this offer, and is the rate at which it will
    // be placed, even if crossing offers change the amounts.
    std::uint64_t const uRate = getRate (saTakerGets, saTakerPays);

    TER terResult (tesSUCCESS);

    // This is the ledger view that we work against. Transactions are applied
    // as we go on processing transactions.
    legacy::core::LedgerView& view (mEngine->view ());

    // This is a checkpoint with just the fees paid. If something goes wrong
    // with this transaction, we roll back to this ledger.
    legacy::core::LedgerView view_checkpoint (view);

    view.bumpSeq (); // Begin ledger variance.

    SLE::pointer sleCreator = mEngine->view().entryCache (
        ltACCOUNT_ROOT, getAccountRootIndex (mTxnAccountID));

    if (uTxFlags & tfOfferCreateMask)
    {
        if (m_journal.debug) m_journal.debug <<
            "Malformed transaction: Invalid flags set.";

        terResult = temINVALID_FLAG;
    }
    else if (bImmediateOrCancel && bFillOrKill)
    {
        if (m_journal.debug) m_journal.debug <<
            "Malformed transaction: both IoC and FoK set.";

        terResult = temINVALID_FLAG;
    }
    else if (bHaveExpiration && !uExpiration)
    {
        m_journal.warning <<
            "Malformed offer: bad expiration";

        terResult = temBAD_EXPIRATION;
    }
    else if (saTakerPays.isNative () && saTakerGets.isNative ())
    {
        m_journal.warning <<
            "Malformed offer: XRP for XRP";

        terResult = temBAD_OFFER;
    }
    else if (saTakerPays <= zero || saTakerGets <= zero)
    {
        m_journal.warning <<
            "Malformed offer: bad amount";

        terResult = temBAD_OFFER;
    }
    else if (uPaysCurrency == uGetsCurrency && uPaysIssuerID == uGetsIssuerID)
    {
        m_journal.warning <<
            "Malformed offer: redundant offer";

        terResult = temREDUNDANT;
    }
    // We don't allow a non-native currency to use the currency code XRP.
    else if (badCurrency() == uPaysCurrency || badCurrency() == uGetsCurrency)
    {
        m_journal.warning <<
            "Malformed offer: Bad currency.";

        terResult = temBAD_CURRENCY;
    }
    else if (saTakerPays.isNative () != !uPaysIssuerID ||
                saTakerGets.isNative () != !uGetsIssuerID)
    {
        m_journal.warning <<
            "Malformed offer: bad issuer";

        terResult = temBAD_ISSUER;
    }
    else if (view.isGlobalFrozen (uPaysIssuerID) || view.isGlobalFrozen (uGetsIssuerID))
    {
        if (m_journal.debug) m_journal.debug <<
            "Offer involves frozen asset";

        terResult = tecFROZEN;
    }
    else if (view.accountFunds (
        mTxnAccountID, saTakerGets, fhZERO_IF_FROZEN) <= zero)
    {
        if (m_journal.debug) m_journal.debug <<
            "delay: Offers must be at least partially funded.";

        terResult = tecUNFUNDED_OFFER;
    }
    // This can probably be simplified to make sure that you cancel sequences
    // before the transaction sequence number.
    else if (bHaveCancel && (!uCancelSequence || uAccountSequenceNext - 1 <= uCancelSequence))
    {
        if (m_journal.debug) m_journal.debug <<
            "uAccountSequenceNext=" << uAccountSequenceNext <<
            " uOfferSequence=" << uCancelSequence;

        terResult = temBAD_SEQUENCE;
    }

    if (terResult != tesSUCCESS)
    {
        if (m_journal.debug) m_journal.debug <<
            "final terResult=" << transToken (terResult);

        return terResult;
    }

    // Process a cancellation request that's passed along with an offer.
    if ((terResult == tesSUCCESS) && bHaveCancel)
    {
        uint256 const uCancelIndex (
            getOfferIndex (mTxnAccountID, uCancelSequence));
        SLE::pointer sleCancel = mEngine->view().entryCache (ltOFFER, uCancelIndex);

        // It's not an error to not find the offer to cancel: it might have
        // been consumed or removed as we are processing.
        if (sleCancel)
        {
            if (m_journal.debug) m_journal.debug <<
                "Cancelling order with sequence " << uCancelSequence;

            terResult = view.offerDelete (sleCancel);
        }
    }

    // Expiration is defined in terms of the close time of the parent ledger,
    // because we definitively know the time that it closed but we do not
    // know the closing time of the ledger that is under construction.
    if (bHaveExpiration &&
        (mEngine->getLedger ()->getParentCloseTimeNC () >= uExpiration))
    {
        return tesSUCCESS;
    }

    // Make sure that we are authorized to hold what the taker will pay us.
    if (terResult == tesSUCCESS && !saTakerPays.isNative ())
        terResult = checkAcceptAsset (Issue (uPaysCurrency, uPaysIssuerID));

    bool crossed = false;
    bool const bOpenLedger (mParams & tapOPEN_LEDGER);

    if (terResult == tesSUCCESS)
    {
        // We reverse gets and pays because during offer crossing we are taking.
        legacy::core::Amounts const taker_amount (saTakerGets, saTakerPays);

        // The amount of the offer that we will need to place, after we finish
        // offer crossing processing. It may be equal to the original amount,
        // empty (fully crossed), or something in-between.
        legacy::core::Amounts place_offer;

        std::tie(terResult, place_offer) = crossOffers (view, taker_amount);

        if (terResult == tecFAILED_PROCESSING && bOpenLedger)
            terResult = telFAILED_PROCESSING;

        if (terResult == tesSUCCESS)
        {
            // We now need to reduce the offer by the cross flow. We reverse
            // in and out here, since during crossing we were takers.
            assert (saTakerPays.getCurrency () == place_offer.out.getCurrency ());
            assert (saTakerPays.getIssuer () == place_offer.out.getIssuer ());
            assert (saTakerGets.getCurrency () == place_offer.in.getCurrency ());
            assert (saTakerGets.getIssuer () == place_offer.in.getIssuer ());

            if (taker_amount != place_offer)
                crossed = true;

            if (m_journal.debug)
            {
                m_journal.debug << "Offer Crossing: " << transToken (terResult);

                if (terResult == tesSUCCESS)
                {
                    m_journal.debug <<
                        "    takerPays: " << saTakerPays.getFullText () <<
                        " -> " << place_offer.out.getFullText ();
                    m_journal.debug <<
                        "    takerGets: " << saTakerGets.getFullText () <<
                        " -> " << place_offer.in.getFullText ();
                }
            }

            saTakerPays = place_offer.out;
            saTakerGets = place_offer.in;
        }
    }

    if (terResult != tesSUCCESS)
    {
        m_journal.debug <<
            "final terResult=" << transToken (terResult);

        return terResult;
    }

    if (m_journal.debug)
    {
        m_journal.debug <<
            "takeOffers: saTakerPays=" <<saTakerPays.getFullText ();
        m_journal.debug <<
            "takeOffers: saTakerGets=" << saTakerGets.getFullText ();
        m_journal.debug <<
            "takeOffers: mTxnAccountID=" <<
            to_string (mTxnAccountID);
        m_journal.debug <<
            "takeOffers:         FUNDS=" << view.accountFunds (
            mTxnAccountID, saTakerGets, fhZERO_IF_FROZEN).getFullText ();
    }

    if (saTakerPays < zero || saTakerGets < zero)
    {
        // Earlier, we verified that the amounts, as specified in the offer,
        // were not negative. That they are now suggests that something went
        // very wrong with offer crossing.
        m_journal.fatal << (crossed ? "Partially consumed" : "Full") <<
            " offer has negative component:" <<
            " pays=" << saTakerPays.getFullText () <<
            " gets=" << saTakerGets.getFullText ();

        assert (saTakerPays >= zero);
        assert (saTakerGets >= zero);
        return tefINTERNAL;
    }

    if (bFillOrKill && (saTakerPays != zero || saTakerGets != zero))
    {
        // Fill or kill and have leftovers.
        view.swapWith (view_checkpoint); // Restore with just fees paid.
        return tesSUCCESS;
    }

    // What the reserve would be if this offer was placed.
    auto const accountReserve (mEngine->getLedger ()->getReserve (
        sleCreator->getFieldU32 (sfOwnerCount) + 1));

    if (saTakerPays == zero ||                // Wants nothing more.
        saTakerGets == zero ||                // Offering nothing more.
        bImmediateOrCancel)                   // Do not persist.
    {
        // Complete as is.
    }
    else if (mPriorBalance < accountReserve)
    {
        // If we are here, the signing account had an insufficient reserve
        // *prior* to our processing. We use the prior balance to simplify
        // client writing and make the user experience better.

        if (bOpenLedger) // Ledger is not final, can vote no.
        {
            // Hope for more reserve to come in or more offers to consume. If we
            // specified a local error this transaction will not be retried, so
            // specify a tec to distribute the transaction and allow it to be
            // retried. In particular, it may have been successful to a
            // degree (partially filled) and if it hasn't, it might succeed.
            terResult = tecINSUF_RESERVE_OFFER;
        }
        else if (!crossed)
        {
            // Ledger is final, insufficent reserve to create offer, processed
            // nothing.
            terResult = tecINSUF_RESERVE_OFFER;
        }
        else
        {
            // Ledger is final, insufficent reserve to create offer, processed
            // something.
            // Consider the offer unfunded. Treat as tesSUCCESS.
        }
    }
    else
    {
        assert (saTakerPays > zero);
        assert (saTakerGets > zero);

        // We need to place the remainder of the offer into its order book.
        if (m_journal.debug) m_journal.debug <<
            "offer not fully consumed:" <<
            " saTakerPays=" << saTakerPays.getFullText () <<
            " saTakerGets=" << saTakerGets.getFullText ();

        std::uint64_t uOwnerNode;
        std::uint64_t uBookNode;
        uint256 uDirectory;

        // Add offer to owner's directory.
        terResult = view.dirAdd (uOwnerNode,
            getOwnerDirIndex (mTxnAccountID), uLedgerIndex,
            std::bind (
                &Ledger::ownerDirDescriber, std::placeholders::_1,
                std::placeholders::_2, mTxnAccountID));

        if (tesSUCCESS == terResult)
        {
            // Update owner count.
            view.incrementOwnerCount (sleCreator);

            uint256 const uBookBase (getBookBase (
                {{uPaysCurrency, uPaysIssuerID},
                    {uGetsCurrency, uGetsIssuerID}}));

            if (m_journal.debug) m_journal.debug <<
                "adding to book: " << to_string (uBookBase) <<
                " : " << saTakerPays.getHumanCurrency () <<
                "/" << to_string (saTakerPays.getIssuer ()) <<
                " -> " << saTakerGets.getHumanCurrency () <<
                "/" << to_string (saTakerGets.getIssuer ());

            // We use the original rate to place the offer.
            uDirectory = getQualityIndex (uBookBase, uRate);

            // Add offer to order book.
            terResult = view.dirAdd (uBookNode, uDirectory, uLedgerIndex,
                std::bind (
                    &Ledger::qualityDirDescriber, std::placeholders::_1,
                    std::placeholders::_2, saTakerPays.getCurrency (),
                    uPaysIssuerID, saTakerGets.getCurrency (),
                    uGetsIssuerID, uRate));
        }

        if (tesSUCCESS == terResult)
        {
            if (m_journal.debug)
            {
                m_journal.debug <<
                    "sfAccount=" <<
                    to_string (mTxnAccountID);
                m_journal.debug <<
                    "uPaysIssuerID=" <<
                    to_string (uPaysIssuerID);
                m_journal.debug <<
                    "uGetsIssuerID=" <<
                    to_string (uGetsIssuerID);
                m_journal.debug <<
                    "saTakerPays.isNative()=" <<
                    saTakerPays.isNative ();
                m_journal.debug <<
                    "saTakerGets.isNative()=" <<
                    saTakerGets.isNative ();
                m_journal.debug <<
                    "uPaysCurrency=" <<
                    saTakerPays.getHumanCurrency ();
                m_journal.debug <<
                    "uGetsCurrency=" <<
                    saTakerGets.getHumanCurrency ();
            }

            SLE::pointer sleOffer (mEngine->view().entryCreate (ltOFFER, uLedgerIndex));

            sleOffer->setFieldAccount (sfAccount, mTxnAccountID);
            sleOffer->setFieldU32 (sfSequence, uSequence);
            sleOffer->setFieldH256 (sfBookDirectory, uDirectory);
            sleOffer->setFieldAmount (sfTakerPays, saTakerPays);
            sleOffer->setFieldAmount (sfTakerGets, saTakerGets);
            sleOffer->setFieldU64 (sfOwnerNode, uOwnerNode);
            sleOffer->setFieldU64 (sfBookNode, uBookNode);

            if (uExpiration)
                sleOffer->setFieldU32 (sfExpiration, uExpiration);

            if (bPassive)
                sleOffer->setFlag (lsfPassive);

            if (bSell)
                sleOffer->setFlag (lsfSell);

            if (m_journal.debug) m_journal.debug <<
                "final terResult=" << transToken (terResult) <<
                " sleOffer=" << sleOffer->getJson (0);
        }
    }

    if (terResult != tesSUCCESS)
    {
        m_journal.debug <<
            "final terResult=" << transToken (terResult);
    }

    return terResult;
}

//------------------------------------------------------------------------------

std::pair <bool, TER>
transact_CreateOffer (STTx const& txn,
    TransactionEngineParams params, TransactionEngine* engine)
{
    // If we are emulating 0.27, we process the transaction using the 0.27
    // semantics and no autobridging.
    if (emulate027 (engine->getLedger ()))
        return std::make_pair (true, CreateOffer (txn, params, engine).apply ());

    // Otherwise, we use 0.28 semantics with autobridging.
    return std::make_pair (false, tesSUCCESS);
}

}

}
