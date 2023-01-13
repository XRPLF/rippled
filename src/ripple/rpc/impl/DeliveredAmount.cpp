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

  GetLedgerIndex is a callable that returns a LedgerIndex
  GetCloseTime is a callable that returns a
               std::optional<NetClock::time_point>
 */
template <class GetLedgerIndex, class GetCloseTime>
std::optional<STAmount>
getDeliveredAmount(
    GetLedgerIndex const& getLedgerIndex,
    GetCloseTime const& getCloseTime,
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const& transactionMeta)
{
    if (!serializedTx)
        return {};

    // The field transactionMeta.hasDeliveredAmount value is always correct
    // for partial payments, but is not 100% guaranteed to be correct for
    // CheckCash.
    bool const isCheckCash =
        serializedTx->getFieldU16(sfTransactionType) == ttCHECK_CASH;

    if (transactionMeta.hasDeliveredAmount() && !isCheckCash)
    {
        return transactionMeta.getDeliveredAmount();
    }

    using namespace std::chrono_literals;

    // Ledger 4594095 is the first ledger in which the DeliveredAmount field
    // was present when a partial payment was made and its absence indicates
    // that the amount delivered is listed in the Amount field.
    //
    // If the ledger closed long after the DeliveredAmount code was deployed
    // then its absence indicates that the amount delivered is listed in the
    // Amount field. DeliveredAmount went live January 24, 2014.
    // 446000000 is in Feb 2014, well after DeliveredAmount went live
    if (isCheckCash || getLedgerIndex() >= 4594095 ||
        getCloseTime() > NetClock::time_point{446000000s})
    {
        bool const hasAmount = serializedTx->isFieldPresent(sfAmount);
        bool const hasDeliverMin = serializedTx->isFieldPresent(sfDeliverMin);

        if (hasAmount || hasDeliverMin)
        {
            // This code previously simply returned the Amount field of the
            // transaction.  However there are corner cases where that fails,
            // in part because of the floating-point like limitations of
            // non-XRP STAmounts.
            //
            // Consider an account with a very large IOU balance on a trust
            // line receiving a tiny IOU payment.  If the payment is small
            // enough, then the very large IOU balance may not change at all.
            // So, in effect, no money changed hands.
            //
            // To accommodate this, we compute the actual difference between
            // the Previous and Final fields of the relevant RippleState
            // object according to the transaction metadata. If for some
            // reason this can't be calculated then an empty optional is
            // returned and the RPC caller sees: "delivered_amount":
            // "unavailable".

            STObject const meta = transactionMeta.getAsObject();

            // if there are no modified fields (somehow) then nothing was
            // delivered
            if (!meta.isFieldPresent(sfAffectedNodes))
                return {};

            // CheckCash may specify DeliverMin instead of Amount
            auto const txAmt = serializedTx->getFieldAmount(
                hasAmount ? sfAmount : sfDeliverMin);

            // if it's XRP then it was all delivered if the payment was
            // successful
            if (isXRP(txAmt))
                return transactionMeta.hasDeliveredAmount()
                    ? transactionMeta.getDeliveredAmount()
                    : txAmt;

            // It's not XRP.  Return the difference between final and initial
            // fields in the metadata.

            // in a CheckCash txn the destination is the sender.  Otherwise
            // we need an explicit destination.
            if (!isCheckCash && !serializedTx->isFieldPresent(sfDestination))
                return {};

            // get the destination
            auto const txAcc = serializedTx->getAccountID(
                isCheckCash ? sfAccount : sfDestination);

            // get the issuer/currency
            auto const issue = txAmt.issue();

            // place the accounts into the canonical ripple state order
            auto const& accA = issue.account < txAcc ? issue.account : txAcc;
            auto const& accB = issue.account < txAcc ? txAcc : issue.account;

            // search the Affected nodes in the metadata to find the receiving
            // trustline
            for (auto const& node : meta.getFieldArray(sfAffectedNodes))
            {
                uint16_t nodeType = node.getFieldU16(sfLedgerEntryType);
                if (nodeType != ltRIPPLE_STATE)
                    continue;

                // if newly created RippleState
                if (node.isFieldPresent(sfNewFields) &&
                    !node.isFieldPresent(sfFinalFields) &&
                    !node.isFieldPresent(sfPreviousFields))
                {
                    auto const& nfBase = node.peekAtField(sfNewFields);
                    auto const& newFields =
                        nfBase.template downcast<STObject>();

                    if (newFields.getFieldAmount(sfLowLimit).getIssuer() !=
                            accA ||
                        newFields.getFieldAmount(sfHighLimit).getIssuer() !=
                            accB)
                        continue;

                    STAmount balance = newFields.getFieldAmount(sfBalance);
                    balance.setIssue(issue);

                    if (balance.negative())
                        balance.negate();

                    return balance;
                }

                // if RippleState not modified then keep looking
                if (!node.isFieldPresent(sfFinalFields) ||
                    !node.isFieldPresent(sfPreviousFields))
                    continue;

                auto const& ffBase = node.peekAtField(sfFinalFields);
                auto const& finalFields = ffBase.template downcast<STObject>();
                auto const& pfBase = node.peekAtField(sfPreviousFields);
                auto const& previousFields =
                    pfBase.template downcast<STObject>();

                if (finalFields.getFieldAmount(sfLowLimit).getIssuer() !=
                        accA ||
                    finalFields.getFieldAmount(sfHighLimit).getIssuer() != accB)
                    continue;

                // execution to here means we are on the correct trustline
                try
                {
                    // compute and return the balance mutation on this trustline
                    STAmount difference =
                        finalFields.getFieldAmount(sfBalance) -
                        previousFields.getFieldAmount(sfBalance);

                    difference.setIssue(issue);

                    if (difference.negative())
                        difference.negate();

                    return difference;
                }
                catch (std::runtime_error& e)
                {
                    // overflow computing difference so
                    // err on the side of caution and
                    // do not return a delivered amount
                    return {};
                }
            }
        }
    }

    return {};
}

