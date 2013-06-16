//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

SETUP_LOG (Ledger)

DECLARE_INSTANCE (Ledger);

Ledger::Ledger (const RippleAddress& masterID, uint64 startAmount)
    : mTotCoins (startAmount)
    , mLedgerSeq (1)
    , mCloseTime (0)
    , mParentCloseTime (0)
    , mCloseResolution (LEDGER_TIME_ACCURACY)
    , mCloseFlags (0)
    , mClosed (false)
    , mValidHash (false)
    , mAccepted (false)
    , mImmutable (false)
    , mTransactionMap (boost::make_shared <SHAMap> (smtTRANSACTION))
    , mAccountStateMap (boost::make_shared <SHAMap> (smtSTATE))
{
    // special case: put coins in root account
    AccountState::pointer startAccount = boost::make_shared<AccountState> (masterID);

    startAccount->peekSLE ().setFieldAmount (sfBalance, startAmount);

    startAccount->peekSLE ().setFieldU32 (sfSequence, 1);

    WriteLog (lsTRACE, Ledger) << "root account: " << startAccount->peekSLE ().getJson (0);

    mAccountStateMap->armDirty ();

    writeBack (lepCREATE, startAccount->getSLE ());

    SHAMap::flushDirty (*mAccountStateMap->disarmDirty (), 256, hotACCOUNT_NODE, mLedgerSeq);

    initializeFees ();
}

Ledger::Ledger (uint256 const& parentHash,
                uint256 const& transHash,
                uint256 const& accountHash,
                uint64 totCoins,
                uint32 closeTime,
                uint32 parentCloseTime,
                int closeFlags,
                int closeResolution,
                uint32 ledgerSeq,
                bool& loaded)
    : mParentHash (parentHash)
    , mTransHash (transHash)
    , mAccountHash (accountHash)
    , mTotCoins (totCoins)
    , mLedgerSeq (ledgerSeq)
    , mCloseTime (closeTime)
    , mParentCloseTime (parentCloseTime)
    , mCloseResolution (closeResolution)
    , mCloseFlags (closeFlags)
    , mClosed (false)
    , mValidHash (false)
    , mAccepted (false)
    , mImmutable (true)
    , mTransactionMap (boost::make_shared <SHAMap> (smtTRANSACTION, transHash))
    , mAccountStateMap (boost::make_shared <SHAMap> (smtSTATE, accountHash))
{
    // This will throw if the root nodes are not available locally
    updateHash ();
    loaded = true;

    if (mTransHash.isNonZero () && !mTransactionMap->fetchRoot (mTransHash, NULL))
    {
        loaded = false;
        WriteLog (lsWARNING, Ledger) << "Don't have TX root for ledger";
    }

    if (mAccountHash.isNonZero () && !mAccountStateMap->fetchRoot (mAccountHash, NULL))
    {
        loaded = false;
        WriteLog (lsWARNING, Ledger) << "Don't have AS root for ledger";
    }

    mTransactionMap->setImmutable ();
    mAccountStateMap->setImmutable ();

    initializeFees ();
}

// Create a new ledger that's a snapshot of this one
Ledger::Ledger (Ledger& ledger,
                bool isMutable)
    : mParentHash (ledger.mParentHash)
    , mTotCoins (ledger.mTotCoins)
    , mLedgerSeq (ledger.mLedgerSeq)
    , mCloseTime (ledger.mCloseTime)
    , mParentCloseTime (ledger.mParentCloseTime)
    , mCloseResolution (ledger.mCloseResolution)
    , mCloseFlags (ledger.mCloseFlags)
    , mClosed (ledger.mClosed)
    , mValidHash (false)
    , mAccepted (ledger.mAccepted)
    , mImmutable (!isMutable)
    , mTransactionMap (ledger.mTransactionMap->snapShot (isMutable))
    , mAccountStateMap (ledger.mAccountStateMap->snapShot (isMutable))
{
    updateHash ();
    initializeFees ();
}

// Create a new ledger that follows this one
Ledger::Ledger (bool /* dummy */,
                Ledger& prevLedger)
    : mTotCoins (prevLedger.mTotCoins)
    , mLedgerSeq (prevLedger.mLedgerSeq + 1)
    , mParentCloseTime (prevLedger.mCloseTime)
    , mCloseResolution (prevLedger.mCloseResolution)
    , mCloseFlags (0)
    , mClosed (false)
    , mValidHash (false)
    , mAccepted (false)
    , mImmutable (false)
    , mTransactionMap (boost::make_shared <SHAMap> (smtTRANSACTION))
    , mAccountStateMap (prevLedger.mAccountStateMap->snapShot (true))
{
    prevLedger.updateHash ();

    mParentHash = prevLedger.getHash ();

    assert (mParentHash.isNonZero ());

    mCloseResolution = ContinuousLedgerTiming::getNextLedgerTimeResolution (
                           prevLedger.mCloseResolution,
                           prevLedger.getCloseAgree (),
                           mLedgerSeq);

    if (prevLedger.mCloseTime == 0)
    {
        mCloseTime = roundCloseTime (theApp->getOPs ().getCloseTimeNC (), mCloseResolution);
    }
    else
    {
        mCloseTime = prevLedger.mCloseTime + mCloseResolution;
    }

    initializeFees ();
}

Ledger::Ledger (Blob const& rawLedger,
                bool hasPrefix)
    : mClosed (false)
    , mValidHash (false)
    , mAccepted (false)
    , mImmutable (true)
{
    Serializer s (rawLedger);

    setRaw (s, hasPrefix);

    initializeFees ();
}

Ledger::Ledger (const std::string& rawLedger, bool hasPrefix) :
    mClosed (false), mValidHash (false), mAccepted (false), mImmutable (true)
{
    Serializer s (rawLedger);
    setRaw (s, hasPrefix);
    initializeFees ();
}

void Ledger::setImmutable ()
{
    updateHash ();
    mImmutable = true;

    if (mTransactionMap)
        mTransactionMap->setImmutable ();

    if (mAccountStateMap)
        mAccountStateMap->setImmutable ();
}

void Ledger::updateHash ()
{
    if (!mImmutable)
    {
        if (mTransactionMap)
            mTransHash = mTransactionMap->getHash ();
        else
            mTransHash.zero ();

        if (mAccountStateMap)
            mAccountHash = mAccountStateMap->getHash ();
        else
            mAccountHash.zero ();
    }

    Serializer s (118);
    s.add32 (HashPrefix::ledgerMaster);
    addRaw (s);
    mHash = s.getSHA512Half ();
    mValidHash = true;
}

void Ledger::setRaw (Serializer& s, bool hasPrefix)
{
    SerializerIterator sit (s);

    if (hasPrefix) sit.get32 ();

    mLedgerSeq =        sit.get32 ();
    mTotCoins =         sit.get64 ();
    mParentHash =       sit.get256 ();
    mTransHash =        sit.get256 ();
    mAccountHash =      sit.get256 ();
    mParentCloseTime =  sit.get32 ();
    mCloseTime =        sit.get32 ();
    mCloseResolution =  sit.get8 ();
    mCloseFlags =       sit.get8 ();
    updateHash ();

    if (mValidHash)
    {
        mTransactionMap = boost::make_shared<SHAMap> (smtTRANSACTION, mTransHash);
        mAccountStateMap = boost::make_shared<SHAMap> (smtSTATE, mAccountHash);
    }
}

void Ledger::addRaw (Serializer& s) const
{
    s.add32 (mLedgerSeq);
    s.add64 (mTotCoins);
    s.add256 (mParentHash);
    s.add256 (mTransHash);
    s.add256 (mAccountHash);
    s.add32 (mParentCloseTime);
    s.add32 (mCloseTime);
    s.add8 (mCloseResolution);
    s.add8 (mCloseFlags);
}

