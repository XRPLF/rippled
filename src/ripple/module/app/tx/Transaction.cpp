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

namespace ripple {

Transaction::Transaction (SerializedTransaction::ref sit, bool bValidate)
    : mInLedger (0), mStatus (INVALID), mResult (temUNCERTAIN), mTransaction (sit)
{
    try
    {
        mFromPubKey.setAccountPublic (mTransaction->getSigningPubKey ());
        mTransactionID  = mTransaction->getTransactionID ();
        mAccountFrom    = mTransaction->getSourceAccount ();
    }
    catch (...)
    {
        return;
    }

    if (!bValidate || (passesLocalChecks (*mTransaction) && checkSign ()))
        mStatus = NEW;
}

Transaction::pointer Transaction::sharedTransaction (Blob const& vucTransaction, bool bValidate)
{
    try
    {
        Serializer          s (vucTransaction);
        SerializerIterator  sit (s);

        SerializedTransaction::pointer  st  = std::make_shared<SerializedTransaction> (std::ref (sit));

        return std::make_shared<Transaction> (st, bValidate);
    }
    catch (...)
    {
        Log (lsWARNING) << "Exception constructing transaction";
        return std::shared_ptr<Transaction> ();
    }
}

//
// Generic transaction construction
//

Transaction::Transaction (
    TxType ttKind,
    const RippleAddress&    naPublicKey,
    const RippleAddress&    naSourceAccount,
    std::uint32_t           uSeq,
    const STAmount&         saFee,
    std::uint32_t           uSourceTag) :
    mAccountFrom (naSourceAccount), mFromPubKey (naPublicKey), mInLedger (0), mStatus (NEW), mResult (temUNCERTAIN)
{
    assert (mFromPubKey.isValid ());

    mTransaction    = std::make_shared<SerializedTransaction> (ttKind);

    // Log(lsINFO) << str(boost::format("Transaction: account: %s") % naSourceAccount.humanAccountID());
    // Log(lsINFO) << str(boost::format("Transaction: mAccountFrom: %s") % mAccountFrom.humanAccountID());

    mTransaction->setSigningPubKey (mFromPubKey);
    mTransaction->setSourceAccount (mAccountFrom);
    mTransaction->setSequence (uSeq);
    mTransaction->setTransactionFee (saFee);

    if (uSourceTag)
    {
        mTransaction->makeFieldPresent (sfSourceTag);
        mTransaction->setFieldU32 (sfSourceTag, uSourceTag);
    }
}

bool Transaction::sign (const RippleAddress& naAccountPrivate)
{
    bool    bResult = true;

    if (!naAccountPrivate.isValid ())
    {
        Log (lsWARNING) << "No private key for signing";
        bResult = false;
    }

    getSTransaction ()->sign (naAccountPrivate);

    if (bResult)
    {
        updateID ();
    }
    else
    {
        mStatus = INCOMPLETE;
    }

    return bResult;
}





//
// Misc.
//

bool Transaction::checkSign () const
{
    if (!mFromPubKey.isValid ())
    {
        Log (lsWARNING) << "Transaction has bad source public key";
        return false;
    }

    return mTransaction->checkSign (mFromPubKey);
}

void Transaction::setStatus (TransStatus ts, std::uint32_t lseq)
{
    mStatus     = ts;
    mInLedger   = lseq;
}

Transaction::pointer Transaction::transactionFromSQL (Database* db, bool bValidate)
{
    Serializer rawTxn;
    std::string status;
    std::uint32_t inLedger;

    int txSize = 2048;
    rawTxn.resize (txSize);

    db->getStr ("Status", status);
    inLedger = db->getInt ("LedgerSeq");
    txSize = db->getBinary ("RawTxn", &*rawTxn.begin (), rawTxn.getLength ());

    if (txSize > rawTxn.getLength ())
    {
        rawTxn.resize (txSize);
        db->getBinary ("RawTxn", &*rawTxn.begin (), rawTxn.getLength ());
    }

    rawTxn.resize (txSize);

    SerializerIterator it (rawTxn);
    SerializedTransaction::pointer txn = std::make_shared<SerializedTransaction> (std::ref (it));
    Transaction::pointer tr = std::make_shared<Transaction> (txn, bValidate);

    TransStatus st (INVALID);

    switch (status[0])
    {
    case TXN_SQL_NEW:
        st = NEW;
        break;

    case TXN_SQL_CONFLICT:
        st = CONFLICTED;
        break;

    case TXN_SQL_HELD:
        st = HELD;
        break;

    case TXN_SQL_VALIDATED:
        st = COMMITTED;
        break;

    case TXN_SQL_INCLUDED:
        st = INCLUDED;
        break;

    case TXN_SQL_UNKNOWN:
        break;

    default:
        assert (false);
    }

    tr->setStatus (st);
    tr->setLedger (inLedger);
    return tr;
}

// DAVID: would you rather duplicate this code or keep the lock longer?
Transaction::pointer Transaction::transactionFromSQL (const std::string& sql)
{
    Serializer rawTxn;
    std::string status;
    std::uint32_t inLedger;

    int txSize = 2048;
    rawTxn.resize (txSize);

    {
        DeprecatedScopedLock sl (getApp().getTxnDB ()->getDBLock ());
        Database* db = getApp().getTxnDB ()->getDB ();

        if (!db->executeSQL (sql, true) || !db->startIterRows ())
            return Transaction::pointer ();

        db->getStr ("Status", status);
        inLedger = db->getInt ("LedgerSeq");
        txSize = db->getBinary ("RawTxn", &*rawTxn.begin (), rawTxn.getLength ());

        if (txSize > rawTxn.getLength ())
        {
            rawTxn.resize (txSize);
            db->getBinary ("RawTxn", &*rawTxn.begin (), rawTxn.getLength ());
        }

        db->endIterRows ();
    }
    rawTxn.resize (txSize);

    SerializerIterator it (rawTxn);
    SerializedTransaction::pointer txn = std::make_shared<SerializedTransaction> (std::ref (it));
    Transaction::pointer tr = std::make_shared<Transaction> (txn, true);

    TransStatus st (INVALID);

    switch (status[0])
    {
    case TXN_SQL_NEW:
        st = NEW;
        break;

    case TXN_SQL_CONFLICT:
        st = CONFLICTED;
        break;

    case TXN_SQL_HELD:
        st = HELD;
        break;

    case TXN_SQL_VALIDATED:
        st = COMMITTED;
        break;

    case TXN_SQL_INCLUDED:
        st = INCLUDED;
        break;

    case TXN_SQL_UNKNOWN:
        break;

    default:
        assert (false);
    }

    tr->setStatus (st);
    tr->setLedger (inLedger);
    return tr;
}


Transaction::pointer Transaction::load (uint256 const& id)
{
    std::string sql = "SELECT LedgerSeq,Status,RawTxn FROM Transactions WHERE TransID='";
    sql.append (to_string (id));
    sql.append ("';");
    return transactionFromSQL (sql);
}

bool Transaction::convertToTransactions (std::uint32_t firstLedgerSeq, std::uint32_t secondLedgerSeq,
        bool checkFirstTransactions, bool checkSecondTransactions, const SHAMap::Delta& inMap,
        std::map<uint256, std::pair<Transaction::pointer, Transaction::pointer> >& outMap)
{
    // convert a straight SHAMap payload difference to a transaction difference table
    // return value: true=ledgers are valid, false=a ledger is invalid

    for (auto it = inMap.begin (); it != inMap.end (); ++it)
    {
        uint256 const& id = it->first;
        SHAMapItem::ref first = it->second.first;
        SHAMapItem::ref second = it->second.second;

        Transaction::pointer firstTrans, secondTrans;

        if (!!first)
        {
            // transaction in our table
            firstTrans = sharedTransaction (first->peekData (), checkFirstTransactions);

            if ((firstTrans->getStatus () == INVALID) || (firstTrans->getID () != id ))
            {
                firstTrans->setStatus (INVALID, firstLedgerSeq);
                return false;
            }
            else firstTrans->setStatus (INCLUDED, firstLedgerSeq);
        }

        if (!!second)
        {
            // transaction in other table
            secondTrans = sharedTransaction (second->peekData (), checkSecondTransactions);

            if ((secondTrans->getStatus () == INVALID) || (secondTrans->getID () != id))
            {
                secondTrans->setStatus (INVALID, secondLedgerSeq);
                return false;
            }
            else secondTrans->setStatus (INCLUDED, secondLedgerSeq);
        }

        assert (firstTrans || secondTrans);

        if (firstTrans && secondTrans && (firstTrans->getStatus () != INVALID) && (secondTrans->getStatus () != INVALID))
            return false; // one or the other SHAMap is structurally invalid or a miracle has happened

        outMap[id] = std::pair<Transaction::pointer, Transaction::pointer> (firstTrans, secondTrans);
    }

    return true;
}

// options 1 to include the date of the transaction
Json::Value Transaction::getJson (int options, bool binary) const
{
    Json::Value ret (mTransaction->getJson (0, binary));

    if (mInLedger)
    {
        ret["inLedger"] = mInLedger;        // Deprecated.
        ret["ledger_index"] = mInLedger;

        if (options == 1)
        {
            Ledger::pointer ledger = getApp().getLedgerMaster ().getLedgerBySeq (mInLedger);

            if (ledger)
                ret["date"] = ledger->getCloseTimeNC ();
        }
    }

    return ret;
}

bool Transaction::isHexTxID (const std::string& txid)
{
    if (txid.size () != 64) 
        return false;

    auto const ret = std::find_if_not (txid.begin (), txid.end (),
        std::bind (
            std::isxdigit <std::string::value_type>,
            std::placeholders::_1,
            std::locale ()));

    return (ret == txid.end ());
}

} // ripple
