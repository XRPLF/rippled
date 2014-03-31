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

#include "../book/OfferStream.h"
#include "../book/Taker.h"
#include "../../beast/beast/streams/debug_ostream.h"

namespace ripple {

//------------------------------------------------------------------------------

// NIKB Move this in the right place
std::pair<TER,bool>
process_order (
    core::LedgerView& view,
    core::BookRef const book,
    core::Account const& account,
    core::Amounts const& amount,
    core::Amounts& cross_flow,
    core::Taker::Options const options,
    core::Clock::time_point const when,
    beast::Journal& journal)
{
    TER result (tesSUCCESS);
    core::LedgerView view_cancel (view.duplicate());
    core::OfferStream offers (view, view_cancel, book, when, journal);
    core::Taker taker (offers.view(), book, account, amount, options);

    if (journal.debug) journal.debug <<
        "process_order: " <<
        (options.sell? "sell" : "buy") << " " <<
        (options.passive? "passive" : "") << std::endl <<
        "     taker: " << taker.account() << std::endl <<
        "  balances: " <<
            view.accountFunds (taker.account(), amount.in) << ", " <<
            view.accountFunds (taker.account(), amount.out);

    cross_flow.in.clear (amount.in);
    cross_flow.out.clear (amount.out);

    bool place_order (true);

    while (true)
    {
        // Modifying the order or logic of these
        // operations causes a protocol breaking change.

        // Checks which remove offers are performed early so we
        // can reduce the size of the order book as much as possible
        // before terminating the loop.

        if (taker.done())
        {
            journal.debug << "The taker reports he's done during crossing!";
            place_order = false;
            break;
        }

        if (! offers.step())
        {
            // Place the order since there are no
            // more offers and the order has a balance.
            journal.debug << "No more offers to consider during crossing!";
            break;
        }

        auto const offer (offers.tip());

        if (journal.debug) journal.debug <<
            "Considering offer: " << std::endl <<
            "  Id: " << offer.entry()->getIndex() << std::endl <<
            "  In: " << offer.amount().in << std::endl <<
            " Out: " << offer.amount().out << std::endl <<
            "  By: " << offer.account();

        if (taker.reject (offer.quality()))
        {
            // Place the order since there are no more offers
            // at the desired quality, and the order has a balance.
            break;
        }

        if (offer.account() == taker.account())
        {
            if (journal.debug) journal.debug <<
                " skipping self-offer " << offer.entry()->getIndex() << std::endl <<
                "  pays/gets " << offer.amount().in << ", " << offer.amount().out << std::endl <<
                " during cross for " << std::endl <<
                "   pays/gets " << amount.in << ", " << amount.out;
                ;

            // Skip offer from self.
            // (Offer will be considered expired, and get deleted)
            continue;
        }

        if (journal.debug) journal.debug <<
            "   offer " << offer.entry()->getIndex() << std::endl <<
            "  pays/gets " << offer.amount().in << ", " << offer.amount().out
            ;

        core::Amounts flow;
        bool consumed;
        std::tie (flow, consumed) = taker.fill (offer);

        result = taker.process (flow, offer);

        if (journal.debug) journal.debug <<
            "       flow " <<
                flow.in << ", " << flow.out << std::endl <<
            "   balances " <<
                view.accountFunds (taker.account(), amount.in) << ", " <<
                view.accountFunds (taker.account(), amount.out)
            ;

        if (result != tesSUCCESS)
        {
            // VFALCO TODO Return the tec and let a caller higher
            //             up convert the error if the ledger is open.
            //result = bOpenLedger ?
            //    telFAILED_PROCESSING : tecFAILED_PROCESSING;
            result = tecFAILED_PROCESSING;
            break;
        }

        cross_flow.in += flow.in;
        cross_flow.out += flow.out;
    }

    if (result == tesSUCCESS)
    {
        // No point in placing an offer for a fill-or-kill offer - the offer
        // will not succeed, since it wasn't filled.
        if (options.fill_or_kill)
            place_order = false;

        // An immediate or cancel order will fill however much it is possible
        // to fill and the remainder is not filled.
        if (options.immediate_or_cancel)
            place_order = false;
    }

    if (result == tesSUCCESS)
    {
        if (place_order)
        {

        }
    }
    else
    {

    }

    return std::make_pair(result,place_order);
}

/** Take as much as possible.
    We adjusts account balances and charges fees on top to taker.

    @param saTakerPays What the taker offers (w/ issuer)
    @param saTakerGets What the taker wanted (w/ issuer)
    @param saTakerPaid What taker could have paid including saved not including
                       fees. To reduce an offer.
    @param saTakerGot What taker got not including fees. To reduce an offer.
    @return tesSUCCESS, terNO_ACCOUNT, telFAILED_PROCESSING, or
            tecFAILED_PROCESSING
*/
std::pair<TER,bool> DirectOfferCreateTransactor::crossOffers (
    core::LedgerView& view,
    const STAmount&     saTakerPays,
    const STAmount&     saTakerGets,
    STAmount&           saTakerPaid,
    STAmount&           saTakerGot)
{
    if (m_journal.debug) m_journal.debug << "takeOffers: ";

    core::Book book (
        core::AssetRef (
            saTakerPays.getCurrency(), saTakerPays.getIssuer()), 
        core::AssetRef (
            saTakerGets.getCurrency(), saTakerGets.getIssuer()));

    core::Amounts cross_flow (
        core::Amount (saTakerPays.getCurrency(), saTakerPays.getIssuer()),
        core::Amount (saTakerGets.getCurrency(), saTakerGets.getIssuer()));

    auto const result (process_order (
        view, book, mTxnAccountID,
        core::Amounts (saTakerPays, saTakerGets), cross_flow, 
        core::Taker::Options (mTxn.getFlags()),
        mEngine->getLedger ()->getParentCloseTimeNC (),
        m_journal));

    core::Amounts const funds (
        view.accountFunds (mTxnAccountID, saTakerPays),
        view.accountFunds (mTxnAccountID, saTakerGets));
    
    if (m_journal.debug) m_journal.debug << " cross_flow: " <<
            cross_flow.in << ", " << cross_flow.out;
    
    if (m_journal.debug) m_journal.debug << "   balances: " <<
        funds.in << ", " << funds.out;

    saTakerPaid = cross_flow.in; 
    saTakerGot = cross_flow.out;

    if (m_journal.debug) m_journal.debug <<
        "        result: " << transToken (result.first) <<
        (result.second ? " (consumed)" : "");

    return result;
}

TER DirectOfferCreateTransactor::doApply ()
{
    if (m_journal.debug) m_journal.debug <<
        "OfferCreate> " << mTxn.getJson (0);
        
    std::uint32_t const uTxFlags = mTxn.getFlags ();

    bool const bPassive = is_bit_set (uTxFlags, tfPassive);
    bool const bImmediateOrCancel = is_bit_set (uTxFlags, tfImmediateOrCancel);
    bool const bFillOrKill = is_bit_set (uTxFlags, tfFillOrKill);
    bool const bSell = is_bit_set (uTxFlags, tfSell);

    STAmount saTakerPays = mTxn.getFieldAmount (sfTakerPays);
    STAmount saTakerGets = mTxn.getFieldAmount (sfTakerGets);

    if (!saTakerPays.isLegalNet () || !saTakerGets.isLegalNet ())
        return temBAD_AMOUNT;

    uint160 const uPaysIssuerID = saTakerPays.getIssuer ();
    uint160 const uGetsIssuerID = saTakerGets.getIssuer ();

    bool const bHaveExpiration (mTxn.isFieldPresent (sfExpiration));
    bool const bHaveCancel (mTxn.isFieldPresent (sfOfferSequence));

    std::uint32_t const uExpiration = mTxn.getFieldU32 (sfExpiration);
    std::uint32_t const uCancelSequence = mTxn.getFieldU32 (sfOfferSequence);

    // FIXME understand why we use SequenceNext instead of current transaction
    //       sequence to determine the transaction. Why is the offer seuqnce
    //       number insufficient?

    std::uint32_t const uAccountSequenceNext = mTxnAccount->getFieldU32 (sfSequence);
    std::uint32_t const uSequence = mTxn.getSequence ();

    const uint256 uLedgerIndex = Ledger::getOfferIndex (mTxnAccountID, uSequence);

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

    uint160 const uPaysCurrency = saTakerPays.getCurrency ();
    uint160 const uGetsCurrency = saTakerGets.getCurrency ();
    std::uint64_t const uRate = STAmount::getRate (saTakerGets, saTakerPays);

    TER terResult (tesSUCCESS);
    uint256 uDirectory; // Delete hints.
    std::uint64_t uOwnerNode;
    std::uint64_t uBookNode;

    // This is the ledger view that we work against. Transactions are applied
    // as we go on processing transactions.
    core::LedgerView& view (mEngine->view ());

    // This is a checkpoint with just the fees paid. If something goes wrong
    // with this transaction, we roll back to this ledger.
    core::LedgerView view_checkpoint (view);

    view.bumpSeq (); // Begin ledger variance.

    SLE::pointer sleCreator = mEngine->entryCache (
        ltACCOUNT_ROOT, Ledger::getAccountRootIndex (mTxnAccountID));

    if (uTxFlags & tfOfferCreateMask)
    {
        if (m_journal.debug) m_journal.debug <<
            "Malformed transaction: Invalid flags set.";

        return temINVALID_FLAG;
    }
    else if (bImmediateOrCancel && bFillOrKill)
    {
        if (m_journal.debug) m_journal.debug <<
            "Malformed transaction: both IoC and FoK set.";

        return temINVALID_FLAG;
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

        terResult   = temBAD_OFFER;
    }
    else if (saTakerPays <= zero || saTakerGets <= zero)
    {
        m_journal.warning <<
            "Malformed offer: bad amount";

        terResult   = temBAD_OFFER;
    }
    else if (uPaysCurrency == uGetsCurrency && uPaysIssuerID == uGetsIssuerID)
    {
        m_journal.warning <<
            "Malformed offer: redundant offer";

        terResult = temREDUNDANT;
    }
    // We don't allow a non-native currency to use the currency code XRP.
    else if (CURRENCY_BAD == uPaysCurrency || CURRENCY_BAD == uGetsCurrency)
    {
        m_journal.warning <<
            "Malformed offer: Bad currency.";

        terResult = temBAD_CURRENCY;
    }
    else if (saTakerPays.isNative () != !uPaysIssuerID || saTakerGets.isNative () != !uGetsIssuerID)
    {
        m_journal.warning <<
            "Malformed offer: bad issuer";

        terResult = temBAD_ISSUER;
    }
    else if (view.accountFunds (mTxnAccountID, saTakerGets) <= zero)
    {
        m_journal.warning <<
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

    if (tesSUCCESS != terResult)
    {
        if (m_journal.debug) m_journal.debug <<
            "final terResult=" << transToken (terResult);

        return terResult;
    }

    // Process a cancellation request that's passed along with an offer.
    if ((tesSUCCESS == terResult) && bHaveCancel)
    {
        uint256 const uCancelIndex (
            Ledger::getOfferIndex (mTxnAccountID, uCancelSequence));
        SLE::pointer sleCancel = mEngine->entryCache (ltOFFER, uCancelIndex);

        // It's not an error to not find the offer to cancel: it might have
        // been consumed or removed as we are processing. Additionally, it
        // might not even have been an offer - we don't care.
        if (sleCancel)
        {
            m_journal.warning <<
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

    // If all is well and this isn't an offer to XRP, then we make sure we are
    // authorized to hold what the taker will pay.
    if (tesSUCCESS == terResult && !saTakerPays.isNative ())
    {
        SLE::pointer sleTakerPays = mEngine->entryCache (
            ltACCOUNT_ROOT, Ledger::getAccountRootIndex (uPaysIssuerID));

        if (!sleTakerPays)
        {
            m_journal.warning <<
                "delay: can't receive IOUs from non-existent issuer: " <<
                RippleAddress::createHumanAccountID (uPaysIssuerID);

            return is_bit_set (mParams, tapRETRY)
                ? terNO_ACCOUNT
                : tecNO_ISSUER;
        }
        
        if (is_bit_set (sleTakerPays->getFieldU32 (sfFlags), lsfRequireAuth))
        {
            SLE::pointer sleRippleState (mEngine->entryCache (
                ltRIPPLE_STATE,
                Ledger::getRippleStateIndex (
                    mTxnAccountID, uPaysIssuerID, uPaysCurrency)));

            if (!sleRippleState)
            {
                return is_bit_set (mParams, tapRETRY)
                    ? terNO_LINE
                    : tecNO_LINE;
            }

            // Entries have a canonical representation, determined by a
            // lexicographical "greater than" comparison employing strict weak
            // ordering. Determine which entry we need to access.
            bool const canonical_gt (mTxnAccountID > uPaysIssuerID);

            bool const need_auth (is_bit_set (
                sleRippleState->getFieldU32 (sfFlags),
                (canonical_gt ? lsfLowAuth : lsfHighAuth)));

            if (need_auth)
            {
                if (m_journal.debug) m_journal.debug <<
                    "delay: can't receive IOUs from issuer without auth.";

                return is_bit_set (mParams, tapRETRY)
                    ? terNO_AUTH
                    : tecNO_AUTH;
            }
        }
    }

    STAmount saPaid;
    STAmount saGot;
    bool const bOpenLedger = is_bit_set (mParams, tapOPEN_LEDGER);
    bool const placeOffer = true;

    if (tesSUCCESS == terResult)
    {
        // Take using the parameters of the offer.
        if (m_journal.debug) m_journal.debug <<
            "takeOffers: BEFORE saTakerGets=" << saTakerGets.getFullText ();

        auto ret = crossOffers (
            view,
            saTakerGets,  // Reverse as we are the taker for taking.
            saTakerPays,
            saPaid,       // Buy semantics: how much would have sold at full price. Sell semantics: how much was sold.
            saGot);       // How much was got.

        terResult = ret.first;

        if (terResult == tecFAILED_PROCESSING && bOpenLedger)
            terResult = telFAILED_PROCESSING;

        if (m_journal.debug)
        {
            m_journal.debug << "takeOffers=" << terResult;
            m_journal.debug << "takeOffers: saPaid=" << saPaid.getFullText ();
            m_journal.debug << "takeOffers:  saGot=" << saGot.getFullText ();
        }

        if (tesSUCCESS == terResult)
        {
            // Reduce pay in from takers by what offer just got.
            saTakerPays -= saGot;

            // Reduce pay out to takers by what srcAccount just paid.
            saTakerGets -= saPaid;

            if (m_journal.debug)
            {
                m_journal.debug <<
                    "takeOffers: AFTER saTakerPays=" <<
                    saTakerPays.getFullText ();
                m_journal.debug <<
                    "takeOffers: AFTER saTakerGets=" <<
                    saTakerGets.getFullText ();
            }
        }
    }

    if (m_journal.debug)
    {
        m_journal.debug <<
            "takeOffers: saTakerPays=" <<saTakerPays.getFullText ();
        m_journal.debug <<
            "takeOffers: saTakerGets=" << saTakerGets.getFullText ();
        m_journal.debug <<
            "takeOffers: mTxnAccountID=" <<
            RippleAddress::createHumanAccountID (mTxnAccountID);
        m_journal.debug <<
            "takeOffers:         FUNDS=" <<
            view.accountFunds (mTxnAccountID, saTakerGets).getFullText ();
    }

    if (tesSUCCESS != terResult)
    {
        m_journal.debug <<
            "final terResult=" << transToken (terResult);

        return terResult;
    }

    if (bFillOrKill && (saTakerPays || saTakerGets))
    {
        // Fill or kill and have leftovers.
        view.swapWith (view_checkpoint); // Restore with just fees paid.
        return tesSUCCESS;
    }

    if (!placeOffer
        || saTakerPays <= zero                                          // Wants nothing more.
        || saTakerGets <= zero                                          // Offering nothing more.
        || bImmediateOrCancel                                           // Do not persist.
        || view.accountFunds (mTxnAccountID, saTakerGets) <= zero)      // Not funded.
    {
        // Complete as is.
        nothing ();
    }
    else if (mPriorBalance.getNValue () < mEngine->getLedger ()->getReserve (sleCreator->getFieldU32 (sfOwnerCount) + 1))
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
        else if (!saPaid && !saGot)
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
            nothing ();
        }
    }
    else
    {
        // We need to place the remainder of the offer into its order book.
        if (m_journal.debug) m_journal.debug <<
            "offer not fully consumed:" <<
            " saTakerPays=" << saTakerPays.getFullText () <<
            " saTakerGets=" << saTakerGets.getFullText ();

        // Add offer to owner's directory.
        terResult   = view.dirAdd (uOwnerNode,
            Ledger::getOwnerDirIndex (mTxnAccountID), uLedgerIndex,
            BIND_TYPE (&Ledger::ownerDirDescriber, P_1, P_2, mTxnAccountID));

        if (tesSUCCESS == terResult)
        {
            // Update owner count.
            view.ownerCountAdjust (mTxnAccountID, 1, sleCreator);

            uint256 uBookBase   = Ledger::getBookBase (
                uPaysCurrency,
                uPaysIssuerID,
                uGetsCurrency,
                uGetsIssuerID);

            if (m_journal.debug) m_journal.debug <<
                "adding to book: " << to_string (uBookBase) <<
                " : " << saTakerPays.getHumanCurrency () <<
                "/" << RippleAddress::createHumanAccountID (saTakerPays.getIssuer ()) <<
                " -> " << saTakerGets.getHumanCurrency () <<
                "/" << RippleAddress::createHumanAccountID (saTakerGets.getIssuer ());

            // We use the original rate to place the offer.
            uDirectory = Ledger::getQualityIndex (uBookBase, uRate);

            // Add offer to order book.
            terResult = view.dirAdd (uBookNode, uDirectory, uLedgerIndex,
                BIND_TYPE (&Ledger::qualityDirDescriber, P_1, P_2,
                    saTakerPays.getCurrency (), uPaysIssuerID,
                    saTakerGets.getCurrency (), uGetsIssuerID, uRate));
        }

        if (tesSUCCESS == terResult)
        {
            if (m_journal.debug)
            {
                m_journal.debug <<
                    "sfAccount=" <<
                    RippleAddress::createHumanAccountID (mTxnAccountID);
                m_journal.debug <<
                    "uPaysIssuerID=" <<
                    RippleAddress::createHumanAccountID (uPaysIssuerID);
                m_journal.debug <<
                    "uGetsIssuerID=" <<
                    RippleAddress::createHumanAccountID (uGetsIssuerID);
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

            SLE::pointer sleOffer (mEngine->entryCreate (ltOFFER, uLedgerIndex));

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

    if (tesSUCCESS != terResult)
    {
        m_journal.debug <<
            "final terResult=" << transToken (terResult);
    }

    return terResult;
}

}

