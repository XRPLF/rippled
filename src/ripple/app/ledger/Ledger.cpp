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

#include <ripple/app/ledger/AcceptedLedger.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/LedgerToJson.h>
#include <ripple/app/ledger/OrderBookDB.h>
#include <ripple/app/ledger/PendingSaves.h>
#include <ripple/app/ledger/TransactionMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/HashRouter.h>
#include <ripple/app/misc/LoadFeeTrack.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/basics/contract.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/consensus/LedgerTiming.h>
#include <ripple/core/Config.h>
#include <ripple/core/JobQueue.h>
#include <ripple/core/SQLInterface.h>
#include <ripple/json/to_string.h>
#include <ripple/nodestore/Database.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/UintTypes.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/jss.h>
#include <boost/optional.hpp>
#include <cassert>
#include <utility>

namespace ripple {

create_genesis_t const create_genesis{};

static uint256
calculateLedgerHash(LedgerInfo const& info)
{
    // VFALCO This has to match addRaw in View.h.
    return sha512Half(
        HashPrefix::ledgerMaster,
        std::uint32_t(info.seq),
        std::uint64_t(info.drops.drops()),
        info.parentHash,
        info.txHash,
        info.accountHash,
        std::uint32_t(info.parentCloseTime.time_since_epoch().count()),
        std::uint32_t(info.closeTime.time_since_epoch().count()),
        std::uint8_t(info.closeTimeResolution.count()),
        std::uint8_t(info.closeFlags));
}

//------------------------------------------------------------------------------

class Ledger::sles_iter_impl : public sles_type::iter_base
{
private:
    SHAMap::const_iterator iter_;

public:
    sles_iter_impl() = delete;
    sles_iter_impl&
    operator=(sles_iter_impl const&) = delete;

    sles_iter_impl(sles_iter_impl const&) = default;

    sles_iter_impl(SHAMap::const_iterator iter) : iter_(iter)
    {
    }

    std::unique_ptr<base_type>
    copy() const override
    {
        return std::make_unique<sles_iter_impl>(*this);
    }

    bool
    equal(base_type const& impl) const override
    {
        auto const& other = dynamic_cast<sles_iter_impl const&>(impl);
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
        return std::make_shared<SLE const>(sit, item.key());
    }
};

//------------------------------------------------------------------------------

class Ledger::txs_iter_impl : public txs_type::iter_base
{
private:
    bool metadata_;
    SHAMap::const_iterator iter_;

public:
    txs_iter_impl() = delete;
    txs_iter_impl&
    operator=(txs_iter_impl const&) = delete;

    txs_iter_impl(txs_iter_impl const&) = default;

    txs_iter_impl(bool metadata, SHAMap::const_iterator iter)
        : metadata_(metadata), iter_(iter)
    {
    }

    std::unique_ptr<base_type>
    copy() const override
    {
        return std::make_unique<txs_iter_impl>(*this);
    }

