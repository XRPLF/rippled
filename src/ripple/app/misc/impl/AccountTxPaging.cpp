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

#include <BeastConfig.h>
#include <ripple/app/ledger/LedgerToJson.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/Transaction.h>
#include <ripple/app/misc/impl/AccountTxPaging.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/types.h>
#include <boost/format.hpp>
#include <memory>

namespace ripple {

void
convertBlobsToTxResult (
    NetworkOPs::AccountTxs& to,
    std::uint32_t ledger_index,
    std::string const& status,
    Blob const& rawTxn,
    Blob const& rawMeta,
    Application& app)
{
    SerialIter it (makeSlice(rawTxn));
    auto txn = std::make_shared<STTx const> (it);
    std::string reason;

    auto tr = std::make_shared<Transaction> (txn, reason, app);

    tr->setStatus (Transaction::sqlTransactionStatus(status));
    tr->setLedger (ledger_index);

    auto metaset = std::make_shared<TxMeta> (
        tr->getID (), tr->getLedger (), rawMeta, app.journal ("TxMeta"));

    to.emplace_back(std::move(tr), metaset);
};

void
saveLedgerAsync (Application& app, std::uint32_t seq)
{
    if (auto l = app.getLedgerMaster().getLedgerBySeq(seq))
        pendSaveValidated(app, l, false, false);
}

void
accountTxPage (
    DatabaseCon& connection,
    AccountIDCache const& idCache,
    std::function<void (std::uint32_t)> const& onUnsavedLedger,
    std::function<void (std::uint32_t,
                        std::string const&,
                        Blob const&,
                        Blob const&)> const& onTransaction,
    AccountID const& account,
    std::int32_t minLedger,
    std::int32_t maxLedger,
    bool forward,
    Json::Value& token,
    int limit,
    bool bAdmin,
    std::uint32_t page_length)
{
    bool lookingForMarker = token.isObject();

    std::uint32_t numberOfResults;

    if (limit <= 0 || (limit > page_length && !bAdmin))
        numberOfResults = page_length;
    else
        numberOfResults = limit;

    // As an account can have many thousands of transactions, there is a limit
    // placed on the amount of transactions returned. If the limit is reached
    // before the result set has been exhausted (we always query for one more
    // than the limit), then we return an opaque marker that can be supplied in
    // a subsequent query.
    std::uint32_t queryLimit = numberOfResults + 1;
    std::uint32_t findLedger = 0, findSeq = 0;

    if (lookingForMarker)
    {
        try
        {
            if (!token.isMember(jss::ledger) || !token.isMember(jss::seq))
                return;
            findLedger = token[jss::ledger].asInt();
            findSeq = token[jss::seq].asInt();
        }
        catch (std::exception const&)
        {
            return;
        }
    }

    // We're using the token reference both for passing inputs and outputs, so
    // we need to clear it in between.
    token = Json::nullValue;

    static std::string const prefix (
        R"(SELECT AccountTransactions.LedgerSeq,AccountTransactions.TxnSeq,
          Status,RawTxn,TxnMeta
          FROM AccountTransactions INNER JOIN Transactions
          ON Transactions.TransID = AccountTransactions.TransID
          AND AccountTransactions.Account = '%s' WHERE
          )");

    std::string sql;

    // SQL's BETWEEN uses a closed interval ([a,b])

    if (forward && (findLedger == 0))
    {
        sql = boost::str (boost::format(
            prefix +
            (R"(AccountTransactions.LedgerSeq BETWEEN '%u' AND '%u'
             ORDER BY AccountTransactions.LedgerSeq ASC,
             AccountTransactions.TxnSeq ASC
             LIMIT %u;)"))
            % idCache.toBase58(account)
            % minLedger
            % maxLedger
            % queryLimit);
    }
    else if (forward && (findLedger != 0))
    {
        auto b58acct = idCache.toBase58(account);
        sql = boost::str (boost::format(
            (R"(SELECT AccountTransactions.LedgerSeq,AccountTransactions.TxnSeq,
            Status,RawTxn,TxnMeta
            FROM AccountTransactions, Transactions WHERE
            (AccountTransactions.TransID = Transactions.TransID AND
            AccountTransactions.Account = '%s' AND
            AccountTransactions.LedgerSeq BETWEEN '%u' AND '%u')
            OR
            (AccountTransactions.TransID = Transactions.TransID AND
            AccountTransactions.Account = '%s' AND
            AccountTransactions.LedgerSeq = '%u' AND
            AccountTransactions.TxnSeq >= '%u')
            ORDER BY AccountTransactions.LedgerSeq ASC,
            AccountTransactions.TxnSeq ASC
            LIMIT %u;
            )"))
        % b58acct
        % (findLedger + 1)
        % maxLedger
        % b58acct
        % findLedger
        % findSeq
        % queryLimit);
    }
    else if (!forward && (findLedger == 0))
    {
        sql = boost::str (boost::format(
            prefix +
            (R"(AccountTransactions.LedgerSeq BETWEEN '%u' AND '%u'
             ORDER BY AccountTransactions.LedgerSeq DESC,
             AccountTransactions.TxnSeq DESC
             LIMIT %u;)"))
            % idCache.toBase58(account)
            % minLedger
            % maxLedger
            % queryLimit);
    }
    else if (!forward && (findLedger != 0))
    {
        auto b58acct = idCache.toBase58(account);
        sql = boost::str (boost::format(
            (R"(SELECT AccountTransactions.LedgerSeq,AccountTransactions.TxnSeq,
            Status,RawTxn,TxnMeta
            FROM AccountTransactions, Transactions WHERE
            (AccountTransactions.TransID = Transactions.TransID AND
            AccountTransactions.Account = '%s' AND
            AccountTransactions.LedgerSeq BETWEEN '%u' AND '%u')
            OR
            (AccountTransactions.TransID = Transactions.TransID AND
            AccountTransactions.Account = '%s' AND
            AccountTransactions.LedgerSeq = '%u' AND
            AccountTransactions.TxnSeq <= '%u')
            ORDER BY AccountTransactions.LedgerSeq DESC,
            AccountTransactions.TxnSeq DESC
            LIMIT %u;
            )"))
            % b58acct
            % minLedger
            % (findLedger - 1)
            % b58acct
            % findLedger
            % findSeq
            % queryLimit);
    }
    else
    {
        assert (false);
        // sql is empty
        return;
    }

    {
        auto db (connection.checkoutDb());

        Blob rawData;
        Blob rawMeta;

        boost::optional<std::uint64_t> ledgerSeq;
        boost::optional<std::uint32_t> txnSeq;
        boost::optional<std::string> status;
        soci::blob txnData (*db);
        soci::blob txnMeta (*db);
        soci::indicator dataPresent, metaPresent;

        soci::statement st = (db->prepare << sql,
            soci::into (ledgerSeq),
            soci::into (txnSeq),
            soci::into (status),
            soci::into (txnData, dataPresent),
            soci::into (txnMeta, metaPresent));

        st.execute ();

        while (st.fetch ())
        {
            if (lookingForMarker)
            {
                if (findLedger == ledgerSeq.value_or (0) &&
                    findSeq == txnSeq.value_or (0))
                {
                    lookingForMarker = false;
                }
            }
            else if (numberOfResults == 0)
            {
                token = Json::objectValue;
                token[jss::ledger] = rangeCheckedCast<std::uint32_t>(ledgerSeq.value_or (0));
                token[jss::seq] = txnSeq.value_or (0);
                break;
            }

            if (!lookingForMarker)
            {
                if (dataPresent == soci::i_ok)
                    convert (txnData, rawData);
                else
                    rawData.clear ();

                if (metaPresent == soci::i_ok)
                    convert (txnMeta, rawMeta);
                else
                    rawMeta.clear ();

                // Work around a bug that could leave the metadata missing
                if (rawMeta.size() == 0)
                    onUnsavedLedger(ledgerSeq.value_or (0));

                onTransaction(rangeCheckedCast<std::uint32_t>(ledgerSeq.value_or (0)),
                    *status, rawData, rawMeta);
                --numberOfResults;
            }
        }
    }

    return;
}

}
