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

#include <ripple/app/book/OfferStream.h>
#include <ripple/app/book/Taker.h>
#include <ripple/app/book/Types.h>
#include <ripple/app/book/Amounts.h>
#include <ripple/app/book/Quality.h>

#include <beast/cxx14/memory.h>
#include <stdexcept>

namespace ripple {

class CreateOffer
    : public Transactor
{
private:
    // What kind of offer we are placing
    core::CrossType cross_type_;

    /** Determine if we are authorized to hold the asset we want to get */
    TER
    checkAcceptAsset(IssueRef issue) const
    {
        // Only valid for custom currencies
        assert (!isXRP (issue.currency));

        SLE::pointer const issuerAccount = mEngine->entryCache (
            ltACCOUNT_ROOT, Ledger::getAccountRootIndex (issue.account));

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
            SLE::pointer const trustLine (mEngine->entryCache (
                ltRIPPLE_STATE, Ledger::getRippleStateIndex (
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

    static
    bool
    dry_offer (core::LedgerView& view, core::Offer const& offer)
    {
        if (offer.fully_consumed ())
            return true;

        auto const funds (view.accountFunds (offer.owner(),
            offer.amount().out, fhZERO_IF_FROZEN));

        return (funds <= zero);
    }

    static
    std::pair<bool, core::Quality>
    select_path (
        bool have_direct, core::OfferStream const& offers_direct,
        bool have_bridge, core::OfferStream const& leg1, core::OfferStream const& leg2);

    std::pair<TER, core::Amounts> bridged_cross (
        core::Taker& taker,
        core::LedgerView& view,
        core::LedgerView& view_cancel,
        core::Clock::time_point const when);

    std::pair<TER, core::Amounts> direct_cross (
        core::Taker& taker,
        core::LedgerView& view,
        core::LedgerView& view_cancel,
        core::Clock::time_point const when);

    // Step through the stream for as long as possible, skipping any offers
    // that are from the taker or which cross the taker's threshold.
    // Return false if the is no offer in the book, true otherwise.
    static
    bool
    step_account (core::OfferStream& stream, core::Taker const& taker)
    {
        while (stream.step ())
        {
            auto const& offer = stream.tip ();

            // This offer at the tip crosses the taker's threshold. We're done.
            if (taker.reject (offer.quality ()))
                return true;

            // This offer at the tip is not from the taker. We're done.
            if (offer.owner () != taker.account ())
                return true;
        }

        // We ran out of offers. Can't advance.
        return false;
    }

    // Fill offer as much as possible by consuming offers already on the books,
    // and adjusting account balances accordingly. 
    //
    // Charges fees on top to taker.
    std::pair<TER, core::Amounts>
    cross (
        core::LedgerView& view,
        core::LedgerView& cancel_view,
        core::Amounts const& taker_amount)
    {
        core::Clock::time_point const when (
            mEngine->getLedger ()->getParentCloseTimeNC ());

        core::Taker taker (cross_type_, view, mTxnAccountID, taker_amount, mTxn.getFlags());

        try
        {
            if (m_journal.debug)
            {
                auto const funds = view.accountFunds (
                    taker.account(), taker_amount.in, fhIGNORE_FREEZE);

                m_journal.debug << "Crossing:";
                m_journal.debug << "      Taker: " << to_string (mTxnAccountID);
                m_journal.debug << "    Balance: " << format_amount (funds);
            }

#if RIPPLE_ENABLE_AUTOBRIDGING
            if (cross_type_ == core::CrossType::IouToIou)
                return bridged_cross (taker, view, cancel_view, when);
#endif

            return direct_cross (taker, view, cancel_view, when);
        }
        catch (std::exception const& e)
        {
            m_journal.error << "Exception during offer crossing: " << e.what ();
            return std::make_pair (tecINTERNAL, taker.remaining_offer ());
        }
        catch (...)
        {
            m_journal.error << "Exception during offer crossing.";
            return std::make_pair (tecINTERNAL, taker.remaining_offer ());
        }
    }

    static
    std::string
    format_amount (STAmount const& amount)
    {
        std::string txt = amount.getText ();
        txt += "/";
        txt += amount.getHumanCurrency ();
        return txt;
    }

public:
    CreateOffer (
            core::CrossType cross_type,
            SerializedTransaction const& txn,
            TransactionEngineParams params,
            TransactionEngine* engine)
        : Transactor (
            txn,
            params,
            engine,
            deprecatedLogs().journal("CreateOffer"))
        , cross_type_ (cross_type)
    {

    }

    /** Returns the reserve the account would have if an offer was added. */
    std::uint32_t
    getAccountReserve (SLE::pointer account)
    {
        return mEngine->getLedger ()->getReserve (
            account->getFieldU32 (sfOwnerCount) + 1);
    }

    TER
    doApply () override
    {
        std::uint32_t const uTxFlags = mTxn.getFlags ();

        bool const bPassive (uTxFlags & tfPassive);
        bool const bImmediateOrCancel (uTxFlags & tfImmediateOrCancel);
        bool const bFillOrKill (uTxFlags & tfFillOrKill);
        bool const bSell (uTxFlags & tfSell);

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
        //       sequence to determine the transaction. Why is the offer sequence
        //       number insufficient?

        std::uint32_t const uAccountSequenceNext = mTxnAccount->getFieldU32 (sfSequence);
        std::uint32_t const uSequence = mTxn.getSequence ();

        // This is the original rate of the offer, and is the rate at which
        // it will be placed, even if crossing offers change the amounts that
        // end up on the books.
        std::uint64_t const uRate = getRate (saTakerGets, saTakerPays);

        TER result = tesSUCCESS;

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

            result = temINVALID_FLAG;
        }
        else if (bImmediateOrCancel && bFillOrKill)
        {
            if (m_journal.debug) m_journal.debug <<
                "Malformed transaction: both IoC and FoK set.";

            result = temINVALID_FLAG;
        }
        else if (bHaveExpiration && !uExpiration)
        {
            m_journal.warning <<
                "Malformed offer: bad expiration";

            result = temBAD_EXPIRATION;
        }
        else if (saTakerPays.isNative () && saTakerGets.isNative ())
        {
            m_journal.warning <<
                "Malformed offer: XRP for XRP";

            result = temBAD_OFFER;
        }
        else if (saTakerPays <= zero || saTakerGets <= zero)
        {
            m_journal.warning <<
                "Malformed offer: bad amount";

            result = temBAD_OFFER;
        }
        else if (uPaysCurrency == uGetsCurrency && uPaysIssuerID == uGetsIssuerID)
        {
            m_journal.warning <<
                "Malformed offer: redundant offer";

            result = temREDUNDANT;
        }
        // We don't allow a non-native currency to use the currency code XRP.
        else if (badCurrency() == uPaysCurrency || badCurrency() == uGetsCurrency)
        {
            m_journal.warning <<
                "Malformed offer: Bad currency.";

            result = temBAD_CURRENCY;
        }
        else if (saTakerPays.isNative () != !uPaysIssuerID ||
                 saTakerGets.isNative () != !uGetsIssuerID)
        {
            m_journal.warning <<
                "Malformed offer: bad issuer";

            result = temBAD_ISSUER;
        }
        else if (view.isGlobalFrozen (uPaysIssuerID) || view.isGlobalFrozen (uGetsIssuerID))
        {
            m_journal.warning <<
                "Offer involves frozen asset";

            result = tecFROZEN;
        }
        else if (view.accountFunds (
            mTxnAccountID, saTakerGets, fhZERO_IF_FROZEN) <= zero)
        {
            m_journal.warning <<
                "delay: Offers must be at least partially funded.";

            result = tecUNFUNDED_OFFER;
        }
        // This can probably be simplified to make sure that you cancel sequences
        // before the transaction sequence number.
        else if (bHaveCancel && (!uCancelSequence || uAccountSequenceNext - 1 <= uCancelSequence))
        {
            if (m_journal.debug) m_journal.debug <<
                "uAccountSequenceNext=" << uAccountSequenceNext <<
                " uOfferSequence=" << uCancelSequence;

            result = temBAD_SEQUENCE;
        }

        if (result != tesSUCCESS)
        {
            m_journal.debug << "final result: " << transToken (result);
            return result;
        }

        // Process a cancellation request that's passed along with an offer.
        if ((result == tesSUCCESS) && bHaveCancel)
        {
            SLE::pointer sleCancel = mEngine->entryCache (ltOFFER,
                Ledger::getOfferIndex (mTxnAccountID, uCancelSequence));

            // It's not an error to not find the offer to cancel: it might have
            // been consumed or removed. If it is found, however, it's an error
            // to fail to delete it.
            if (sleCancel)
            {
                m_journal.debug << "Create cancels order " << uCancelSequence;
                result = view.offerDelete (sleCancel);
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
        if (result == tesSUCCESS && !saTakerPays.isNative ())
            result = checkAcceptAsset (Issue (uPaysCurrency, uPaysIssuerID));

        bool const bOpenLedger (mParams & tapOPEN_LEDGER);
        bool crossed = false;

        if (result == tesSUCCESS)
        {
            // We reverse pays and gets because during crossing we are taking.
            core::Amounts const taker_amount (saTakerGets, saTakerPays);

            // The amount of the offer that is unfilled after crossing has been
            // performed. It may be equal to the original amount (didn't cross),
            // empty (fully crossed), or something in-between.
            core::Amounts place_offer;

            m_journal.debug << "Attempting cross: " << 
                to_string (taker_amount.in.issue ()) << " -> " <<
                to_string (taker_amount.out.issue ());

            if (m_journal.trace)
            {
                m_journal.debug << "   mode: " <<
                    (bPassive ? "passive " : "") <<
                    (bSell ? "sell" : "buy");
                m_journal.trace <<"     in: " << format_amount (taker_amount.in);
                m_journal.trace << "    out: " << format_amount (taker_amount.out);
            }

            std::tie(result, place_offer) = cross (view, view_checkpoint, taker_amount);
            assert (result != tefINTERNAL);

            if (m_journal.trace)
            {
                m_journal.trace << "Cross result: " << transToken (result);
                m_journal.trace << "     in: " << format_amount (place_offer.in);
                m_journal.trace << "    out: " << format_amount (place_offer.out);
            }

            if (result == tecFAILED_PROCESSING && bOpenLedger)
                result = telFAILED_PROCESSING;

            if (result != tesSUCCESS)
            {
                m_journal.debug << "final result: " << transToken (result);
                return result;
            }

            assert (saTakerGets.issue () == place_offer.in.issue ());
            assert (saTakerPays.issue () == place_offer.out.issue ());

            if (taker_amount != place_offer)
                crossed = true;

            // The offer that we need to place after offer crossing should
            // never be negative. If it is, something went very very wrong.
            if (place_offer.in < zero || place_offer.out < zero)
            {
                m_journal.fatal << "Cross left offer negative!";
                return tefINTERNAL;
            }

            if (place_offer.in == zero || place_offer.out == zero)
            {
                m_journal.debug << "Offer fully crossed!";
                return result;
            }

            // We now need to adjust the offer to reflect the amount left after
            // crossing. We reverse in and out here, since during crossing we
            // were the taker.
            saTakerPays = place_offer.out;
            saTakerGets = place_offer.in;
        }

        assert (saTakerPays > zero && saTakerGets > zero);

        if (result != tesSUCCESS)
        {
            m_journal.debug << "final result: " << transToken (result);
            return result;
        }

        if (m_journal.trace)
        {
            m_journal.trace << "Place" << (crossed ? " remaining " : " ") << "offer:";
            m_journal.trace << "    Pays: " << saTakerPays.getFullText ();
            m_journal.trace << "    Gets: " << saTakerGets.getFullText ();
        }

        // For 'fill or kill' offers, failure to fully cross means that the
        // entire operation should be aborted, with only fees paid.
        if (bFillOrKill)
        {
            m_journal.trace << "Fill or Kill: offer killed";
            view.swapWith (view_checkpoint);
            return result;
        }

        // For 'immediate or cancel' offers, the amount remaining doesn't get
        // placed - it gets cancelled and the operation succeeds.
        if (bImmediateOrCancel)
        {
            m_journal.trace << "Immediate or cancel: offer cancelled";
            return result;
        }

        if (mPriorBalance.getNValue () < getAccountReserve (sleCreator))
        {
            // If we are here, the signing account had an insufficient reserve
            // *prior* to our processing. If something actually crossed, then
            // allow this; otherwise, we just claim a fee.
            if (!crossed)
                result = tecINSUF_RESERVE_OFFER;

            if (result != tesSUCCESS)
                m_journal.debug << "final result: " << transToken (result);

            return result;
        }

        // We need to place the remainder of the offer into its order book.
        auto const offer_index = Ledger::getOfferIndex (mTxnAccountID, uSequence);

        std::uint64_t uOwnerNode;
        std::uint64_t uBookNode;
        uint256 uDirectory;

        // Add offer to owner's directory.
        result = view.dirAdd (uOwnerNode,
            Ledger::getOwnerDirIndex (mTxnAccountID), offer_index,
            std::bind (
                &Ledger::ownerDirDescriber, std::placeholders::_1,
                std::placeholders::_2, mTxnAccountID));

        if (result == tesSUCCESS)
        {
            // Update owner count.
            view.incrementOwnerCount (sleCreator);

            if (m_journal.trace) m_journal.trace <<
                "adding to book: " << to_string (saTakerPays.issue ()) <<
                " : " << to_string (saTakerGets.issue ());

            uint256 const book_base (Ledger::getBookBase (
                { saTakerPays.issue (), saTakerGets.issue () }));

            // We use the original rate to place the offer.
            uDirectory = Ledger::getQualityIndex (book_base, uRate);

            // Add offer to order book.
            result = view.dirAdd (uBookNode, uDirectory, offer_index,
                std::bind (
                    &Ledger::qualityDirDescriber, std::placeholders::_1,
                    std::placeholders::_2, saTakerPays.getCurrency (),
                    uPaysIssuerID, saTakerGets.getCurrency (),
                    uGetsIssuerID, uRate));
        }

        if (result == tesSUCCESS)
        {
            SLE::pointer sleOffer (mEngine->entryCreate (ltOFFER, offer_index));

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
        }

        if (result != tesSUCCESS)
            m_journal.debug << "final result: " << transToken (result);

        return result;
    }
};

TER
transact_CreateOffer (
    SerializedTransaction const& txn,
    TransactionEngineParams params,
    TransactionEngine* engine)
{
    core::CrossType cross_type = core::CrossType::IouToIou;

    bool const pays_xrp = txn.getFieldAmount (sfTakerPays).isNative ();
    bool const gets_xrp = txn.getFieldAmount (sfTakerGets).isNative ();

    if (pays_xrp && !gets_xrp)
        cross_type = core::CrossType::IouToXrp;
    else if (gets_xrp && !pays_xrp)
        cross_type = core::CrossType::XrpToIou;

    return CreateOffer (cross_type, txn, params, engine).apply ();
}

}