void Ledger::setAccepted (uint32 closeTime, int closeResolution, bool correctCloseTime)
{
    // used when we witnessed the consensus
    assert (mClosed && !mAccepted);
    mCloseTime = correctCloseTime ? roundCloseTime (closeTime, closeResolution) : closeTime;
    mCloseResolution = closeResolution;
    mCloseFlags = correctCloseTime ? 0 : sLCF_NoConsensusTime;
    mAccepted = true;
    setImmutable ();
}

void Ledger::setAccepted ()
{
    // used when we acquired the ledger
    // FIXME assert(mClosed && (mCloseTime != 0) && (mCloseResolution != 0));
    if ((mCloseFlags & sLCF_NoConsensusTime) == 0)
        mCloseTime = roundCloseTime (mCloseTime, mCloseResolution);

    mAccepted = true;
    setImmutable ();
}

bool Ledger::hasAccount (const RippleAddress& accountID)
{
    return mAccountStateMap->hasItem (Ledger::getAccountRootIndex (accountID));
}

AccountState::pointer Ledger::getAccountState (const RippleAddress& accountID)
{
#ifdef DEBUG
    //  std::cerr << "Ledger:getAccountState(" << accountID.humanAccountID() << ")" << std::endl;
#endif

    SLE::pointer sle = getSLEi (Ledger::getAccountRootIndex (accountID));

    if (!sle)
    {
        WriteLog (lsDEBUG, Ledger) << boost::str (boost::format ("Ledger:getAccountState: not found: %s: %s")
                                   % accountID.humanAccountID ()
                                   % Ledger::getAccountRootIndex (accountID).GetHex ());

        return AccountState::pointer ();
    }

    if (sle->getType () != ltACCOUNT_ROOT)
        return AccountState::pointer ();

    return boost::make_shared<AccountState> (sle, accountID);
}

NicknameState::pointer Ledger::getNicknameState (uint256 const& uNickname)
{
    SHAMapItem::pointer item = mAccountStateMap->peekItem (Ledger::getNicknameIndex (uNickname));

    if (!item)
    {
        return NicknameState::pointer ();
    }

    SerializedLedgerEntry::pointer sle =
        boost::make_shared<SerializedLedgerEntry> (item->peekSerializer (), item->getTag ());

    if (sle->getType () != ltNICKNAME) return NicknameState::pointer ();

    return boost::make_shared<NicknameState> (sle);
}

bool Ledger::addTransaction (uint256 const& txID, const Serializer& txn)
{
    // low-level - just add to table
    SHAMapItem::pointer item = boost::make_shared<SHAMapItem> (txID, txn.peekData ());

    if (!mTransactionMap->addGiveItem (item, true, false))
    {
        WriteLog (lsWARNING, Ledger) << "Attempt to add transaction to ledger that already had it";
        return false;
    }

    mValidHash = false;
    return true;
}

bool Ledger::addTransaction (uint256 const& txID, const Serializer& txn, const Serializer& md)
{
    // low-level - just add to table
    Serializer s (txn.getDataLength () + md.getDataLength () + 16);
    s.addVL (txn.peekData ());
    s.addVL (md.peekData ());
    SHAMapItem::pointer item = boost::make_shared<SHAMapItem> (txID, s.peekData ());

    if (!mTransactionMap->addGiveItem (item, true, true))
    {
        WriteLog (lsFATAL, Ledger) << "Attempt to add transaction+MD to ledger that already had it";
        return false;
    }

    mValidHash = false;
    return true;
}

Transaction::pointer Ledger::getTransaction (uint256 const& transID) const
{
    SHAMapTreeNode::TNType type;
    SHAMapItem::pointer item = mTransactionMap->peekItem (transID, type);

    if (!item) return Transaction::pointer ();

    Transaction::pointer txn = theApp->getMasterTransaction ().fetch (transID, false);

    if (txn)
        return txn;

    if (type == SHAMapTreeNode::tnTRANSACTION_NM)
        txn = Transaction::sharedTransaction (item->getData (), true);
    else if (type == SHAMapTreeNode::tnTRANSACTION_MD)
    {
        Blob txnData;
        int txnLength;

        if (!item->peekSerializer ().getVL (txnData, 0, txnLength))
            return Transaction::pointer ();

        txn = Transaction::sharedTransaction (txnData, false);
    }
    else
    {
        assert (false);
        return Transaction::pointer ();
    }

    if (txn->getStatus () == NEW)
        txn->setStatus (mClosed ? COMMITTED : INCLUDED, mLedgerSeq);

    theApp->getMasterTransaction ().canonicalize (txn, false);
    return txn;
}

SerializedTransaction::pointer Ledger::getSTransaction (SHAMapItem::ref item, SHAMapTreeNode::TNType type)
{
    SerializerIterator sit (item->peekSerializer ());

    if (type == SHAMapTreeNode::tnTRANSACTION_NM)
        return boost::make_shared<SerializedTransaction> (boost::ref (sit));
    else if (type == SHAMapTreeNode::tnTRANSACTION_MD)
    {
        Serializer sTxn (sit.getVL ());
        SerializerIterator tSit (sTxn);
        return boost::make_shared<SerializedTransaction> (boost::ref (tSit));
    }

    return SerializedTransaction::pointer ();
}

SerializedTransaction::pointer Ledger::getSMTransaction (SHAMapItem::ref item, SHAMapTreeNode::TNType type,
        TransactionMetaSet::pointer& txMeta)
{
    SerializerIterator sit (item->peekSerializer ());

    if (type == SHAMapTreeNode::tnTRANSACTION_NM)
    {
        txMeta.reset ();
        return boost::make_shared<SerializedTransaction> (boost::ref (sit));
    }
    else if (type == SHAMapTreeNode::tnTRANSACTION_MD)
    {
        Serializer sTxn (sit.getVL ());
        SerializerIterator tSit (sTxn);

        txMeta = boost::make_shared<TransactionMetaSet> (item->getTag (), mLedgerSeq, sit.getVL ());
        return boost::make_shared<SerializedTransaction> (boost::ref (tSit));
    }

    txMeta.reset ();
    return SerializedTransaction::pointer ();
}

bool Ledger::getTransaction (uint256 const& txID, Transaction::pointer& txn, TransactionMetaSet::pointer& meta)
{
    SHAMapTreeNode::TNType type;
    SHAMapItem::pointer item = mTransactionMap->peekItem (txID, type);

    if (!item)
        return false;

    if (type == SHAMapTreeNode::tnTRANSACTION_NM)
    {
        // in tree with no metadata
        txn = theApp->getMasterTransaction ().fetch (txID, false);
        meta.reset ();

        if (!txn)
            txn = Transaction::sharedTransaction (item->peekData (), true);
    }
    else if (type == SHAMapTreeNode::tnTRANSACTION_MD)
    {
        // in tree with metadata
        SerializerIterator it (item->peekSerializer ());
        txn = theApp->getMasterTransaction ().fetch (txID, false);

        if (!txn)
            txn = Transaction::sharedTransaction (it.getVL (), true);
        else
            it.getVL (); // skip transaction

        meta = boost::make_shared<TransactionMetaSet> (txID, mLedgerSeq, it.getVL ());
    }
    else
        return false;

    if (txn->getStatus () == NEW)
        txn->setStatus (mClosed ? COMMITTED : INCLUDED, mLedgerSeq);

    theApp->getMasterTransaction ().canonicalize (txn, false);
    return true;
}

bool Ledger::getTransactionMeta (uint256 const& txID, TransactionMetaSet::pointer& meta)
{
    SHAMapTreeNode::TNType type;
    SHAMapItem::pointer item = mTransactionMap->peekItem (txID, type);

    if (!item)
        return false;

    if (type != SHAMapTreeNode::tnTRANSACTION_MD)
        return false;

    SerializerIterator it (item->peekSerializer ());
    it.getVL (); // skip transaction
    meta = boost::make_shared<TransactionMetaSet> (txID, mLedgerSeq, it.getVL ());

    return true;
}

