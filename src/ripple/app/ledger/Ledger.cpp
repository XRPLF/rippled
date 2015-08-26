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
#include <ripple/app/misc/HashRouter.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/tx/TransactionMaster.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/core/Config.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/core/JobQueue.h>
#include <ripple/core/SociDB.h>
#include <ripple/core/TimeKeeper.h>
#include <ripple/json/to_string.h>
#include <ripple/nodestore/Database.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/types.h>
#include <beast/module/core/text/LexicalCast.h>
#include <beast/unit_test/suite.h>
#include <boost/optional.hpp>
#include <cassert>
#include <utility>

namespace ripple {

create_genesis_t const create_genesis {};

//------------------------------------------------------------------------------

class Ledger::sles_iter_impl
    : public sles_type::iter_base
{
private:
    ReadView const* view_;
    SHAMap::const_iterator iter_;

public:
    sles_iter_impl() = delete;
    sles_iter_impl& operator= (sles_iter_impl const&) = delete;

    sles_iter_impl (sles_iter_impl const&) = default;

    sles_iter_impl (SHAMap::const_iterator iter,
            ReadView const& view)
        : view_ (&view)
        , iter_ (iter)
    {
    }

    std::unique_ptr<base_type>
    copy() const override
    {
        return std::make_unique<
            sles_iter_impl>(*this);
    }

    bool
    equal (base_type const& impl) const override
    {
        auto const& other = dynamic_cast<
            sles_iter_impl const&>(impl);
        return iter_ == other.iter_;
    }

    void
    increment() override
    {
        ++iter_;
    }

    sles_type::value_type
    dereference() const override
    {
        auto const item = *iter_;
        SerialIter sit(item.slice());
        return std::make_shared<SLE const>(
            sit, item.key());
    }
};

//------------------------------------------------------------------------------

class Ledger::txs_iter_impl
    : public txs_type::iter_base
{
private:
    bool metadata_;
    ReadView const* view_;
    SHAMap::const_iterator iter_;

public:
    txs_iter_impl() = delete;
    txs_iter_impl& operator= (txs_iter_impl const&) = delete;

    txs_iter_impl (txs_iter_impl const&) = default;

    txs_iter_impl (bool metadata,
        SHAMap::const_iterator iter,
            ReadView const& view)
        : metadata_ (metadata)
        , view_ (&view)
        , iter_ (iter)
    {
    }

    std::unique_ptr<base_type>
    copy() const override
    {
        return std::make_unique<
            txs_iter_impl>(*this);
    }

    bool
    equal (base_type const& impl) const override
    {
        auto const& other = dynamic_cast<
            txs_iter_impl const&>(impl);
        return iter_ == other.iter_;
    }

    void
    increment() override
    {
        ++iter_;
    }

    txs_type::value_type
    dereference() const override
    {
        auto const item = *iter_;
        if (metadata_)
            return deserializeTxPlusMeta(item);
        return { deserializeTx(item), nullptr };
    }
};

//------------------------------------------------------------------------------

Ledger::Ledger (create_genesis_t, Config const& config)
    : mImmutable (false)
    , txMap_  (std::make_shared <SHAMap> (SHAMapType::TRANSACTION,
        getApp().family(), deprecatedLogs().journal("SHAMap")))
    , stateMap_ (std::make_shared <SHAMap> (SHAMapType::STATE,
        getApp().family(), deprecatedLogs().journal("SHAMap")))
{
    info_.seq = 1;
    info_.drops = SYSTEM_CURRENCY_START;
    info_.closeTimeResolution = ledgerDefaultTimeResolution;
    auto const id = calcAccountID(
        generateKeyPair(KeyType::secp256k1,
            generateSeed("masterpassphrase")).first);
    auto const sle = std::make_shared<SLE>(keylet::account(id));
    sle->setFieldU32 (sfSequence, 1);
    sle->setAccountID (sfAccount, id);
    sle->setFieldAmount (sfBalance, info_.drops);
    rawInsert(sle);
    stateMap_->flushDirty (hotACCOUNT_NODE, info_.seq);
    updateHash();
    setClosed();
    setImmutable();
    setup(config);
}

Ledger::Ledger (uint256 const& parentHash,
                uint256 const& transHash,
                uint256 const& accountHash,
                std::uint64_t totDrops,
                std::uint32_t closeTime,
                std::uint32_t parentCloseTime,
                int closeFlags,
                int closeResolution,
                std::uint32_t ledgerSeq,
                bool& loaded,
                Config const& config)
    : mImmutable (true)
    , txMap_ (std::make_shared <SHAMap> (
        SHAMapType::TRANSACTION, transHash, getApp().family(),
                deprecatedLogs().journal("SHAMap")))
    , stateMap_ (std::make_shared <SHAMap> (SHAMapType::STATE, accountHash,
        getApp().family(), deprecatedLogs().journal("SHAMap")))
{
    info_.seq = ledgerSeq;
    info_.parentCloseTime = parentCloseTime;
    info_.closeTime = closeTime;
    info_.drops = totDrops;
    info_.txHash = transHash;
    info_.accountHash = accountHash;
    info_.parentHash = parentHash;
    info_.closeTimeResolution = closeResolution;
    info_.closeFlags = closeFlags;
    loaded = true;

    if (info_.txHash.isNonZero () &&
        !txMap_->fetchRoot (info_.txHash, nullptr))
    {
        loaded = false;
        WriteLog (lsWARNING, Ledger) << "Don't have TX root for ledger";
    }

    if (info_.accountHash.isNonZero () &&
        !stateMap_->fetchRoot (info_.accountHash, nullptr))
    {
        loaded = false;
        WriteLog (lsWARNING, Ledger) << "Don't have AS root for ledger";
    }

    txMap_->setImmutable ();
    stateMap_->setImmutable ();
    setup(config);
}

// Create a new ledger that's a snapshot of this one
Ledger::Ledger (Ledger const& ledger,
                bool isMutable)
    : mImmutable (!isMutable)
    , txMap_ (ledger.txMap_->snapShot (isMutable))
    , stateMap_ (ledger.stateMap_->snapShot (isMutable))
    , fees_ (ledger.fees_)
    , info_ (ledger.info_)
{
    updateHash ();
}

// Create a new open ledger that follows this one
Ledger::Ledger (open_ledger_t, Ledger const& prevLedger)
    : mImmutable (false)
    , txMap_ (std::make_shared <SHAMap> (SHAMapType::TRANSACTION,
        getApp().family(), deprecatedLogs().journal("SHAMap")))
    , stateMap_ (prevLedger.stateMap_->snapShot (true))
    , fees_(prevLedger.fees_)
{
    info_.open = true;
    info_.seq = prevLedger.info_.seq + 1;
    info_.parentCloseTime =
        prevLedger.info_.closeTime;
    info_.hash = prevLedger.info().hash + uint256(1);
    info_.drops = prevLedger.info().drops;
    info_.closeTimeResolution = prevLedger.info_.closeTimeResolution;
    info_.parentHash = prevLedger.getHash ();
    info_.closeTimeResolution = getNextLedgerTimeResolution (
        prevLedger.info_.closeTimeResolution,
        getCloseAgree(prevLedger.info()), info_.seq);
    // VFALCO Remove this call to getApp
    if (prevLedger.info_.closeTime == 0)
    {
        info_.closeTime = roundCloseTime (
            getApp().timeKeeper().closeTime().time_since_epoch().count(),
            info_.closeTimeResolution);
    }
    else
    {
        info_.closeTime =
            prevLedger.info_.closeTime + info_.closeTimeResolution;
    }
}

Ledger::Ledger (void const* data,
    std::size_t size, bool hasPrefix,
        Config const& config)
    : mImmutable (true)
    , txMap_ (std::make_shared <SHAMap> (
          SHAMapType::TRANSACTION, getApp().family(),
            deprecatedLogs().journal("SHAMap")))
    , stateMap_ (std::make_shared <SHAMap> (
          SHAMapType::STATE, getApp().family(),
            deprecatedLogs().journal("SHAMap")))
{
    SerialIter sit (data, size);
    setRaw (sit, hasPrefix);
    // Can't set up until the stateMap is filled in
}

Ledger::Ledger (std::uint32_t ledgerSeq,
        std::uint32_t closeTime, Config const& config)
    : mImmutable (false)
    , txMap_ (std::make_shared <SHAMap> (
          SHAMapType::TRANSACTION, getApp().family(),
            deprecatedLogs().journal("SHAMap")))
    , stateMap_ (std::make_shared <SHAMap> (
          SHAMapType::STATE, getApp().family(),
            deprecatedLogs().journal("SHAMap")))
{
    info_.seq = ledgerSeq;
    info_.closeTime = closeTime;
    info_.closeTimeResolution = ledgerDefaultTimeResolution;
    setup(config);
}

//------------------------------------------------------------------------------

Ledger::~Ledger ()
{
}

void Ledger::setImmutable ()
{
    // Force update, since this is the only
    // place the hash transitions to valid
    updateHash ();

    mImmutable = true;
    if (txMap_)
        txMap_->setImmutable ();
    if (stateMap_)
        stateMap_->setImmutable ();
    setup(getConfig ());
}

void Ledger::updateHash()
{
    if (! mImmutable)
    {
        if (txMap_)
            info_.txHash = txMap_->getHash ();
        else
            info_.txHash.zero ();

        if (stateMap_)
            info_.accountHash = stateMap_->getHash ();
        else
            info_.accountHash.zero ();
    }

    // VFALCO This has to match addRaw in View.h.
    info_.hash = sha512Half(
        HashPrefix::ledgerMaster,
        std::uint32_t(info_.seq),
        std::uint64_t(info_.drops.drops ()),
        info_.parentHash,
        info_.txHash,
        info_.accountHash,
        std::uint32_t(info_.parentCloseTime),
        std::uint32_t(info_.closeTime),
        std::uint8_t(info_.closeTimeResolution),
        std::uint8_t(info_.closeFlags));
    mValidHash = true;
}

void Ledger::setRaw (SerialIter& sit, bool hasPrefix)
{
    if (hasPrefix)
        sit.get32 ();

    info_.seq = sit.get32 ();
    info_.drops = sit.get64 ();
    info_.parentHash = sit.get256 ();
    info_.txHash = sit.get256 ();
    info_.accountHash = sit.get256 ();
    info_.parentCloseTime = sit.get32 ();
    info_.closeTime = sit.get32 ();
    info_.closeTimeResolution = sit.get8 ();
    info_.closeFlags = sit.get8 ();
    updateHash ();
    txMap_ = std::make_shared<SHAMap> (SHAMapType::TRANSACTION, info_.txHash,
        getApp().family(), deprecatedLogs().journal("SHAMap"));
    stateMap_ = std::make_shared<SHAMap> (SHAMapType::STATE, info_.accountHash,
        getApp().family(), deprecatedLogs().journal("SHAMap"));
}

void Ledger::addRaw (Serializer& s) const
{
    ripple::addRaw(info_, s);
}

void Ledger::setAccepted (
    std::uint32_t closeTime, int closeResolution, bool correctCloseTime)
{
    // Used when we witnessed the consensus.  Rounds the close time, updates the
    // hash, and sets the ledger accepted and immutable.
    assert (closed());

    info_.closeTime = closeTime;
    info_.closeTimeResolution = closeResolution;
    info_.closeFlags = correctCloseTime ? 0 : sLCF_NoConsensusTime;
    setImmutable ();
}

bool Ledger::addSLE (SLE const& sle)
{
    SHAMapItem item (sle.getIndex(), sle.getSerializer());
    return stateMap_->addItem(item, false, false);
}

bool Ledger::saveValidatedLedger (bool current)
{
    // TODO(tom): Fix this hard-coded SQL!
    WriteLog (lsTRACE, Ledger)
        << "saveValidatedLedger "
        << (current ? "" : "fromAcquire ") << info().seq;
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

    if (!info().accountHash.isNonZero ())
    {
        WriteLog (lsFATAL, Ledger) << "AH is zero: "
                                   << getJson (*this);
        assert (false);
    }

    if (info().accountHash != stateMap_->getHash ())
    {
        WriteLog (lsFATAL, Ledger) << "sAL: " << info().accountHash
                                   << " != " << stateMap_->getHash ();
        WriteLog (lsFATAL, Ledger) << "saveAcceptedLedger: seq="
                                   << info_.seq << ", current=" << current;
        assert (false);
    }

    assert (info().txHash == txMap_->getHash ());

    // Save the ledger header in the hashed object store
    {
        Serializer s (128);
        s.add32 (HashPrefix::ledgerMaster);
        addRaw (s);
        getApp().getNodeStore ().store (
            hotLEDGER, std::move (s.modData ()), info_.hash);
    }

    AcceptedLedger::pointer aLedger;
    try
    {
        aLedger = AcceptedLedger::makeAcceptedLedger (shared_from_this ());
    }
    catch (...)
    {
        WriteLog (lsWARNING, Ledger) << "An accepted ledger was missing nodes";
        getApp().getLedgerMaster().failedSave(info_.seq, info_.hash);
        // Clients can now trust the database for information about this
        // ledger sequence.
        getApp().pendingSaves().erase(info().seq);
        return false;
    }

    {
        auto db = getApp().getLedgerDB ().checkoutDb();
        *db << boost::str (deleteLedger % info_.seq);
    }

    {
        auto db = getApp().getTxnDB ().checkoutDb ();

        soci::transaction tr(*db);

        *db << boost::str (deleteTrans1 % info().seq);
        *db << boost::str (deleteTrans2 % info().seq);

        std::string const ledgerSeq (std::to_string (info().seq));

        for (auto const& vt : aLedger->getMap ())
        {
            uint256 transactionID = vt.second->getTransactionID ();

            getApp().getMasterTransaction ().inLedger (
                transactionID, info().seq);

            std::string const txnId (to_string (transactionID));
            std::string const txnSeq (std::to_string (vt.second->getTxnSeq ()));

            *db << boost::str (deleteAcctTrans % transactionID);

            auto const& accts = vt.second->getAffected ();

            if (!accts.empty ())
            {
                std::string sql (
                    "INSERT INTO AccountTransactions "
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
                    << "Transaction in ledger " << info_.seq
                    << " affects no accounts";

            *db <<
               (STTx::getMetaSQLInsertReplaceHeader () +
                vt.second->getTxn ()->getMetaSQL (
                    info().seq, vt.second->getEscMeta ()) + ";");
        }

        tr.commit ();
    }

    {
        auto db (getApp().getLedgerDB ().checkoutDb ());

        // TODO(tom): ARG!
        *db << boost::str (
            addLedger %
            to_string (getHash ()) % info_.seq % to_string (info_.parentHash) %
            to_string (info_.drops) % info_.closeTime %
            info_.parentCloseTime % info_.closeTimeResolution %
            info_.closeFlags % to_string (info_.accountHash) %
            to_string (info_.txHash));
    }

    // Clients can now trust the database for
    // information about this ledger sequence.
    getApp().pendingSaves().erase(info().seq);
    return true;
}

//------------------------------------------------------------------------------

std::shared_ptr<STTx const>
deserializeTx (SHAMapItem const& item)
{
    SerialIter sit(item.slice());
    return std::make_shared<STTx const>(sit);
}

std::pair<std::shared_ptr<
    STTx const>, std::shared_ptr<
        STObject const>>
deserializeTxPlusMeta (SHAMapItem const& item)
{
    std::pair<std::shared_ptr<
        STTx const>, std::shared_ptr<
            STObject const>> result;
    SerialIter sit(item.slice());
    {
        SerialIter s(sit.getSlice(
            sit.getVLDataLength()));
        result.first = std::make_shared<
            STTx const>(s);
    }
    {
        SerialIter s(sit.getSlice(
            sit.getVLDataLength()));
        result.second = std::make_shared<
            STObject const>(s, sfMetadata);
    }
    return result;
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
    uint256 ledgerHash{};
    std::uint32_t ledgerSeq{0};

    auto db = getApp ().getLedgerDB ().checkoutDb ();

    boost::optional<std::string> sLedgerHash, sPrevHash, sAccountHash,
        sTransHash;
    boost::optional<std::uint64_t> totDrops, closingTime, prevClosingTime,
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
            soci::into(totDrops),
            soci::into(closingTime),
            soci::into(prevClosingTime),
            soci::into(closeResolution),
            soci::into(closeFlags),
            soci::into(ledgerSeq64);

    if (!db->got_data ())
    {
        WriteLog (lsDEBUG, Ledger) << "Ledger not found: " << sqlSuffix;
        return std::make_tuple (Ledger::pointer (), ledgerSeq, ledgerHash);
    }

    ledgerSeq =
        rangeCheckedCast<std::uint32_t>(ledgerSeq64.value_or (0));

    uint256 prevHash{}, accountHash{}, transHash{};
    if (sLedgerHash)
        ledgerHash.SetHexExact (*sLedgerHash);
    if (sPrevHash)
        prevHash.SetHexExact (*sPrevHash);
    if (sAccountHash)
        accountHash.SetHexExact (*sAccountHash);
    if (sTransHash)
        transHash.SetHexExact (*sTransHash);

    bool loaded = false;
    ledger = std::make_shared<Ledger>(prevHash,
                                      transHash,
                                      accountHash,
                                      totDrops.value_or(0),
                                      closingTime.value_or(0),
                                      prevClosingTime.value_or(0),
                                      closeFlags.value_or(0),
                                      closeResolution.value_or(0),
                                      ledgerSeq,
                                      loaded,
                                      getConfig());

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
        if (ph)
            hashes.second.SetHexExact (*ph);
        else
            hashes.second.zero ();
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
    return ptFromSeconds (info_.closeTime);
}

void Ledger::setCloseTime (boost::posix_time::ptime ptm)
{
    assert (!mImmutable);
    info_.closeTime = iToSeconds (ptm);
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
    boost::optional<uint256> const& last) const
{
    auto item = stateMap_->upper_bound(key);
    if (item == stateMap_->end())
        return boost::none;
    if (last && item->key() >= last)
        return boost::none;
    return item->key();
}

std::shared_ptr<SLE const>
Ledger::read (Keylet const& k) const
{
    if (k.key == zero)
    {
        assert(false);
        return nullptr;
    }
    auto const& item =
        stateMap_->peekItem(k.key);
    if (! item)
        return nullptr;
    auto sle = std::make_shared<SLE>(
        SerialIter{item->data(),
            item->size()}, item->key());
    if (! k.check(*sle))
        return nullptr;
    // VFALCO TODO Eliminate "immutable" runtime property
    sle->setImmutable();
    // need move otherwise makes a copy
    // because return type is different
    return std::move(sle);
}

//------------------------------------------------------------------------------

auto
Ledger::slesBegin() const ->
    std::unique_ptr<sles_type::iter_base>
{
    return std::make_unique<
        sles_iter_impl>(
            stateMap_->begin(), *this);
}

auto
Ledger::slesEnd() const ->
    std::unique_ptr<sles_type::iter_base>
{
    return std::make_unique<
        sles_iter_impl>(
            stateMap_->end(), *this);
}

auto
Ledger::slesUpperBound(uint256 const& key) const ->
    std::unique_ptr<sles_type::iter_base>
{
    return std::make_unique<
        sles_iter_impl>(
            stateMap_->upper_bound(key), *this);
}

auto
Ledger::txsBegin() const ->
    std::unique_ptr<txs_type::iter_base>
{
    return std::make_unique<
        txs_iter_impl>(closed(),
            txMap_->begin(), *this);
}

auto
Ledger::txsEnd() const ->
    std::unique_ptr<txs_type::iter_base>
{
    return std::make_unique<
        txs_iter_impl>(closed(),
            txMap_->end(), *this);
}

bool
Ledger::txExists (uint256 const& key) const
{
    return txMap_->hasItem (key);
}

auto
Ledger::txRead(
    key_type const& key) const ->
        tx_type
{
    auto const& item =
        txMap_->peekItem(key);
    if (! item)
        return {};
    if (closed())
    {
        auto result =
            deserializeTxPlusMeta(*item);
        return { std::move(result.first),
            std::move(result.second) };
    }
    return { deserializeTx(*item), nullptr };
}

auto
Ledger::digest (key_type const& key) const ->
    boost::optional<digest_type>
{
    digest_type digest;
    // VFALCO Unfortunately this loads the item
    //        from the NodeStore needlessly.
    if (! stateMap_->peekItem(key, digest))
        return boost::none;
    return digest;
}

//------------------------------------------------------------------------------

void
Ledger::rawErase(std::shared_ptr<SLE> const& sle)
{
    if (! stateMap_->delItem(sle->key()))
        LogicError("Ledger::rawErase: key not found");
}

void
Ledger::rawInsert(std::shared_ptr<SLE> const& sle)
{
    Serializer ss;
    sle->add(ss);
    auto item = std::make_shared<
        SHAMapItem const>(sle->key(),
            std::move(ss));
    // VFALCO NOTE addGiveItem should take ownership
    if (! stateMap_->addGiveItem(
            std::move(item), false, false))
        LogicError("Ledger::rawInsert: key already exists");
}

void
Ledger::rawReplace(std::shared_ptr<SLE> const& sle)
{
    Serializer ss;
    sle->add(ss);
    auto item = std::make_shared<
        SHAMapItem const>(sle->key(),
            std::move(ss));
    // VFALCO NOTE updateGiveItem should take ownership
    if (! stateMap_->updateGiveItem(
            std::move(item), false, false))
        LogicError("Ledger::rawReplace: key not found");
}

void
Ledger::rawTxInsert (uint256 const& key,
    std::shared_ptr<Serializer const
        > const& txn, std::shared_ptr<
            Serializer const> const& metaData)
{
    assert (static_cast<bool>(metaData) != info_.open);

    if (metaData)
    {
        // low-level - just add to table
        Serializer s(txn->getDataLength () +
            metaData->getDataLength () + 16);
        s.addVL (txn->peekData ());
        s.addVL (metaData->peekData ());
        auto item = std::make_shared<
            SHAMapItem const> (key, std::move(s));
        if (! txMap().addGiveItem
                (std::move(item), true, true))
            LogicError("duplicate_tx: " + to_string(key));
    }
    else
    {
        // low-level - just add to table
        auto item = std::make_shared<
            SHAMapItem const>(key, txn->peekData());
        if (! txMap().addGiveItem(
                std::move(item), true, false))
            LogicError("duplicate_tx: " + to_string(key));
    }
}

void
Ledger::setup (Config const& config)
{
    fees_.base = config.FEE_DEFAULT;
    fees_.units = config.TRANSACTION_FEE_BASE;
    fees_.reserve = config.FEE_ACCOUNT_RESERVE;
    fees_.increment = config.FEE_OWNER_RESERVE;
    auto const sle = read(keylet::fees());
    if (sle)
    {
        // VFALCO NOTE Why getFieldIndex and not isFieldPresent?

        if (sle->getFieldIndex (sfBaseFee) != -1)
            fees_.base = sle->getFieldU64 (sfBaseFee);

        if (sle->getFieldIndex (sfReferenceFeeUnits) != -1)
            fees_.units = sle->getFieldU32 (sfReferenceFeeUnits);

        if (sle->getFieldIndex (sfReserveBase) != -1)
            fees_.reserve = sle->getFieldU32 (sfReserveBase);

        if (sle->getFieldIndex (sfReserveIncrement) != -1)
            fees_.increment = sle->getFieldU32 (sfReserveIncrement);
    }
    rules_ = Rules(*this);
}

std::shared_ptr<SLE>
Ledger::peek (Keylet const& k) const
{
    auto const& value =
        stateMap_->peekItem(k.key);
    if (! value)
        return nullptr;
    auto sle = std::make_shared<SLE>(
        SerialIter{value->data(), value->size()}, value->key());
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
    std::function<void (std::shared_ptr<SLE> const&)>& callback,
        std::shared_ptr<SHAMapItem const> const& item)
{
    callback(std::make_shared<SLE>(SerialIter{item->data(), item->size()},
                                    item->key()));
}

void Ledger::visitStateItems (std::function<void (SLE::ref)> callback) const
{
    try
    {
        if (stateMap_)
        {
            stateMap_->visitLeaves(
                std::bind(&visitHelper, std::ref(callback),
                          std::placeholders::_1));
        }
    }
    catch (SHAMapMissingNode&)
    {
        if (info_.hash.isNonZero ())
        {
            getApp().getInboundLedgers().acquire(
                info_.hash, info_.seq, InboundLedger::fcGENERIC);
        }
        throw;
    }
}

bool Ledger::walkLedger () const
{
    std::vector <SHAMapMissingNode> missingNodes1;
    std::vector <SHAMapMissingNode> missingNodes2;

    if (stateMap_->getHash().isZero() &&
        ! info_.accountHash.isZero() &&
        ! stateMap_->fetchRoot (info_.accountHash, nullptr))
    {
        missingNodes1.emplace_back (SHAMapType::STATE, info_.accountHash);
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
        info_.txHash.isNonZero() &&
        ! txMap_->fetchRoot (info_.txHash, nullptr))
    {
        missingNodes2.emplace_back (SHAMapType::TRANSACTION, info_.txHash);
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
    if (info_.hash.isNonZero () &&
            info_.accountHash.isNonZero () &&
            stateMap_ &&
            txMap_ &&
            (info_.accountHash == stateMap_->getHash ()) &&
            (info_.txHash == txMap_->getHash ()))
    {
        return true;
    }

    Json::Value j = getJson (*this);

    j [jss::accountTreeHash] = to_string (info_.accountHash);
    j [jss::transTreeHash] = to_string (info_.txHash);

    WriteLog (lsFATAL, Ledger) << "ledger is not sane" << j;

    assert (false);

    return false;
}

// update the skip list with the information from our previous ledger
// VFALCO TODO Document this skip list concept
void Ledger::updateSkipList ()
{
    if (info_.seq == 0) // genesis ledger has no previous ledger
        return;

    std::uint32_t prevIndex = info_.seq - 1;

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
        hashes.push_back (info_.parentHash);
        sle->setFieldV256 (sfHashes, STVector256 (hashes));
        sle->setFieldU32 (sfLastLedgerSequence, prevIndex);
        if (created)
            rawInsert(sle);
        else
            rawReplace(sle);
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
    hashes.push_back (info_.parentHash);
    sle->setFieldV256 (sfHashes, STVector256 (hashes));
    sle->setFieldU32 (sfLastLedgerSequence, prevIndex);
    if (created)
        rawInsert(sle);
    else
        rawReplace(sle);
}

/** Save, or arrange to save, a fully-validated ledger
    Returns false on error
*/
bool Ledger::pendSaveValidated (bool isSynchronous, bool isCurrent)
{
    if (!getApp().getHashRouter ().setFlags (getHash (), SF_SAVED))
    {
        WriteLog (lsDEBUG, Ledger) << "Double pend save for " << info().seq;
        return true;
    }

    assert (isImmutable ());

    if (!getApp().pendingSaves().insert(info().seq))
    {
        WriteLog (lsDEBUG, Ledger)
            << "Pend save with seq in pending saves " << info().seq;
        return true;
    }

    if (isSynchronous)
        return saveValidatedLedger(isCurrent);

    auto that = shared_from_this();
    auto job = [that, isCurrent] (Job&) {
        that->saveValidatedLedger(isCurrent);
    };

    if (isCurrent)
    {
        getApp().getJobQueue().addJob(
            jtPUBLEDGER, "Ledger::pendSave", job);
    }
    else
    {
        getApp().getJobQueue ().addJob(
            jtPUBOLDLEDGER, "Ledger::pendOldSave", job);
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
        // VFALCO NO! This shouldn't be done here!
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

    // VFALCO NOTE this doesn't go through the CachedSLEs
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

std::vector<uint256>
Ledger::getNeededTransactionHashes (
    int max, SHAMapSyncFilter* filter) const
{
    std::vector<uint256> ret;

    if (info_.txHash.isNonZero ())
    {
        if (txMap_->getHash ().isZero ())
            ret.push_back (info_.txHash);
        else
            ret = txMap_->getNeededHashes (max, filter);
    }

    return ret;
}

std::vector<uint256>
Ledger::getNeededAccountStateHashes (
    int max, SHAMapSyncFilter* filter) const
{
    std::vector<uint256> ret;

    if (info_.accountHash.isNonZero ())
    {
        if (stateMap_->getHash ().isZero ())
            ret.push_back (info_.accountHash);
        else
            ret = stateMap_->getNeededHashes (max, filter);
    }

    return ret;
}

} // ripple
