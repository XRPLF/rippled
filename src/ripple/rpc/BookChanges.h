//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

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

#ifndef RIPPLE_RPC_BOOKCHANGES_H_INCLUDED
#define RIPPLE_RPC_BOOKCHANGES_H_INCLUDED

namespace Json {
class Value;
}

namespace ripple {

class ReadView;
class Transaction;
class TxMeta;
class STTx;

namespace RPC {

template <class L>
Json::Value
computeBookChanges(std::shared_ptr<L const> const& lpAccepted)
{
    std::map<
        std::string,
        std::tuple<
            STAmount,  // side A volume
            STAmount,  // side B volume
            STAmount,  // high rate
            STAmount,  // low rate
            STAmount,  // open rate
            STAmount   // close rate
            >>
        tally;

    for (auto& tx : lpAccepted->txs)
    {
        if (!tx.first || !tx.second ||
            !tx.first->isFieldPresent(sfTransactionType))
            continue;

        std::optional<uint32_t> offerCancel;
        uint16_t tt = tx.first->getFieldU16(sfTransactionType);
        switch (tt)
        {
            case ttOFFER_CANCEL:
            case ttOFFER_CREATE: {
                if (tx.first->isFieldPresent(sfOfferSequence))
                    offerCancel = tx.first->getFieldU32(sfOfferSequence);
                break;
            }
            // in future if any other ways emerge to cancel an offer
            // this switch makes them easy to add
            default:
                break;
        }

        for (auto const& node : tx.second->getFieldArray(sfAffectedNodes))
        {
            SField const& metaType = node.getFName();
            uint16_t nodeType = node.getFieldU16(sfLedgerEntryType);

            // we only care about ltOFFER objects being modified or
            // deleted
            if (nodeType != ltOFFER || metaType == sfCreatedNode)
                continue;

            // if either FF or PF are missing we can't compute
            // but generally these are cancelled rather than crossed
            // so skipping them is consistent
            if (!node.isFieldPresent(sfFinalFields) ||
                !node.isFieldPresent(sfPreviousFields))
                continue;

            auto const& ffBase = node.peekAtField(sfFinalFields);
            auto const& finalFields = ffBase.template downcast<STObject>();
            auto const& pfBase = node.peekAtField(sfPreviousFields);
            auto const& previousFields = pfBase.template downcast<STObject>();

            // defensive case that should never be hit
            if (!finalFields.isFieldPresent(sfTakerGets) ||
                !finalFields.isFieldPresent(sfTakerPays) ||
                !previousFields.isFieldPresent(sfTakerGets) ||
                !previousFields.isFieldPresent(sfTakerPays))
                continue;

            // filter out any offers deleted by explicit offer cancels
            if (metaType == sfDeletedNode && offerCancel &&
                finalFields.getFieldU32(sfSequence) == *offerCancel)
                continue;

            // compute the difference in gets and pays actually
            // affected onto the offer
            STAmount deltaGets = finalFields.getFieldAmount(sfTakerGets) -
                previousFields.getFieldAmount(sfTakerGets);
            STAmount deltaPays = finalFields.getFieldAmount(sfTakerPays) -
                previousFields.getFieldAmount(sfTakerPays);

            std::string g{to_string(deltaGets.issue())};
            std::string p{to_string(deltaPays.issue())};

            bool const noswap =
                isXRP(deltaGets) ? true : (isXRP(deltaPays) ? false : (g < p));

            STAmount first = noswap ? deltaGets : deltaPays;
            STAmount second = noswap ? deltaPays : deltaGets;

            // defensively programmed, should (probably) never happen
            if (second == beast::zero)
                continue;

            STAmount rate = divide(first, second, noIssue());

            if (first < beast::zero)
                first = -first;

            if (second < beast::zero)
                second = -second;

            std::stringstream ss;
            if (noswap)
                ss << g << "|" << p;
            else
                ss << p << "|" << g;

            std::string key{ss.str()};

            if (tally.find(key) == tally.end())
                tally[key] = {
                    first,   // side A vol
                    second,  // side B vol
                    rate,    // high
                    rate,    // low
                    rate,    // open
                    rate     // close
                };
            else
            {
                // increment volume
                auto& entry = tally[key];

                std::get<0>(entry) += first;   // side A vol
                std::get<1>(entry) += second;  // side B vol

                if (std::get<2>(entry) < rate)  // high
                    std::get<2>(entry) = rate;

                if (std::get<3>(entry) > rate)  // low
                    std::get<3>(entry) = rate;

                std::get<5>(entry) = rate;  // close
            }
        }
    }

    Json::Value jvObj(Json::objectValue);
    jvObj[jss::type] = "bookChanges";
    jvObj[jss::ledger_index] = lpAccepted->info().seq;
    jvObj[jss::ledger_hash] = to_string(lpAccepted->info().hash);
    jvObj[jss::ledger_time] = Json::Value::UInt(
        lpAccepted->info().closeTime.time_since_epoch().count());

    jvObj[jss::changes] = Json::arrayValue;

    for (auto const& entry : tally)
    {
        Json::Value& inner = jvObj[jss::changes].append(Json::objectValue);

        STAmount volA = std::get<0>(entry.second);
        STAmount volB = std::get<1>(entry.second);

        inner[jss::currency_a] =
            (isXRP(volA) ? "XRP_drops" : to_string(volA.issue()));
        inner[jss::currency_b] =
            (isXRP(volB) ? "XRP_drops" : to_string(volB.issue()));

        inner[jss::volume_a] =
            (isXRP(volA) ? to_string(volA.xrp()) : to_string(volA.iou()));
        inner[jss::volume_b] =
            (isXRP(volB) ? to_string(volB.xrp()) : to_string(volB.iou()));

        inner[jss::high] = to_string(std::get<2>(entry.second).iou());
        inner[jss::low] = to_string(std::get<3>(entry.second).iou());
        inner[jss::open] = to_string(std::get<4>(entry.second).iou());
        inner[jss::close] = to_string(std::get<5>(entry.second).iou());
    }

    return jvObj;
}

}  // namespace RPC
}  // namespace ripple

#endif
