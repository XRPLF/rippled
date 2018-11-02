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

#include <ripple/rpc/DeliveredAmount.h>

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/OpenLedger.h>
#include <ripple/app/misc/Transaction.h>
#include <ripple/ledger/View.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/AccountID.h>
#include <ripple/protocol/Feature.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <boost/algorithm/string/case_conv.hpp>

namespace ripple {
namespace RPC {

/*
  GetLedgerIndex and GetCloseTime are lambdas that allow the close time and
  ledger index to be lazily calculated. Without these lambdas, these values
  would be calculated even when not needed, and in some circumstances they are
  not trivial to compute.

  GetFix1623Enabled is a callable that returns a bool
  GetLedgerIndex is a callable that returns a LedgerIndex
  GetCloseTime is a callable that returns a
               boost::optional<NetClock::time_point>
 */
template<class GetFix1623Enabled, class GetLedgerIndex, class GetCloseTime>
void
insertDeliveredAmount(
    Json::Value& meta,
    GetFix1623Enabled const& getFix1623Enabled,
    GetLedgerIndex const& getLedgerIndex,
    GetCloseTime const& getCloseTime,
    std::shared_ptr<STTx const> serializedTx,
    TxMeta const& transactionMeta)
{
    {
        TxType const tt{serializedTx->getTxnType()};
        if (tt != ttPAYMENT &&
            tt != ttCHECK_CASH &&
            tt != ttACCOUNT_DELETE)
            return;

        if (tt == ttCHECK_CASH &&
            !getFix1623Enabled())
            return;
    }

    // if the transaction failed nothing could have been delivered.
    if (transactionMeta.getResultTER() != tesSUCCESS)
        return;

    if (transactionMeta.hasDeliveredAmount())
    {
        meta[jss::delivered_amount] =
            transactionMeta.getDeliveredAmount()
                .getJson(JsonOptions::include_date);
        return;
    }

    if (serializedTx->isFieldPresent(sfAmount))
    {
        using namespace std::chrono_literals;

        // Ledger 4594095 is the first ledger in which the DeliveredAmount field
        // was present when a partial payment was made and its absence indicates
        // that the amount delivered is listed in the Amount field.
        //
        // If the ledger closed long after the DeliveredAmount code was deployed
        // then its absence indicates that the amount delivered is listed in the
        // Amount field. DeliveredAmount went live January 24, 2014.
        // 446000000 is in Feb 2014, well after DeliveredAmount went live
        if (getLedgerIndex() >= 4594095 ||
            getCloseTime() > NetClock::time_point{446000000s})
        {
            meta[jss::delivered_amount] =
                serializedTx->getFieldAmount(sfAmount)
                   .getJson(JsonOptions::include_date);
            return;
        }
    }

    // report "unavailable" which cannot be parsed into a sensible amount.
    meta[jss::delivered_amount] = Json::Value("unavailable");
}

void
insertDeliveredAmount(
    Json::Value& meta,
    ReadView const& ledger,
    std::shared_ptr<STTx const> serializedTx,
    TxMeta const& transactionMeta)
{
    if (!serializedTx)
        return;

    auto const info = ledger.info();
    auto const getFix1623Enabled = [&ledger] {
        return ledger.rules().enabled(fix1623);
    };
    auto const getLedgerIndex = [&info] {
        return info.seq;
    };
    auto const getCloseTime = [&info] {
        return info.closeTime;
    };

    insertDeliveredAmount(
        meta,
        getFix1623Enabled,
        getLedgerIndex,
        getCloseTime,
        std::move(serializedTx),
        transactionMeta);
}

void
insertDeliveredAmount(
    Json::Value& meta,
    RPC::Context& context,
    std::shared_ptr<Transaction> transaction,
    TxMeta const& transactionMeta)
{
    if (!transaction)
        return;

    auto const serializedTx = transaction->getSTransaction ();
    if (! serializedTx)
        return;


    // These lambdas are used to compute the values lazily
    auto const getFix1623Enabled = [&context]() -> bool {
        auto const view = context.app.openLedger().current();
        if (!view)
            return false;
        return view->rules().enabled(fix1623);
    };
    auto const getLedgerIndex = [&transaction]() -> LedgerIndex {
        return transaction->getLedger();
    };
    auto const getCloseTime =
        [&context, &transaction]() -> boost::optional<NetClock::time_point> {
        return context.ledgerMaster.getCloseTimeBySeq(transaction->getLedger());
    };

    insertDeliveredAmount(
        meta,
        getFix1623Enabled,
        getLedgerIndex,
        getCloseTime,
        std::move(serializedTx),
        transactionMeta);
}
} // RPC
} // ripple
