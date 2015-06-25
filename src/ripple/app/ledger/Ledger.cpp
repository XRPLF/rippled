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
#include <ripple/app/ledger/PendingSaves.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/IHashRouter.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/tx/TransactionMaster.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/SHA512Half.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/core/SociDB.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/core/Config.h>
#include <ripple/core/JobQueue.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/json/to_string.h>
#include <ripple/nodestore/Database.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/types.h>
#include <beast/module/core/text/LexicalCast.h>
#include <beast/unit_test/suite.h>
#include <boost/optional.hpp>
#include <cassert>
#include <utility>

namespace ripple {

/*  Create the "genesis" account root.
    The genesis account root contains all the XRP
    that will ever exist in the system.
    @param id The AccountID of the account root
    @param drops The number of drops to start with
*/
static
std::shared_ptr<SLE>
makeGenesisAccount (AccountID const& id,
    std::uint64_t drops)
{
    std::shared_ptr<SLE> sle =
        std::make_shared<SLE>(ltACCOUNT_ROOT,
            getAccountRootIndex(id));
    sle->setAccountID (sfAccount, id);
    sle->setFieldAmount (sfBalance, drops);
    sle->setFieldU32 (sfSequence, 1);
    return sle;
}

// VFALCO This constructor could be eliminating by providing
//        a free function createGenesisLedger, call the
//        other constructor with appropriate parameters, and
//        then create the master account / flush dirty.
//
// VFALCO Use `AnyPublicKey masterPublicKey`
Ledger::Ledger (RippleAddress const& masterPublicKey,
        std::uint64_t balanceInDrops)
    : mTotCoins (balanceInDrops)
    , seq_ (1) // First Ledger
    , mCloseTime (0)
    , mParentCloseTime (0)
    , mCloseResolution (ledgerDefaultTimeResolution)
    , mCloseFlags (0)
    , mImmutable (false)
    , txMap_  (std::make_shared <SHAMap> (SHAMapType::TRANSACTION,
        getApp().family(), deprecatedLogs().journal("SHAMap")))
    , stateMap_ (std::make_shared <SHAMap> (SHAMapType::STATE,
        getApp().family(), deprecatedLogs().journal("SHAMap")))
    // VFALCO Needs audit
    , fees_(getFees(*this, getConfig()))
{
    auto sle = makeGenesisAccount(
        calcAccountID(masterPublicKey),
            balanceInDrops);
    WriteLog (lsTRACE, Ledger)
            << "root account: " << sle->getJson(0);
    unchecked_insert(std::move(sle));
    stateMap_->flushDirty (hotACCOUNT_NODE, seq_);
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
    , seq_ (ledgerSeq)
    , mCloseTime (closeTime)
    , mParentCloseTime (parentCloseTime)
    , mCloseResolution (closeResolution)
    , mCloseFlags (closeFlags)
    , mImmutable (true)
    , txMap_ (std::make_shared <SHAMap> (
        SHAMapType::TRANSACTION, transHash, getApp().family(),
                deprecatedLogs().journal("SHAMap")))
    , stateMap_ (std::make_shared <SHAMap> (SHAMapType::STATE, accountHash,
        getApp().family(), deprecatedLogs().journal("SHAMap")))
    // VFALCO Needs audit
    , fees_(getFees(*this, getConfig()))
{
    updateHash ();
    loaded = true;

    if (mTransHash.isNonZero () &&
        !txMap_->fetchRoot (mTransHash, nullptr))
    {
        loaded = false;
        WriteLog (lsWARNING, Ledger) << "Don't have TX root for ledger";
    }

    if (mAccountHash.isNonZero () &&
        !stateMap_->fetchRoot (mAccountHash, nullptr))
    {
        loaded = false;
        WriteLog (lsWARNING, Ledger) << "Don't have AS root for ledger";
    }

    txMap_->setImmutable ();
    stateMap_->setImmutable ();
}

// Create a new ledger that's a snapshot of this one
Ledger::Ledger (Ledger const& ledger,
                bool isMutable)
    : mParentHash (ledger.mParentHash)
    , mTotCoins (ledger.mTotCoins)
    , seq_ (ledger.seq_)
    , mCloseTime (ledger.mCloseTime)
    , mParentCloseTime (ledger.mParentCloseTime)
    , mCloseResolution (ledger.mCloseResolution)
    , mCloseFlags (ledger.mCloseFlags)
    , mClosed (ledger.mClosed)
    , mValidated (ledger.mValidated)
    , mAccepted (ledger.mAccepted)
    , mImmutable (!isMutable)
    , txMap_ (ledger.txMap_->snapShot (isMutable))
    , stateMap_ (ledger.stateMap_->snapShot (isMutable))
    // VFALCO Needs audit
    , fees_(getFees(*this, getConfig()))
{
    updateHash ();
}

// Create a new ledger that follows this one
Ledger::Ledger (bool /* dummy */,
                Ledger& prevLedger)
    : mTotCoins (prevLedger.mTotCoins)
    , seq_ (prevLedger.seq_ + 1)
    , mParentCloseTime (prevLedger.mCloseTime)
    , mCloseResolution (prevLedger.mCloseResolution)
    , mCloseFlags (0)
    , mImmutable (false)
    , txMap_ (std::make_shared <SHAMap> (SHAMapType::TRANSACTION,
        getApp().family(), deprecatedLogs().journal("SHAMap")))
    , stateMap_ (prevLedger.stateMap_->snapShot (true))
    // VFALCO Needs audit
    , fees_(getFees(*this, getConfig()))
{
    prevLedger.updateHash ();

    mParentHash = prevLedger.getHash ();

    assert (mParentHash.isNonZero ());

    mCloseResolution = getNextLedgerTimeResolution (prevLedger.mCloseResolution,
        prevLedger.getCloseAgree (), seq_);

    if (prevLedger.mCloseTime == 0)
    {
        mCloseTime = roundCloseTime (
            getApp().getOPs ().getCloseTimeNC (), mCloseResolution);
    }
    else
    {
        mCloseTime = prevLedger.mCloseTime + mCloseResolution;
    }
}

Ledger::Ledger (void const* data,
        std::size_t size, bool hasPrefix)
    : mImmutable (true)
{
    SerialIter sit (data, size);
    setRaw (sit, hasPrefix);
    fees_ = getFees(*this, getConfig());
}

Ledger::Ledger (std::uint32_t ledgerSeq, std::uint32_t closeTime)
    : mTotCoins (0)
    , seq_ (ledgerSeq)
    , mCloseTime (closeTime)
    , mParentCloseTime (0)
    , mCloseResolution (ledgerDefaultTimeResolution)
    , mCloseFlags (0)
    , mImmutable (false)
    , txMap_ (std::make_shared <SHAMap> (
          SHAMapType::TRANSACTION, getApp().family(),
            deprecatedLogs().journal("SHAMap")))
    , stateMap_ (std::make_shared <SHAMap> (
          SHAMapType::STATE, getApp().family(),
            deprecatedLogs().journal("SHAMap")))
    // VFALCO Needs audit
    , fees_(getFees(*this, getConfig()))
{
}

//------------------------------------------------------------------------------

Ledger::~Ledger ()
{
}

void Ledger::setImmutable ()
{
    // Updates the hash and marks the ledger and its maps immutable

    updateHash ();
    mImmutable = true;

    if (txMap_)
        txMap_->setImmutable ();

    if (stateMap_)
        stateMap_->setImmutable ();
}

void Ledger::updateHash()
{
    if (! mImmutable)
    {
        if (txMap_)
            mTransHash = txMap_->getHash ();
        else
            mTransHash.zero ();

        if (stateMap_)
            mAccountHash = stateMap_->getHash ();
        else
            mAccountHash.zero ();
    }

    // VFALCO This has to match addRaw
    mHash = sha512Half(
        HashPrefix::ledgerMaster,
        std::uint32_t(seq_),
        std::uint64_t(mTotCoins),
        mParentHash,
        mTransHash,
        mAccountHash,
        std::uint32_t(mParentCloseTime),
        std::uint32_t(mCloseTime),
        std::uint8_t(mCloseResolution),
        std::uint8_t(mCloseFlags));
    mValidHash = true;
}

void Ledger::setRaw (SerialIter& sit, bool hasPrefix)
{
    if (hasPrefix)
        sit.get32 ();

    seq_ =        sit.get32 ();
    mTotCoins =         sit.get64 ();
    mParentHash =       sit.get256 ();
    mTransHash =        sit.get256 ();
    mAccountHash =      sit.get256 ();
    mParentCloseTime =  sit.get32 ();
    mCloseTime =        sit.get32 ();
    mCloseResolution =  sit.get8 ();
    mCloseFlags =       sit.get8 ();
    updateHash ();
    txMap_ = std::make_shared<SHAMap> (SHAMapType::TRANSACTION, mTransHash,
        getApp().family(), deprecatedLogs().journal("SHAMap"));
    stateMap_ = std::make_shared<SHAMap> (SHAMapType::STATE, mAccountHash,
        getApp().family(), deprecatedLogs().journal("SHAMap"));
}

void Ledger::addRaw (Serializer& s) const
{
    s.add32 (seq_);
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

bool Ledger::addSLE (SLE const& sle)
{
    SHAMapItem item (sle.getIndex(), sle.getSerializer());
    return stateMap_->addItem(item, false, false);
}

Transaction::pointer
getTransaction (Ledger const& ledger,
    uint256 const& transID, TransactionMaster& cache)
{
    SHAMapTreeNode::TNType type;
    auto const item =
        ledger.txMap().peekItem (transID, type);

    if (!item)
        return Transaction::pointer ();

    auto txn = cache.fetch (transID, false);

    if (txn)
        return txn;

    if (type == SHAMapTreeNode::tnTRANSACTION_NM)
        txn = Transaction::sharedTransaction (item->peekData (), Validate::YES);
    else if (type == SHAMapTreeNode::tnTRANSACTION_MD)
    {
        Blob txnData;
        int txnLength;

        if (!item->peekSerializer().getVL (txnData, 0, txnLength))
            return Transaction::pointer ();

        txn = Transaction::sharedTransaction (txnData, Validate::NO);
    }
    else
    {
        assert (false);
        return Transaction::pointer ();
    }

    if (txn->getStatus () == NEW)
        txn->setStatus (ledger.isClosed() ? COMMITTED : INCLUDED, ledger.getLedgerSeq());

    cache.canonicalize (&txn);
    return txn;
}

bool
getTransaction (Ledger const& ledger,
    uint256 const& txID, Transaction::pointer& txn,
        TransactionMetaSet::pointer& meta,
            TransactionMaster& cache)
{
    SHAMapTreeNode::TNType type;
    auto const item =
        ledger.txMap().peekItem (txID, type);
    if (!item)
        return false;

    if (type == SHAMapTreeNode::tnTRANSACTION_NM)
    {
        // in tree with no metadata
        txn = cache.fetch (txID, false);
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
        SerialIter it (item->slice());
        txn = getApp().getMasterTransaction ().fetch (txID, false);

        if (!txn)
            txn = Transaction::sharedTransaction (it.getVL (), Validate::YES);
        else
            it.getVL (); // skip transaction

        meta = std::make_shared<TransactionMetaSet> (
            txID, ledger.seq(), it.getVL ());
    }
    else
        return false;

    if (txn->getStatus () == NEW)
        txn->setStatus (ledger.isClosed() ? COMMITTED : INCLUDED, ledger.seq());

    cache.canonicalize (&txn);
    return true;
}

bool getTransactionMeta (Ledger const& ledger,
    uint256 const& txID, TransactionMetaSet::pointer& meta)
{
    SHAMapTreeNode::TNType type;
    auto const item =
        ledger.txMap().peekItem (txID, type);

    if (!item)
        return false;

    if (type != SHAMapTreeNode::tnTRANSACTION_MD)
        return false;

    SerialIter it (item->slice());
    it.getVL (); // skip transaction
    meta = std::make_shared<TransactionMetaSet> (txID, ledger.seq(), it.getVL ());

    return true;
}

uint256 const&
Ledger::getHash()
{
    if (! mValidHash)
        updateHash();
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

    if (getAccountHash () != stateMap_->getHash ())
    {
        WriteLog (lsFATAL, Ledger) << "sAL: " << getAccountHash ()
                                   << " != " << stateMap_->getHash ();
        WriteLog (lsFATAL, Ledger) << "saveAcceptedLedger: seq="
                                   << seq_ << ", current=" << current;
        assert (false);
    }

    assert (getTransHash () == txMap_->getHash ());

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
        getApp().getLedgerMaster().failedSave(seq_, mHash);
        // Clients can now trust the database for information about this
        // ledger sequence.
        getApp().pendingSaves().erase(getLedgerSeq());
        return false;
    }

    {
        auto db = getApp().getLedgerDB ().checkoutDb();
        *db << boost::str (deleteLedger % seq_);
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
                for (auto const& account : accts)
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
                    sql += getApp().accountIDCache().toBase58(account);
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
                    << "Transaction in ledger " << seq_
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
                           to_string (getHash ()) % seq_ % to_string (mParentHash) %
                           beast::lexicalCastThrow <std::string> (mTotCoins) % mCloseTime %
                           mParentCloseTime % mCloseResolution % mCloseFlags %
                           to_string (mAccountHash) % to_string (mTransHash));
    }

    // Clients can now trust the database for
    // information about this ledger sequence.
    getApp().pendingSaves().erase(getLedgerSeq());
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
loadLedgerHelper(std::string const& sqlSuffix)
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
            loadLedgerHelper (s.str ());
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
            loadLedgerHelper (s.str ());
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

void Ledger::setAcquiring (void)
{
    if (!txMap_ || !stateMap_)
        throw std::runtime_error ("invalid map");

    txMap_->setSynching ();
    stateMap_->setSynching ();
}

bool Ledger::isAcquiring (void) const
{
    return isAcquiringTx () || isAcquiringAS ();
}

bool Ledger::isAcquiringTx (void) const
{
    return txMap_->isSynching ();
}

bool Ledger::isAcquiringAS (void) const
{
    return stateMap_->isSynching ();
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

//------------------------------------------------------------------------------

bool
Ledger::exists (Keylet const& k) const
{
    // VFALCO NOTE Perhaps check the type for debug builds?
    return stateMap_->hasItem(k.key);
}

boost::optional<uint256>
Ledger::succ (uint256 const& key,
    boost::optional<uint256> last) const
{
    auto const item =
        stateMap_->peekNextItem(key);
    if (! item)
        return boost::none;
    if (last && item->key() >= last)
        return boost::none;
    return item->key();
}

std::shared_ptr<SLE const>
Ledger::read (Keylet const& k) const
{
    auto const& value =
        stateMap_->peekItem(k.key);
    if (! value)
        return nullptr;
    auto sle = std::make_shared<SLE>(
        value->peekSerializer(), value->key());
    if (! k.check(*sle))
        return nullptr;
    // VFALCO TODO Eliminate "immutable" runtime property
    sle->setImmutable();
    // need move otherwise makes a copy
    // because return type is different
    return std::move(sle);
}

bool
Ledger::unchecked_erase(
    uint256 const& key)
{
    return stateMap_->delItem(key);
}

void
Ledger::unchecked_insert(
    std::shared_ptr<SLE>&& sle)
{
    assert(! stateMap_->hasItem(sle->getIndex()));
    Serializer ss;
    sle->add(ss);
    auto item = std::make_shared<
        SHAMapItem const>(sle->key(),
            std::move(ss));
    // VFALCO NOTE addGiveItem should take ownership
    auto const success =
        stateMap_->addGiveItem(
            std::move(item), false, false);
    (void)success;
    assert(success);
    auto const ours = std::move(sle);
}

void
Ledger::unchecked_replace(
    std::shared_ptr<SLE>&& sle)
{
    assert(stateMap_->hasItem(sle->getIndex()));
    Serializer ss;
    sle->add(ss);
    auto item = std::make_shared<
        SHAMapItem const>(sle->key(),
            std::move(ss));
    // VFALCO NOTE updateGiveItem should take ownership
    auto const success =
        stateMap_->updateGiveItem(
            std::move(item), false, false);
    (void)success;
    assert(success);
    auto const ours = std::move(sle);
}

bool
Ledger::txExists (uint256 const& key) const
{
    return txMap().hasItem (key);
}

bool
Ledger::txInsert (uint256 const& key,
    std::shared_ptr<Serializer const> const& txn,
        std::shared_ptr<Serializer const> const& metaData)
{
    if (metaData)
    {
        // low-level - just add to table
        Serializer s (txn->getDataLength () + metaData->getDataLength () + 16);
        s.addVL (txn->peekData ());
        s.addVL (metaData->peekData ());
        auto item = std::make_shared<
            SHAMapItem const> (key, std::move(s));

        // VFALCO Should just terminate the app
        //        with a fatal error here.

        if (! txMap().addGiveItem (std::move(item), true, true))
        {
            WriteLog (lsFATAL, Ledger)
                    << "Attempt to add transaction+MD to ledger that already had it";
            return false;
        }

        auto const temp = std::move(*metaData);
    }
    else
    {
        // low-level - just add to table
        auto item = std::make_shared<
            SHAMapItem const> (key, txn->peekData ());

        if (! txMap().addGiveItem (std::move(item), true, false))
        {
            WriteLog (lsWARNING, Ledger)
                    << "Attempt to add transaction to ledger that already had it";
            return false;
        }
    }

    // VFALCO TODO We could touch only the txMap
    touch();
    return true;
}

std::vector<uint256>
Ledger::txList() const
{
    std::vector<uint256> list;
    for (auto const& item : *txMap_)
    {
        list.push_back(item->key());
    }
    return list;
}

std::shared_ptr<SLE>
Ledger::peek (Keylet const& k) const
{
    auto const& value =
        stateMap_->peekItem(k.key);
    if (! value)
        return nullptr;
    auto sle = std::make_shared<SLE>(
        value->peekSerializer(), value->key());
    if (! k.check(*sle))
        return nullptr;
    // VFALCO TODO Eliminate "immutable" runtime property
    sle->setImmutable();
    // need move otherwise makes a copy
    // because return type is different
    return std::move(sle);
}

//------------------------------------------------------------------------------

static void visitHelper (
    std::function<void (std::shared_ptr<SLE> const&)>& function,
        std::shared_ptr<SHAMapItem const> const& item)
{
    function (std::make_shared<SLE> (item->peekSerializer(), item->key()));
}

void Ledger::visitStateItems (std::function<void (SLE::ref)> function) const
{
    try
    {
        if (stateMap_)
        {
            stateMap_->visitLeaves(
                std::bind(&visitHelper, std::ref(function),
                          std::placeholders::_1));
        }
    }
    catch (SHAMapMissingNode&)
    {
        if (mHash.isNonZero ())
        {
            getApp().getInboundLedgers().acquire(
                mHash, seq_, InboundLedger::fcGENERIC);
        }
        throw;
    }
}

uint256 Ledger::getNextLedgerIndex (uint256 const& hash,
    boost::optional<uint256> const& last) const
{
    auto const node =
        stateMap_->peekNextItem(hash);
    if ((! node) || (last && node->key() >= *last))
        return {};
    return node->key();
}

bool Ledger::walkLedger () const
{
    std::vector <SHAMapMissingNode> missingNodes1;
    std::vector <SHAMapMissingNode> missingNodes2;

    if (stateMap_->getHash().isZero() &&
        ! mAccountHash.isZero() &&
        ! stateMap_->fetchRoot (mAccountHash, nullptr))
    {
        missingNodes1.emplace_back (SHAMapType::STATE, mAccountHash);
    }
    else
    {
        stateMap_->walkMap (missingNodes1, 32);
    }

    if (ShouldLog (lsINFO, Ledger) && !missingNodes1.empty ())
    {
        WriteLog (lsINFO, Ledger)
            << missingNodes1.size () << " missing account node(s)";
        WriteLog (lsINFO, Ledger)
            << "First: " << missingNodes1[0];
    }

    if (txMap_->getHash().isZero() &&
        mTransHash.isNonZero() &&
        ! txMap_->fetchRoot (mTransHash, nullptr))
    {
        missingNodes2.emplace_back (SHAMapType::TRANSACTION, mTransHash);
    }
    else
    {
        txMap_->walkMap (missingNodes2, 32);
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

bool Ledger::assertSane ()
{
    if (mHash.isNonZero () &&
            mAccountHash.isNonZero () &&
            stateMap_ &&
            txMap_ &&
            (mAccountHash == stateMap_->getHash ()) &&
            (mTransHash == txMap_->getHash ()))
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
// VFALCO TODO Document this skip list concept
void Ledger::updateSkipList ()
{
    if (seq_ == 0) // genesis ledger has no previous ledger
        return;

    std::uint32_t prevIndex = seq_ - 1;

    // update record of every 256th ledger
    if ((prevIndex & 0xff) == 0)
    {
        auto const k = keylet::skip(prevIndex);
        auto sle = peek(k);
        std::vector<uint256> hashes;

        bool created;
        if (! sle)
        {
            sle = std::make_shared<SLE>(k);
            created = true;
        }
        else
        {
            hashes = static_cast<decltype(hashes)>(
                sle->getFieldV256(sfHashes));
            created = false;
        }

        assert (hashes.size () <= 256);
        hashes.push_back (mParentHash);
        sle->setFieldV256 (sfHashes, STVector256 (hashes));
        sle->setFieldU32 (sfLastLedgerSequence, prevIndex);
        if (created)
            unchecked_insert(std::move(sle));
        else
            unchecked_replace(std::move(sle));
    }

    // update record of past 256 ledger
    auto const k = keylet::skip();
    auto sle = peek(k);
    std::vector <uint256> hashes;
    bool created;
    if (! sle)
    {
        sle = std::make_shared<SLE>(k);
        created = true;
    }
    else
    {
        hashes = static_cast<decltype(hashes)>(
            sle->getFieldV256 (sfHashes));
        created = false;
    }
    assert (hashes.size () <= 256);
    if (hashes.size () == 256)
        hashes.erase (hashes.begin ());
    hashes.push_back (mParentHash);
    sle->setFieldV256 (sfHashes, STVector256 (hashes));
    sle->setFieldU32 (sfLastLedgerSequence, prevIndex);
    if (created)
        unchecked_insert(std::move(sle));
    else
        unchecked_replace(std::move(sle));
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

    if (!getApp().pendingSaves().insert(getLedgerSeq()))
    {
        WriteLog (lsDEBUG, Ledger)
            << "Pend save with seq in pending saves " << getLedgerSeq();
        return true;
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

void
ownerDirDescriber (SLE::ref sle, bool, AccountID const& owner)
{
    sle->setAccountID (sfOwner, owner);
}

void
qualityDirDescriber (
    SLE::ref sle, bool isNew,
    Currency const& uTakerPaysCurrency, AccountID const& uTakerPaysIssuer,
    Currency const& uTakerGetsCurrency, AccountID const& uTakerGetsIssuer,
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

void Ledger::deprecatedUpdateCachedFees() const
{
    if (mBaseFee)
        return;
    std::uint64_t baseFee = getConfig ().FEE_DEFAULT;
    std::uint32_t referenceFeeUnits = getConfig ().TRANSACTION_FEE_BASE;
    std::uint32_t reserveBase = getConfig ().FEE_ACCOUNT_RESERVE;
    std::int64_t reserveIncrement = getConfig ().FEE_OWNER_RESERVE;

    // VFALCO NOTE this doesn't go through the SLECache
    auto const sle = this->read(keylet::fees());
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
        // VFALCO Why not do this before calling getASNode?
        std::lock_guard<
            std::mutex> lock(mutex_);
        if (mBaseFee == 0)
        {
            mBaseFee = baseFee;
            mReferenceFeeUnits = referenceFeeUnits;
            mReserveBase = reserveBase;
            mReserveIncrement = reserveIncrement;
        }
    }
}

std::vector<uint256> Ledger::getNeededTransactionHashes (
    int max, SHAMapSyncFilter* filter) const
{
    std::vector<uint256> ret;

    if (mTransHash.isNonZero ())
    {
        if (txMap_->getHash ().isZero ())
            ret.push_back (mTransHash);
        else
            ret = txMap_->getNeededHashes (max, filter);
    }

    return ret;
}

std::vector<uint256> Ledger::getNeededAccountStateHashes (
    int max, SHAMapSyncFilter* filter) const
{
    std::vector<uint256> ret;

    if (mAccountHash.isNonZero ())
    {
        if (stateMap_->getHash ().isZero ())
            ret.push_back (mAccountHash);
        else
            ret = stateMap_->getNeededHashes (max, filter);
    }

    return ret;
}

//------------------------------------------------------------------------------
//
// API
//
//------------------------------------------------------------------------------

std::shared_ptr<SLE const>
cachedRead (Ledger const& ledger, uint256 const& key,
    SLECache& cache, boost::optional<LedgerEntryType> type)
{
    uint256 hash;
    auto const& item =
        ledger.stateMap().peekItem(key, hash);
    if (! item)
        return {};
    if (auto const sle = cache.fetch(hash))
    {
        if (type && sle->getType() != type)
            return {};
        return sle;
    }
    SerialIter sit(make_Slice(item->peekData()));
    auto sle = std::make_shared<SLE>(sit, item->key());
    // VFALCO Should we still cache it if the type doesn't match?
    if (type && sle->getType() != type)
        return {};
    sle->setImmutable ();
    cache.canonicalize(hash, sle);
    return sle;
}

boost::optional<uint256>
hashOfSeq (Ledger& ledger, LedgerIndex seq,
    SLECache& cache, beast::Journal journal)
{
    // Easy cases...
    if (seq > ledger.seq())
    {
        if (journal.warning) journal.warning <<
            "Can't get seq " << seq <<
            " from " << ledger.seq() << " future";
        return boost::none;
    }
    if (seq == ledger.seq())
        return ledger.getHash();
    if (seq == (ledger.seq() - 1))
        return ledger.getParentHash();

    // Within 256...
    {
        int diff = ledger.seq() - seq;
        if (diff <= 256)
        {
            auto const hashIndex = cachedRead(
                ledger, getLedgerHashIndex(), cache);
            if (hashIndex)
            {
                assert (hashIndex->getFieldU32 (sfLastLedgerSequence) ==
                        (ledger.seq() - 1));
                STVector256 vec = hashIndex->getFieldV256 (sfHashes);
                if (vec.size () >= diff)
                    return vec[vec.size () - diff];
                if (journal.warning) journal.warning <<
                    "Ledger " << ledger.seq() <<
                    " missing hash for " << seq <<
                    " (" << vec.size () << "," << diff << ")";
            }
            else
            {
                if (journal.warning) journal.warning <<
                    "Ledger " << ledger.seq() <<
                    ":" << ledger.getHash () << " missing normal list";
            }
        }
        if ((seq & 0xff) != 0)
        {
            if (journal.debug) journal.debug <<
                "Can't get seq " << seq <<
                " from " << ledger.seq() << " past";
            return boost::none;
        }
    }

    // in skiplist
    auto const hashIndex = cachedRead(ledger,
        getLedgerHashIndex(seq), cache);
    if (hashIndex)
    {
        auto const lastSeq =
            hashIndex->getFieldU32 (sfLastLedgerSequence);
        assert (lastSeq >= seq);
        assert ((lastSeq & 0xff) == 0);
        auto const diff = (lastSeq - seq) >> 8;
        STVector256 vec = hashIndex->getFieldV256 (sfHashes);
        if (vec.size () > diff)
            return vec[vec.size () - diff - 1];
    }
    if (journal.warning) journal.warning <<
        "Can't get seq " << seq <<
        " from " << ledger.seq() << " error";
    return boost::none;
}

void
injectSLE (Json::Value& jv,
    SLE const& sle)
{
    jv = sle.getJson(0);
    if (sle.getType() == ltACCOUNT_ROOT)
    {
        if (sle.isFieldPresent(sfEmailHash))
        {
            auto const& hash =
                sle.getFieldH128(sfEmailHash);
            Blob const b (hash.begin(), hash.end());
            std::string md5 = strHex(b);
            boost::to_lower(md5);
            // VFALCO TODO Give a name and move this constant
            //             to a more visible location. Also
            //             shouldn't this be https?
            jv[jss::urlgravatar] = str(boost::format(
                "http://www.gravatar.com/avatar/%s") % md5);
        }
    }
    else
    {
        jv[jss::Invalid] = true;
    }
}

//------------------------------------------------------------------------------

bool
getMetaHex (Ledger const& ledger,
    uint256 const& transID, std::string& hex)
{
    SHAMapTreeNode::TNType type;
    auto const item =
        ledger.txMap().peekItem (transID, type);

    if (!item)
        return false;

    if (type != SHAMapTreeNode::tnTRANSACTION_MD)
        return false;

    SerialIter it (item->slice());
    it.getVL (); // skip transaction
    hex = strHex (it.getVL ());
    return true;
}

} // ripple