// Returns true if transaction meta could contain a delivered amount field,
// based on transaction type, transaction result and whether fix1623 is enabled
template <class GetFix1623Enabled>
bool
canHaveDeliveredAmountHelp(
    GetFix1623Enabled const& getFix1623Enabled,
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const& transactionMeta)
{
    if (!serializedTx)
        return false;

    {
        TxType const tt{serializedTx->getTxnType()};
        if (tt != ttPAYMENT && tt != ttCHECK_CASH && tt != ttACCOUNT_DELETE)
            return false;

        if (tt == ttCHECK_CASH && !getFix1623Enabled())
            return false;
    }

    // if the transaction failed nothing could have been delivered.
    if (transactionMeta.getResultTER() != tesSUCCESS)
        return false;

    return true;
}

// Returns true if transaction meta could contain a delivered amount field,
// based on transaction type, transaction result and whether fix1623 is enabled
bool
canHaveDeliveredAmount(
    RPC::Context const& context,
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const& transactionMeta)
{
    // These lambdas are used to compute the values lazily
    auto const getFix1623Enabled = [&context]() -> bool {
        if (context.app.config().reporting())
        {
            auto const view = context.ledgerMaster.getValidatedLedger();
            if (!view)
                return false;
            return view->rules().enabled(fix1623);
        }
        else
        {
            auto const view = context.app.openLedger().current();
            if (!view)
                return false;
            return view->rules().enabled(fix1623);
        }
    };

    return canHaveDeliveredAmountHelp(
        getFix1623Enabled, serializedTx, transactionMeta);
}

void
insertDeliveredAmount(
    Json::Value& meta,
    ReadView const& ledger,
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const& transactionMeta)
{
    auto const info = ledger.info();
    auto const getFix1623Enabled = [&ledger] {
        return ledger.rules().enabled(fix1623);
    };

    if (canHaveDeliveredAmountHelp(
            getFix1623Enabled, serializedTx, transactionMeta))
    {
        auto const getLedgerIndex = [&info] { return info.seq; };
        auto const getCloseTime = [&info] { return info.closeTime; };

        auto amt = getDeliveredAmount(
            getLedgerIndex, getCloseTime, serializedTx, transactionMeta);
        if (amt)
        {
            meta[jss::delivered_amount] =
                amt->getJson(JsonOptions::include_date);
        }
        else
        {
            // report "unavailable" which cannot be parsed into a sensible
            // amount.
            meta[jss::delivered_amount] = Json::Value("unavailable");
        }
    }
}

template <class GetLedgerIndex>
static std::optional<STAmount>
getDeliveredAmount(
    RPC::Context const& context,
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const& transactionMeta,
    GetLedgerIndex const& getLedgerIndex)
{
    if (canHaveDeliveredAmount(context, serializedTx, transactionMeta))
    {
        auto const getCloseTime =
            [&context,
             &getLedgerIndex]() -> std::optional<NetClock::time_point> {
            return context.ledgerMaster.getCloseTimeBySeq(getLedgerIndex());
        };
        return getDeliveredAmount(
            getLedgerIndex, getCloseTime, serializedTx, transactionMeta);
    }

    return {};
}

std::optional<STAmount>
getDeliveredAmount(
    RPC::Context const& context,
    std::shared_ptr<STTx const> const& serializedTx,
    TxMeta const& transactionMeta,
    LedgerIndex const& ledgerIndex)
{
    return getDeliveredAmount(
        context, serializedTx, transactionMeta, [&ledgerIndex]() {
            return ledgerIndex;
        });
}

void
insertDeliveredAmount(
    Json::Value& meta,
    RPC::JsonContext const& context,
    std::shared_ptr<Transaction> const& transaction,
    TxMeta const& transactionMeta)
{
    insertDeliveredAmount(
        meta, context, transaction->getSTransaction(), transactionMeta);
}

void
insertDeliveredAmount(
    Json::Value& meta,
    RPC::JsonContext const& context,
    std::shared_ptr<STTx const> const& transaction,
    TxMeta const& transactionMeta)
{
    if (canHaveDeliveredAmount(context, transaction, transactionMeta))
    {
        auto amt = getDeliveredAmount(
            context, transaction, transactionMeta, [&transactionMeta]() {
                return transactionMeta.getLgrSeq();
            });

        if (amt)
        {
            meta[jss::delivered_amount] =
                amt->getJson(JsonOptions::include_date);
        }
        else
        {
            // report "unavailable" which cannot be parsed into a sensible
            // amount.
            meta[jss::delivered_amount] = Json::Value("unavailable");
        }
    }
}

}  // namespace RPC
}  // namespace ripple
