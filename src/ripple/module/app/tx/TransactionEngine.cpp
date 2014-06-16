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

//
// XXX Make sure all fields are recognized in transactions.
//

SETUP_LOG (TransactionEngine)

void TransactionEngine::txnWrite ()
{
    // Write back the account states
    typedef std::map<uint256, LedgerEntrySetEntry>::value_type u256_LES_pair;
    BOOST_FOREACH (u256_LES_pair & it, mNodes)
    {
        SLE::ref    sleEntry    = it.second.mEntry;

        switch (it.second.mAction)
        {
        case taaNONE:
            assert (false);
            break;

        case taaCACHED:
            break;

        case taaCREATE:
        {
            WriteLog (lsINFO, TransactionEngine) << "applyTransaction: taaCREATE: " << sleEntry->getText ();

            if (mLedger->writeBack (lepCREATE, sleEntry) & lepERROR)
                assert (false);
        }
        break;

        case taaMODIFY:
        {
            WriteLog (lsINFO, TransactionEngine) << "applyTransaction: taaMODIFY: " << sleEntry->getText ();

            if (mLedger->writeBack (lepNONE, sleEntry) & lepERROR)
                assert (false);
        }
        break;

        case taaDELETE:
        {
            WriteLog (lsINFO, TransactionEngine) << "applyTransaction: taaDELETE: " << sleEntry->getText ();

            if (!mLedger->peekAccountStateMap ()->delItem (it.first))
                assert (false);
        }
        break;
        }
    }
}

TER TransactionEngine::applyTransaction (const SerializedTransaction& txn, TransactionEngineParams params,
        bool& didApply)
{
    WriteLog (lsTRACE, TransactionEngine) << "applyTransaction>";
    didApply = false;
    assert (mLedger);
    mNodes.init (mLedger, txn.getTransactionID (), mLedger->getLedgerSeq (), params);

#ifdef BEAST_DEBUG

    if (1)
    {
        Serializer ser;
        txn.add (ser);
        SerializerIterator sit (ser);
        SerializedTransaction s2 (sit);

        if (!s2.isEquivalent (txn))
        {
            WriteLog (lsFATAL, TransactionEngine) << "Transaction serdes mismatch";
            Json::StyledStreamWriter ssw;
            WriteLog (lsINFO, TransactionEngine) << txn.getJson (0);
            WriteLog (lsFATAL, TransactionEngine) << s2.getJson (0);
            assert (false);
        }
    }

#endif

    uint256 txID = txn.getTransactionID ();

    if (!txID)
    {
        WriteLog (lsWARNING, TransactionEngine) << "applyTransaction: invalid transaction id";
        return temINVALID;
    }

    std::unique_ptr<Transactor> transactor = Transactor::makeTransactor (txn, params, this);

    if (transactor.get () == nullptr)
    {
        WriteLog (lsWARNING, TransactionEngine) << "applyTransaction: Invalid transaction: unknown transaction type";
        return temUNKNOWN;
    }

    TER terResult = transactor->apply ();
    std::string strToken;
    std::string strHuman;

    transResultInfo (terResult, strToken, strHuman);

    WriteLog (lsINFO, TransactionEngine) << "applyTransaction: terResult=" << strToken << " : " << terResult << " : " << strHuman;

    if (isTesSuccess (terResult))
        didApply = true;
    else if (isTecClaim (terResult) && !(params & tapRETRY))
    {
        // only claim the transaction fee
        WriteLog (lsDEBUG, TransactionEngine) << "Reprocessing to only claim fee";
        mNodes.clear ();

        SLE::pointer txnAcct = entryCache (ltACCOUNT_ROOT, Ledger::getAccountRootIndex (txn.getSourceAccount ()));

        if (!txnAcct)
            terResult = terNO_ACCOUNT;
        else
        {
            std::uint32_t t_seq = txn.getSequence ();
            std::uint32_t a_seq = txnAcct->getFieldU32 (sfSequence);

            if (a_seq < t_seq)
                terResult = terPRE_SEQ;
            else if (a_seq > t_seq)
                terResult = tefPAST_SEQ;
            else
            {
                STAmount fee        = txn.getTransactionFee ();
                STAmount balance    = txnAcct->getFieldAmount (sfBalance);

                if (balance < fee)
                    terResult = terINSUF_FEE_B;
                else
                {
                    txnAcct->setFieldAmount (sfBalance, balance - fee);
                    txnAcct->setFieldU32 (sfSequence, t_seq + 1);
                    entryModify (txnAcct);
                    didApply = true;
                }
            }
        }
    }
    else
        WriteLog (lsDEBUG, TransactionEngine) << "Not applying transaction " << txID;

    if (didApply)
    {
        if (!checkInvariants (terResult, txn, params))
        {
            WriteLog (lsFATAL, TransactionEngine) << "Transaction violates invariants";
            WriteLog (lsFATAL, TransactionEngine) << txn.getJson (0);
            WriteLog (lsFATAL, TransactionEngine) << transToken (terResult) << ": " << transHuman (terResult);
            WriteLog (lsFATAL, TransactionEngine) << mNodes.getJson (0);
            didApply = false;
            terResult = tefINTERNAL;
        }
        else
        {
            // Transaction succeeded fully or (retries are not allowed and the transaction could claim a fee)
            Serializer m;
            mNodes.calcRawMeta (m, terResult, mTxnSeq++);

            txnWrite ();

            Serializer s;
            txn.add (s);

            if (params & tapOPEN_LEDGER)
            {
                if (!mLedger->addTransaction (txID, s))
                {
                    WriteLog (lsFATAL, TransactionEngine) << "Tried to add transaction to open ledger that already had it";
                    assert (false);
                    throw std::runtime_error ("Duplicate transaction applied");
                }
            }
            else
            {
                if (!mLedger->addTransaction (txID, s, m))
                {
                    WriteLog (lsFATAL, TransactionEngine) << "Tried to add transaction to ledger that already had it";
                    assert (false);
                    throw std::runtime_error ("Duplicate transaction applied to closed ledger");
                }

                // Charge whatever fee they specified.
                STAmount saPaid = txn.getTransactionFee ();
                mLedger->destroyCoins (saPaid.getNValue ());
            }
        }
    }

    mTxnAccount.reset ();
    mNodes.clear ();

    if (!(params & tapOPEN_LEDGER) && isTemMalformed (terResult))
    {
        // XXX Malformed or failed transaction in closed ledger must bow out.
    }

    return terResult;
}

} // ripple
