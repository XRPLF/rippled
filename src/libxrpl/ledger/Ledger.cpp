#include <xrpl/ledger/Ledger.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/LedgerTiming.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/shamap/Family.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapMissingNode.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace xrpl {

create_genesis_t const create_genesis{};

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

    [[nodiscard]] std::unique_ptr<base_type>
    copy() const override
    {
        return std::make_unique<sles_iter_impl>(*this);
    }

    [[nodiscard]] bool
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

    [[nodiscard]] sles_type::value_type
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

    txs_iter_impl(bool metadata, SHAMap::const_iterator iter) : metadata_(metadata), iter_(iter)
    {
    }

    [[nodiscard]] std::unique_ptr<base_type>
    copy() const override
    {
        return std::make_unique<txs_iter_impl>(*this);
    }

    [[nodiscard]] bool
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

    [[nodiscard]] txs_type::value_type
    dereference() const override
    {
        auto const& item = *iter_;
        if (metadata_)
            return Ledger::deserializeTxPlusMeta(item);
        return {Ledger::deserializeTx(item), nullptr};
    }
};

//------------------------------------------------------------------------------

Ledger::Ledger(
    create_genesis_t,
    Rules rules,
    Fees const& fees,
    std::vector<uint256> const& amendments,
    Family& family)
    : mImmutable(false)
    , txMap_(SHAMapType::TRANSACTION, family)
    , stateMap_(SHAMapType::STATE, family)
    , fees_(fees)
    , rules_(std::move(rules))
    , j_(beast::Journal(beast::Journal::getNullSink()))
{
    header_.seq = 1;
    header_.drops = INITIAL_XRP;
    header_.closeTimeResolution = ledgerGenesisTimeResolution;

    static auto const id =
        calcAccountID(generateKeyPair(KeyType::secp256k1, generateSeed("masterpassphrase")).first);
    {
        auto const sle = std::make_shared<SLE>(keylet::account(id));
        sle->setFieldU32(sfSequence, 1);
        sle->setAccountID(sfAccount, id);
        sle->setFieldAmount(sfBalance, header_.drops);
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
        if (std::ranges::find(amendments, featureXRPFees) != amendments.end())
        {
            sle->at(sfBaseFeeDrops) = fees.base;
            sle->at(sfReserveBaseDrops) = fees.reserve;
            sle->at(sfReserveIncrementDrops) = fees.increment;
        }
        else
        {
            if (auto const f = fees.base.dropsAs<std::uint64_t>())
                sle->at(sfBaseFee) = *f;
            if (auto const f = fees.reserve.dropsAs<std::uint32_t>())
                sle->at(sfReserveBase) = *f;
            if (auto const f = fees.increment.dropsAs<std::uint32_t>())
                sle->at(sfReserveIncrement) = *f;
            sle->at(sfReferenceFeeUnits) = FEE_UNITS_DEPRECATED;
        }
        rawInsert(sle);
    }

    stateMap_.flushDirty(NodeObjectType::hotACCOUNT_NODE);
    setImmutable();
}

