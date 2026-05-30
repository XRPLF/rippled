#include <xrpl/ledger/FlatStateMap.h>

#include <xrpl/ledger/Ledger.h>
#include <xrpl/ledger/ReadView.h>

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <utility>

namespace xrpl {

FlatStateMap::value_type
FlatStateMap::read(key_type const& key) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto const it = map_.find(key);
    if (it == map_.end())
        return nullptr;
    return it->second;
}

bool
FlatStateMap::exists(key_type const& key) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return map_.find(key) != map_.end();
}

void
FlatStateMap::insert(key_type const& key, value_type sle)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    map_.insert_or_assign(key, std::move(sle));
}

void
FlatStateMap::erase(key_type const& key)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    map_.erase(key);
}

std::size_t
FlatStateMap::size() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return map_.size();
}

bool
FlatStateMap::empty() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return map_.empty();
}

void
FlatStateMap::clear()
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    map_.clear();
}

std::unique_ptr<FlatStateMap>
FlatStateMap::snapshot() const
{
    auto out = std::make_unique<FlatStateMap>();
    std::shared_lock<std::shared_mutex> lock(mutex_);
    // Reserve to avoid rehash during the bulk copy.
    out->map_.reserve(map_.size());
    for (auto const& entry : map_)
        out->map_.insert(entry);
    return out;
}

void
populateFromReadView(FlatStateMap& target, ReadView const& source)
{
    populateFromRange(target, source.sles);
}

void
attachFlatStateMapTo(Ledger& ledger)
{
    auto map = std::make_shared<FlatStateMap>();
    populateFromReadView(*map, ledger);
    ledger.setFlatStateMap(std::move(map));
}

void
mirrorRawInsert(FlatStateMap& map, std::shared_ptr<STLedgerEntry const> sle)
{
    auto const key = sle->key();
    map.insert(key, std::move(sle));
}

void
mirrorRawReplace(FlatStateMap& map, std::shared_ptr<STLedgerEntry const> sle)
{
    auto const key = sle->key();
    map.insert(key, std::move(sle));  // insert == insert_or_assign here
}

void
mirrorRawErase(
    FlatStateMap& map,
    std::shared_ptr<STLedgerEntry const> const& sle)
{
    map.erase(sle->key());
}

void
mirrorRawErase(FlatStateMap& map, uint256 const& key)
{
    map.erase(key);
}

std::shared_ptr<STLedgerEntry const>
readFromFlatStateMap(FlatStateMap const& map, Keylet const& k)
{
    auto sle = map.read(k.key);
    if (!sle)
        return nullptr;
    if (!k.check(*sle))
        return nullptr;
    return sle;
}

}  // namespace xrpl
