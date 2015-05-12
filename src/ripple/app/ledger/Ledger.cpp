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
#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/ledger/AcceptedLedger.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/LedgerTiming.h>
#include <ripple/app/ledger/LedgerToJson.h>
#include <ripple/app/ledger/OrderBookDB.h>
#include <ripple/app/data/DatabaseCon.h>
#include <ripple/app/data/SociDB.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/IHashRouter.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/tx/TransactionMaster.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/LoggedTimings.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/core/Config.h>
#include <ripple/core/JobQueue.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/json/to_string.h>
#include <ripple/nodestore/Database.h>
#include <ripple/protocol/HashPrefix.h>
#include <beast/module/core/text/LexicalCast.h>
#include <beast/unit_test/suite.h>
#include <boost/optional.hpp>

namespace ripple {

Ledger::Ledger (RippleAddress const& masterID, std::uint64_t startAmount)
    : mTotCoins (startAmount)
    , mLedgerSeq (1) // First Ledger
    , mCloseTime (0)
    , mParentCloseTime (0)
    , mCloseResolution (LEDGER_TIME_ACCURACY)
    , mCloseFlags (0)
    , mClosed (false)
    , mValidated (false)
    , mValidHash (false)
    , mAccepted (false)
    , mImmutable (false)
    , mTransactionMap  (std::make_shared <SHAMap> (SHAMapType::TRANSACTION,
        getApp().family(), deprecatedLogs().journal("SHAMap")))
    , mAccountStateMap (std::make_shared <SHAMap> (SHAMapType::STATE,
        getApp().family(), deprecatedLogs().journal("SHAMap")))
{
    // special case: put coins in root account
    auto startAccount = std::make_shared<AccountState> (masterID);
    auto& sle = startAccount->peekSLE ();
    sle.setFieldAmount (sfBalance, startAmount);
    sle.setFieldU32 (sfSequence, 1);

    WriteLog (lsTRACE, Ledger)
            << "root account: " << startAccount->peekSLE ().getJson (0);

    writeBack (lepCREATE, startAccount->getSLE ());

    mAccountStateMap->flushDirty (hotACCOUNT_NODE, mLedgerSeq);

    initializeFees ();
}

Ledger::Ledger (uint256 const& parentHash,
                uint256 const& transHash,
                uint256 const& accountHash,
                std::uint64_t totCoins,
                std::uint32_t closeTime,
                std::uint32_t parentCloseTime,
                int closeFlags,
                int closeResolution,
                std::uint32_t ledgerSeq,
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
    , mValidated (false)
    , mValidHash (false)
    , mAccepted (false)
    , mImmutable (true)
    , mTransactionMap (std::make_shared <SHAMap> (
        SHAMapType::TRANSACTION, transHash, getApp().family(),
                deprecatedLogs().journal("SHAMap")))
    , mAccountStateMap (std::make_shared <SHAMap> (SHAMapType::STATE, accountHash,
        getApp().family(), deprecatedLogs().journal("SHAMap")))
{
    updateHash ();
    loaded = true;

    if (mTransHash.isNonZero () &&
        !mTransactionMap->fetchRoot (mTransHash, nullptr))
    {
        loaded = false;
        WriteLog (lsWARNING, Ledger) << "Don't have TX root for ledger";
    }

    if (mAccountHash.isNonZero () &&
        !mAccountStateMap->fetchRoot (mAccountHash, nullptr))
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
    , mValidated (ledger.mValidated)
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
    , mValidated (false)
    , mValidHash (false)
    , mAccepted (false)
    , mImmutable (false)
    , mTransactionMap (std::make_shared <SHAMap> (SHAMapType::TRANSACTION,
        getApp().family(), deprecatedLogs().journal("SHAMap")))
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
        mCloseTime = roundCloseTime (
            getApp().getOPs ().getCloseTimeNC (), mCloseResolution);
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
    , mValidated (false)
    , mValidHash (false)
    , mAccepted (false)
    , mImmutable (true)
{
    Serializer s (rawLedger);

    setRaw (s, hasPrefix);

    initializeFees ();
}

Ledger::Ledger (std::string const& rawLedger, bool hasPrefix)
    : mClosed (false)
    , mValidated (false)
    , mValidHash (false)
    , mAccepted (false)
    , mImmutable (true)
{
    Serializer s (rawLedger);
    setRaw (s, hasPrefix);
    initializeFees ();
}

/** Used for ledgers loaded from JSON files */
Ledger::Ledger (std::uint32_t ledgerSeq, std::uint32_t closeTime)
    : mTotCoins (0),
      mLedgerSeq (ledgerSeq),
      mCloseTime (closeTime),
      mParentCloseTime (0),
      mCloseResolution (LEDGER_TIME_ACCURACY),
      mCloseFlags (0),
      mClosed (false),
      mValidated (false),
      mValidHash (false),
      mAccepted (false),
      mImmutable (false),
      mTransactionMap (std::make_shared <SHAMap> (
          SHAMapType::TRANSACTION, getApp().family(),
            deprecatedLogs().journal("SHAMap"))),
      mAccountStateMap (std::make_shared <SHAMap> (
          SHAMapType::STATE, getApp().family(),
            deprecatedLogs().journal("SHAMap")))
{
    initializeFees ();
}


Ledger::~Ledger ()
{
    if (mTransactionMap)
    {
        logTimedDestroy <Ledger> (mTransactionMap, "mTransactionMap");
    }

    if (mAccountStateMap)
    {
        logTimedDestroy <Ledger> (mAccountStateMap, "mAccountStateMap");
    }
}

void Ledger::setImmutable ()
{
    // Updates the hash and marks the ledger and its maps immutable

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

    // VFALCO TODO Fix this hard coded magic number 122
    Serializer s (122);
    s.add32 (HashPrefix::ledgerMaster);
    addRaw (s);
    mHash = s.getSHA512Half ();
    mValidHash = true;
}

void Ledger::setRaw (Serializer& s, bool hasPrefix)
{
    SerialIter sit (s);

    if (hasPrefix)
        sit.get32 ();

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
        mTransactionMap = std::make_shared<SHAMap> (SHAMapType::TRANSACTION, mTransHash,
            getApp().family(), deprecatedLogs().journal("SHAMap"));
        mAccountStateMap = std::make_shared<SHAMap> (SHAMapType::STATE, mAccountHash,
            getApp().family(), deprecatedLogs().journal("SHAMap"));
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

void Ledger::setAccepted (
    std::uint32_t closeTime, int closeResolution, bool correctCloseTime)
{
    // Used when we witnessed the consensus.  Rounds the close time, updates the
    // hash, and sets the ledger accepted and immutable.
    assert (mClosed && !mAccepted);
    mCloseTime = correctCloseTime ? roundCloseTime (closeTime, closeResolution)
            : closeTime;
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

bool Ledger::hasAccount (RippleAddress const& accountID) const
{
    return mAccountStateMap->hasItem (getAccountRootIndex (accountID));
}

bool Ledger::addSLE (SLE const& sle)
{
    SHAMapItem item (sle.getIndex(), sle.getSerializer());
    return mAccountStateMap->addItem(item, false, false);
}

AccountState::pointer Ledger::getAccountState (RippleAddress const& accountID) const
{
    SLE::pointer sle = getSLEi (getAccountRootIndex (accountID));

    if (!sle)
    {
        WriteLog (lsDEBUG, Ledger) << "Ledger:getAccountState:" <<
            " not found: " << accountID.humanAccountID () <<
            ": " << to_string (getAccountRootIndex (accountID));

        return AccountState::pointer ();
    }

    if (sle->getType () != ltACCOUNT_ROOT)
        return AccountState::pointer ();

    return std::make_shared<AccountState> (sle, accountID);
}

bool Ledger::addTransaction (uint256 const& txID, const Serializer& txn)
{
    // low-level - just add to table
    auto item = std::make_shared<SHAMapItem> (txID, txn.peekData ());

    if (!mTransactionMap->addGiveItem (item, true, false))
    {
        WriteLog (lsWARNING, Ledger)
                << "Attempt to add transaction to ledger that already had it";
        return false;
    }

    mValidHash = false;
    return true;
}

bool Ledger::addTransaction (
    uint256 const& txID, const Serializer& txn, const Serializer& md)
{
    // low-level - just add to table
    Serializer s (txn.getDataLength () + md.getDataLength () + 16);
    s.addVL (txn.peekData ());
    s.addVL (md.peekData ());
    auto item = std::make_shared<SHAMapItem> (txID, s.peekData ());

    if (!mTransactionMap->addGiveItem (item, true, true))
    {
        WriteLog (lsFATAL, Ledger)
                << "Attempt to add transaction+MD to ledger that already had it";
        return false;
    }

    mValidHash = false;
    return true;
}

Transaction::pointer Ledger::getTransaction (uint256 const& transID) const
{
    SHAMapTreeNode::TNType type;
    std::shared_ptr<SHAMapItem> item = mTransactionMap->peekItem (transID, type);

    if (!item)
        return Transaction::pointer ();

    auto txn = getApp().getMasterTransaction ().fetch (transID, false);

    if (txn)
        return txn;

    if (type == SHAMapTreeNode::tnTRANSACTION_NM)
        txn = Transaction::sharedTransaction (item->peekData (), Validate::YES);
    else if (type == SHAMapTreeNode::tnTRANSACTION_MD)
    {
        Blob txnData;
        int txnLength;

        if (!item->peekSerializer ().getVL (txnData, 0, txnLength))
            return Transaction::pointer ();

        txn = Transaction::sharedTransaction (txnData, Validate::NO);
    }
    else
    {
        assert (false);
        return Transaction::pointer ();
    }

    if (txn->getStatus () == NEW)
        txn->setStatus (mClosed ? COMMITTED : INCLUDED, mLedgerSeq);

    getApp().getMasterTransaction ().canonicalize (&txn);
    return txn;
}

STTx::pointer Ledger::getSTransaction (
    std::shared_ptr<SHAMapItem> const& item, SHAMapTreeNode::TNType type)
{
    SerialIter sit (item->peekSerializer ());

    if (type == SHAMapTreeNode::tnTRANSACTION_NM)
        return std::make_shared<STTx> (sit);

    if (type == SHAMapTreeNode::tnTRANSACTION_MD)
    {
        Serializer sTxn (sit.getVL ());
        SerialIter tSit (sTxn);
        return std::make_shared<STTx> (tSit);
    }

    return STTx::pointer ();
}

STTx::pointer Ledger::getSMTransaction (
    std::shared_ptr<SHAMapItem> const& item, SHAMapTreeNode::TNType type,
    TransactionMetaSet::pointer& txMeta) const
{
    SerialIter sit (item->peekSerializer ());

    if (type == SHAMapTreeNode::tnTRANSACTION_NM)
    {
        txMeta.reset ();
        return std::make_shared<STTx> (sit);
    }
    else if (type == SHAMapTreeNode::tnTRANSACTION_MD)
    {
        Serializer sTxn (sit.getVL ());
        SerialIter tSit (sTxn);

        txMeta = std::make_shared<TransactionMetaSet> (
            item->getTag (), mLedgerSeq, sit.getVL ());
        return std::make_shared<STTx> (tSit);
    }

    txMeta.reset ();
    return STTx::pointer ();
}

bool Ledger::getTransaction (
    uint256 const& txID, Transaction::pointer& txn,
    TransactionMetaSet::pointer& meta) const
{
    SHAMapTreeNode::TNType type;
    std::shared_ptr<SHAMapItem> item = mTransactionMap->peekItem (txID, type);

    if (!item)
        return false;

    if (type == SHAMapTreeNode::tnTRANSACTION_NM)
    {
        // in tree with no metadata
        txn = getApp().getMasterTransaction ().fetch (txID, false);
        meta.reset ();

        if (!txn)
        {
            txn = Transaction::sharedTransaction (
                item->peekData (), Validate::YES);
        }
    }
    else if (type == SHAMapTreeNode::tnTRANSACTION_MD)
    {
        // in tree with metadata
        SerialIter it (item->peekSerializer ());
        txn = getApp().getMasterTransaction ().fetch (txID, false);

        if (!txn)
            txn = Transaction::sharedTransaction (it.getVL (), Validate::YES);
        else
            it.getVL (); // skip transaction

        meta = std::make_shared<TransactionMetaSet> (
            txID, mLedgerSeq, it.getVL ());
    }
    else
        return false;

    if (txn->getStatus () == NEW)
        txn->setStatus (mClosed ? COMMITTED : INCLUDED, mLedgerSeq);

    getApp().getMasterTransaction ().canonicalize (&txn);
    return true;
}

bool Ledger::getTransactionMeta (
    uint256 const& txID, TransactionMetaSet::pointer& meta) const
{
    SHAMapTreeNode::TNType type;
    std::shared_ptr<SHAMapItem> item = mTransactionMap->peekItem (txID, type);

    if (!item)
        return false;

    if (type != SHAMapTreeNode::tnTRANSACTION_MD)
        return false;

    SerialIter it (item->peekSerializer ());
    it.getVL (); // skip transaction
    meta = std::make_shared<TransactionMetaSet> (txID, mLedgerSeq, it.getVL ());

    return true;
}

bool Ledger::getMetaHex (uint256 const& transID, std::string& hex) const
{
    SHAMapTreeNode::TNType type;
    std::shared_ptr<SHAMapItem> item = mTransactionMap->peekItem (transID, type);

    if (!item)
        return false;

    if (type != SHAMapTreeNode::tnTRANSACTION_MD)
        return false;

    SerialIter it (item->peekSerializer ());
    it.getVL (); // skip transaction
    hex = strHex (it.getVL ());
    return true;
}

uint256 const&
Ledger::getHash ()
{
    if (!mValidHash)
        updateHash ();

    return mHash;
}

bool Ledger::saveValidatedLedger (bool current)
{
    // TODO(tom): Fix this hard-coded SQL!
    WriteLog (lsTRACE, Ledger)
        << "saveValidatedLedger "
        << (current ? "" : "fromAcquire ") << getLedgerSeq ();
    static boost::format deleteLedger (
        "DELETE FROM Ledgers WHERE LedgerSeq = %u;");
    static boost::format deleteTrans1 (
        "DELETE FROM Transactions WHERE LedgerSeq = %u;");
    static boost::format deleteTrans2 (
        "DELETE FROM AccountTransactions WHERE LedgerSeq = %u;");
    static boost::format deleteAcctTrans (
        "DELETE FROM AccountTransactions WHERE TransID = '%s';");
    static boost::format transExists (
        "SELECT Status FROM Transactions WHERE TransID = '%s';");
    static boost::format updateTx (
        "UPDATE Transactions SET LedgerSeq = %u, Status = '%c', TxnMeta = %s "
        "WHERE TransID = '%s';");
    static boost::format addLedger (
        "INSERT OR REPLACE INTO Ledgers "
        "(LedgerHash,LedgerSeq,PrevHash,TotalCoins,ClosingTime,PrevClosingTime,"
        "CloseTimeRes,CloseFlags,AccountSetHash,TransSetHash) VALUES "
        "('%s','%u','%s','%s','%u','%u','%d','%u','%s','%s');");

    if (!getAccountHash ().isNonZero ())
    {
        WriteLog (lsFATAL, Ledger) << "AH is zero: "
                                   << getJson (*this);
        assert (false);
    }

    if (getAccountHash () != mAccountStateMap->getHash ())
    {
        WriteLog (lsFATAL, Ledger) << "sAL: " << getAccountHash ()
                                   << " != " << mAccountStateMap->getHash ();
        WriteLog (lsFATAL, Ledger) << "saveAcceptedLedger: seq="
                                   << mLedgerSeq << ", current=" << current;
        assert (false);
    }

    assert (getTransHash () == mTransactionMap->getHash ());

    // Save the ledger header in the hashed object store
    {
        Serializer s (128);
        s.add32 (HashPrefix::ledgerMaster);
        addRaw (s);
        getApp().getNodeStore ().store (
            hotLEDGER, std::move (s.modData ()), mHash);
    }

    AcceptedLedger::pointer aLedger;
    try
    {
        aLedger = AcceptedLedger::makeAcceptedLedger (shared_from_this ());
    }
    catch (...)
    {
        WriteLog (lsWARNING, Ledger) << "An accepted ledger was missing nodes";
        getApp().getLedgerMaster().failedSave(mLedgerSeq, mHash);
        {
            // Clients can now trust the database for information about this
            // ledger sequence.
            StaticScopedLockType sl (sPendingSaveLock);
            sPendingSaves.erase(getLedgerSeq());
        }
        return false;
    }

    {
        auto db = getApp().getLedgerDB ().checkoutDb();
        *db << boost::str (deleteLedger % mLedgerSeq);
    }

    {
        auto db = getApp().getTxnDB ().checkoutDb ();

        soci::transaction tr(*db);

        *db << boost::str (deleteTrans1 % getLedgerSeq ());
        *db << boost::str (deleteTrans2 % getLedgerSeq ());

        std::string const ledgerSeq (std::to_string (getLedgerSeq ()));

        for (auto const& vt : aLedger->getMap ())
        {
            uint256 transactionID = vt.second->getTransactionID ();

            getApp().getMasterTransaction ().inLedger (
                transactionID, getLedgerSeq ());

            std::string const txnId (to_string (transactionID));
            std::string const txnSeq (std::to_string (vt.second->getTxnSeq ()));

            *db << boost::str (deleteAcctTrans % transactionID);

            auto const& accts = vt.second->getAffected ();

            if (!accts.empty ())
            {
                std::string sql ("INSERT INTO AccountTransactions "
                                 "(TransID, Account, LedgerSeq, TxnSeq) VALUES ");

                // Try to make an educated guess on how much space we'll need
                // for our arguments. In argument order we have:
                // 64 + 34 + 10 + 10 = 118 + 10 extra = 128 bytes
                sql.reserve (sql.length () + (accts.size () * 128));

                bool first = true;
                for (auto const& it : accts)
                {
                    if (!first)
                        sql += ", ('";
                    else
                    {
                        sql += "('";
                        first = false;
                    }

                    sql += txnId;
                    sql += "','";
                    sql += it.humanAccountID ();
                    sql += "',";
                    sql += ledgerSeq;
                    sql += ",";
                    sql += txnSeq;
                    sql += ")";
                }
                sql += ";";
                if (ShouldLog (lsTRACE, Ledger))
                {
                    WriteLog (lsTRACE, Ledger) << "ActTx: " << sql;
                }
                *db << sql;
            }
            else
                WriteLog (lsWARNING, Ledger)
                    << "Transaction in ledger " << mLedgerSeq
                    << " affects no accounts";

            *db <<
               (STTx::getMetaSQLInsertReplaceHeader () +
                vt.second->getTxn ()->getMetaSQL (
                    getLedgerSeq (), vt.second->getEscMeta ()) + ";");
        }

        tr.commit ();
    }

    {
        auto db (getApp().getLedgerDB ().checkoutDb ());

        // TODO(tom): ARG!
        *db << boost::str (addLedger %
                           to_string (getHash ()) % mLedgerSeq % to_string (mParentHash) %
                           beast::lexicalCastThrow <std::string> (mTotCoins) % mCloseTime %
                           mParentCloseTime % mCloseResolution % mCloseFlags %
                           to_string (mAccountHash) % to_string (mTransHash));
    }

    {
        // Clients can now trust the database for information about this ledger
        // sequence.
        StaticScopedLockType sl (sPendingSaveLock);
        sPendingSaves.erase(getLedgerSeq());
    }
    return true;
}

/*
 * Load a ledger from the database.
 *
 * @param sqlSuffix: Additional string to append to the sql query.
 *        (typically a where clause).
 * @return The ledger, ledger sequence, and ledger hash.
 */
std::tuple<Ledger::pointer, std::uint32_t, uint256>
loadHelper(std::string const& sqlSuffix)
{
    Ledger::pointer ledger;
    uint256 ledgerHash;
    std::uint32_t ledgerSeq{0};

    auto db = getApp ().getLedgerDB ().checkoutDb ();

    boost::optional<std::string> sLedgerHash, sPrevHash, sAccountHash,
        sTransHash;
    boost::optional<std::uint64_t> totCoins, closingTime, prevClosingTime,
        closeResolution, closeFlags, ledgerSeq64;

    std::string const sql =
            "SELECT "
            "LedgerHash, PrevHash, AccountSetHash, TransSetHash, "
            "TotalCoins,"
            "ClosingTime, PrevClosingTime, CloseTimeRes, CloseFlags,"
            "LedgerSeq from Ledgers " +
            sqlSuffix + ";";

    *db << sql,
            soci::into(sLedgerHash),
            soci::into(sPrevHash),
            soci::into(sAccountHash),
            soci::into(sTransHash),
            soci::into(totCoins),
            soci::into(closingTime),
            soci::into(prevClosingTime),
            soci::into(closeResolution),
            soci::into(closeFlags),
            soci::into(ledgerSeq64);

    if (!db->got_data ())
    {
        std::stringstream s;
        WriteLog (lsINFO, Ledger) << "Ledger not found: " << sqlSuffix;
        return std::make_tuple (Ledger::pointer (), ledgerSeq, ledgerHash);
    }

    ledgerSeq =
        rangeCheckedCast<std::uint32_t>(ledgerSeq64.value_or (0));

    uint256 prevHash, accountHash, transHash;
    ledgerHash.SetHexExact (sLedgerHash.value_or(""));
    prevHash.SetHexExact (sPrevHash.value_or(""));
    accountHash.SetHexExact (sAccountHash.value_or(""));
    transHash.SetHexExact (sTransHash.value_or(""));

    bool loaded = false;
    ledger = std::make_shared<Ledger>(prevHash,
                                      transHash,
                                      accountHash,
                                      totCoins.value_or(0),
                                      closingTime.value_or(0),
                                      prevClosingTime.value_or(0),
                                      closeFlags.value_or(0),
                                      closeResolution.value_or(0),
                                      ledgerSeq,
                                      loaded);

    if (!loaded)
        return std::make_tuple (Ledger::pointer (), ledgerSeq, ledgerHash);

    return std::make_tuple (ledger, ledgerSeq, ledgerHash);
}

void finishLoadByIndexOrHash(Ledger::pointer& ledger)
{
    if (!ledger)
        return;

    ledger->setClosed ();
    ledger->setImmutable ();

    if (getApp ().getOPs ().haveLedger (ledger->getLedgerSeq ()))
        ledger->setAccepted ();

    WriteLog (lsTRACE, Ledger)
        << "Loaded ledger: " << to_string (ledger->getHash ());

    ledger->setFull ();
}

Ledger::pointer Ledger::loadByIndex (std::uint32_t ledgerIndex)
{
    Ledger::pointer ledger;
    {
        std::ostringstream s;
        s << "WHERE LedgerSeq = " << ledgerIndex;
        std::tie (ledger, std::ignore, std::ignore) =
            loadHelper (s.str ());
    }

    finishLoadByIndexOrHash (ledger);
    return ledger;
}

Ledger::pointer Ledger::loadByHash (uint256 const& ledgerHash)
{
    Ledger::pointer ledger;
    {
        std::ostringstream s;
        s << "WHERE LedgerHash = '" << ledgerHash << "'";
        std::tie (ledger, std::ignore, std::ignore) =
            loadHelper (s.str ());
    }

    finishLoadByIndexOrHash (ledger);

    assert (!ledger || ledger->getHash () == ledgerHash);

    return ledger;
}

uint256 Ledger::getHashByIndex (std::uint32_t ledgerIndex)
{
    uint256 ret;

    std::string sql =
        "SELECT LedgerHash FROM Ledgers INDEXED BY SeqLedger WHERE LedgerSeq='";
    sql.append (beast::lexicalCastThrow <std::string> (ledgerIndex));
    sql.append ("';");

    std::string hash;
    {
        auto db = getApp().getLedgerDB ().checkoutDb ();

        boost::optional<std::string> lh;
        *db << sql,
                soci::into (lh);

        if (!db->got_data () || !lh)
            return ret;

        hash = *lh;
        if (hash.empty ())
            return ret;
    }

    ret.SetHexExact (hash);
    return ret;
}

bool Ledger::getHashesByIndex (
    std::uint32_t ledgerIndex, uint256& ledgerHash, uint256& parentHash)
{
    auto db = getApp().getLedgerDB ().checkoutDb ();

    boost::optional <std::string> lhO, phO;

    *db << "SELECT LedgerHash,PrevHash FROM Ledgers "
            "INDEXED BY SeqLedger Where LedgerSeq = :ls;",
            soci::into (lhO),
            soci::into (phO),
            soci::use (ledgerIndex);

    if (!lhO || !phO)
    {
        WriteLog (lsTRACE, Ledger) << "Don't have ledger " << ledgerIndex;
        return false;
    }

    ledgerHash.SetHexExact (*lhO);
    parentHash.SetHexExact (*phO);

    return true;
}

std::map< std::uint32_t, std::pair<uint256, uint256> >
Ledger::getHashesByIndex (std::uint32_t minSeq, std::uint32_t maxSeq)
{
    std::map< std::uint32_t, std::pair<uint256, uint256> > ret;

    std::string sql =
        "SELECT LedgerSeq,LedgerHash,PrevHash FROM Ledgers WHERE LedgerSeq >= ";
    sql.append (beast::lexicalCastThrow <std::string> (minSeq));
    sql.append (" AND LedgerSeq <= ");
    sql.append (beast::lexicalCastThrow <std::string> (maxSeq));
    sql.append (";");

    auto db = getApp().getLedgerDB ().checkoutDb ();

    std::uint64_t ls;
    std::string lh;
    boost::optional<std::string> ph;
    soci::statement st =
        (db->prepare << sql,
         soci::into (ls),
         soci::into (lh),
         soci::into (ph));

    st.execute ();
    while (st.fetch ())
    {
        std::pair<uint256, uint256>& hashes =
                ret[rangeCheckedCast<std::uint32_t>(ls)];
        hashes.first.SetHexExact (lh);
        hashes.second.SetHexExact (ph.value_or (""));
        if (!ph)
        {
            WriteLog (lsWARNING, Ledger)
                << "Null prev hash for ledger seq: " << ls;
        }
    }

    return ret;
}

Ledger::pointer Ledger::getLastFullLedger ()
{
    try
    {
        Ledger::pointer ledger;
        std::uint32_t ledgerSeq;
        uint256 ledgerHash;
        std::tie (ledger, ledgerSeq, ledgerHash) =
                loadHelper ("order by LedgerSeq desc limit 1");

        if (!ledger)
            return ledger;

        ledger->setClosed ();

        if (getApp().getOPs ().haveLedger (ledgerSeq))
        {
            ledger->setAccepted ();
            ledger->setValidated ();
        }

        if (ledger->getHash () != ledgerHash)
        {
            if (ShouldLog (lsERROR, Ledger))
            {
                WriteLog (lsERROR, Ledger) << "Failed on ledger";
                Json::Value p;
                addJson (p, {*ledger, LedgerFill::full});
                WriteLog (lsERROR, Ledger) << p;
            }

            assert (false);
            return Ledger::pointer ();
        }

        WriteLog (lsTRACE, Ledger) << "Loaded ledger: " << ledgerHash;
        return ledger;
    }
    catch (SHAMapMissingNode& sn)
    {
        WriteLog (lsWARNING, Ledger)
                << "Database contains ledger with missing nodes: " << sn;
        return Ledger::pointer ();
    }
}

void Ledger::setAcquiring (void)
{
    if (!mTransactionMap || !mAccountStateMap)
        throw std::runtime_error ("invalid map");

    mTransactionMap->setSynching ();
    mAccountStateMap->setSynching ();
}

bool Ledger::isAcquiring (void) const
{
    return isAcquiringTx () || isAcquiringAS ();
}

bool Ledger::isAcquiringTx (void) const
{
    return mTransactionMap->isSynching ();
}

bool Ledger::isAcquiringAS (void) const
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

LedgerStateParms Ledger::writeBack (LedgerStateParms parms, SLE::ref entry)
{
    bool create = false;

    if (!mAccountStateMap->hasItem (entry->getIndex ()))
    {
        if ((parms & lepCREATE) == 0)
        {
            WriteLog (lsERROR, Ledger)
                << "WriteBack non-existent node without create";
            return lepMISSING;
        }

        create = true;
    }

    auto item = std::make_shared<SHAMapItem> (entry->getIndex ());
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

SLE::pointer Ledger::getSLE (uint256 const& uHash) const
{
    std::shared_ptr<SHAMapItem> node = mAccountStateMap->peekItem (uHash);

    if (!node)
        return SLE::pointer ();

    return std::make_shared<SLE> (node->peekSerializer (), node->getTag ());
}

SLE::pointer Ledger::getSLEi (uint256 const& uId) const
{
    uint256 hash;

    std::shared_ptr<SHAMapItem> node = mAccountStateMap->peekItem (uId, hash);

    if (!node)
        return SLE::pointer ();

    SLE::pointer ret = getApp().getSLECache ().fetch (hash);

    if (!ret)
    {
        ret = std::make_shared<SLE> (node->peekSerializer (), node->getTag ());
        ret->setImmutable ();
        getApp().getSLECache ().canonicalize (hash, ret);
    }

    return ret;
}

void Ledger::visitAccountItems (
    Account const& accountID, std::function<void (SLE::ref)> func) const
{
    // Visit each item in this account's owner directory
    uint256 rootIndex       = getOwnerDirIndex (accountID);
    uint256 currentIndex    = rootIndex;

    while (1)
    {
        SLE::pointer ownerDir   = getSLEi (currentIndex);

        if (!ownerDir || (ownerDir->getType () != ltDIR_NODE))
            return;

        for (auto const& node : ownerDir->getFieldV256 (sfIndexes))
        {
            func (getSLEi (node));
        }

        std::uint64_t uNodeNext = ownerDir->getFieldU64 (sfIndexNext);

        if (!uNodeNext)
            return;

        currentIndex = getDirNodeIndex (rootIndex, uNodeNext);
    }

}

bool Ledger::visitAccountItems (
    Account const& accountID,
    uint256 const& startAfter,
    std::uint64_t const hint,
    unsigned int limit,
    std::function <bool (SLE::ref)> func) const
{
    // Visit each item in this account's owner directory
    uint256 const rootIndex (getOwnerDirIndex (accountID));
    uint256 currentIndex (rootIndex);

    // If startAfter is not zero try jumping to that page using the hint
    if (startAfter.isNonZero ())
    {
        uint256 const hintIndex (getDirNodeIndex (rootIndex, hint));
        SLE::pointer hintDir (getSLEi (hintIndex));
        if (hintDir != nullptr)
        {
            for (auto const& node : hintDir->getFieldV256 (sfIndexes))
            {
                if (node == startAfter)
                {
                    // We found the hint, we can start here
                    currentIndex = hintIndex;
                    break;
                }
            }
        }

        bool found (false);
        for (;;)
        {
            SLE::pointer ownerDir (getSLEi (currentIndex));

            if (! ownerDir || ownerDir->getType () != ltDIR_NODE)
                return found;

            for (auto const& node : ownerDir->getFieldV256 (sfIndexes))
            {
                if (!found)
                {
                    if (node == startAfter)
                        found = true;
                }
                else if (func (getSLEi (node)) && limit-- <= 1)
                {
                    return found;
                }
            }

            std::uint64_t const uNodeNext (ownerDir->getFieldU64 (sfIndexNext));

            if (uNodeNext == 0)
                return found;

            currentIndex = getDirNodeIndex (rootIndex, uNodeNext);
        }
    }
    else
    {
        for (;;)
        {
            SLE::pointer ownerDir (getSLEi (currentIndex));

            if (! ownerDir || ownerDir->getType () != ltDIR_NODE)
                return true;

            for (auto const& node : ownerDir->getFieldV256 (sfIndexes))
            {
                if (func (getSLEi (node)) && limit-- <= 1)
                    return true;
            }

            std::uint64_t const uNodeNext (ownerDir->getFieldU64 (sfIndexNext));

            if (uNodeNext == 0)
                return true;

            currentIndex = getDirNodeIndex (rootIndex, uNodeNext);
        }
    }
}

static void visitHelper (
    std::function<void (SLE::ref)>& function, std::shared_ptr<SHAMapItem> const& item)
{
    function (std::make_shared<SLE> (item->peekSerializer (), item->getTag ()));
}

void Ledger::visitStateItems (std::function<void (SLE::ref)> function) const
{
    try
    {
        if (mAccountStateMap)
        {
            mAccountStateMap->visitLeaves(
                std::bind(&visitHelper, std::ref(function),
                          std::placeholders::_1));
        }
    }
    catch (SHAMapMissingNode&)
    {
        if (mHash.isNonZero ())
        {
            getApp().getInboundLedgers().acquire(
                mHash, mLedgerSeq, InboundLedger::fcGENERIC);
        }
        throw;
    }
}

uint256 Ledger::getFirstLedgerIndex () const
{
    std::shared_ptr<SHAMapItem> node = mAccountStateMap->peekFirstItem ();
    return node ? node->getTag () : uint256 ();
}

uint256 Ledger::getLastLedgerIndex () const
{
    std::shared_ptr<SHAMapItem> node = mAccountStateMap->peekLastItem ();
    return node ? node->getTag () : uint256 ();
}

uint256 Ledger::getNextLedgerIndex (uint256 const& uHash) const
{
    std::shared_ptr<SHAMapItem> node = mAccountStateMap->peekNextItem (uHash);
    return node ? node->getTag () : uint256 ();
}

uint256 Ledger::getNextLedgerIndex (uint256 const& uHash, uint256 const& uEnd) const
{
    std::shared_ptr<SHAMapItem> node = mAccountStateMap->peekNextItem (uHash);

    if ((!node) || (node->getTag () > uEnd))
        return uint256 ();

    return node->getTag ();
}

uint256 Ledger::getPrevLedgerIndex (uint256 const& uHash) const
{
    std::shared_ptr<SHAMapItem> node = mAccountStateMap->peekPrevItem (uHash);
    return node ? node->getTag () : uint256 ();
}

uint256 Ledger::getPrevLedgerIndex (uint256 const& uHash, uint256 const& uBegin) const
{
    std::shared_ptr<SHAMapItem> node = mAccountStateMap->peekNextItem (uHash);

    if ((!node) || (node->getTag () < uBegin))
        return uint256 ();

    return node->getTag ();
}

SLE::pointer Ledger::getASNodeI (uint256 const& nodeID, LedgerEntryType let) const
{
    SLE::pointer node = getSLEi (nodeID);

    if (node && (node->getType () != let))
        node.reset ();

    return node;
}

SLE::pointer Ledger::getASNode (
    LedgerStateParms& parms, uint256 const& nodeID, LedgerEntryType let) const
{
    std::shared_ptr<SHAMapItem> account = mAccountStateMap->peekItem (nodeID);

    if (!account)
    {
        if ( (parms & lepCREATE) == 0 )
        {
            parms = lepMISSING;

            return SLE::pointer ();
        }

        parms = parms | lepCREATED | lepOKAY;
        SLE::pointer sle = std::make_shared<SLE> (let, nodeID);

        return sle;
    }

    SLE::pointer sle =
        std::make_shared<SLE> (account->peekSerializer (), nodeID);

    if (sle->getType () != let)
    {
        // maybe it's a currency or something
        parms = parms | lepWRONGTYPE;
        return SLE::pointer ();
    }

    parms = parms | lepOKAY;

    return sle;
}

SLE::pointer Ledger::getAccountRoot (Account const& accountID) const
{
    return getASNodeI (getAccountRootIndex (accountID), ltACCOUNT_ROOT);
}

SLE::pointer Ledger::getAccountRoot (RippleAddress const& naAccountID) const
{
    return getASNodeI (getAccountRootIndex (
        naAccountID.getAccountID ()), ltACCOUNT_ROOT);
}

SLE::pointer Ledger::getDirNode (uint256 const& uNodeIndex) const
{
    return getASNodeI (uNodeIndex, ltDIR_NODE);
}

SLE::pointer
Ledger::getOffer (uint256 const& uIndex) const
{
    return getASNodeI (uIndex, ltOFFER);
}

SLE::pointer
Ledger::getOffer (Account const& account, std::uint32_t uSequence) const
{
    return getOffer (getOfferIndex (account, uSequence));
}

SLE::pointer
Ledger::getRippleState (uint256 const& uNode) const
{
    return getASNodeI (uNode, ltRIPPLE_STATE);
}

SLE::pointer
Ledger::getRippleState (
    Account const& a, Account const& b, Currency const& currency) const
{
    return getRippleState (getRippleStateIndex (a, b, currency));
}

uint256 Ledger::getLedgerHash (std::uint32_t ledgerIndex)
{
    // Return the hash of the specified ledger, 0 if not available

    // Easy cases...
    if (ledgerIndex > mLedgerSeq)
    {
        WriteLog (lsWARNING, Ledger) << "Can't get seq " << ledgerIndex
                                     << " from " << mLedgerSeq << " future";
        return uint256 ();
    }

    if (ledgerIndex == mLedgerSeq)
        return getHash ();

    if (ledgerIndex == (mLedgerSeq - 1))
        return mParentHash;

    // Within 256...
    int diff = mLedgerSeq - ledgerIndex;

    if (diff <= 256)
    {
        auto hashIndex = getSLEi (getLedgerHashIndex ());

        if (hashIndex)
        {
            assert (hashIndex->getFieldU32 (sfLastLedgerSequence) ==
                    (mLedgerSeq - 1));
            STVector256 vec = hashIndex->getFieldV256 (sfHashes);

            if (vec.size () >= diff)
                return vec[vec.size () - diff];

            WriteLog (lsWARNING, Ledger)
                    << "Ledger " << mLedgerSeq
                    << " missing hash for " << ledgerIndex
                    << " (" << vec.size () << "," << diff << ")";
        }
        else
        {
            WriteLog (lsWARNING, Ledger)
                    << "Ledger " << mLedgerSeq
                    << ":" << getHash () << " missing normal list";
        }
    }

    if ((ledgerIndex & 0xff) != 0)
    {
        WriteLog (lsDEBUG, Ledger) << "Can't get seq " << ledgerIndex
                                     << " from " << mLedgerSeq << " past";
        return uint256 ();
    }

    // in skiplist
    auto hashIndex = getSLEi (getLedgerHashIndex (ledgerIndex));

    if (hashIndex)
    {
        int lastSeq = hashIndex->getFieldU32 (sfLastLedgerSequence);
        assert (lastSeq >= ledgerIndex);
        assert ((lastSeq & 0xff) == 0);
        int sDiff = (lastSeq - ledgerIndex) >> 8;

        STVector256 vec = hashIndex->getFieldV256 (sfHashes);

        if (vec.size () > sDiff)
            return vec[vec.size () - sDiff - 1];
    }

    WriteLog (lsWARNING, Ledger) << "Can't get seq " << ledgerIndex
                                 << " from " << mLedgerSeq << " error";
    return uint256 ();
}

Ledger::LedgerHashes Ledger::getLedgerHashes () const
{
    LedgerHashes ret;
    SLE::pointer hashIndex = getSLEi (getLedgerHashIndex ());

    if (hashIndex)
    {
        STVector256 vec = hashIndex->getFieldV256 (sfHashes);
        int size = vec.size ();
        ret.reserve (size);
        auto seq = hashIndex->getFieldU32 (sfLastLedgerSequence) - size;

        for (int i = 0; i < size; ++i)
            ret.push_back (std::make_pair (++seq, vec[i]));
    }

    return ret;
}

std::vector<uint256> Ledger::getLedgerAmendments () const
{
    std::vector<uint256> amendments;
    SLE::pointer sleAmendments = getSLEi (getLedgerAmendmentIndex ());

    if (sleAmendments)
        amendments = static_cast<decltype(amendments)> (sleAmendments->getFieldV256 (sfAmendments));

    return amendments;
}

bool Ledger::walkLedger () const
{
    std::vector <SHAMapMissingNode> missingNodes1;
    std::vector <SHAMapMissingNode> missingNodes2;

    if (mAccountStateMap->getHash().isZero() &&
        ! mAccountHash.isZero() &&
        ! mAccountStateMap->fetchRoot (mAccountHash, nullptr))
    {
        missingNodes1.emplace_back (SHAMapType::STATE, mAccountHash);
    }
    else
    {
        mAccountStateMap->walkMap (missingNodes1, 32);
    }

    if (ShouldLog (lsINFO, Ledger) && !missingNodes1.empty ())
    {
        WriteLog (lsINFO, Ledger)
            << missingNodes1.size () << " missing account node(s)";
        WriteLog (lsINFO, Ledger)
            << "First: " << missingNodes1[0];
    }

    if (mTransactionMap->getHash().isZero() &&
        mTransHash.isNonZero() &&
        ! mTransactionMap->fetchRoot (mTransHash, nullptr))
    {
        missingNodes2.emplace_back (SHAMapType::TRANSACTION, mTransHash);
    }
    else
    {
        mTransactionMap->walkMap (missingNodes2, 32);
    }

    if (ShouldLog (lsINFO, Ledger) && !missingNodes2.empty ())
    {
        WriteLog (lsINFO, Ledger)
            << missingNodes2.size () << " missing transaction node(s)";
        WriteLog (lsINFO, Ledger)
            << "First: " << missingNodes2[0];
    }

    return missingNodes1.empty () && missingNodes2.empty ();
}

bool Ledger::assertSane () const
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

    Json::Value j = getJson (*this);

    j [jss::accountTreeHash] = to_string (mAccountHash);
    j [jss::transTreeHash] = to_string (mTransHash);

    WriteLog (lsFATAL, Ledger) << "ledger is not sane" << j;

    assert (false);

    return false;
}

// update the skip list with the information from our previous ledger
void Ledger::updateSkipList ()
{
    if (mLedgerSeq == 0) // genesis ledger has no previous ledger
        return;

    std::uint32_t prevIndex = mLedgerSeq - 1;

    // update record of every 256th ledger
    if ((prevIndex & 0xff) == 0)
    {
        uint256 hash = getLedgerHashIndex (prevIndex);
        SLE::pointer skipList = getSLE (hash);
        std::vector<uint256> hashes;

        // VFALCO TODO Document this skip list concept
        if (!skipList)
            skipList = std::make_shared<SLE> (ltLEDGER_HASHES, hash);
        else
            hashes = static_cast<decltype(hashes)> (skipList->getFieldV256 (sfHashes));

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
        skipList = std::make_shared<SLE> (ltLEDGER_HASHES, hash);
    else
        hashes = static_cast<decltype(hashes)>(skipList->getFieldV256 (sfHashes));

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

std::uint32_t Ledger::roundCloseTime (
    std::uint32_t closeTime, std::uint32_t closeResolution)
{
    if (closeTime == 0)
        return 0;

    closeTime += (closeResolution / 2);
    return closeTime - (closeTime % closeResolution);
}

/** Save, or arrange to save, a fully-validated ledger
    Returns false on error
*/
bool Ledger::pendSaveValidated (bool isSynchronous, bool isCurrent)
{
    if (!getApp().getHashRouter ().setFlag (getHash (), SF_SAVED))
    {
        WriteLog (lsDEBUG, Ledger) << "Double pend save for " << getLedgerSeq();
        return true;
    }

    assert (isImmutable ());

    {
        StaticScopedLockType sl (sPendingSaveLock);
        if (!sPendingSaves.insert(getLedgerSeq()).second)
        {
            WriteLog (lsDEBUG, Ledger)
                << "Pend save with seq in pending saves " << getLedgerSeq();
            return true;
        }
    }

    if (isSynchronous)
    {
        return saveValidatedLedger(isCurrent);
    }
    else if (isCurrent)
    {
        getApp().getJobQueue ().addJob (jtPUBLEDGER, "Ledger::pendSave",
            std::bind (&Ledger::saveValidatedLedgerAsync, shared_from_this (),
                       std::placeholders::_1, isCurrent));
    }
    else
    {
        getApp().getJobQueue ().addJob (jtPUBOLDLEDGER, "Ledger::pendOldSave",
            std::bind (&Ledger::saveValidatedLedgerAsync, shared_from_this (),
                       std::placeholders::_1, isCurrent));
    }

    return true;
}

std::set<std::uint32_t> Ledger::getPendingSaves()
{
   StaticScopedLockType sl (sPendingSaveLock);
   return sPendingSaves;
}

void Ledger::ownerDirDescriber (SLE::ref sle, bool, Account const& owner)
{
    sle->setFieldAccount (sfOwner, owner);
}

void Ledger::qualityDirDescriber (
    SLE::ref sle, bool isNew,
    Currency const& uTakerPaysCurrency, Account const& uTakerPaysIssuer,
    Currency const& uTakerGetsCurrency, Account const& uTakerGetsIssuer,
    const std::uint64_t& uRate)
{
    sle->setFieldH160 (sfTakerPaysCurrency, uTakerPaysCurrency);
    sle->setFieldH160 (sfTakerPaysIssuer, uTakerPaysIssuer);
    sle->setFieldH160 (sfTakerGetsCurrency, uTakerGetsCurrency);
    sle->setFieldH160 (sfTakerGetsIssuer, uTakerGetsIssuer);
    sle->setFieldU64 (sfExchangeRate, uRate);
    if (isNew)
    {
        getApp().getOrderBookDB().addOrderBook(
            {{uTakerPaysCurrency, uTakerPaysIssuer},
                {uTakerGetsCurrency, uTakerGetsIssuer}});
    }
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
    if (mBaseFee)
        return;
    std::uint64_t baseFee = getConfig ().FEE_DEFAULT;
    std::uint32_t referenceFeeUnits = getConfig ().TRANSACTION_FEE_BASE;
    std::uint32_t reserveBase = getConfig ().FEE_ACCOUNT_RESERVE;
    std::int64_t reserveIncrement = getConfig ().FEE_OWNER_RESERVE;

    LedgerStateParms p = lepNONE;
    auto sle = getASNode (p, getLedgerFeeIndex (), ltFEE_SETTINGS);

    if (sle)
    {
        if (sle->getFieldIndex (sfBaseFee) != -1)
            baseFee = sle->getFieldU64 (sfBaseFee);

        if (sle->getFieldIndex (sfReferenceFeeUnits) != -1)
            referenceFeeUnits = sle->getFieldU32 (sfReferenceFeeUnits);

        if (sle->getFieldIndex (sfReserveBase) != -1)
            reserveBase = sle->getFieldU32 (sfReserveBase);

        if (sle->getFieldIndex (sfReserveIncrement) != -1)
            reserveIncrement = sle->getFieldU32 (sfReserveIncrement);
    }

    {
        StaticScopedLockType sl (sPendingSaveLock);
        if (mBaseFee == 0)
        {
            mBaseFee = baseFee;
            mReferenceFeeUnits = referenceFeeUnits;
            mReserveBase = reserveBase;
            mReserveIncrement = reserveIncrement;
        }
    }
}

std::uint64_t Ledger::scaleFeeBase (std::uint64_t fee)
{
    // Converts a fee in fee units to a fee in drops
    updateFees ();
    return getApp().getFeeTrack ().scaleFeeBase (
        fee, mBaseFee, mReferenceFeeUnits);
}

std::uint64_t Ledger::scaleFeeLoad (std::uint64_t fee, bool bAdmin)
{
    updateFees ();
    return getApp().getFeeTrack ().scaleFeeLoad (
        fee, mBaseFee, mReferenceFeeUnits, bAdmin);
}

std::vector<uint256> Ledger::getNeededTransactionHashes (
    int max, SHAMapSyncFilter* filter) const
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

std::vector<uint256> Ledger::getNeededAccountStateHashes (
    int max, SHAMapSyncFilter* filter) const
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

Ledger::StaticLockType Ledger::sPendingSaveLock;
std::set<std::uint32_t> Ledger::sPendingSaves;

} // ripple