bool Ledger::getMetaHex (uint256 const& transID, std::string& hex)
{
    SHAMapTreeNode::TNType type;
    SHAMapItem::pointer item = mTransactionMap->peekItem (transID, type);

    if (!item)
        return false;

    if (type != SHAMapTreeNode::tnTRANSACTION_MD)
        return false;

    SerializerIterator it (item->peekSerializer ());
    it.getVL (); // skip transaction
    hex = strHex (it.getVL ());
    return true;
}

uint256 Ledger::getHash ()
{
    if (!mValidHash)
        updateHash ();

    return mHash;
}

void Ledger::saveAcceptedLedger (Job&, bool fromConsensus)
{
    WriteLog (lsTRACE, Ledger) << "saveAcceptedLedger " << (fromConsensus ? "fromConsensus " : "fromAcquire ") << getLedgerSeq ();
    static boost::format ledgerExists ("SELECT LedgerSeq FROM Ledgers INDEXED BY SeqLedger where LedgerSeq = %u;");
    static boost::format deleteLedger ("DELETE FROM Ledgers WHERE LedgerSeq = %u;");
    static boost::format deleteTrans1 ("DELETE FROM Transactions WHERE LedgerSeq = %u;");
    static boost::format deleteTrans2 ("DELETE FROM AccountTransactions WHERE LedgerSeq = %u;");
    static boost::format deleteAcctTrans ("DELETE FROM AccountTransactions WHERE TransID = '%s';");
    static boost::format transExists ("SELECT Status FROM Transactions WHERE TransID = '%s';");
    static boost::format
    updateTx ("UPDATE Transactions SET LedgerSeq = %u, Status = '%c', TxnMeta = %s WHERE TransID = '%s';");
    static boost::format addLedger ("INSERT OR REPLACE INTO Ledgers "
                                    "(LedgerHash,LedgerSeq,PrevHash,TotalCoins,ClosingTime,PrevClosingTime,CloseTimeRes,CloseFlags,"
                                    "AccountSetHash,TransSetHash) VALUES ('%s','%u','%s','%s','%u','%u','%d','%u','%s','%s');");

    if (!getAccountHash ().isNonZero ())
    {
        WriteLog (lsFATAL, Ledger) << "AH is zero: " << getJson (0);
        assert (false);
    }

    if (getAccountHash () != mAccountStateMap->getHash ())
    {
        WriteLog (lsFATAL, Ledger) << "sAL: " << getAccountHash () << " != " << mAccountStateMap->getHash ();
        WriteLog (lsFATAL, Ledger) << "saveAcceptedLedger: seq=" << mLedgerSeq << ", fromcons=" << fromConsensus;
        assert (false);
    }

    assert (getTransHash () == mTransactionMap->getHash ());

    // Save the ledger header in the hashed object store
    Serializer s (128);
    s.add32 (HashPrefix::ledgerMaster);
    addRaw (s);
    theApp->getHashedObjectStore ().store (hotLEDGER, mLedgerSeq, s.peekData (), mHash);

    AcceptedLedger::pointer aLedger = AcceptedLedger::makeAcceptedLedger (shared_from_this ());

    {
        ScopedLock sl (theApp->getLedgerDB ()->getDBLock ());
        theApp->getLedgerDB ()->getDB ()->executeSQL (boost::str (deleteLedger % mLedgerSeq));
    }

    {
        Database* db = theApp->getTxnDB ()->getDB ();
        ScopedLock dbLock (theApp->getTxnDB ()->getDBLock ());
        db->executeSQL ("BEGIN TRANSACTION;");

        db->executeSQL (boost::str (deleteTrans1 % mLedgerSeq));
        db->executeSQL (boost::str (deleteTrans2 % mLedgerSeq));

        BOOST_FOREACH (const AcceptedLedger::value_type & vt, aLedger->getMap ())
        {
            uint256 txID = vt.second->getTransactionID ();
            theApp->getMasterTransaction ().inLedger (txID, mLedgerSeq);

            db->executeSQL (boost::str (deleteAcctTrans % txID.GetHex ()));

            const std::vector<RippleAddress>& accts = vt.second->getAffected ();

            if (!accts.empty ())
            {
                std::string sql = "INSERT INTO AccountTransactions (TransID, Account, LedgerSeq, TxnSeq) VALUES ";
                bool first = true;

                for (std::vector<RippleAddress>::const_iterator it = accts.begin (), end = accts.end (); it != end; ++it)
                {
                    if (!first)
                        sql += ", ('";
                    else
                    {
                        sql += "('";
                        first = false;
                    }

                    sql += txID.GetHex ();
                    sql += "','";
                    sql += it->humanAccountID ();
                    sql += "',";
                    sql += boost::lexical_cast<std::string> (getLedgerSeq ());
                    sql += ",";
                    sql += boost::lexical_cast<std::string> (vt.second->getTxnSeq ());
                    sql += ")";
                }

                sql += ";";
                WriteLog (lsTRACE, Ledger) << "ActTx: " << sql;
                db->executeSQL (sql);
            }
            else
                WriteLog (lsWARNING, Ledger) << "Transaction in ledger " << mLedgerSeq << " affects no accounts";

            db->executeSQL (SerializedTransaction::getMetaSQLInsertReplaceHeader () +
                            vt.second->getTxn ()->getMetaSQL (getLedgerSeq (), vt.second->getEscMeta ()) + ";");
        }
        db->executeSQL ("COMMIT TRANSACTION;");
    }

    {
        ScopedLock sl (theApp->getLedgerDB ()->getDBLock ());
        theApp->getLedgerDB ()->getDB ()->executeSQL (boost::str (addLedger %
                getHash ().GetHex () % mLedgerSeq % mParentHash.GetHex () %
                boost::lexical_cast<std::string> (mTotCoins) % mCloseTime % mParentCloseTime %
                mCloseResolution % mCloseFlags % mAccountHash.GetHex () % mTransHash.GetHex ()));
    }

    if (!fromConsensus && (theConfig.NODE_SIZE < 2)) // tiny or small
        dropCache ();

    if (theApp->getJobQueue ().getJobCountTotal (jtPUBOLDLEDGER) < 2)
        theApp->getLedgerMaster ().resumeAcquiring ();
    else
        WriteLog (lsTRACE, Ledger) << "no resume, too many pending ledger saves";
}

#ifndef NO_SQLITE3_PREPARE

Ledger::pointer Ledger::loadByIndex (uint32 ledgerIndex)
{
    Ledger::pointer ledger;
    {
        Database* db = theApp->getLedgerDB ()->getDB ();
        ScopedLock sl (theApp->getLedgerDB ()->getDBLock ());

        SqliteStatement pSt (db->getSqliteDB (), "SELECT "
                             "LedgerHash,PrevHash,AccountSetHash,TransSetHash,TotalCoins,"
                             "ClosingTime,PrevClosingTime,CloseTimeRes,CloseFlags,LedgerSeq"
                             " from Ledgers WHERE LedgerSeq = ?;");

        pSt.bind (1, ledgerIndex);
        ledger = getSQL1 (&pSt);
    }

    if (ledger)
    {
        Ledger::getSQL2 (ledger);
        ledger->setFull ();
    }

    return ledger;
}

Ledger::pointer Ledger::loadByHash (uint256 const& ledgerHash)
{
    Ledger::pointer ledger;
    {
        Database* db = theApp->getLedgerDB ()->getDB ();
        ScopedLock sl (theApp->getLedgerDB ()->getDBLock ());

        SqliteStatement pSt (db->getSqliteDB (), "SELECT "
                             "LedgerHash,PrevHash,AccountSetHash,TransSetHash,TotalCoins,"
                             "ClosingTime,PrevClosingTime,CloseTimeRes,CloseFlags,LedgerSeq"
                             " from Ledgers WHERE LedgerHash = ?;");

        pSt.bind (1, ledgerHash.GetHex ());
        ledger = getSQL1 (&pSt);
    }

    if (ledger)
    {
        assert (ledger->getHash () == ledgerHash);
        Ledger::getSQL2 (ledger);
        ledger->setFull ();
    }

    return ledger;
}

