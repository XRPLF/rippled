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

AcceptedLedgerTx::AcceptedLedgerTx (std::uint32_t seq, SerializerIterator& sit)
{
    Serializer          txnSer (sit.getVL ());
    SerializerIterator  txnIt (txnSer);

    mTxn =      std::make_shared<SerializedTransaction> (std::ref (txnIt));
    mRawMeta =  sit.getVL ();
    mMeta =     std::make_shared<TransactionMetaSet> (mTxn->getTransactionID (), seq, mRawMeta);
    mAffected = mMeta->getAffectedAccounts ();
    mResult =   mMeta->getResultTER ();
    buildJson ();
}

AcceptedLedgerTx::AcceptedLedgerTx (SerializedTransaction::ref txn, TransactionMetaSet::ref met) :
    mTxn (txn), mMeta (met), mAffected (met->getAffectedAccounts ())
{
    mResult = mMeta->getResultTER ();
    buildJson ();
}

AcceptedLedgerTx::AcceptedLedgerTx (SerializedTransaction::ref txn, TER result) :
    mTxn (txn), mResult (result), mAffected (txn->getMentionedAccounts ())
{
    buildJson ();
}

std::string AcceptedLedgerTx::getEscMeta () const
{
    assert (!mRawMeta.empty ());
    return sqlEscape (mRawMeta);
}

void AcceptedLedgerTx::buildJson ()
{
    mJson = Json::objectValue;
    mJson[jss::transaction] = mTxn->getJson (0);

    if (mMeta)
    {
        mJson[jss::meta] = mMeta->getJson (0);
        mJson[jss::raw_meta] = strHex (mRawMeta);
    }

    mJson[jss::result] = transHuman (mResult);

    if (!mAffected.empty ())
    {
        Json::Value& affected = (mJson[jss::affected] = Json::arrayValue);
        BOOST_FOREACH (const RippleAddress & ra, mAffected)
        {
            affected.append (ra.humanAccountID ());
        }
    }
}

} // ripple
