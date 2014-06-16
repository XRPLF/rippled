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

#include <ripple/module/app/transactors/CreateOfferDirect.h>
#include <ripple/module/app/transactors/CreateOfferBridged.h>

#if RIPPLE_USE_OLD_CREATE_TRANSACTOR
#include <ripple/module/app/transactors/CreateOfferLegacy.h>
#endif

#include <ripple/module/app/book/OfferStream.h>
#include <ripple/module/app/book/Taker.h>
#include <ripple/module/app/book/Types.h>

namespace ripple {

CreateOffer::CreateOffer (
    SerializedTransaction const& txn,
    TransactionEngineParams params,
    TransactionEngine* engine)
    : Transactor (
        txn,
        params,
        engine,
        LogPartition::getJournal <CreateOfferLog> ())
{

}

TER
CreateOffer::checkAcceptAsset(core::AssetRef asset) const
{
    /* Only valid for custom currencies */
    assert (!asset.is_xrp ());

    SLE::pointer const issuerAccount = mEngine->entryCache (
        ltACCOUNT_ROOT, Ledger::getAccountRootIndex (asset.issuer));

    if (!issuerAccount)
    {
        if (m_journal.warning) m_journal.warning <<
            "delay: can't receive IOUs from non-existent issuer: " <<
            RippleAddress::createHumanAccountID (asset.issuer);

        return (mParams & tapRETRY)
            ? terNO_ACCOUNT
            : tecNO_ISSUER;
    }

    if (issuerAccount->getFieldU32 (sfFlags) & lsfRequireAuth)
    {
        SLE::pointer const trustLine (mEngine->entryCache (
            ltRIPPLE_STATE, Ledger::getRippleStateIndex (
                mTxnAccountID, asset.issuer, asset.currency)));

        if (!trustLine)
        {
            return (mParams & tapRETRY)
                ? terNO_LINE
                : tecNO_LINE;
        }

        // Entries have a canonical representation, determined by a
        // lexicographical "greater than" comparison employing strict weak
        // ordering. Determine which entry we need to access.
        bool const canonical_gt (mTxnAccountID > asset.issuer);

        bool const need_auth (trustLine->getFieldU32 (sfFlags) &
            (canonical_gt ? lsfLowAuth : lsfHighAuth));

        if (need_auth)
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

TER
CreateOffer::doApply ()
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

    if (!saTakerPays.isLegalNet () || !saTakerGets.isLegalNet ())
        return temBAD_AMOUNT;

    uint160 const& uPaysIssuerID = saTakerPays.getIssuer ();
    uint160 const& uPaysCurrency = saTakerPays.getCurrency ();

    uint160 const& uGetsIssuerID = saTakerGets.getIssuer ();
    uint160 const& uGetsCurrency = saTakerGets.getCurrency ();

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

    // This is the original rate of this offer, and is the rate at which it will
    // be placed, even if crossing offers change the amounts.
    std::uint64_t const uRate = STAmount::getRate (saTakerGets, saTakerPays);

    TER terResult (tesSUCCESS);
    uint256 uDirectory;
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
    else if (saTakerPays.isNative () != !uPaysIssuerID ||
             saTakerGets.isNative () != !uGetsIssuerID)
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
            Ledger::getOfferIndex (mTxnAccountID, uCancelSequence));
        SLE::pointer sleCancel = mEngine->entryCache (ltOFFER, uCancelIndex);

        // It's not an error to not find the offer to cancel: it might have
        // been consumed or removed as we are processing.
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

    // Make sure that we are authorized to hold what the taker will pay us.
    if (terResult == tesSUCCESS && !saTakerPays.isNative ())
        terResult = checkAcceptAsset (core::Asset (uPaysCurrency, uPaysIssuerID));

    bool crossed = false;
    bool const bOpenLedger (mParams & tapOPEN_LEDGER);

    if (terResult == tesSUCCESS)
    {
        // We reverse gets and pays because during offer crossing we are taking.
        core::Amounts const taker_amount (saTakerGets, saTakerPays);
        
        // The amount of the offer that we will need to place, after we finish
        // offer crossing processing. It may be equal to the original amount,
        // empty (fully crossed), or something in-between.
        core::Amounts place_offer;

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
            RippleAddress::createHumanAccountID (mTxnAccountID);
        m_journal.debug <<
            "takeOffers:         FUNDS=" <<
            view.accountFunds (mTxnAccountID, saTakerGets).getFullText ();
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

    if (saTakerPays <= zero ||                // Wants nothing more.
        saTakerGets <= zero ||                // Offering nothing more.
        bImmediateOrCancel)                   // Do not persist.
    {
        // Complete as is.
    }
    else if (mPriorBalance.getNValue () < accountReserve)
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
        // We need to place the remainder of the offer into its order book.
        if (m_journal.debug) m_journal.debug <<
            "offer not fully consumed:" <<
            " saTakerPays=" << saTakerPays.getFullText () <<
            " saTakerGets=" << saTakerGets.getFullText ();

        // Add offer to owner's directory.
        terResult   = view.dirAdd (uOwnerNode,
            Ledger::getOwnerDirIndex (mTxnAccountID), uLedgerIndex,
            std::bind (&Ledger::ownerDirDescriber, std::placeholders::_1,
                       std::placeholders::_2, mTxnAccountID));

        if (tesSUCCESS == terResult)
        {
            // Update owner count.
            view.ownerCountAdjust (mTxnAccountID, 1, sleCreator);

            uint256 uBookBase = Ledger::getBookBase (
                uPaysCurrency, uPaysIssuerID,
                uGetsCurrency, uGetsIssuerID);

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
                std::bind (&Ledger::qualityDirDescriber, std::placeholders::_1,
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

    if (terResult != tesSUCCESS)
    {
        m_journal.debug <<
            "final terResult=" << transToken (terResult);
    }

    return terResult;
}

std::unique_ptr <Transactor> make_CreateOffer (
    SerializedTransaction const& txn,
    TransactionEngineParams params,
    TransactionEngine* engine)
{
#if RIPPLE_USE_OLD_CREATE_TRANSACTOR
    return std::make_unique <CreateOfferLegacy> (txn, params, engine);
#else
    STAmount const& amount_in = txn.getFieldAmount (sfTakerPays);
    STAmount const& amount_out = txn.getFieldAmount (sfTakerGets);
    
    // Autobridging is only in effect when an offer does not involve XRP
    if (!amount_in.isNative() && !amount_out.isNative ())
        return std::make_unique <CreateOfferBridged> (txn, params, engine);
    
    return std::make_unique <CreateOfferDirect> (txn, params, engine);
#endif
}

}