#else

Ledger::pointer Ledger::loadByIndex (uint32 ledgerIndex)
{
    // This is a low-level function with no caching
    std::string sql = "SELECT * from Ledgers WHERE LedgerSeq='";
    sql.append (boost::lexical_cast<std::string> (ledgerIndex));
    sql.append ("';");
    return getSQL (sql);
}

Ledger::pointer Ledger::loadByHash (uint256 const& ledgerHash)
{
    // This is a low-level function with no caching and only gets accepted ledgers
    std::string sql = "SELECT * from Ledgers WHERE LedgerHash='";
    sql.append (ledgerHash.GetHex ());
    sql.append ("';");
    return getSQL (sql);
}

#endif

Ledger::pointer Ledger::getSQL (const std::string& sql)
{
    // only used with sqlite3 prepared statements not used
    uint256 ledgerHash, prevHash, accountHash, transHash;
    uint64 totCoins;
    uint32 closingTime, prevClosingTime, ledgerSeq;
    int closeResolution;
    unsigned closeFlags;
    std::string hash;

    {
        Database* db = theApp->getLedgerDB ()->getDB ();
        ScopedLock sl (theApp->getLedgerDB ()->getDBLock ());

        if (!db->executeSQL (sql) || !db->startIterRows ())
            return Ledger::pointer ();

        db->getStr ("LedgerHash", hash);
        ledgerHash.SetHexExact (hash);
        db->getStr ("PrevHash", hash);
        prevHash.SetHexExact (hash);
        db->getStr ("AccountSetHash", hash);
        accountHash.SetHexExact (hash);
        db->getStr ("TransSetHash", hash);
        transHash.SetHexExact (hash);
        totCoins = db->getBigInt ("TotalCoins");
        closingTime = db->getBigInt ("ClosingTime");
        prevClosingTime = db->getBigInt ("PrevClosingTime");
        closeResolution = db->getBigInt ("CloseTimeRes");
        closeFlags = db->getBigInt ("CloseFlags");
        ledgerSeq = db->getBigInt ("LedgerSeq");
        db->endIterRows ();
    }

    // CAUTION: code below appears in two places
    bool loaded;
    Ledger::pointer ret (new Ledger (prevHash, transHash, accountHash, totCoins,
                                     closingTime, prevClosingTime, closeFlags, closeResolution, ledgerSeq, loaded));

    if (!loaded)
        return Ledger::pointer ();

    ret->setClosed ();

    if (theApp->getOPs ().haveLedger (ledgerSeq))
        ret->setAccepted ();

    if (ret->getHash () != ledgerHash)
    {
        if (ShouldLog (lsERROR, Ledger))
        {
            Log (lsERROR) << "Failed on ledger";
            Json::Value p;
            ret->addJson (p, LEDGER_JSON_FULL);
            Log (lsERROR) << p;
        }

        assert (false);
        return Ledger::pointer ();
    }

    WriteLog (lsTRACE, Ledger) << "Loaded ledger: " << ledgerHash;
    return ret;
}

Ledger::pointer Ledger::getSQL1 (SqliteStatement* stmt)
{
    int iRet = stmt->step ();

    if (stmt->isDone (iRet))
        return Ledger::pointer ();

    if (!stmt->isRow (iRet))
    {
        WriteLog (lsINFO, Ledger) << "Ledger not found: " << iRet << " = " << stmt->getError (iRet);
        return Ledger::pointer ();
    }

    uint256 ledgerHash, prevHash, accountHash, transHash;
    uint64 totCoins;
    uint32 closingTime, prevClosingTime, ledgerSeq;
    int closeResolution;
    unsigned closeFlags;

    ledgerHash.SetHexExact (stmt->peekString (0));
    prevHash.SetHexExact (stmt->peekString (1));
    accountHash.SetHexExact (stmt->peekString (2));
    transHash.SetHexExact (stmt->peekString (3));
    totCoins = stmt->getInt64 (4);
    closingTime = stmt->getUInt32 (5);
    prevClosingTime = stmt->getUInt32 (6);
    closeResolution = stmt->getUInt32 (7);
    closeFlags = stmt->getUInt32 (8);
    ledgerSeq = stmt->getUInt32 (9);

    // CAUTION: code below appears in two places
    bool loaded;
    Ledger::pointer ret (new Ledger (prevHash, transHash, accountHash, totCoins,
                                     closingTime, prevClosingTime, closeFlags, closeResolution, ledgerSeq, loaded));

    if (!loaded)
        return Ledger::pointer ();

    return ret;
}

void Ledger::getSQL2 (Ledger::ref ret)
{
    ret->setClosed ();
    ret->setImmutable ();

    if (theApp->getOPs ().haveLedger (ret->getLedgerSeq ()))
        ret->setAccepted ();

    WriteLog (lsTRACE, Ledger) << "Loaded ledger: " << ret->getHash ().GetHex ();
}

uint256 Ledger::getHashByIndex (uint32 ledgerIndex)
{
    uint256 ret;

    std::string sql = "SELECT LedgerHash FROM Ledgers INDEXED BY SeqLedger WHERE LedgerSeq='";
    sql.append (boost::lexical_cast<std::string> (ledgerIndex));
    sql.append ("';");

    std::string hash;
    {
        Database* db = theApp->getLedgerDB ()->getDB ();
        ScopedLock sl (theApp->getLedgerDB ()->getDBLock ());

        if (!db->executeSQL (sql) || !db->startIterRows ())
            return ret;

        db->getStr ("LedgerHash", hash);
        db->endIterRows ();
    }

    ret.SetHexExact (hash);
    return ret;
}

bool Ledger::getHashesByIndex (uint32 ledgerIndex, uint256& ledgerHash, uint256& parentHash)
{
#ifndef NO_SQLITE3_PREPARE

    DatabaseCon* con = theApp->getLedgerDB ();
    ScopedLock sl (con->getDBLock ());

    SqliteStatement pSt (con->getDB ()->getSqliteDB (),
                         "SELECT LedgerHash,PrevHash FROM Ledgers INDEXED BY SeqLedger Where LedgerSeq = ?;");

    pSt.bind (1, ledgerIndex);

    int ret = pSt.step ();

    if (pSt.isDone (ret))
    {
        WriteLog (lsTRACE, Ledger) << "Don't have ledger " << ledgerIndex;
        return false;
    }

    if (!pSt.isRow (ret))
    {
        assert (false);
        WriteLog (lsFATAL, Ledger) << "Unexpected statement result " << ret;
        return false;
    }

    ledgerHash.SetHexExact (pSt.peekString (0));
    parentHash.SetHexExact (pSt.peekString (1));

    return true;

#else

    std::string sql = "SELECT LedgerHash,PrevHash FROM Ledgers WHERE LedgerSeq='";
    sql.append (boost::lexical_cast<std::string> (ledgerIndex));
    sql.append ("';");

    std::string hash, prevHash;
    {
        Database* db = theApp->getLedgerDB ()->getDB ();
        ScopedLock sl (theApp->getLedgerDB ()->getDBLock ());

        if (!db->executeSQL (sql) || !db->startIterRows ())
            return false;

        db->getStr ("LedgerHash", hash);
        db->getStr ("PrevHash", prevHash);
        db->endIterRows ();
    }

    ledgerHash.SetHexExact (hash);
    parentHash.SetHexExact (prevHash);

    assert (ledgerHash.isNonZero () && ((ledgerIndex == 0) || parentHash.isNonZero ()));

    return true;

#endif
}

