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

#ifndef RIPPLE_TX_OFFERCREATE_H_INCLUDED
#define RIPPLE_TX_OFFERCREATE_H_INCLUDED

#include <unordered_set>

namespace ripple {

class OfferCreateTransactorLog;

template <>
char const*
LogPartition::getPartitionName <OfferCreateTransactorLog> ()
{
    return "Tx/OfferCreate";
}

class OfferCreateTransactor
    : public Transactor
{
private:
    template <class T>
    static std::string
    get_compare_sign (T const& lhs, T const& rhs)
    {
        if (lhs > rhs)
            return ">";

        if (rhs > lhs)
            return "<";

        // If neither is bigger than the other, they must be equal
        return "=";
    }

public:
    OfferCreateTransactor (
        SerializedTransaction const& txn,
        TransactionEngineParams params,
        TransactionEngine* engine)
        : Transactor (
            txn,
            params,
            engine,
            LogPartition::getJournal <OfferCreateTransactorLog> ())
    {

    }

    TER doApply ();

private:
    bool isValidOffer (
        SLE::ref            sleOfferDir,
        uint160 const&      uOfferOwnerID,
        STAmount const&     saOfferPays,
        STAmount const&     saOfferGets,
        uint160 const&      uTakerAccountID,
        std::unordered_set<uint256>&  usOfferUnfundedBecame,
        std::unordered_set<uint160>&  usAccountTouched,
        STAmount&           saOfferFunds);

    TER takeOffers (
        bool const bOpenLedger,
        bool const bPassive,
        bool const bSell,
        uint256 const&      uBookBase,
        uint160 const&      uTakerAccountID,
        SLE::ref            sleTakerAccount,
        STAmount const&     saTakerPays,
        STAmount const&     saTakerGets,
        STAmount&           saTakerPaid,
        STAmount&           saTakerGot,
        bool&               bUnfunded);

    // Offers found unfunded.
    std::unordered_set<uint256> usOfferUnfundedFound;

    typedef std::pair <uint256, uint256> missingOffer_t;
    std::set<missingOffer_t> usMissingOffers;
};

}

#endif
