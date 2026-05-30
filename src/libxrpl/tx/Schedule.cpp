#include <xrpl/tx/Schedule.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/tx/apply.h>
#include <xrpl/tx/applySteps.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <map>
#include <numeric>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace xrpl {

namespace {

// Disjoint-set (union-find) with path compression + union by size.
class UnionFind
{
public:
    explicit UnionFind(std::size_t n) : parent_(n), size_(n, 1)
    {
        std::iota(parent_.begin(), parent_.end(), std::size_t{0});
    }

    std::size_t
    find(std::size_t x)
    {
        while (parent_[x] != x)
            x = parent_[x] = parent_[parent_[x]];
        return x;
    }

    void
    unite(std::size_t a, std::size_t b)
    {
        a = find(a);
        b = find(b);
        if (a == b)
            return;
        if (size_[a] < size_[b])
            std::swap(a, b);
        parent_[b] = a;
        size_[a] += size_[b];
    }

private:
    std::vector<std::size_t> parent_;
    std::vector<std::size_t> size_;
};

}  // namespace

Schedule
scheduleApply(std::vector<std::shared_ptr<STTx const>> const& txns, ReadView const& base)
{
    Schedule sched;

    std::vector<AccessSet> access;
    access.reserve(txns.size());
    bool anyGlobal = false;
    for (auto const& tx : txns)
    {
        access.push_back(accessSetOf(*tx, base));
        anyGlobal = anyGlobal || access.back().touchesGlobal;
    }

    // Conservative fallback: any global-touching (or pseudo) transaction forces
    // a single serial ordering. Correctness over throughput on flag ledgers.
    if (anyGlobal)
    {
        sched.fullySerial = true;
        sched.serial = txns;
        return sched;
    }

    // Union transactions that share any declared ledger entry. An inverted
    // index (key -> first transaction seen touching it) makes this near-linear
    // in the total number of declared keys, rather than O(n^2) pairwise.
    UnionFind uf(txns.size());
    std::unordered_map<uint256, std::size_t, beast::Uhash<>> ownerOfKey;
    for (std::size_t i = 0; i < access.size(); ++i)
    {
        for (auto const& key : access[i].keys())
        {
            auto const [it, inserted] = ownerOfKey.try_emplace(key, i);
            if (!inserted)
                uf.unite(i, it->second);
        }
    }

    // Collect components. Iterating i ascending preserves canonical order within
    // each group; groups are then ordered deterministically by the transaction
    // id of their first (lowest canonical index) member.
    std::map<std::size_t, std::vector<std::size_t>> components;
    for (std::size_t i = 0; i < txns.size(); ++i)
        components[uf.find(i)].push_back(i);

    sched.groups.reserve(components.size());
    for (auto const& [root, members] : components)
    {
        ConflictGroup group;
        group.txns.reserve(members.size());
        for (auto const idx : members)
            group.txns.push_back(txns[idx]);
        sched.groups.push_back(std::move(group));
    }

    // Deterministic group order: by each group's first (lowest-index) member.
    std::sort(
        sched.groups.begin(),
        sched.groups.end(),
        [](ConflictGroup const& a, ConflictGroup const& b) {
            return a.txns.front()->getTransactionID() < b.txns.front()->getTransactionID();
        });

    return sched;
}

namespace {

// Apply a group's transactions into a fresh isolated view over `closed`,
// returning the view (with its accumulated, not-yet-merged write-set) and the
// count that applied. Reads only `closed`, so this is independent of every
// other group.
std::pair<OpenView, std::size_t>
applyGroupIsolated(
    ServiceRegistry& registry,
    ReadView const& closed,
    std::vector<std::shared_ptr<STTx const>> const& group,
    beast::Journal j)
{
    OpenView gv(&closed);
    std::size_t applied = 0;
    for (auto const& tx : group)
    {
        if (apply(registry, gv, *tx, TapNone, j).applied)
            ++applied;
    }
    return {std::move(gv), applied};
}

}  // namespace

ScheduledApplyResult
applyScheduled(
    ServiceRegistry& registry,
    ReadView const& closed,
    TxsRawView& to,
    std::vector<std::shared_ptr<STTx const>> const& txns,
    beast::Journal j,
    unsigned workers)
{
    auto const sched = scheduleApply(txns, closed);

    ScheduledApplyResult res;
    res.fullySerial = sched.fullySerial;

    if (sched.fullySerial)
    {
        res.groupCount = 1;
        auto [gv, applied] = applyGroupIsolated(registry, closed, sched.serial, j);
        gv.apply(to);
        res.applied = applied;
        return res;
    }

    res.groupCount = sched.groups.size();

    // Phase 1 — apply each group in isolation. Results are written into a
    // pre-sized, index-keyed slot vector so the subsequent merge order is the
    // (deterministic) schedule group order regardless of completion order.
    std::vector<std::optional<std::pair<OpenView, std::size_t>>> slots(sched.groups.size());

    unsigned const nThreads =
        std::min<unsigned>(std::max<unsigned>(workers, 1u), static_cast<unsigned>(slots.size()));

    if (nThreads <= 1)
    {
        for (std::size_t i = 0; i < sched.groups.size(); ++i)
            slots[i].emplace(applyGroupIsolated(registry, closed, sched.groups[i].txns, j));
    }
    else
    {
        std::atomic<std::size_t> next{0};
        auto worker = [&]() {
            for (std::size_t i = next.fetch_add(1); i < sched.groups.size();
                 i = next.fetch_add(1))
                slots[i].emplace(applyGroupIsolated(registry, closed, sched.groups[i].txns, j));
        };
        std::vector<std::thread> pool;
        pool.reserve(nThreads);
        for (unsigned t = 0; t < nThreads; ++t)
            pool.emplace_back(worker);
        for (auto& th : pool)
            th.join();
    }

    // Phase 2 — merge the disjoint write-sets into the target, in fixed group
    // order. Sequential by design: `to` is a single mutable ledger.
    for (auto& slot : slots)
    {
        slot->first.apply(to);
        res.applied += slot->second;
    }
    return res;
}

}  // namespace xrpl