std::map< uint32, std::pair<uint256, uint256> > Ledger::getHashesByIndex (uint32 minSeq, uint32 maxSeq)
{
#ifndef NO_SQLITE_PREPARE
    std::map< uint32, std::pair<uint256, uint256> > ret;
    DatabaseCon* con = theApp->getLedgerDB ();
    ScopedLock sl (con->getDBLock ());

    SqliteStatement pSt (con->getDB ()->getSqliteDB (),
                         "SELECT LedgerSeq,LedgerHash,PrevHash FROM Ledgers INDEXED BY SeqLedger "
                         "WHERE LedgerSeq >= ? AND LedgerSeq <= ?;");

    std::pair<uint256, uint256> hashes;

    pSt.bind (1, minSeq);
    pSt.bind (2, maxSeq);

    do
    {
        int r = pSt.step ();

        if (pSt.isDone (r))
            return ret;

        if (!pSt.isRow (r))
            return ret;

        hashes.first.SetHexExact (pSt.peekString (1));
        hashes.second.SetHexExact (pSt.peekString (2));
        ret[pSt.getUInt32 (0)] = hashes;
    }
    while (1);

#else
#error SQLite prepare is required
#endif
}

Ledger::pointer Ledger::getLastFullLedger ()
{
    try
    {
        return getSQL ("SELECT * from Ledgers order by LedgerSeq desc limit 1;");
    }
    catch (SHAMapMissingNode& sn)
    {
        WriteLog (lsWARNING, Ledger) << "Database contains ledger with missing nodes: " << sn;
        return Ledger::pointer ();
    }
}

void Ledger::addJson (Json::Value& ret, int options)
{
    ret["ledger"] = getJson (options);
}

Json::Value Ledger::getJson (int options)
{
    Json::Value ledger (Json::objectValue);

    bool bFull = isSetBit (options, LEDGER_JSON_FULL);

    boost::recursive_mutex::scoped_lock sl (mLock);

    ledger["seqNum"]                = boost::lexical_cast<std::string> (mLedgerSeq); // DEPRECATED

    ledger["parent_hash"]           = mParentHash.GetHex ();
    ledger["ledger_index"]          = boost::lexical_cast<std::string> (mLedgerSeq);

    if (mClosed || bFull)
    {
        if (mClosed)
            ledger["closed"] = true;

        ledger["hash"]              = mHash.GetHex ();                              // DEPRECATED
        ledger["totalCoins"]        = boost::lexical_cast<std::string> (mTotCoins); // DEPRECATED

        ledger["ledger_hash"]       = mHash.GetHex ();
        ledger["transaction_hash"]  = mTransHash.GetHex ();
        ledger["account_hash"]      = mAccountHash.GetHex ();
        ledger["accepted"]          = mAccepted;
        ledger["total_coins"]       = boost::lexical_cast<std::string> (mTotCoins);

        if (mCloseTime != 0)
        {
            ledger["close_time"]            = mCloseTime;
            ledger["close_time_human"]      = boost::posix_time::to_simple_string (ptFromSeconds (mCloseTime));
            ledger["close_time_resolution"] = mCloseResolution;

            if ((mCloseFlags & sLCF_NoConsensusTime) != 0)
                ledger["close_time_estimated"] = true;
        }
    }
    else
    {
        ledger["closed"] = false;
    }

    if (mTransactionMap && (bFull || isSetBit (options, LEDGER_JSON_DUMP_TXRP)))
    {
        Json::Value txns (Json::arrayValue);
        SHAMapTreeNode::TNType type;
        ScopedLock l (mTransactionMap->Lock ());

        for (SHAMapItem::pointer item = mTransactionMap->peekFirstItem (type); !!item;
                item = mTransactionMap->peekNextItem (item->getTag (), type))
        {
            if (bFull || isSetBit (options, LEDGER_JSON_EXPAND))
            {
                if (type == SHAMapTreeNode::tnTRANSACTION_NM)
                {
                    SerializerIterator sit (item->peekSerializer ());
                    SerializedTransaction txn (sit);
                    txns.append (txn.getJson (0));
                }
                else if (type == SHAMapTreeNode::tnTRANSACTION_MD)
                {
                    SerializerIterator sit (item->peekSerializer ());
                    Serializer sTxn (sit.getVL ());

                    SerializerIterator tsit (sTxn);
                    SerializedTransaction txn (tsit);

                    TransactionMetaSet meta (item->getTag (), mLedgerSeq, sit.getVL ());
                    Json::Value txJson = txn.getJson (0);
                    txJson["metaData"] = meta.getJson (0);
                    txns.append (txJson);
                }
                else
                {
                    Json::Value error = Json::objectValue;
                    error[item->getTag ().GetHex ()] = type;
                    txns.append (error);
                }
            }
            else txns.append (item->getTag ().GetHex ());
        }

        ledger["transactions"] = txns;
    }

    if (mAccountStateMap && (bFull || isSetBit (options, LEDGER_JSON_DUMP_STATE)))
    {
        Json::Value state (Json::arrayValue);
        ScopedLock l (mAccountStateMap->Lock ());

        for (SHAMapItem::pointer item = mAccountStateMap->peekFirstItem (); !!item;
                item = mAccountStateMap->peekNextItem (item->getTag ()))
        {
            if (bFull || isSetBit (options, LEDGER_JSON_EXPAND))
            {
                SerializerIterator sit (item->peekSerializer ());
                SerializedLedgerEntry sle (sit, item->getTag ());
                state.append (sle.getJson (0));
            }
            else
                state.append (item->getTag ().GetHex ());
        }

        ledger["accountState"] = state;
    }

    return ledger;
}

void Ledger::setAcquiring (void)
{
    if (!mTransactionMap || !mAccountStateMap) throw std::runtime_error ("invalid map");

    mTransactionMap->setSynching ();
    mAccountStateMap->setSynching ();
}

bool Ledger::isAcquiring (void)
{
    return isAcquiringTx () || isAcquiringAS ();
}

bool Ledger::isAcquiringTx (void)
{
    return mTransactionMap->isSynching ();
}

bool Ledger::isAcquiringAS (void)
{
    return mAccountStateMap->isSynching ();
}

boost::posix_time::ptime Ledger::getCloseTime () const
{
    return ptFromSeconds (mCloseTime);
}

void Ledger::setCloseTime (boost::posix_time::ptime ptm)
{
    assert (!mImmutable);
    mCloseTime = iToSeconds (ptm);
}

// XXX Use shared locks where possible?
LedgerStateParms Ledger::writeBack (LedgerStateParms parms, SLE::ref entry)
{
    ScopedLock l (mAccountStateMap->Lock ());
    bool create = false;

    if (!mAccountStateMap->hasItem (entry->getIndex ()))
    {
        if ((parms & lepCREATE) == 0)
        {
            Log (lsERROR) << "WriteBack non-existent node without create";
            return lepMISSING;
        }

        create = true;
    }

    SHAMapItem::pointer item = boost::make_shared<SHAMapItem> (entry->getIndex ());
    entry->add (item->peekSerializer ());

    if (create)
    {
        assert (!mAccountStateMap->hasItem (entry->getIndex ()));

        if (!mAccountStateMap->addGiveItem (item, false, false))
        {
            assert (false);
            return lepERROR;
        }

        return lepCREATED;
    }

    if (!mAccountStateMap->updateGiveItem (item, false, false))
    {
        assert (false);
        return lepERROR;
    }

    return lepOKAY;
}

SLE::pointer Ledger::getSLE (uint256 const& uHash)
{
    SHAMapItem::pointer node = mAccountStateMap->peekItem (uHash);

    if (!node)
        return SLE::pointer ();

    return boost::make_shared<SLE> (node->peekSerializer (), node->getTag ());
}

SLE::pointer Ledger::getSLEi (uint256 const& uId)
{
    uint256 hash;

    ScopedLock sl (mAccountStateMap->Lock ());

    SHAMapItem::pointer node = mAccountStateMap->peekItem (uId, hash);

    if (!node)
        return SLE::pointer ();

    SLE::pointer ret = theApp->getSLECache ().fetch (hash);

    if (!ret)
    {
        ret = boost::make_shared<SLE> (node->peekSerializer (), node->getTag ());
        ret->setImmutable ();
        theApp->getSLECache ().canonicalize (hash, ret);
    }

    return ret;
}

