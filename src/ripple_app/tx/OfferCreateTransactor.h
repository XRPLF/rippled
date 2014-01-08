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

#ifndef __OFFERCREATETRANSACTOR__
#define __OFFERCREATETRANSACTOR__

class OfferCreateTransactor : public Transactor
{
public:
    OfferCreateTransactor (const SerializedTransaction& txn, TransactionEngineParams params, TransactionEngine* engine) : Transactor (txn, params, engine) {}
    TER doApply ();

private:
    bool bValidOffer (
        SLE::ref            sleOfferDir,
        const uint160&      uOfferOwnerID,
        const STAmount&     saOfferPays,
        const STAmount&     saOfferGets,
        const uint160&      uTakerAccountID,
        boost::unordered_set<uint256>&  usOfferUnfundedFound,
        boost::unordered_set<uint256>&  usOfferUnfundedBecame,
        boost::unordered_set<uint160>&  usAccountTouched,
        STAmount&           saOfferFunds);

    TER takeOffers (
        const bool          bOpenLedger,
        const bool          bPassive,
        const bool          bSell,
        uint256 const&      uBookBase,
        const uint160&      uTakerAccountID,
        SLE::ref            sleTakerAccount,
        const STAmount&     saTakerPays,
        const STAmount&     saTakerGets,
        STAmount&           saTakerPaid,
        STAmount&           saTakerGot,
        bool&               bUnfunded);

    boost::unordered_set<uint256>   usOfferUnfundedFound;   // Offers found unfunded.

    typedef std::pair <uint256, uint256> missingOffer_t;
    boost::unordered_set<missingOffer_t> usMissingOffers;
};

#endif

// vim:ts=4
