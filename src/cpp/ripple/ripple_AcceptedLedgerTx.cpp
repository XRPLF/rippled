AcceptedLedgerTx::AcceptedLedgerTx (uint32 seq, SerializerIterator& sit)
{
    Serializer          txnSer (sit.getVL ());
    SerializerIterator  txnIt (txnSer);

    mTxn =      boost::make_shared<SerializedTransaction> (boost::ref (txnIt));
    mRawMeta =   sit.getVL ();
    mMeta =     boost::make_shared<TransactionMetaSet> (mTxn->getTransactionID (), seq, mRawMeta);
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
    mJson["transaction"] = mTxn->getJson (0);

    if (mMeta)
    {
        mJson["meta"] = mMeta->getJson (0);
        mJson["raw_meta"] = strHex (mRawMeta);
    }

    mJson["result"] = transHuman (mResult);

    if (!mAffected.empty ())
    {
        Json::Value& affected = (mJson["affected"] = Json::arrayValue);
        BOOST_FOREACH (const RippleAddress & ra, mAffected)
        {
            affected.append (ra.humanAccountID ());
        }
    }
}