void Ledger::visitAccountItems (const uint160& accountID, FUNCTION_TYPE<void (SLE::ref)> func)
{
    // Visit each item in this account's owner directory
    uint256 rootIndex       = Ledger::getOwnerDirIndex (accountID);
    uint256 currentIndex    = rootIndex;

    while (1)
    {
        SLE::pointer ownerDir   = getSLEi (currentIndex);

        if (!ownerDir || (ownerDir->getType () != ltDIR_NODE))
            return;

        BOOST_FOREACH (uint256 const & uNode, ownerDir->getFieldV256 (sfIndexes).peekValue ())
        {
            func (getSLEi (uNode));
        }

        uint64 uNodeNext    = ownerDir->getFieldU64 (sfIndexNext);

        if (!uNodeNext)
            return;

        currentIndex    = Ledger::getDirNodeIndex (rootIndex, uNodeNext);
    }

}

/*
// VFALCO: A proof of concept for making an iterator instead of a visitor
class AccountItemIterator
{
public:
    explicit AccountItemIterator (uint160 const& accountID)
    {
        // Convert the account ID to the root hash
        //
        m_rootKey = Ledger::getOwnerDirIndex (accountID);

        // Start iterating from the root
        //
        m_currentKey = rootKey;

    }

    SerializedLedgerEntry::ref operator* () const
    {
        return m_currentEntry;
    }

    SerializedLedgerEntry::ref end () const
    {
        return s_end;
    }

    AccountItemIterator& operator++ (int)
    {
    }

private:
    static SerializedLedgerEntry s_end;

    uint256 m_rootKey;
    uint256 m_currentKey;

    SerializedLedgerEntry::pointer m_currentEntry;
}
//  typedef const boost::shared_ptr<SerializedLedgerEntry>& ref;
*/


uint256 Ledger::getFirstLedgerIndex ()
{
    SHAMapItem::pointer node = mAccountStateMap->peekFirstItem ();
    return node ? node->getTag () : uint256 ();
}

uint256 Ledger::getLastLedgerIndex ()
{
    SHAMapItem::pointer node = mAccountStateMap->peekLastItem ();
    return node ? node->getTag () : uint256 ();
}

uint256 Ledger::getNextLedgerIndex (uint256 const& uHash)
{
    SHAMapItem::pointer node = mAccountStateMap->peekNextItem (uHash);
    return node ? node->getTag () : uint256 ();
}

uint256 Ledger::getNextLedgerIndex (uint256 const& uHash, uint256 const& uEnd)
{
    SHAMapItem::pointer node = mAccountStateMap->peekNextItem (uHash);

    if ((!node) || (node->getTag () > uEnd))
        return uint256 ();

    return node->getTag ();
}

uint256 Ledger::getPrevLedgerIndex (uint256 const& uHash)
{
    SHAMapItem::pointer node = mAccountStateMap->peekPrevItem (uHash);
    return node ? node->getTag () : uint256 ();
}

uint256 Ledger::getPrevLedgerIndex (uint256 const& uHash, uint256 const& uBegin)
{
    SHAMapItem::pointer node = mAccountStateMap->peekNextItem (uHash);

    if ((!node) || (node->getTag () < uBegin))
        return uint256 ();

    return node->getTag ();
}

SLE::pointer Ledger::getASNodeI (uint256 const& nodeID, LedgerEntryType let)
{
    SLE::pointer node = getSLEi (nodeID);

    if (node && (node->getType () != let))
        node.reset ();

    return node;
}

SLE::pointer Ledger::getASNode (LedgerStateParms& parms, uint256 const& nodeID,
                                LedgerEntryType let )
{
    SHAMapItem::pointer account = mAccountStateMap->peekItem (nodeID);

    if (!account)
    {
        if ( (parms & lepCREATE) == 0 )
        {
            parms = lepMISSING;

            return SLE::pointer ();
        }

        parms = parms | lepCREATED | lepOKAY;
        SLE::pointer sle = boost::make_shared<SLE> (let, nodeID);

        return sle;
    }

    SLE::pointer sle =
        boost::make_shared<SLE> (account->peekSerializer (), nodeID);

    if (sle->getType () != let)
    {
        // maybe it's a currency or something
        parms = parms | lepWRONGTYPE;
        return SLE::pointer ();
    }

    parms = parms | lepOKAY;

    return sle;
}

SLE::pointer Ledger::getAccountRoot (const uint160& accountID)
{
    return getASNodeI (getAccountRootIndex (accountID), ltACCOUNT_ROOT);
}

SLE::pointer Ledger::getAccountRoot (const RippleAddress& naAccountID)
{
    return getASNodeI (getAccountRootIndex (naAccountID.getAccountID ()), ltACCOUNT_ROOT);
}

//
// Directory
//

SLE::pointer Ledger::getDirNode (uint256 const& uNodeIndex)
{
    return getASNodeI (uNodeIndex, ltDIR_NODE);
}

//
// Generator Map
//

SLE::pointer Ledger::getGenerator (const uint160& uGeneratorID)
{
    return getASNodeI (getGeneratorIndex (uGeneratorID), ltGENERATOR_MAP);
}

//
// Nickname
//

SLE::pointer Ledger::getNickname (uint256 const& uNickname)
{
    return getASNodeI (uNickname, ltNICKNAME);
}

//
// Offer
//


SLE::pointer Ledger::getOffer (uint256 const& uIndex)
{
    return getASNodeI (uIndex, ltOFFER);
}

//
// Ripple State
//

SLE::pointer Ledger::getRippleState (uint256 const& uNode)
{
    return getASNodeI (uNode, ltRIPPLE_STATE);
}

// For an entry put in the 64 bit index or quality.
uint256 Ledger::getQualityIndex (uint256 const& uBase, const uint64 uNodeDir)
{
    // Indexes are stored in big endian format: they print as hex as stored.
    // Most significant bytes are first.  Least significant bytes represent adjacent entries.
    // We place uNodeDir in the 8 right most bytes to be adjacent.
    // Want uNodeDir in big endian format so ++ goes to the next entry for indexes.
    uint256 uNode (uBase);

    ((uint64*) uNode.end ())[-1] = htobe64 (uNodeDir);

    return uNode;
}

// Return the last 64 bits.
uint64 Ledger::getQuality (uint256 const& uBase)
{
    return be64toh (((uint64*) uBase.end ())[-1]);
}

uint256 Ledger::getQualityNext (uint256 const& uBase)
{
    static  uint256 uNext ("10000000000000000");

    uint256 uResult = uBase;

    uResult += uNext;

    return uResult;
}

uint256 Ledger::getAccountRootIndex (const uint160& uAccountID)
{
    Serializer  s (22);

    s.add16 (spaceAccount); //  2
    s.add160 (uAccountID);  // 20

    return s.getSHA512Half ();
}

uint256 Ledger::getLedgerFeeIndex ()
{
    // get the index of the node that holds the fee schedul
    Serializer s (2);
    s.add16 (spaceFee);
    return s.getSHA512Half ();
}

uint256 Ledger::getLedgerFeatureIndex ()
{
    // get the index of the node that holds the last 256 ledgers
    Serializer s (2);
    s.add16 (spaceFeature);
    return s.getSHA512Half ();
}

uint256 Ledger::getLedgerHashIndex ()
{
    // get the index of the node that holds the last 256 ledgers
    Serializer s (2);
    s.add16 (spaceSkipList);
    return s.getSHA512Half ();
}

uint256 Ledger::getLedgerHashIndex (uint32 desiredLedgerIndex)
{
    // get the index of the node that holds the set of 256 ledgers that includes this ledger's hash
    // (or the first ledger after it if it's not a multiple of 256)
    Serializer s (6);
    s.add16 (spaceSkipList);
    s.add32 (desiredLedgerIndex >> 16);
    return s.getSHA512Half ();
}