    bool
    equal(base_type const& impl) const override
    {
        auto const& other = dynamic_cast<txs_iter_impl const&>(impl);
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
        return {deserializeTx(item), nullptr};
    }
};

//------------------------------------------------------------------------------

Ledger::Ledger(
    create_genesis_t,
    Config const& config,
    std::vector<uint256> const& amendments,
    Family& family)
    : mImmutable(false)
    , txMap_(std::make_shared<SHAMap>(SHAMapType::TRANSACTION, family))
    , stateMap_(std::make_shared<SHAMap>(SHAMapType::STATE, family))
    , rules_{config.features}
{
    info_.seq = 1;
    info_.drops = INITIAL_XRP;
    info_.closeTimeResolution = ledgerDefaultTimeResolution;

    static auto const id = calcAccountID(
        generateKeyPair(KeyType::secp256k1, generateSeed("masterpassphrase"))
            .first);
    {
        auto const sle = std::make_shared<SLE>(keylet::account(id));
        sle->setFieldU32(sfSequence, 1);
        sle->setAccountID(sfAccount, id);
        sle->setFieldAmount(sfBalance, info_.drops);
        rawInsert(sle);
    }

    if (!amendments.empty())
    {
        auto const sle = std::make_shared<SLE>(keylet::amendments());
        sle->setFieldV256(sfAmendments, STVector256{amendments});
        rawInsert(sle);
    }

    stateMap_->flushDirty(hotACCOUNT_NODE, info_.seq);
    setImmutable(config);
}

Ledger::Ledger(
    LedgerInfo const& info,
    bool& loaded,
    bool acquire,
    Config const& config,
    Family& family,
    beast::Journal j)
    : mImmutable(true)
    , txMap_(std::make_shared<SHAMap>(
          SHAMapType::TRANSACTION,
          info.txHash,
          family))
    , stateMap_(
          std::make_shared<SHAMap>(SHAMapType::STATE, info.accountHash, family))
    , rules_(config.features)
    , info_(info)
{
    loaded = true;

    if (info_.txHash.isNonZero() &&
        !txMap_->fetchRoot(SHAMapHash{info_.txHash}, nullptr))
    {
        loaded = false;
        JLOG(j.warn()) << "Don't have transaction root for ledger" << info_.seq;
    }

    if (info_.accountHash.isNonZero() &&
        !stateMap_->fetchRoot(SHAMapHash{info_.accountHash}, nullptr))
    {
        loaded = false;
        JLOG(j.warn()) << "Don't have state data root for ledger" << info_.seq;
    }

    txMap_->setImmutable();
    stateMap_->setImmutable();

    if (!setup(config))
        loaded = false;

    if (!loaded)
    {
        info_.hash = calculateLedgerHash(info_);
        if (acquire)
            family.missingNode(info_.hash, info_.seq);
    }
}

// Create a new ledger that follows this one
Ledger::Ledger(Ledger const& prevLedger, NetClock::time_point closeTime)
    : mImmutable(false)
    , txMap_(std::make_shared<SHAMap>(
          SHAMapType::TRANSACTION,
          prevLedger.stateMap_->family()))
    , stateMap_(prevLedger.stateMap_->snapShot(true))
    , fees_(prevLedger.fees_)
    , rules_(prevLedger.rules_)
{
    info_.seq = prevLedger.info_.seq + 1;
    info_.parentCloseTime = prevLedger.info_.closeTime;
    info_.hash = prevLedger.info().hash + uint256(1);
    info_.drops = prevLedger.info().drops;
    info_.closeTimeResolution = prevLedger.info_.closeTimeResolution;
    info_.parentHash = prevLedger.info().hash;
    info_.closeTimeResolution = getNextLedgerTimeResolution(
        prevLedger.info_.closeTimeResolution,
        getCloseAgree(prevLedger.info()),
        info_.seq);

    if (prevLedger.info_.closeTime == NetClock::time_point{})
    {
        info_.closeTime = roundCloseTime(closeTime, info_.closeTimeResolution);
    }
    else
    {
        info_.closeTime =
            prevLedger.info_.closeTime + info_.closeTimeResolution;
    }
}

Ledger::Ledger(LedgerInfo const& info, Config const& config, Family& family)
    : mImmutable(true)
    , txMap_(std::make_shared<SHAMap>(
          SHAMapType::TRANSACTION,
          info.txHash,
          family))
    , stateMap_(
          std::make_shared<SHAMap>(SHAMapType::STATE, info.accountHash, family))
    , rules_{config.features}
    , info_(info)
{
    info_.hash = calculateLedgerHash(info_);
}

Ledger::Ledger(
    std::uint32_t ledgerSeq,
    NetClock::time_point closeTime,
    Config const& config,
    Family& family)
    : mImmutable(false)
    , txMap_(std::make_shared<SHAMap>(SHAMapType::TRANSACTION, family))
    , stateMap_(std::make_shared<SHAMap>(SHAMapType::STATE, family))
    , rules_{config.features}
{
    info_.seq = ledgerSeq;
    info_.closeTime = closeTime;
    info_.closeTimeResolution = ledgerDefaultTimeResolution;
    setup(config);
}

void
Ledger::setImmutable(Config const& config)
{
    // Force update, since this is the only
    // place the hash transitions to valid
    if (!mImmutable)
    {
        info_.txHash = txMap_->getHash().as_uint256();
        info_.accountHash = stateMap_->getHash().as_uint256();
    }

    info_.hash = calculateLedgerHash(info_);

    mImmutable = true;
    txMap_->setImmutable();
    stateMap_->setImmutable();
    setup(config);
}

void
Ledger::setAccepted(
    NetClock::time_point closeTime,
    NetClock::duration closeResolution,
    bool correctCloseTime,
    Config const& config)
{
    // Used when we witnessed the consensus.
    assert(!open());

    info_.closeTime = closeTime;
    info_.closeTimeResolution = closeResolution;
    info_.closeFlags = correctCloseTime ? 0 : sLCF_NoConsensusTime;
    setImmutable(config);
}

bool
Ledger::addSLE(SLE const& sle)
{
    SHAMapItem item(sle.key(), sle.getSerializer());
    return stateMap_->addItem(std::move(item), false, false);
}

//------------------------------------------------------------------------------

std::shared_ptr<STTx const>
deserializeTx(SHAMapItem const& item)
{
    SerialIter sit(item.slice());
    return std::make_shared<STTx const>(sit);
}

std::pair<std::shared_ptr<STTx const>, std::shared_ptr<STObject const>>
deserializeTxPlusMeta(SHAMapItem const& item)
{
    std::pair<std::shared_ptr<STTx const>, std::shared_ptr<STObject const>>
        result;
    SerialIter sit(item.slice());
    {
        SerialIter s(sit.getSlice(sit.getVLDataLength()));
        result.first = std::make_shared<STTx const>(s);
    }
    {
        SerialIter s(sit.getSlice(sit.getVLDataLength()));
        result.second = std::make_shared<STObject const>(s, sfMetadata);
    }
    return result;
}

//------------------------------------------------------------------------------

bool
Ledger::exists(Keylet const& k) const
{
    // VFALCO NOTE Perhaps check the type for debug builds?
    return stateMap_->hasItem(k.key);
}

boost::optional<uint256>
Ledger::succ(uint256 const& key, boost::optional<uint256> const& last) const
{
    auto item = stateMap_->upper_bound(key);
    if (item == stateMap_->end())
        return boost::none;
    if (last && item->key() >= last)
        return boost::none;
    return item->key();
}

std::shared_ptr<SLE const>
Ledger::read(Keylet const& k) const
{
    if (k.key == beast::zero)
    {
        assert(false);
        return nullptr;
    }
    auto const& item = stateMap_->peekItem(k.key);
    if (!item)
        return nullptr;
    auto sle = std::make_shared<SLE>(
        SerialIter{item->data(), item->size()}, item->key());
    if (!k.check(*sle))
        return nullptr;
    return sle;
}

//------------------------------------------------------------------------------

auto
Ledger::slesBegin() const -> std::unique_ptr<sles_type::iter_base>
{
    return std::make_unique<sles_iter_impl>(stateMap_->begin());
}

auto
Ledger::slesEnd() const -> std::unique_ptr<sles_type::iter_base>
{
    return std::make_unique<sles_iter_impl>(stateMap_->end());
}

auto
Ledger::slesUpperBound(uint256 const& key) const
    -> std::unique_ptr<sles_type::iter_base>
{
    return std::make_unique<sles_iter_impl>(stateMap_->upper_bound(key));
}

auto
Ledger::txsBegin() const -> std::unique_ptr<txs_type::iter_base>
{
    return std::make_unique<txs_iter_impl>(!open(), txMap_->begin());
}

auto
Ledger::txsEnd() const -> std::unique_ptr<txs_type::iter_base>
{
    return std::make_unique<txs_iter_impl>(!open(), txMap_->end());
}

bool
Ledger::txExists(uint256 const& key) const
{
    return txMap_->hasItem(key);
}

auto
Ledger::txRead(key_type const& key) const -> tx_type
{
    auto const& item = txMap_->peekItem(key);
    if (!item)
        return {};
    if (!open())
    {
        auto result = deserializeTxPlusMeta(*item);
        return {std::move(result.first), std::move(result.second)};
    }
    return {deserializeTx(*item), nullptr};
}

auto
Ledger::digest(key_type const& key) const -> boost::optional<digest_type>
{
    SHAMapHash digest;
    // VFALCO Unfortunately this loads the item
    //        from the NodeStore needlessly.
    if (!stateMap_->peekItem(key, digest))
        return boost::none;
    return digest.as_uint256();
}

//------------------------------------------------------------------------------

void
Ledger::rawErase(std::shared_ptr<SLE> const& sle)
{
    if (!stateMap_->delItem(sle->key()))
        LogicError("Ledger::rawErase: key not found");
}

void
Ledger::rawInsert(std::shared_ptr<SLE> const& sle)
{
    Serializer ss;
    sle->add(ss);
    auto item = std::make_shared<SHAMapItem const>(sle->key(), std::move(ss));
    if (!stateMap_->addGiveItem(std::move(item), false, false))
        LogicError("Ledger::rawInsert: key already exists");
}

void
Ledger::rawReplace(std::shared_ptr<SLE> const& sle)
{
    Serializer ss;
    sle->add(ss);
    auto item = std::make_shared<SHAMapItem const>(sle->key(), std::move(ss));

    if (!stateMap_->updateGiveItem(std::move(item), false, false))
        LogicError("Ledger::rawReplace: key not found");
}

void
Ledger::rawTxInsert(
    uint256 const& key,
    std::shared_ptr<Serializer const> const& txn,
    std::shared_ptr<Serializer const> const& metaData)
{
    assert(metaData);

    // low-level - just add to table
    Serializer s(txn->getDataLength() + metaData->getDataLength() + 16);
    s.addVL(txn->peekData());
    s.addVL(metaData->peekData());
    auto item = std::make_shared<SHAMapItem const>(key, std::move(s));
    if (!txMap().addGiveItem(std::move(item), true, true))
        LogicError("duplicate_tx: " + to_string(key));
}

bool
Ledger::setup(Config const& config)
{
    bool ret = true;

    fees_.base = config.FEE_DEFAULT;
    fees_.units = config.TRANSACTION_FEE_BASE;
    fees_.reserve = config.FEE_ACCOUNT_RESERVE;
    fees_.increment = config.FEE_OWNER_RESERVE;

    try
    {
        if (auto const sle = read(keylet::fees()))
        {
            // VFALCO NOTE Why getFieldIndex and not isFieldPresent?

            if (sle->getFieldIndex(sfBaseFee) != -1)
                fees_.base = sle->getFieldU64(sfBaseFee);

            if (sle->getFieldIndex(sfReferenceFeeUnits) != -1)
                fees_.units = sle->getFieldU32(sfReferenceFeeUnits);

            if (sle->getFieldIndex(sfReserveBase) != -1)
                fees_.reserve = sle->getFieldU32(sfReserveBase);

            if (sle->getFieldIndex(sfReserveIncrement) != -1)
                fees_.increment = sle->getFieldU32(sfReserveIncrement);
        }
    }
    catch (SHAMapMissingNode const&)
    {
        ret = false;
    }
    catch (std::exception const&)
    {
        Rethrow();
    }

    try
    {
        rules_ = Rules(*this, config.features);
    }
    catch (SHAMapMissingNode const&)
    {
        ret = false;
    }
    catch (std::exception const&)
    {
        Rethrow();
    }

    return ret;
}

std::shared_ptr<SLE>
Ledger::peek(Keylet const& k) const
{
    auto const& value = stateMap_->peekItem(k.key);
    if (!value)
        return nullptr;
    auto sle = std::make_shared<SLE>(
        SerialIter{value->data(), value->size()}, value->key());
    if (!k.check(*sle))
        return nullptr;
    return sle;
}

hash_set<PublicKey>
Ledger::negativeUnl() const
{
    hash_set<PublicKey> negUnl;
    if (auto sle = read(keylet::negativeUNL());
        sle && sle->isFieldPresent(sfNegativeUNL))
    {
        auto const& nUnlData = sle->getFieldArray(sfNegativeUNL);
        for (auto const& n : nUnlData)
        {
            if (n.isFieldPresent(sfPublicKey))
            {
                auto d = n.getFieldVL(sfPublicKey);
                auto s = makeSlice(d);
                if (!publicKeyType(s))
                {
                    continue;
                }
                negUnl.emplace(s);
            }
        }
    }

    return negUnl;
}

boost::optional<PublicKey>
Ledger::negativeUnlToDisable() const
{
    if (auto sle = read(keylet::negativeUNL());
        sle && sle->isFieldPresent(sfNegativeUNLToDisable))
    {
        auto d = sle->getFieldVL(sfNegativeUNLToDisable);
        auto s = makeSlice(d);
        if (publicKeyType(s))
            return PublicKey(s);
    }

    return boost::none;
}

boost::optional<PublicKey>
Ledger::negativeUnlToReEnable() const
{
    if (auto sle = read(keylet::negativeUNL());
        sle && sle->isFieldPresent(sfNegativeUNLToReEnable))
    {
        auto d = sle->getFieldVL(sfNegativeUNLToReEnable);
        auto s = makeSlice(d);
        if (publicKeyType(s))
            return PublicKey(s);
    }

    return boost::none;
}

void
Ledger::updateNegativeUNL()
{
    auto sle = peek(keylet::negativeUNL());
    if (!sle)
        return;

    bool const hasToDisable = sle->isFieldPresent(sfNegativeUNLToDisable);
    bool const hasToReEnable = sle->isFieldPresent(sfNegativeUNLToReEnable);

    if (!hasToDisable && !hasToReEnable)
        return;

    STArray newNUnl;
    if (sle->isFieldPresent(sfNegativeUNL))
    {
        auto const& oldNUnl = sle->getFieldArray(sfNegativeUNL);
        for (auto v : oldNUnl)
        {
            if (hasToReEnable && v.isFieldPresent(sfPublicKey) &&
                v.getFieldVL(sfPublicKey) ==
                    sle->getFieldVL(sfNegativeUNLToReEnable))
                continue;
            newNUnl.push_back(v);
        }
    }

    if (hasToDisable)
    {
        newNUnl.emplace_back(sfNegativeUNLEntry);
        newNUnl.back().setFieldVL(
            sfPublicKey, sle->getFieldVL(sfNegativeUNLToDisable));
        newNUnl.back().setFieldU32(sfFirstLedgerSequence, seq());
    }

    if (!newNUnl.empty())
    {
        sle->setFieldArray(sfNegativeUNL, newNUnl);
        if (hasToReEnable)
            sle->makeFieldAbsent(sfNegativeUNLToReEnable);
        if (hasToDisable)
            sle->makeFieldAbsent(sfNegativeUNLToDisable);
        rawReplace(sle);
    }
    else
    {
        rawErase(sle);
    }
}

//------------------------------------------------------------------------------
bool
Ledger::walkLedger(beast::Journal j) const
{
    std::vector<SHAMapMissingNode> missingNodes1;
    std::vector<SHAMapMissingNode> missingNodes2;

    if (stateMap_->getHash().isZero() && !info_.accountHash.isZero() &&
        !stateMap_->fetchRoot(SHAMapHash{info_.accountHash}, nullptr))
    {
        missingNodes1.emplace_back(
            SHAMapType::STATE, SHAMapHash{info_.accountHash});
    }
    else
    {
        stateMap_->walkMap(missingNodes1, 32);
    }

    if (!missingNodes1.empty())
    {
        if (auto stream = j.info())
        {
            stream << missingNodes1.size() << " missing account node(s)";
            stream << "First: " << missingNodes1[0].what();
        }
    }

    if (txMap_->getHash().isZero() && info_.txHash.isNonZero() &&
        !txMap_->fetchRoot(SHAMapHash{info_.txHash}, nullptr))
    {
        missingNodes2.emplace_back(
            SHAMapType::TRANSACTION, SHAMapHash{info_.txHash});
    }
    else
    {
        txMap_->walkMap(missingNodes2, 32);
    }

    if (!missingNodes2.empty())
    {
        if (auto stream = j.info())
        {
            stream << missingNodes2.size() << " missing transaction node(s)";
            stream << "First: " << missingNodes2[0].what();
        }
    }
    return missingNodes1.empty() && missingNodes2.empty();
}

bool
Ledger::assertSane(beast::Journal ledgerJ) const
{
    if (info_.hash.isNonZero() && info_.accountHash.isNonZero() && stateMap_ &&
        txMap_ && (info_.accountHash == stateMap_->getHash().as_uint256()) &&
        (info_.txHash == txMap_->getHash().as_uint256()))
    {
        return true;
    }

    Json::Value j = getJson(*this);

    j[jss::accountTreeHash] = to_string(info_.accountHash);
    j[jss::transTreeHash] = to_string(info_.txHash);

    JLOG(ledgerJ.fatal()) << "ledger is not sane" << j;

    assert(false);

    return false;
}

// update the skip list with the information from our previous ledger
// VFALCO TODO Document this skip list concept
void
Ledger::updateSkipList()
{
    if (info_.seq == 0)  // genesis ledger has no previous ledger
        return;

    std::uint32_t prevIndex = info_.seq - 1;

    // update record of every 256th ledger
    if ((prevIndex & 0xff) == 0)
    {
        auto const k = keylet::skip(prevIndex);
        auto sle = peek(k);
        std::vector<uint256> hashes;

        bool created;
        if (!sle)
        {
            sle = std::make_shared<SLE>(k);
            created = true;
        }
        else
        {
            hashes = static_cast<decltype(hashes)>(sle->getFieldV256(sfHashes));
            created = false;
        }

        assert(hashes.size() <= 256);
        hashes.push_back(info_.parentHash);
        sle->setFieldV256(sfHashes, STVector256(hashes));
        sle->setFieldU32(sfLastLedgerSequence, prevIndex);
        if (created)
            rawInsert(sle);
        else
            rawReplace(sle);
    }

    // update record of past 256 ledger
    auto const k = keylet::skip();
    auto sle = peek(k);
    std::vector<uint256> hashes;
    bool created;
    if (!sle)
    {
        sle = std::make_shared<SLE>(k);
        created = true;
    }
    else
    {
        hashes = static_cast<decltype(hashes)>(sle->getFieldV256(sfHashes));
        created = false;
    }
    assert(hashes.size() <= 256);
    if (hashes.size() == 256)
        hashes.erase(hashes.begin());
    hashes.push_back(info_.parentHash);
    sle->setFieldV256(sfHashes, STVector256(hashes));
    sle->setFieldU32(sfLastLedgerSequence, prevIndex);
    if (created)
        rawInsert(sle);
    else
        rawReplace(sle);
}

bool
Ledger::isFlagLedger() const
{
    return info_.seq % FLAG_LEDGER_INTERVAL == 0;
}
bool
Ledger::isVotingLedger() const
{
    return (info_.seq + 1) % FLAG_LEDGER_INTERVAL == 0;
}

bool
isFlagLedger(LedgerIndex seq)
{
    return seq % FLAG_LEDGER_INTERVAL == 0;
}

static bool
saveValidatedLedger(
    Application& app,
    std::shared_ptr<Ledger const> const& ledger,
    bool current)
{
    auto j = app.journal("Ledger");
    auto seq = ledger->info().seq;
    if (!app.pendingSaves().startWork(seq))
    {
        // The save was completed synchronously
        JLOG(j.debug()) << "Save aborted";
        return true;
    }

    auto res = app.getLedgerDB()->getInterface()->saveValidatedLedger(
        app.getLedgerDB(), app.getTxnDB(), app, ledger, current);

    // Clients can now trust the database for
    // information about this ledger sequence.
    app.pendingSaves().finishWork(seq);
    return res;
}

/** Save, or arrange to save, a fully-validated ledger
    Returns false on error
*/
bool
pendSaveValidated(
    Application& app,
    std::shared_ptr<Ledger const> const& ledger,
    bool isSynchronous,
    bool isCurrent)
{
    if (!app.getHashRouter().setFlags(ledger->info().hash, SF_SAVED))
    {
        // We have tried to save this ledger recently
        auto stream = app.journal("Ledger").debug();
        JLOG(stream) << "Double pend save for " << ledger->info().seq;

        if (!isSynchronous || !app.pendingSaves().pending(ledger->info().seq))
        {
            // Either we don't need it to be finished
            // or it is finished
            return true;
        }
    }

    assert(ledger->isImmutable());

    if (!app.pendingSaves().shouldWork(ledger->info().seq, isSynchronous))
    {
        auto stream = app.journal("Ledger").debug();
        JLOG(stream) << "Pend save with seq in pending saves "
                     << ledger->info().seq;

        return true;
    }

    JobType const jobType{isCurrent ? jtPUBLEDGER : jtPUBOLDLEDGER};
    char const* const jobName{
        isCurrent ? "Ledger::pendSave" : "Ledger::pendOldSave"};

    // See if we can use the JobQueue.
    if (!isSynchronous &&
        app.getJobQueue().addJob(
            jobType, jobName, [&app, ledger, isCurrent](Job&) {
                saveValidatedLedger(app, ledger, isCurrent);
            }))
    {
        return true;
    }

    // The JobQueue won't do the Job.  Do the save synchronously.
    return saveValidatedLedger(app, ledger, isCurrent);
}

void
Ledger::unshare() const
{
    stateMap_->unshare();
    txMap_->unshare();
}

void
Ledger::invariants() const
{
    stateMap_->invariants();
    txMap_->invariants();
}

//------------------------------------------------------------------------------

std::tuple<std::shared_ptr<Ledger>, std::uint32_t, uint256>
loadLedgerHelper(
    SQLInterface::SQLLedgerInfo const& sinfo,
    Application& app,
    bool acquire)
{
    uint256 ledgerHash{};
    std::uint32_t ledgerSeq{0};

    ledgerSeq = rangeCheckedCast<std::uint32_t>(sinfo.ledgerSeq64.value_or(0));

    uint256 prevHash{}, accountHash{}, transHash{};
    if (sinfo.sLedgerHash)
        ledgerHash.SetHexExact(*sinfo.sLedgerHash);
    if (sinfo.sPrevHash)
        prevHash.SetHexExact(*sinfo.sPrevHash);
    if (sinfo.sAccountHash)
        accountHash.SetHexExact(*sinfo.sAccountHash);
    if (sinfo.sTransHash)
        transHash.SetHexExact(*sinfo.sTransHash);

    using time_point = NetClock::time_point;
    using duration = NetClock::duration;

    LedgerInfo info;
    info.parentHash = prevHash;
    info.txHash = transHash;
    info.accountHash = accountHash;
    info.drops = sinfo.totDrops.value_or(0);
    info.closeTime = time_point{duration{sinfo.closingTime.value_or(0)}};
    info.parentCloseTime =
        time_point{duration{sinfo.prevClosingTime.value_or(0)}};
    info.closeFlags = sinfo.closeFlags.value_or(0);
    info.closeTimeResolution = duration{sinfo.closeResolution.value_or(0)};
    info.seq = ledgerSeq;

    bool loaded;
    auto ledger = std::make_shared<Ledger>(
        info,
        loaded,
        acquire,
        app.config(),
        app.getNodeFamily(),
        app.journal("Ledger"));

    if (!loaded)
        ledger.reset();

    return std::make_tuple(ledger, ledgerSeq, ledgerHash);
}

static void
finishLoadByIndexOrHash(
    std::shared_ptr<Ledger> const& ledger,
    Config const& config,
    beast::Journal j)
{
    if (!ledger)
        return;

    ledger->setImmutable(config);

    JLOG(j.trace()) << "Loaded ledger: " << to_string(ledger->info().hash);

    ledger->setFull();
}

std::shared_ptr<Ledger>
loadByIndex(std::uint32_t ledgerIndex, Application& app, bool acquire)
{
    std::shared_ptr<Ledger> ledger;
    {
        SQLInterface::SQLLedgerInfo sinfo;
        if (app.getLedgerDB()->getInterface()->loadLedgerInfoByIndex(
                app.getLedgerDB(), sinfo, app.journal("Ledger"), ledgerIndex))
            std::tie(ledger, std::ignore, std::ignore) =
                loadLedgerHelper(sinfo, app, acquire);
    }

    finishLoadByIndexOrHash(ledger, app.config(), app.journal("Ledger"));
    return ledger;
}

std::shared_ptr<Ledger>
loadByHash(uint256 const& ledgerHash, Application& app, bool acquire)
{
    std::shared_ptr<Ledger> ledger;
    {
        SQLInterface::SQLLedgerInfo sinfo;
        if (app.getLedgerDB()->getInterface()->loadLedgerInfoByHash(
                app.getLedgerDB(), sinfo, app.journal("Ledger"), ledgerHash))
            std::tie(ledger, std::ignore, std::ignore) =
                loadLedgerHelper(sinfo, app, acquire);
    }

    finishLoadByIndexOrHash(ledger, app.config(), app.journal("Ledger"));

    assert(!ledger || ledger->info().hash == ledgerHash);

    return ledger;
}

}  // namespace ripple