Ledger::Ledger(
    LedgerHeader const& info,
    bool& loaded,
    bool acquire,
    Rules rules,
    Fees const& fees,
    Family& family,
    beast::Journal j)
    : mImmutable(true)
    , txMap_(SHAMapType::TRANSACTION, info.txHash, family)
    , stateMap_(SHAMapType::STATE, info.accountHash, family)
    , fees_(fees)
    , rules_(std::move(rules))
    , header_(info)
    , j_(j)
{
    loaded = true;

    if (header_.txHash.isNonZero() && !txMap_.fetchRoot(SHAMapHash{header_.txHash}, nullptr))
    {
        loaded = false;
        JLOG(j.warn()) << "Don't have transaction root for ledger" << header_.seq;
    }

    if (header_.accountHash.isNonZero() &&
        !stateMap_.fetchRoot(SHAMapHash{header_.accountHash}, nullptr))
    {
        loaded = false;
        JLOG(j.warn()) << "Don't have state data root for ledger" << header_.seq;
    }

    txMap_.setImmutable();
    stateMap_.setImmutable();

    if (!setup())
        loaded = false;

    if (!loaded)
    {
        header_.hash = calculateLedgerHash(header_);
        if (acquire)
            family.missingNodeAcquireByHash(header_.hash, header_.seq);
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
    header_.seq = prevLedger.header_.seq + 1;
    header_.parentCloseTime = prevLedger.header_.closeTime;
    header_.hash = prevLedger.header().hash + uint256(1);
    header_.drops = prevLedger.header().drops;
    header_.closeTimeResolution = prevLedger.header_.closeTimeResolution;
    header_.parentHash = prevLedger.header().hash;
    header_.closeTimeResolution = getNextLedgerTimeResolution(
        prevLedger.header_.closeTimeResolution, getCloseAgree(prevLedger.header()), header_.seq);

    if (prevLedger.header_.closeTime == NetClock::time_point{})
    {
        header_.closeTime = roundCloseTime(closeTime, header_.closeTimeResolution);
    }
    else
    {
        header_.closeTime = prevLedger.header_.closeTime + header_.closeTimeResolution;
    }
}

Ledger::Ledger(LedgerHeader const& info, Rules rules, Family& family)
    : mImmutable(true)
    , txMap_(SHAMapType::TRANSACTION, info.txHash, family)
    , stateMap_(SHAMapType::STATE, info.accountHash, family)
    , rules_(std::move(rules))
    , header_(info)
    , j_(beast::Journal(beast::Journal::getNullSink()))
{
    header_.hash = calculateLedgerHash(header_);
}

Ledger::Ledger(
    std::uint32_t ledgerSeq,
    NetClock::time_point closeTime,
    Rules rules,
    Fees const& fees,
    Family& family)
    : mImmutable(false)
    , txMap_(SHAMapType::TRANSACTION, family)
    , stateMap_(SHAMapType::STATE, family)
    , fees_(fees)
    , rules_(std::move(rules))
    , j_(beast::Journal(beast::Journal::getNullSink()))
{
    header_.seq = ledgerSeq;
    header_.closeTime = closeTime;
    header_.closeTimeResolution = ledgerDefaultTimeResolution;
    setup();
}

void
Ledger::setImmutable(bool rehash)
{
    // Force update, since this is the only
    // place the hash transitions to valid
    if (!mImmutable && rehash)
    {
        header_.txHash = txMap_.getHash().as_uint256();
        header_.accountHash = stateMap_.getHash().as_uint256();
    }

    if (rehash)
        header_.hash = calculateLedgerHash(header_);

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
    XRPL_ASSERT(!open(), "xrpl::Ledger::setAccepted : valid ledger state");

    header_.closeTime = closeTime;
    header_.closeTimeResolution = closeResolution;
    header_.closeFlags = correctCloseTime ? 0 : sLCF_NoConsensusTime;
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
Ledger::deserializeTx(SHAMapItem const& item)
{
    SerialIter sit(item.slice());
    return std::make_shared<STTx const>(sit);
}

std::pair<std::shared_ptr<STTx const>, std::shared_ptr<STObject const>>
Ledger::deserializeTxPlusMeta(SHAMapItem const& item)
{
    std::pair<std::shared_ptr<STTx const>, std::shared_ptr<STObject const>> result;
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
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::Ledger::read : zero key");
        return nullptr;
        // LCOV_EXCL_STOP
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
Ledger::slesUpperBound(uint256 const& key) const -> std::unique_ptr<sles_type::iter_base>
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
        Throw<std::logic_error>("Ledger::rawErase: key not found");
}

void
Ledger::rawErase(uint256 const& key)
{
    if (!stateMap_.delItem(key))
        Throw<std::logic_error>("Ledger::rawErase: key not found");
}

void
Ledger::rawInsert(std::shared_ptr<SLE> const& sle)
{
    Serializer ss;
    sle->add(ss);
    if (!stateMap_.addGiveItem(
            SHAMapNodeType::tnACCOUNT_STATE, make_shamapitem(sle->key(), ss.slice())))
        Throw<std::logic_error>("Ledger::rawInsert: key already exists");
}

void
Ledger::rawReplace(std::shared_ptr<SLE> const& sle)
{
    Serializer ss;
    sle->add(ss);
    if (!stateMap_.updateGiveItem(
            SHAMapNodeType::tnACCOUNT_STATE, make_shamapitem(sle->key(), ss.slice())))
        Throw<std::logic_error>("Ledger::rawReplace: key not found");
}

void
Ledger::rawTxInsert(
    uint256 const& key,
    std::shared_ptr<Serializer const> const& txn,
    std::shared_ptr<Serializer const> const& metaData)
{
    XRPL_ASSERT(metaData, "xrpl::Ledger::rawTxInsert : non-null metadata input");

    // low-level - just add to table
    Serializer s(txn->getDataLength() + metaData->getDataLength() + 16);
    s.addVL(txn->peekData());
    s.addVL(metaData->peekData());
    if (!txMap_.addGiveItem(SHAMapNodeType::tnTRANSACTION_MD, make_shamapitem(key, s.slice())))
        Throw<std::logic_error>("duplicate_tx: " + to_string(key));
}

uint256
Ledger::rawTxInsertWithHash(
    uint256 const& key,
    std::shared_ptr<Serializer const> const& txn,
    std::shared_ptr<Serializer const> const& metaData)
{
    XRPL_ASSERT(metaData, "xrpl::Ledger::rawTxInsertWithHash : non-null metadata input");

    // low-level - just add to table
    Serializer s(txn->getDataLength() + metaData->getDataLength() + 16);
    s.addVL(txn->peekData());
    s.addVL(metaData->peekData());
    auto item = make_shamapitem(key, s.slice());
    auto hash = sha512Half(HashPrefix::txNode, item->slice(), item->key());
    if (!txMap_.addGiveItem(SHAMapNodeType::tnTRANSACTION_MD, std::move(item)))
        Throw<std::logic_error>("duplicate_tx: " + to_string(key));

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
                auto const reserveIncrementXRP = sle->at(~sfReserveIncrementDrops);
                auto assign = [&ret](XRPAmount& dest, std::optional<STAmount> const& src) {
                    if (src)
                    {
                        if (src->native())
                        {
                            dest = src->xrp();
                        }
                        else
                        {
                            ret = false;
                        }
                    }
                };
                assign(fees_.base, baseFeeXRP);
                assign(fees_.reserve, reserveBaseXRP);
                assign(fees_.increment, reserveIncrementXRP);
                newFees = baseFeeXRP || reserveBaseXRP || reserveIncrementXRP;
            }
            if (oldFees && newFees)
            {
                // Should be all of one or the other, but not both
                ret = false;
            }
            if (!rules_.enabled(featureXRPFees) && newFees)
            {
                // Can't populate the new fees before the amendment is enabled
                ret = false;
            }
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
    if (auto sle = read(keylet::negativeUNL()); sle && sle->isFieldPresent(sfDisabledValidators))
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
    if (auto sle = read(keylet::negativeUNL()); sle && sle->isFieldPresent(sfValidatorToDisable))
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
    if (auto sle = read(keylet::negativeUNL()); sle && sle->isFieldPresent(sfValidatorToReEnable))
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
        for (auto const& v : oldNUnl)
        {
            if (hasToReEnable && v.isFieldPresent(sfPublicKey) &&
                v.getFieldVL(sfPublicKey) == sle->getFieldVL(sfValidatorToReEnable))
                continue;
            newNUnl.push_back(v);
        }
    }

    if (hasToDisable)
    {
        newNUnl.push_back(STObject::makeInnerObject(sfDisabledValidator));
        newNUnl.back().setFieldVL(sfPublicKey, sle->getFieldVL(sfValidatorToDisable));
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

    if (stateMap_.getHash().isZero() && !header_.accountHash.isZero() &&
        !stateMap_.fetchRoot(SHAMapHash{header_.accountHash}, nullptr))
    {
        missingNodes1.emplace_back(SHAMapType::STATE, SHAMapHash{header_.accountHash});
    }
    else
    {
        if (parallel)
        {
            return stateMap_.walkMapParallel(missingNodes1, 32);
        }

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

    if (txMap_.getHash().isZero() && header_.txHash.isNonZero() &&
        !txMap_.fetchRoot(SHAMapHash{header_.txHash}, nullptr))
    {
        missingNodes2.emplace_back(SHAMapType::TRANSACTION, SHAMapHash{header_.txHash});
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
Ledger::isSensible() const
{
    if (header_.hash.isZero())
        return false;
    if (header_.accountHash.isZero())
        return false;
    if (header_.accountHash != stateMap_.getHash().as_uint256())
        return false;
    if (header_.txHash != txMap_.getHash().as_uint256())
        return false;
    return true;
}

// update the skip list with the information from our previous ledger
// VFALCO TODO Document this skip list concept
void
Ledger::updateSkipList()
{
    if (header_.seq == 0)  // genesis ledger has no previous ledger
        return;

    std::uint32_t const prevIndex = header_.seq - 1;

    // update record of every 256th ledger
    if ((prevIndex & 0xff) == 0)
    {
        auto const k = keylet::skip(prevIndex);
        auto sle = peek(k);
        std::vector<uint256> hashes;

        bool created = false;
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

        XRPL_ASSERT(
            hashes.size() <= 256, "xrpl::Ledger::updateSkipList : first maximum hashes size");
        hashes.push_back(header_.parentHash);
        sle->setFieldV256(sfHashes, STVector256(hashes));
        sle->setFieldU32(sfLastLedgerSequence, prevIndex);
        if (created)
        {
            rawInsert(sle);
        }
        else
        {
            rawReplace(sle);
        }
    }

    // update record of past 256 ledger
    auto const k = keylet::skip();
    auto sle = peek(k);
    std::vector<uint256> hashes;
    bool created = false;
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
    XRPL_ASSERT(hashes.size() <= 256, "xrpl::Ledger::updateSkipList : second maximum hashes size");
    if (hashes.size() == 256)
        hashes.erase(hashes.begin());
    hashes.push_back(header_.parentHash);
    sle->setFieldV256(sfHashes, STVector256(hashes));
    sle->setFieldU32(sfLastLedgerSequence, prevIndex);
    if (created)
    {
        rawInsert(sle);
    }
    else
    {
        rawReplace(sle);
    }
}

bool
Ledger::isFlagLedger() const
{
    return ::xrpl::isFlagLedger(header_.seq);
}
bool
Ledger::isVotingLedger() const
{
    return ::xrpl::isVotingLedger(header_.seq + 1);
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

}  // namespace xrpl