uint256 Ledger::getLedgerHash (uint32 ledgerIndex)
{
    // return the hash of the specified ledger, 0 if not available

    // easy cases
    if (ledgerIndex > mLedgerSeq)
    {
        WriteLog (lsWARNING, Ledger) << "Can't get seq " << ledgerIndex << " from " << mLedgerSeq << " future";
        return uint256 ();
    }

    if (ledgerIndex == mLedgerSeq)
        return getHash ();

    if (ledgerIndex == (mLedgerSeq - 1))
        return mParentHash;

    // within 256
    int diff = mLedgerSeq - ledgerIndex;

    if (diff <= 256)
    {
        SLE::pointer hashIndex = getSLEi (getLedgerHashIndex ());

        if (hashIndex)
        {
            assert (hashIndex->getFieldU32 (sfLastLedgerSequence) == (mLedgerSeq - 1));
            STVector256 vec = hashIndex->getFieldV256 (sfHashes);

            if (vec.size () >= diff)
                return vec.at (vec.size () - diff);

            WriteLog (lsWARNING, Ledger) << "Ledger " << mLedgerSeq << " missing hash for " << ledgerIndex
                                         << " (" << vec.size () << "," << diff << ")";
        }
        else WriteLog (lsWARNING, Ledger) << "Ledger " << mLedgerSeq << ":" << getHash () << " missing normal list";
    }

    if ((ledgerIndex & 0xff) != 0)
    {
        WriteLog (lsWARNING, Ledger) << "Can't get seq " << ledgerIndex << " from " << mLedgerSeq << " past";
        return uint256 ();
    }

    // in skiplist
    SLE::pointer hashIndex = getSLEi (getLedgerHashIndex (ledgerIndex));

    if (hashIndex)
    {
        int lastSeq = hashIndex->getFieldU32 (sfLastLedgerSequence);
        assert (lastSeq >= ledgerIndex);
        assert ((lastSeq & 0xff) == 0);
        int sDiff = (lastSeq - ledgerIndex) >> 8;

        STVector256 vec = hashIndex->getFieldV256 (sfHashes);

        if (vec.size () > sDiff)
            return vec.at (vec.size () - sDiff - 1);
    }

    WriteLog (lsWARNING, Ledger) << "Can't get seq " << ledgerIndex << " from " << mLedgerSeq << " error";
    return uint256 ();
}

std::vector< std::pair<uint32, uint256> > Ledger::getLedgerHashes ()
{
    std::vector< std::pair<uint32, uint256> > ret;
    SLE::pointer hashIndex = getSLEi (getLedgerHashIndex ());

    if (hashIndex)
    {
        STVector256 vec = hashIndex->getFieldV256 (sfHashes);
        int size = vec.size ();
        ret.reserve (size);
        uint32 seq = hashIndex->getFieldU32 (sfLastLedgerSequence) - size;

        for (int i = 0; i < size; ++i)
            ret.push_back (std::make_pair (++seq, vec.at (i)));
    }

    return ret;
}

std::vector<uint256> Ledger::getLedgerFeatures ()
{
    std::vector<uint256> usFeatures;
    SLE::pointer sleFeatures = getSLEi (getLedgerFeatureIndex ());

    if (sleFeatures)
        usFeatures = sleFeatures->getFieldV256 (sfFeatures).peekValue ();

    return usFeatures;
}

// XRP to XRP not allowed.
// Currencies must have appropriate issuer.
// Currencies or accounts must differ.
bool Ledger::isValidBook (const uint160& uTakerPaysCurrency, const uint160& uTakerPaysIssuerID,
                          const uint160& uTakerGetsCurrency, const uint160& uTakerGetsIssuerID)
{
    if (uTakerPaysCurrency.isZero ())
    {
        // XRP in

        if (uTakerPaysIssuerID.isNonZero ())    // XRP cannot have an issuer
            return false;

        if (uTakerGetsCurrency.isZero ())       // XRP to XRP not allowed
            return false;

        if (uTakerGetsIssuerID.isZero ())       // non-XRP must have issuer
            return false;

        return true;
    }

    // non-XRP in
    if (uTakerPaysIssuerID.isZero ())           // non-XRP must have issuer
        return false;

    if (uTakerGetsCurrency.isZero ())           // non-XRP to XRP
    {
        if (uTakerGetsIssuerID.isNonZero ())    // XRP cannot have issuer
            return false;
    }
    else                                        // non-XRP to non-XRP
    {
        if ((uTakerPaysCurrency == uTakerGetsCurrency) && (uTakerGetsIssuerID == uTakerPaysIssuerID))
            return false;                       // Input and output cannot be identical
    }

    return true;
}

uint256 Ledger::getBookBase (const uint160& uTakerPaysCurrency, const uint160& uTakerPaysIssuerID,
                             const uint160& uTakerGetsCurrency, const uint160& uTakerGetsIssuerID)
{
    Serializer  s (82);

    s.add16 (spaceBookDir);         //  2
    s.add160 (uTakerPaysCurrency);  // 20
    s.add160 (uTakerGetsCurrency);  // 20
    s.add160 (uTakerPaysIssuerID);  // 20
    s.add160 (uTakerGetsIssuerID);  // 20

    uint256 uBaseIndex  = getQualityIndex (s.getSHA512Half ()); // Return with quality 0.

    WriteLog (lsTRACE, Ledger) << boost::str (boost::format ("getBookBase(%s,%s,%s,%s) = %s")
                               % STAmount::createHumanCurrency (uTakerPaysCurrency)
                               % RippleAddress::createHumanAccountID (uTakerPaysIssuerID)
                               % STAmount::createHumanCurrency (uTakerGetsCurrency)
                               % RippleAddress::createHumanAccountID (uTakerGetsIssuerID)
                               % uBaseIndex.ToString ());

    assert (isValidBook (uTakerPaysCurrency, uTakerPaysIssuerID, uTakerGetsCurrency, uTakerGetsIssuerID));

    return uBaseIndex;
}

uint256 Ledger::getDirNodeIndex (uint256 const& uDirRoot, const uint64 uNodeIndex)
{
    if (uNodeIndex)
    {
        Serializer  s (42);

        s.add16 (spaceDirNode);     //  2
        s.add256 (uDirRoot);        // 32
        s.add64 (uNodeIndex);       //  8

        return s.getSHA512Half ();
    }
    else
    {
        return uDirRoot;
    }
}

uint256 Ledger::getGeneratorIndex (const uint160& uGeneratorID)
{
    Serializer  s (22);

    s.add16 (spaceGenerator);   //  2
    s.add160 (uGeneratorID);    // 20

    return s.getSHA512Half ();
}

// What is important:
// --> uNickname: is a Sha256
// <-- SHA512/2: for consistency and speed in generating indexes.
uint256 Ledger::getNicknameIndex (uint256 const& uNickname)
{
    Serializer  s (34);

    s.add16 (spaceNickname);    //  2
    s.add256 (uNickname);       // 32

    return s.getSHA512Half ();
}

uint256 Ledger::getOfferIndex (const uint160& uAccountID, uint32 uSequence)
{
    Serializer  s (26);

    s.add16 (spaceOffer);       //  2
    s.add160 (uAccountID);      // 20
    s.add32 (uSequence);        //  4

    return s.getSHA512Half ();
}

uint256 Ledger::getOwnerDirIndex (const uint160& uAccountID)
{
    Serializer  s (22);

    s.add16 (spaceOwnerDir);    //  2
    s.add160 (uAccountID);      // 20

    return s.getSHA512Half ();
}

uint256 Ledger::getRippleStateIndex (const RippleAddress& naA, const RippleAddress& naB, const uint160& uCurrency)
{
    uint160     uAID    = naA.getAccountID ();
    uint160     uBID    = naB.getAccountID ();
    bool        bAltB   = uAID < uBID;
    Serializer  s (62);

    s.add16 (spaceRipple);          //  2
    s.add160 (bAltB ? uAID : uBID); // 20
    s.add160 (bAltB ? uBID : uAID); // 20
    s.add160 (uCurrency);           // 20

    return s.getSHA512Half ();
}

