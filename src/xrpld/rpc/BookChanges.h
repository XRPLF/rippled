#pragma once

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/jss.h>

#include <memory>

namespace json {
class Value;
}  // namespace json

namespace xrpl {

class ReadView;
class Transaction;
class TxMeta;
class STTx;

namespace RPC {

template <class L>
json::Value
computeBookChanges(std::shared_ptr<L const> const& lpAccepted)
{
    std::map<
        std::string,
        std::tuple<
            STAmount,                 // side A volume
            STAmount,                 // side B volume
            STAmount,                 // high rate
            STAmount,                 // low rate
            STAmount,                 // open rate
            STAmount,                 // close rate
            std::optional<uint256>>>  // optional: domain id
        tally;

    for (auto& tx : lpAccepted->txs)
    {
        if (!tx.first || !tx.second || !tx.first->isFieldPresent(sfTransactionType))
            continue;

        std::optional<uint32_t> offerCancel;
        uint16_t const tt = tx.first->getFieldU16(sfTransactionType);
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
            uint16_t const nodeType = node.getFieldU16(sfLedgerEntryType);

            // we only care about ltOFFER objects being modified or
            // deleted
            if (nodeType != ltOFFER || metaType == sfCreatedNode)
                continue;

            // if either FF or PF are missing we can't compute
            // but generally these are cancelled rather than crossed
            // so skipping them is consistent
            if (!node.isFieldPresent(sfFinalFields) || !node.isFieldPresent(sfPreviousFields))
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
            STAmount const deltaGets = finalFields.getFieldAmount(sfTakerGets) -
                previousFields.getFieldAmount(sfTakerGets);
            STAmount const deltaPays = finalFields.getFieldAmount(sfTakerPays) -
                previousFields.getFieldAmount(sfTakerPays);

            std::string const g{to_string(deltaGets.asset())};
            std::string const p{to_string(deltaPays.asset())};

            bool const noswap = isXRP(deltaGets) || (!isXRP(deltaPays) && (g < p));

            STAmount first = noswap ? deltaGets : deltaPays;
            STAmount second = noswap ? deltaPays : deltaGets;

            // defensively programmed, should (probably) never happen
            if (second == beast::kZero)
                continue;

            STAmount const rate = divide(first, second, noIssue());

            if (first < beast::kZero)
                first = -first;

            if (second < beast::kZero)
                second = -second;

            std::stringstream ss;
            if (noswap)
            {
                ss << g << "|" << p;
            }
            else
            {
                ss << p << "|" << g;
            }

            std::optional<uint256> const domain = finalFields[~sfDomainID];

            std::string const key{ss.str()};

            if (!tally.contains(key))
            {
                tally[key] = {
                    first,   // side A vol
                    second,  // side B vol
                    rate,    // high
                    rate,    // low
                    rate,    // open
                    rate,    // close
                    domain};
            }
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

                std::get<5>(entry) = rate;    // close
                std::get<6>(entry) = domain;  // domain
            }
        }
    }

    json::Value jvObj(json::ValueType::Object);
    jvObj[jss::type] = "bookChanges";

    // retrieve validated information from LedgerHeader class
    jvObj[jss::validated] = lpAccepted->header().validated;
    jvObj[jss::ledger_index] = lpAccepted->header().seq;
    jvObj[jss::ledger_hash] = to_string(lpAccepted->header().hash);
    jvObj[jss::ledger_time] =
        json::Value::UInt(lpAccepted->header().closeTime.time_since_epoch().count());

    jvObj[jss::changes] = json::ValueType::Array;

    auto volToStr = [](STAmount const& vol) {
        return vol.asset().visit(
            [&](Issue const& issue) {
                if (isXRP(issue))
                    return to_string(vol.xrp());
                return to_string(vol.iou());
            },
            [&](MPTIssue const&) { return to_string(vol.mpt()); });
    };

    for (auto const& entry : tally)
    {
        json::Value& inner = jvObj[jss::changes].append(json::ValueType::Object);

        STAmount const volA = std::get<0>(entry.second);
        STAmount const volB = std::get<1>(entry.second);

        volA.asset().visit(
            [&](Issue const&) {
                inner[jss::currency_a] = (isXRP(volA) ? "XRP_drops" : to_string(volA.asset()));
            },
            [&](MPTIssue const&) { inner[jss::mpt_issuance_id_a] = to_string(volA.asset()); });

        volB.asset().visit(
            [&](Issue const&) {
                inner[jss::currency_b] = (isXRP(volB) ? "XRP_drops" : to_string(volB.asset()));
            },
            [&](MPTIssue const&) { inner[jss::mpt_issuance_id_b] = to_string(volB.asset()); });

        inner[jss::volume_a] = volToStr(volA);
        inner[jss::volume_b] = volToStr(volB);

        inner[jss::high] = to_string(std::get<2>(entry.second).iou());
        inner[jss::low] = to_string(std::get<3>(entry.second).iou());
        inner[jss::open] = to_string(std::get<4>(entry.second).iou());
        inner[jss::close] = to_string(std::get<5>(entry.second).iou());

        std::optional<uint256> const domain = std::get<6>(entry.second);
        if (domain)
            inner[jss::domain] = to_string(*domain);
    }

    return jvObj;
}

}  // namespace RPC
}  // namespace xrpl
