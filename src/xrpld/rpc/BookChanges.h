/** @file
 *  Header-only template that aggregates per-ledger order book activity into
 *  OHLCV-style market data.
 *
 *  `computeBookChanges` is the sole entry point. It is consumed by two
 *  independent paths: the `book_changes` WebSocket subscription stream
 *  (pushed from `NetworkOPs` on each validated ledger) and the
 *  `book_changes` RPC command (on-demand via `handlers/orderbook/BookChanges.cpp`).
 */

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

/** Scan a closed ledger and produce OHLCV market-data for every crossed order book.
 *
 *  Iterates `sfAffectedNodes` in each transaction's metadata, identifies
 *  modified or deleted `ltOFFER` nodes, and accumulates per-pair Open, High,
 *  Low, Close, and Volume data into a tally map. The tally is then serialised
 *  to JSON and returned.
 *
 *  Filtering rules applied before any accumulation:
 *  - Only `ltOFFER` nodes are considered; all other object types are ignored.
 *  - `sfCreatedNode` entries are skipped — a freshly created, uncrossed offer
 *    has no volume yet.
 *  - Nodes missing either `sfFinalFields` or `sfPreviousFields` are skipped;
 *    this is typical of cancelled offers where no crossing occurred.
 *  - `sfDeletedNode` entries whose `sfSequence` matches the transaction's
 *    cancel target (`sfOfferSequence` in `OfferCancel` / `OfferCreate`) are
 *    excluded so that explicit cancellations are not counted as volume.
 *
 *  Canonical pair key ordering: XRP always occupies the first position; for
 *  two non-XRP assets, the lexicographically smaller asset string comes first.
 *  This ensures one tally entry per book regardless of offer direction.
 *
 *  The exchange rate is `divide(first, second, noIssue())`. `noIssue()` is a
 *  static sentinel (`noCurrency()` / `noAccount()`) used because the rate is a
 *  dimensionless ratio and is not attributable to any specific IOU issuer.
 *
 *  @tparam L Ledger type. Must expose `.txs` (iterable of `(STTx, TxMeta)`
 *      pairs) and `.header()` (providing `seq`, `hash`, `validated`,
 *      `closeTime`). No virtual interface is required; this template works
 *      with both production `ReadView`-derived types and lightweight test
 *      fixtures.
 *  @param lpAccepted The closed ledger to scan.
 *  @return A `Json::Value` object containing `type`, `validated`,
 *      `ledger_index`, `ledger_hash`, `ledger_time`, and a `changes` array.
 *      Each element of `changes` carries `currency_a` / `currency_b` (IOU or
 *      XRP pairs) or `mpt_issuance_id_a` / `mpt_issuance_id_b` (MPT pairs),
 *      plus `volume_a`, `volume_b`, `high`, `low`, `open`, `close`, and an
 *      optional `domain` field for permissioned-DEX tagged books.
 *  @note Open and close reflect transaction ordering within the ledger, not
 *      wall-clock timestamps. The `domain` field of the *last* processed trade
 *      for a given pair wins; this is consistent because all offers within one
 *      permissioned book share the same domain.
 *  @note A zero-value `second` delta causes the pair to be skipped entirely
 *      (defensive guard against malformed metadata — should never occur in
 *      practice). Negative volume deltas are normalised with `abs()` before
 *      accumulation.
 */
template <class L>
json::Value
computeBookChanges(std::shared_ptr<L const> const& lpAccepted)
{
    std::map<
        std::string,
        std::tuple<
            STAmount,                 // vol A
            STAmount,                 // vol B
            STAmount,                 // high
            STAmount,                 // low
            STAmount,                 // open (first trade, never updated)
            STAmount,                 // close (most recent trade)
            std::optional<uint256>>>  // domain id (last trade wins)
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

            if (nodeType != ltOFFER || metaType == sfCreatedNode)
                continue;

            if (!node.isFieldPresent(sfFinalFields) || !node.isFieldPresent(sfPreviousFields))
                continue;

            auto const& ffBase = node.peekAtField(sfFinalFields);
            auto const& finalFields = ffBase.template downcast<STObject>();
            auto const& pfBase = node.peekAtField(sfPreviousFields);
            auto const& previousFields = pfBase.template downcast<STObject>();

            if (!finalFields.isFieldPresent(sfTakerGets) ||
                !finalFields.isFieldPresent(sfTakerPays) ||
                !previousFields.isFieldPresent(sfTakerGets) ||
                !previousFields.isFieldPresent(sfTakerPays))
                continue;

            // filter out any offers deleted by explicit offer cancels
            if (metaType == sfDeletedNode && offerCancel &&
                finalFields.getFieldU32(sfSequence) == *offerCancel)
                continue;

            STAmount const deltaGets = finalFields.getFieldAmount(sfTakerGets) -
                previousFields.getFieldAmount(sfTakerGets);
            STAmount const deltaPays = finalFields.getFieldAmount(sfTakerPays) -
                previousFields.getFieldAmount(sfTakerPays);

            std::string const g{to_string(deltaGets.asset())};
            std::string const p{to_string(deltaPays.asset())};

            bool const noswap = isXRP(deltaGets) || (!isXRP(deltaPays) && (g < p));

            STAmount first = noswap ? deltaGets : deltaPays;
            STAmount second = noswap ? deltaPays : deltaGets;

            if (second == beast::kZERO)
                continue;

            STAmount const rate = divide(first, second, noIssue());

            if (first < beast::kZERO)
                first = -first;

            if (second < beast::kZERO)
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
                tally[key] = {first, second, rate, rate, rate, rate, domain};
            }
            else
            {
                auto& entry = tally[key];

                std::get<0>(entry) += first;
                std::get<1>(entry) += second;

                if (std::get<2>(entry) < rate)
                    std::get<2>(entry) = rate;

                if (std::get<3>(entry) > rate)
                    std::get<3>(entry) = rate;

                std::get<5>(entry) = rate;
                std::get<6>(entry) = domain;
            }
        }
    }

    json::Value jvObj(json::ValueType::Object);
    jvObj[jss::type] = "bookChanges";

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