bool Ledger::walkLedger ()
{
    std::vector <SHAMapMissingNode> missingNodes1;
    std::vector <SHAMapMissingNode> missingNodes2;

    mAccountStateMap->walkMap (missingNodes1, 32);

    if (ShouldLog (lsINFO, Ledger) && !missingNodes1.empty ())
    {
        Log (lsINFO) << missingNodes1.size () << " missing account node(s)";
        Log (lsINFO) << "First: " << missingNodes1[0];
    }

    mTransactionMap->walkMap (missingNodes2, 32);

    if (ShouldLog (lsINFO, Ledger) && !missingNodes2.empty ())
    {
        Log (lsINFO) << missingNodes2.size () << " missing transaction node(s)";
        Log (lsINFO) << "First: " << missingNodes2[0];
    }

    return missingNodes1.empty () && missingNodes2.empty ();
}

bool Ledger::assertSane ()
{
    if (mHash.isNonZero () &&
            mAccountHash.isNonZero () &&
            mAccountStateMap &&
            mTransactionMap &&
            (mAccountHash == mAccountStateMap->getHash ()) &&
            (mTransHash == mTransactionMap->getHash ()))
    {
        return true;
    }

    Log (lsFATAL) << "ledger is not sane";

    Json::Value j = getJson (0);

    j ["accountTreeHash"] = mAccountHash.GetHex ();
    j ["transTreeHash"] = mTransHash.GetHex ();

    assert (false);

    return false;
}

// update the skip list with the information from our previous ledger
void Ledger::updateSkipList ()
{
    if (mLedgerSeq == 0) // genesis ledger has no previous ledger
        return;

    uint32 prevIndex = mLedgerSeq - 1;

    // update record of every 256th ledger
    if ((prevIndex & 0xff) == 0)
    {
        uint256 hash = getLedgerHashIndex (prevIndex);
        SLE::pointer skipList = getSLE (hash);
        std::vector<uint256> hashes;

        // VFALCO TODO Document this skip list concept
        if (!skipList)
            skipList = boost::make_shared<SLE> (ltLEDGER_HASHES, hash);
        else
            hashes = skipList->getFieldV256 (sfHashes).peekValue ();

        assert (hashes.size () <= 256);
        hashes.push_back (mParentHash);
        skipList->setFieldV256 (sfHashes, STVector256 (hashes));
        skipList->setFieldU32 (sfLastLedgerSequence, prevIndex);

        if (writeBack (lepCREATE, skipList) == lepERROR)
        {
            assert (false);
        }
    }

    // update record of past 256 ledger
    uint256 hash = getLedgerHashIndex ();

    SLE::pointer skipList = getSLE (hash);

    std::vector <uint256> hashes;

    if (!skipList)
    {
        skipList = boost::make_shared<SLE> (ltLEDGER_HASHES, hash);
    }
    else
    {
        hashes = skipList->getFieldV256 (sfHashes).peekValue ();
    }

    assert (hashes.size () <= 256);

    if (hashes.size () == 256)
        hashes.erase (hashes.begin ());

    hashes.push_back (mParentHash);
    skipList->setFieldV256 (sfHashes, STVector256 (hashes));
    skipList->setFieldU32 (sfLastLedgerSequence, prevIndex);

    if (writeBack (lepCREATE, skipList) == lepERROR)
    {
        assert (false);
    }
}

uint32 Ledger::roundCloseTime (uint32 closeTime, uint32 closeResolution)
{
    if (closeTime == 0)
        return 0;

    closeTime += (closeResolution / 2);
    return closeTime - (closeTime % closeResolution);
}

void Ledger::pendSave (bool fromConsensus)
{
    if (!fromConsensus && !theApp->getHashRouter ().setFlag (getHash (), SF_SAVED))
        return;

    assert (isImmutable ());

    theApp->getJobQueue ().addJob (
        fromConsensus ? jtPUBLEDGER : jtPUBOLDLEDGER,
        fromConsensus ? "Ledger::pendSave" : "Ledger::pendOldSave",
        BIND_TYPE (&Ledger::saveAcceptedLedger, shared_from_this (), P_1, fromConsensus));

}

void Ledger::ownerDirDescriber (SLE::ref sle, const uint160& owner)
{
    sle->setFieldAccount (sfOwner, owner);
}

void Ledger::qualityDirDescriber (SLE::ref sle,
                                  const uint160& uTakerPaysCurrency, const uint160& uTakerPaysIssuer,
                                  const uint160& uTakerGetsCurrency, const uint160& uTakerGetsIssuer,
                                  const uint64& uRate)
{
    sle->setFieldH160 (sfTakerPaysCurrency, uTakerPaysCurrency);
    sle->setFieldH160 (sfTakerPaysIssuer, uTakerPaysIssuer);
    sle->setFieldH160 (sfTakerGetsCurrency, uTakerGetsCurrency);
    sle->setFieldH160 (sfTakerGetsIssuer, uTakerGetsIssuer);
    sle->setFieldU64 (sfExchangeRate, uRate);
}

void Ledger::initializeFees ()
{
    mBaseFee = 0;
    mReferenceFeeUnits = 0;
    mReserveBase = 0;
    mReserveIncrement = 0;
}

void Ledger::updateFees ()
{
    mBaseFee = theConfig.FEE_DEFAULT;
    mReferenceFeeUnits = 10;
    mReserveBase = theConfig.FEE_ACCOUNT_RESERVE;
    mReserveIncrement = theConfig.FEE_OWNER_RESERVE;

    LedgerStateParms p = lepNONE;
    SLE::pointer sle = getASNode (p, Ledger::getLedgerFeeIndex (), ltFEE_SETTINGS);

    if (!sle)
        return;

    if (sle->getFieldIndex (sfBaseFee) != -1)
        mBaseFee = sle->getFieldU64 (sfBaseFee);

    if (sle->getFieldIndex (sfReferenceFeeUnits) != -1)
        mReferenceFeeUnits = sle->getFieldU32 (sfReferenceFeeUnits);

    if (sle->getFieldIndex (sfReserveBase) != -1)
        mReserveBase = sle->getFieldU32 (sfReserveBase);

    if (sle->getFieldIndex (sfReserveIncrement) != -1)
        mReserveIncrement = sle->getFieldU32 (sfReserveIncrement);
}

uint64 Ledger::scaleFeeBase (uint64 fee)
{
    if (!mBaseFee)
        updateFees ();

    return theApp->getFeeTrack ().scaleFeeBase (fee, mBaseFee, mReferenceFeeUnits);
}

uint64 Ledger::scaleFeeLoad (uint64 fee, bool bAdmin)
{
    if (!mBaseFee)
        updateFees ();

    return theApp->getFeeTrack ().scaleFeeLoad (fee, mBaseFee, mReferenceFeeUnits, bAdmin);
}

std::vector<uint256> Ledger::getNeededTransactionHashes (int max, SHAMapSyncFilter* filter)
{
    std::vector<uint256> ret;

    if (mTransHash.isNonZero ())
    {
        if (mTransactionMap->getHash ().isZero ())
            ret.push_back (mTransHash);
        else
            ret = mTransactionMap->getNeededHashes (max, filter);
    }

    return ret;
}

std::vector<uint256> Ledger::getNeededAccountStateHashes (int max, SHAMapSyncFilter* filter)
{
    std::vector<uint256> ret;

    if (mAccountHash.isNonZero ())
    {
        if (mAccountStateMap->getHash ().isZero ())
            ret.push_back (mAccountHash);
        else
            ret = mAccountStateMap->getNeededHashes (max, filter);
    }

    return ret;
}

BOOST_AUTO_TEST_SUITE (quality)

BOOST_AUTO_TEST_CASE ( getquality )
{
    uint256 uBig ("D2DC44E5DC189318DB36EF87D2104CDF0A0FE3A4B698BEEE55038D7EA4C68000");

    if (6125895493223874560 != Ledger::getQuality (uBig))
        BOOST_FAIL ("Ledger::getQuality fails.");
}

BOOST_AUTO_TEST_SUITE_END ()
// vim:ts=4
