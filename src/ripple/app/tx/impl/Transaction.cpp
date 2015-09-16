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
#include <ripple/app/tx/Transaction.h>
#include <ripple/basics/Log.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/HashRouter.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/JsonFields.h>
#include <boost/optional.hpp>

namespace ripple {

Transaction::Transaction (STTx::ref stx, Validate validate,
    SigVerify sigVerify, std::string& reason, Application& app)
    noexcept
    : mTransaction (stx)
    , mApp (app)
{
    try
    {
        mFromPubKey.setAccountPublic (mTransaction->getSigningPubKey ());
        mTransactionID  = mTransaction->getTransactionID ();
    }
    catch (std::exception& e)
    {
        reason = e.what();
        return;
    }
    catch (...)
    {
        reason = "Unexpected exception";
        return;
    }

    if (validate == Validate::NO ||
        (passesLocalChecks (*mTransaction, reason) &&
            checkSign (reason, sigVerify)))
    {
        mStatus = NEW;
    }
}

Transaction::pointer Transaction::sharedTransaction (
    Blob const& vucTransaction, Validate validate, Application& app)
{
    try
    {
        SerialIter sit (makeSlice(vucTransaction));
        std::string reason;

        return std::make_shared<Transaction> (std::make_shared<STTx> (sit),
            validate, app.getHashRouter().sigVerify(), reason, app);
    }
    catch (std::exception& e)
    {
        WriteLog(lsWARNING, Ledger) << "Exception constructing transaction" <<
            e.what ();
    }
    catch (...)
    {
        WriteLog(lsWARNING, Ledger) << "Exception constructing transaction";
    }

    return std::shared_ptr<Transaction> ();
}

//
// Misc.
//

bool Transaction::checkSign (std::string& reason, SigVerify sigVerify) const
{
    bool const allowMultiSign = mApp.getLedgerMaster().
        getValidatedRules().enabled (featureMultiSign, getConfig().features);

    if (! mFromPubKey.isValid ())
        reason = "Transaction has bad source public key";
    else if (!sigVerify(*mTransaction, [allowMultiSign] (STTx const& tx)
    {
        return tx.checkSign(allowMultiSign);
    }))
        reason = "Transaction has bad signature";
    else
        return true;

    WriteLog (lsWARNING, Ledger) << reason;
    return false;
}

void Transaction::setStatus (TransStatus ts, std::uint32_t lseq)
{
    mStatus     = ts;
    mInLedger   = lseq;
}

TransStatus Transaction::sqlTransactionStatus(
    boost::optional<std::string> const& status)
{
    char const c = (status) ? (*status)[0] : TXN_SQL_UNKNOWN;

    switch (c)
    {
    case TXN_SQL_NEW:       return NEW;
    case TXN_SQL_CONFLICT:  return CONFLICTED;
    case TXN_SQL_HELD:      return HELD;
    case TXN_SQL_VALIDATED: return COMMITTED;
    case TXN_SQL_INCLUDED:  return INCLUDED;
    }

    assert (c == TXN_SQL_UNKNOWN);
    return INVALID;
}

Transaction::pointer Transaction::transactionFromSQL (
    boost::optional<std::uint64_t> const& ledgerSeq,
    boost::optional<std::string> const& status,
    Blob const& rawTxn,
    Validate validate,
    Application& app)
{
    std::uint32_t const inLedger =
        rangeCheckedCast<std::uint32_t>(ledgerSeq.value_or (0));

    SerialIter it (makeSlice(rawTxn));
    auto txn = std::make_shared<STTx> (it);
    std::string reason;
    auto tr = std::make_shared<Transaction> (txn, validate,
        app.getHashRouter().sigVerify(), reason, app);

    tr->setStatus (sqlTransactionStatus (status));
    tr->setLedger (inLedger);
    return tr;
}

Transaction::pointer Transaction::load (uint256 const& id, Application& app)
{
    std::string sql = "SELECT LedgerSeq,Status,RawTxn "
            "FROM Transactions WHERE TransID='";
    sql.append (to_string (id));
    sql.append ("';");

    boost::optional<std::uint64_t> ledgerSeq;
    boost::optional<std::string> status;
    Blob rawTxn;
    {
        auto db = app.getTxnDB ().checkoutDb ();
        soci::blob sociRawTxnBlob (*db);
        soci::indicator rti;

        *db << sql, soci::into (ledgerSeq), soci::into (status),
                soci::into (sociRawTxnBlob, rti);
        if (!db->got_data () || rti != soci::i_ok)
            return {};

        convert(sociRawTxnBlob, rawTxn);
    }

    return Transaction::transactionFromSQL (
        ledgerSeq, status, rawTxn, Validate::YES, app);
}

// options 1 to include the date of the transaction
Json::Value Transaction::getJson (int options, bool binary) const
{
    Json::Value ret (mTransaction->getJson (0, binary));

    if (mInLedger)
    {
        ret[jss::inLedger] = mInLedger;        // Deprecated.
        ret[jss::ledger_index] = mInLedger;

        if (options == 1)
        {
            auto ledger = mApp.getLedgerMaster ().
                    getLedgerBySeq (mInLedger);
            if (ledger)
                ret[jss::date] = ledger->info().closeTime;
        }
    }

    return ret;
}

} // ripple
