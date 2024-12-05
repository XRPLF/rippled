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

#include <xrpld/app/ledger/AcceptedLedger.h>
#include <xrpld/app/ledger/InboundLedgers.h>
#include <xrpld/app/ledger/Ledger.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/LedgerToJson.h>
#include <xrpld/app/ledger/OrderBookDB.h>
#include <xrpld/app/ledger/PendingSaves.h>
#include <xrpld/app/ledger/TransactionMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/HashRouter.h>
#include <xrpld/app/misc/LoadFeeTrack.h>
#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/app/rdb/backend/SQLiteDatabase.h>
#include <xrpld/consensus/LedgerTiming.h>
#include <xrpld/core/Config.h>
#include <xrpld/core/JobQueue.h>
#include <xrpld/core/SociDB.h>
#include <xrpld/nodestore/Database.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/jss.h>
#include <boost/optional.hpp>
#include <utility>
#include <vector>

#include <xrpld/nodestore/detail/DatabaseNodeImp.h>

namespace ripple {

create_genesis_t const create_genesis{};

uint256
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
        if (auto const p = dynamic_cast<sles_iter_impl const*>(&impl))
            return iter_ == p->iter_;
        return false;
    }

    void
    increment() override
    {
        ++iter_;
    }

    sles_type::value_type
    dereference() const override
    {
        SerialIter sit(iter_->slice());
        return std::make_shared<SLE const>(sit, iter_->key());
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
        : metadata_(metadata), iter_(std::move(iter))
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
        if (auto const p = dynamic_cast<txs_iter_impl const*>(&impl))
            return iter_ == p->iter_;
        return false;
    }

    void
    increment() override
    {
        ++iter_;
    }

    txs_type::value_type
    dereference() const override
    {
        auto const& item = *iter_;
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
    , txMap_(SHAMapType::TRANSACTION, family)
    , stateMap_(SHAMapType::STATE, family)
    , rules_{config.features}
    , j_(beast::Journal(beast::Journal::getNullSink()))
{
    info_.seq = 1;
    info_.drops = INITIAL_XRP;
    info_.closeTimeResolution = ledgerGenesisTimeResolution;

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

    {
        auto sle = std::make_shared<SLE>(keylet::fees());
        // Whether featureXRPFees is supported will depend on startup options.
        if (std::find(amendments.begin(), amendments.end(), featureXRPFees) !=
            amendments.end())
        {
            sle->at(sfBaseFeeDrops) = config.FEES.reference_fee;
            sle->at(sfReserveBaseDrops) = config.FEES.account_reserve;
            sle->at(sfReserveIncrementDrops) = config.FEES.owner_reserve;
        }
        else
        {
            if (auto const f =
                    config.FEES.reference_fee.dropsAs<std::uint64_t>())
                sle->at(sfBaseFee) = *f;
            if (auto const f =
                    config.FEES.account_reserve.dropsAs<std::uint32_t>())
                sle->at(sfReserveBase) = *f;
            if (auto const f =
                    config.FEES.owner_reserve.dropsAs<std::uint32_t>())
                sle->at(sfReserveIncrement) = *f;
            sle->at(sfReferenceFeeUnits) = Config::FEE_UNITS_DEPRECATED;
        }
        rawInsert(sle);
    }

    stateMap_.flushDirty(hotACCOUNT_NODE);
    setImmutable();
}

Ledger::Ledger(
    LedgerInfo const& info,
    bool& loaded,
    bool acquire,
    Config const& config,
    Family& family,
    beast::Journal j)
    : mImmutable(true)
    , txMap_(SHAMapType::TRANSACTION, info.txHash, family)
    , stateMap_(SHAMapType::STATE, info.accountHash, family)
    , rules_(config.features)
    , info_(info)
    , j_(j)
{
    loaded = true;

    if (info_.txHash.isNonZero() &&
        !txMap_.fetchRoot(SHAMapHash{info_.txHash}, nullptr))
    {
        loaded = false;
        JLOG(j.warn()) << "Don't have transaction root for ledger" << info_.seq;
    }

    if (info_.accountHash.isNonZero() &&
        !stateMap_.fetchRoot(SHAMapHash{info_.accountHash}, nullptr))
    {
        loaded = false;
        JLOG(j.warn()) << "Don't have state data root for ledger" << info_.seq;
    }

    txMap_.setImmutable();
    stateMap_.setImmutable();

    defaultFees(config);
    if (!setup())
        loaded = false;

    if (!loaded)
    {
        info_.hash = calculateLedgerHash(info_);
        if (acquire)
            family.missingNodeAcquireByHash(info_.hash, info_.seq);
    }
}

// Create a new ledger that follows this one
Ledger::Ledger(Ledger const& prevLedger, NetClock::time_point closeTime)
    : mImmutable(false)
    , txMap_(SHAMapType::TRANSACTION, prevLedger.txMap_.family())
    , stateMap_(prevLedger.stateMap_, true)
    , fees_(prevLedger.fees_)
    , rules_(prevLedger.rules_)
    , j_(beast::Journal(beast::Journal::getNullSink()))
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
    , txMap_(SHAMapType::TRANSACTION, info.txHash, family)
    , stateMap_(SHAMapType::STATE, info.accountHash, family)
    , rules_{config.features}
    , info_(info)
    , j_(beast::Journal(beast::Journal::getNullSink()))
{
    info_.hash = calculateLedgerHash(info_);
}

Ledger::Ledger(
    std::uint32_t ledgerSeq,
    NetClock::time_point closeTime,
    Config const& config,
    Family& family)
    : mImmutable(false)
    , txMap_(SHAMapType::TRANSACTION, family)
    , stateMap_(SHAMapType::STATE, family)
    , rules_{config.features}
    , j_(beast::Journal(beast::Journal::getNullSink()))
{
    info_.seq = ledgerSeq;
    info_.closeTime = closeTime;
    info_.closeTimeResolution = ledgerDefaultTimeResolution;
    defaultFees(config);
    setup();
}

void
Ledger::setImmutable(bool rehash)
{
    // Force update, since this is the only
    // place the hash transitions to valid
    if (!mImmutable && rehash)
    {
        info_.txHash = txMap_.getHash().as_uint256();
        info_.accountHash = stateMap_.getHash().as_uint256();
    }

    if (rehash)
        info_.hash = calculateLedgerHash(info_);

    mImmutable = true;
    txMap_.setImmutable();
    stateMap_.setImmutable();
    setup();
}

void
Ledger::setAccepted(
    NetClock::time_point closeTime,
    NetClock::duration closeResolution,
    bool correctCloseTime)
{
    // Used when we witnessed the consensus.
    ASSERT(!open(), "ripple::Ledger::setAccepted : valid ledger state");

    info_.closeTime = closeTime;
    info_.closeTimeResolution = closeResolution;
    info_.closeFlags = correctCloseTime ? 0 : sLCF_NoConsensusTime;
    setImmutable();
}

bool
Ledger::addSLE(SLE const& sle)
{
    auto const s = sle.getSerializer();
    return stateMap_.addItem(
        SHAMapNodeType::tnACCOUNT_STATE, make_shamapitem(sle.key(), s.slice()));
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
    return stateMap_.hasItem(k.key);
}

bool
Ledger::exists(uint256 const& key) const
{
    return stateMap_.hasItem(key);
}

std::optional<uint256>
Ledger::succ(uint256 const& key, std::optional<uint256> const& last) const
{
    auto item = stateMap_.upper_bound(key);
    if (item == stateMap_.end())
        return std::nullopt;
    if (last && item->key() >= last)
        return std::nullopt;
    return item->key();
}

std::shared_ptr<SLE const>
Ledger::read(Keylet const& k) const
{
    if (k.key == beast::zero)
    {
        UNREACHABLE("ripple::Ledger::read : zero key");
        return nullptr;
    }
    auto const& item = stateMap_.peekItem(k.key);
    if (!item)
        return nullptr;
    auto sle = std::make_shared<SLE>(SerialIter{item->slice()}, item->key());
    if (!k.check(*sle))
        return nullptr;
    return sle;
}

//------------------------------------------------------------------------------

auto
Ledger::slesBegin() const -> std::unique_ptr<sles_type::iter_base>
{
    return std::make_unique<sles_iter_impl>(stateMap_.begin());
}

auto
Ledger::slesEnd() const -> std::unique_ptr<sles_type::iter_base>
{
    return std::make_unique<sles_iter_impl>(stateMap_.end());
}

auto
Ledger::slesUpperBound(uint256 const& key) const
    -> std::unique_ptr<sles_type::iter_base>
{
    return std::make_unique<sles_iter_impl>(stateMap_.upper_bound(key));
}

auto
Ledger::txsBegin() const -> std::unique_ptr<txs_type::iter_base>
{
    return std::make_unique<txs_iter_impl>(!open(), txMap_.begin());
}

auto
Ledger::txsEnd() const -> std::unique_ptr<txs_type::iter_base>
{
    return std::make_unique<txs_iter_impl>(!open(), txMap_.end());
}

bool
Ledger::txExists(uint256 const& key) const
{
    return txMap_.hasItem(key);
}

auto
Ledger::txRead(key_type const& key) const -> tx_type
{
    auto const& item = txMap_.peekItem(key);
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
Ledger::digest(key_type const& key) const -> std::optional<digest_type>
{
    SHAMapHash digest;
    // VFALCO Unfortunately this loads the item
    //        from the NodeStore needlessly.
    if (!stateMap_.peekItem(key, digest))
        return std::nullopt;
    return digest.as_uint256();
}

//------------------------------------------------------------------------------

void
Ledger::rawErase(std::shared_ptr<SLE> const& sle)
{
    if (!stateMap_.delItem(sle->key()))
        LogicError("Ledger::rawErase: key not found");
}

void
Ledger::rawErase(uint256 const& key)
{
    if (!stateMap_.delItem(key))
        LogicError("Ledger::rawErase: key not found");
}

void
Ledger::rawInsert(std::shared_ptr<SLE> const& sle)
{
    Serializer ss;
    sle->add(ss);
    if (!stateMap_.addGiveItem(
            SHAMapNodeType::tnACCOUNT_STATE,
            make_shamapitem(sle->key(), ss.slice())))
        LogicError("Ledger::rawInsert: key already exists");
}

void
Ledger::rawReplace(std::shared_ptr<SLE> const& sle)
{
    Serializer ss;
    sle->add(ss);
    if (!stateMap_.updateGiveItem(
            SHAMapNodeType::tnACCOUNT_STATE,
            make_shamapitem(sle->key(), ss.slice())))
        LogicError("Ledger::rawReplace: key not found");
}

void
Ledger::rawTxInsert(
    uint256 const& key,
    std::shared_ptr<Serializer const> const& txn,
    std::shared_ptr<Serializer const> const& metaData)
{
    ASSERT(
        metaData != nullptr,
        "ripple::Ledger::rawTxInsert : non-null metadata input");

    // low-level - just add to table
    Serializer s(txn->getDataLength() + metaData->getDataLength() + 16);
    s.addVL(txn->peekData());
    s.addVL(metaData->peekData());
    if (!txMap_.addGiveItem(
            SHAMapNodeType::tnTRANSACTION_MD, make_shamapitem(key, s.slice())))
        LogicError("duplicate_tx: " + to_string(key));
}

uint256
Ledger::rawTxInsertWithHash(
    uint256 const& key,
    std::shared_ptr<Serializer const> const& txn,
    std::shared_ptr<Serializer const> const& metaData)
{
    ASSERT(
        metaData != nullptr,
        "ripple::Ledger::rawTxInsertWithHash : non-null metadata input");

    // low-level - just add to table
    Serializer s(txn->getDataLength() + metaData->getDataLength() + 16);
    s.addVL(txn->peekData());
    s.addVL(metaData->peekData());
    auto item = make_shamapitem(key, s.slice());
    auto hash = sha512Half(HashPrefix::txNode, item->slice(), item->key());
    if (!txMap_.addGiveItem(SHAMapNodeType::tnTRANSACTION_MD, std::move(item)))
        LogicError("duplicate_tx: " + to_string(key));

    return hash;
}

bool
Ledger::setup()
{
    bool ret = true;

    try
    {
        rules_ = makeRulesGivenLedger(*this, rules_);
    }
    catch (SHAMapMissingNode const&)
    {
        ret = false;
    }
    catch (std::exception const& ex)
    {
        JLOG(j_.error()) << "Exception in " << __func__ << ": " << ex.what();
        Rethrow();
    }

    try
    {
        if (auto const sle = read(keylet::fees()))
        {
            bool oldFees = false;
            bool newFees = false;
            {
                auto const baseFee = sle->at(~sfBaseFee);
                auto const reserveBase = sle->at(~sfReserveBase);
                auto const reserveIncrement = sle->at(~sfReserveIncrement);
                if (baseFee)
                    fees_.base = *baseFee;
                if (reserveBase)
                    fees_.reserve = *reserveBase;
                if (reserveIncrement)
                    fees_.increment = *reserveIncrement;
                oldFees = baseFee || reserveBase || reserveIncrement;
            }
            {
                auto const baseFeeXRP = sle->at(~sfBaseFeeDrops);
                auto const reserveBaseXRP = sle->at(~sfReserveBaseDrops);
                auto const reserveIncrementXRP =
                    sle->at(~sfReserveIncrementDrops);
                auto assign = [&ret](
                                  XRPAmount& dest,
                                  std::optional<STAmount> const& src) {
                    if (src)
                    {
                        if (src->native())
                            dest = src->xrp();
                        else
                            ret = false;
                    }
                };
                assign(fees_.base, baseFeeXRP);
                assign(fees_.reserve, reserveBaseXRP);
                assign(fees_.increment, reserveIncrementXRP);
                newFees = baseFeeXRP || reserveBaseXRP || reserveIncrementXRP;
            }
            if (oldFees && newFees)
                // Should be all of one or the other, but not both
                ret = false;
            if (!rules_.enabled(featureXRPFees) && newFees)
                // Can't populate the new fees before the amendment is enabled
                ret = false;
        }
    }
    catch (SHAMapMissingNode const&)
    {
        ret = false;
    }
    catch (std::exception const& ex)
    {
        JLOG(j_.error()) << "Exception in " << __func__ << ": " << ex.what();
        Rethrow();
    }

    return ret;
}

void
Ledger::defaultFees(Config const& config)
{
    ASSERT(
        fees_.base == 0 && fees_.reserve == 0 && fees_.increment == 0,
        "ripple::Ledger::defaultFees : zero fees");
    if (fees_.base == 0)
        fees_.base = config.FEES.reference_fee;
    if (fees_.reserve == 0)
        fees_.reserve = config.FEES.account_reserve;
    if (fees_.increment == 0)
        fees_.increment = config.FEES.owner_reserve;
}

std::shared_ptr<SLE>
Ledger::peek(Keylet const& k) const
{
    auto const& value = stateMap_.peekItem(k.key);
    if (!value)
        return nullptr;
    auto sle = std::make_shared<SLE>(SerialIter{value->slice()}, value->key());
    if (!k.check(*sle))
        return nullptr;
    return sle;
}

hash_set<PublicKey>
Ledger::negativeUNL() const
{
    hash_set<PublicKey> negUnl;
    if (auto sle = read(keylet::negativeUNL());
        sle && sle->isFieldPresent(sfDisabledValidators))
    {
        auto const& nUnlData = sle->getFieldArray(sfDisabledValidators);
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

std::optional<PublicKey>
Ledger::validatorToDisable() const
{
    if (auto sle = read(keylet::negativeUNL());
        sle && sle->isFieldPresent(sfValidatorToDisable))
    {
        auto d = sle->getFieldVL(sfValidatorToDisable);
        auto s = makeSlice(d);
        if (publicKeyType(s))
            return PublicKey(s);
    }

    return std::nullopt;
}

std::optional<PublicKey>
Ledger::validatorToReEnable() const
{
    if (auto sle = read(keylet::negativeUNL());
        sle && sle->isFieldPresent(sfValidatorToReEnable))
    {
        auto d = sle->getFieldVL(sfValidatorToReEnable);
        auto s = makeSlice(d);
        if (publicKeyType(s))
            return PublicKey(s);
    }

    return std::nullopt;
}

void
Ledger::updateNegativeUNL()
{
    auto sle = peek(keylet::negativeUNL());
    if (!sle)
        return;

    bool const hasToDisable = sle->isFieldPresent(sfValidatorToDisable);
    bool const hasToReEnable = sle->isFieldPresent(sfValidatorToReEnable);

    if (!hasToDisable && !hasToReEnable)
        return;

    STArray newNUnl;
    if (sle->isFieldPresent(sfDisabledValidators))
    {
        auto const& oldNUnl = sle->getFieldArray(sfDisabledValidators);
        for (auto v : oldNUnl)
        {
            if (hasToReEnable && v.isFieldPresent(sfPublicKey) &&
                v.getFieldVL(sfPublicKey) ==
                    sle->getFieldVL(sfValidatorToReEnable))
                continue;
            newNUnl.push_back(v);
        }
    }

    if (hasToDisable)
    {
        newNUnl.push_back(STObject::makeInnerObject(sfDisabledValidator));
        newNUnl.back().setFieldVL(
            sfPublicKey, sle->getFieldVL(sfValidatorToDisable));
        newNUnl.back().setFieldU32(sfFirstLedgerSequence, seq());
    }

    if (!newNUnl.empty())
    {
        sle->setFieldArray(sfDisabledValidators, newNUnl);
        if (hasToReEnable)
            sle->makeFieldAbsent(sfValidatorToReEnable);
        if (hasToDisable)
            sle->makeFieldAbsent(sfValidatorToDisable);
        rawReplace(sle);
    }
    else
    {
        rawErase(sle);
    }
}

//------------------------------------------------------------------------------
bool
Ledger::walkLedger(beast::Journal j, bool parallel) const
{
    std::vector<SHAMapMissingNode> missingNodes1;
    std::vector<SHAMapMissingNode> missingNodes2;

    if (stateMap_.getHash().isZero() && !info_.accountHash.isZero() &&
        !stateMap_.fetchRoot(SHAMapHash{info_.accountHash}, nullptr))
    {
        missingNodes1.emplace_back(
            SHAMapType::STATE, SHAMapHash{info_.accountHash});
    }
    else
    {
        if (parallel)
            return stateMap_.walkMapParallel(missingNodes1, 32);
        else
            stateMap_.walkMap(missingNodes1, 32);
    }

    if (!missingNodes1.empty())
    {
        if (auto stream = j.info())
        {
            stream << missingNodes1.size() << " missing account node(s)";
            stream << "First: " << missingNodes1[0].what();
        }
    }

    if (txMap_.getHash().isZero() && info_.txHash.isNonZero() &&
        !txMap_.fetchRoot(SHAMapHash{info_.txHash}, nullptr))
    {
        missingNodes2.emplace_back(
            SHAMapType::TRANSACTION, SHAMapHash{info_.txHash});
    }
    else
    {
        txMap_.walkMap(missingNodes2, 32);
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
Ledger::assertSensible(beast::Journal ledgerJ) const
{
    if (info_.hash.isNonZero() && info_.accountHash.isNonZero() &&
        (info_.accountHash == stateMap_.getHash().as_uint256()) &&
        (info_.txHash == txMap_.getHash().as_uint256()))
    {
        return true;
    }

    Json::Value j = getJson({*this, {}});

    j[jss::accountTreeHash] = to_string(info_.accountHash);
    j[jss::transTreeHash] = to_string(info_.txHash);

    JLOG(ledgerJ.fatal()) << "ledger is not sensible" << j;

    UNREACHABLE("ripple::Ledger::assertSensible : ledger is not sensible");

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

        ASSERT(
            hashes.size() <= 256,
            "ripple::Ledger::updateSkipList : first maximum hashes size");
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
    ASSERT(
        hashes.size() <= 256,
        "ripple::Ledger::updateSkipList : second maximum hashes size");
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

    auto const db = dynamic_cast<SQLiteDatabase*>(&app.getRelationalDatabase());
    if (!db)
        Throw<std::runtime_error>("Failed to get relational database");

    auto const res = db->saveValidatedLedger(ledger, current);

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

    ASSERT(
        ledger->isImmutable(), "ripple::pendSaveValidated : immutable ledger");

    if (!app.pendingSaves().shouldWork(ledger->info().seq, isSynchronous))
    {
        auto stream = app.journal("Ledger").debug();
        JLOG(stream) << "Pend save with seq in pending saves "
                     << ledger->info().seq;

        return true;
    }

    // See if we can use the JobQueue.
    if (!isSynchronous &&
        app.getJobQueue().addJob(
            isCurrent ? jtPUBLEDGER : jtPUBOLDLEDGER,
            std::to_string(ledger->seq()),
            [&app, ledger, isCurrent]() {
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
    stateMap_.unshare();
    txMap_.unshare();
}

void
Ledger::invariants() const
{
    stateMap_.invariants();
    txMap_.invariants();
}
//------------------------------------------------------------------------------

/*
 * Make ledger using info loaded from database.
 *
 * @param LedgerInfo: Ledger information.
 * @param app: Link to the Application.
 * @param acquire: Acquire the ledger if not found locally.
 * @return Shared pointer to the ledger.
 */
std::shared_ptr<Ledger>
loadLedgerHelper(LedgerInfo const& info, Application& app, bool acquire)
{
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

    return ledger;
}

static void
finishLoadByIndexOrHash(
    std::shared_ptr<Ledger> const& ledger,
    Config const& config,
    beast::Journal j)
{
    if (!ledger)
        return;

    ASSERT(
        ledger->info().seq < XRP_LEDGER_EARLIEST_FEES ||
            ledger->read(keylet::fees()),
        "ripple::finishLoadByIndexOrHash : valid ledger fees");
    ledger->setImmutable();

    JLOG(j.trace()) << "Loaded ledger: " << to_string(ledger->info().hash);

    ledger->setFull();
}

std::tuple<std::shared_ptr<Ledger>, std::uint32_t, uint256>
getLatestLedger(Application& app)
{
    const std::optional<LedgerInfo> info =
        app.getRelationalDatabase().getNewestLedgerInfo();
    if (!info)
        return {std::shared_ptr<Ledger>(), {}, {}};
    return {loadLedgerHelper(*info, app, true), info->seq, info->hash};
}

std::shared_ptr<Ledger>
loadByIndex(std::uint32_t ledgerIndex, Application& app, bool acquire)
{
    if (std::optional<LedgerInfo> info =
            app.getRelationalDatabase().getLedgerInfoByIndex(ledgerIndex))
    {
        std::shared_ptr<Ledger> ledger = loadLedgerHelper(*info, app, acquire);
        finishLoadByIndexOrHash(ledger, app.config(), app.journal("Ledger"));
        return ledger;
    }
    return {};
}

std::shared_ptr<Ledger>
loadByHash(uint256 const& ledgerHash, Application& app, bool acquire)
{
    if (std::optional<LedgerInfo> info =
            app.getRelationalDatabase().getLedgerInfoByHash(ledgerHash))
    {
        std::shared_ptr<Ledger> ledger = loadLedgerHelper(*info, app, acquire);
        finishLoadByIndexOrHash(ledger, app.config(), app.journal("Ledger"));
        ASSERT(
            !ledger || ledger->info().hash == ledgerHash,
            "ripple::loadByHash : ledger hash match if loaded");
        return ledger;
    }
    return {};
}

}  // namespace ripple
