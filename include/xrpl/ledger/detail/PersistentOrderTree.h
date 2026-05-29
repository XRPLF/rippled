#pragma once

#include <xrpl/basics/base_uint.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace xrpl::detail {

/** Persistent (immutable, structurally-shared) ordered tree for the order-book
    index.

    A weight-balanced BST (Adams BB[α], the family used by Haskell `Data.Map`
    and std::map-replacement libraries) of immutable `shared_ptr<const Node>`.
    Keyed by `(dirRoot, insertSeq)`:

      - `dirRoot` ascending == best-quality-first (book directory pages share a
        prefix, quality is in the low bytes — lower key = better quality).
      - `insertSeq` ascending within a `dirRoot` == directory append order
        (the per-book monotonic counter mirrors `dirAppend`; `dirRemove`
        preserves relative order, so this reproduces the directory walk
        byte-for-byte).

    Operations are persistent via path-copying: insert/delete reallocate only
    the O(log n) nodes on the root→leaf path and SHARE every untouched subtree.
    A "copy" of a tree is just copying the root `shared_ptr` — O(1) — which is
    what lets the order-book index survive the open-ledger copy-on-write cheaply
    and stay warm across transactions.

    Immutable nodes are safe to share across threads/snapshots without locking.
*/
struct OrderTreeNode
{
    uint256 dirRoot;
    std::uint64_t insertSeq;
    uint256 offerKey;
    std::uint32_t size;  // subtree node count (balance + rank)
    std::shared_ptr<OrderTreeNode const> left;
    std::shared_ptr<OrderTreeNode const> right;
};

using OrderTreePtr = std::shared_ptr<OrderTreeNode const>;

// Weight-balance parameters (Adams). delta bounds the size ratio between
// siblings; gamma chooses single vs double rotation.
inline constexpr std::uint32_t kOtDelta = 3;
inline constexpr std::uint32_t kOtGamma = 2;

[[nodiscard]] inline std::uint32_t
otSize(OrderTreePtr const& t) noexcept
{
    return t ? t->size : 0;
}

// -1 / 0 / +1 ordering on (dirRoot, insertSeq).
[[nodiscard]] inline int
otCmp(
    uint256 const& aDir,
    std::uint64_t aSeq,
    uint256 const& bDir,
    std::uint64_t bSeq) noexcept
{
    if (aDir < bDir)
        return -1;
    if (bDir < aDir)
        return 1;
    if (aSeq < bSeq)
        return -1;
    if (bSeq < aSeq)
        return 1;
    return 0;
}

[[nodiscard]] inline OrderTreePtr
otNode(
    uint256 const& dir,
    std::uint64_t seq,
    uint256 const& off,
    OrderTreePtr l,
    OrderTreePtr r)
{
    auto n = std::make_shared<OrderTreeNode>();
    n->dirRoot = dir;
    n->insertSeq = seq;
    n->offerKey = off;
    n->left = std::move(l);
    n->right = std::move(r);
    n->size = otSize(n->left) + otSize(n->right) + 1;
    return n;
}

// Rebalance a node whose subtrees may violate the weight balance by one step.
[[nodiscard]] inline OrderTreePtr
otBalance(
    uint256 const& dir,
    std::uint64_t seq,
    uint256 const& off,
    OrderTreePtr const& l,
    OrderTreePtr const& r)
{
    auto const ln = otSize(l);
    auto const rn = otSize(r);

    if (ln + rn <= 1)
        return otNode(dir, seq, off, l, r);

    if (rn > kOtDelta * ln)
    {
        // Right-heavy.
        auto const& rl = r->left;
        auto const& rr = r->right;
        if (otSize(rl) < kOtGamma * otSize(rr))
            // single left rotation
            return otNode(
                r->dirRoot,
                r->insertSeq,
                r->offerKey,
                otNode(dir, seq, off, l, rl),
                rr);
        // double left rotation
        return otNode(
            rl->dirRoot,
            rl->insertSeq,
            rl->offerKey,
            otNode(dir, seq, off, l, rl->left),
            otNode(r->dirRoot, r->insertSeq, r->offerKey, rl->right, rr));
    }

    if (ln > kOtDelta * rn)
    {
        // Left-heavy.
        auto const& ll = l->left;
        auto const& lr = l->right;
        if (otSize(lr) < kOtGamma * otSize(ll))
            // single right rotation
            return otNode(
                l->dirRoot,
                l->insertSeq,
                l->offerKey,
                ll,
                otNode(dir, seq, off, lr, r));
        // double right rotation
        return otNode(
            lr->dirRoot,
            lr->insertSeq,
            lr->offerKey,
            otNode(l->dirRoot, l->insertSeq, l->offerKey, ll, lr->left),
            otNode(dir, seq, off, lr->right, r));
    }

    return otNode(dir, seq, off, l, r);
}

[[nodiscard]] inline OrderTreePtr
otInsert(OrderTreePtr const& t, uint256 const& dir, std::uint64_t seq, uint256 const& off)
{
    if (!t)
        return otNode(dir, seq, off, nullptr, nullptr);
    int const c = otCmp(dir, seq, t->dirRoot, t->insertSeq);
    if (c < 0)
        return otBalance(
            t->dirRoot, t->insertSeq, t->offerKey, otInsert(t->left, dir, seq, off), t->right);
    if (c > 0)
        return otBalance(
            t->dirRoot, t->insertSeq, t->offerKey, t->left, otInsert(t->right, dir, seq, off));
    // Equal key: replace payload (keys are unique in practice; never hit).
    return otNode(t->dirRoot, t->insertSeq, off, t->left, t->right);
}

// Remove the minimum node of a non-null tree; write its fields into `outMin`.
[[nodiscard]] inline OrderTreePtr
otDeleteMin(OrderTreePtr const& t, OrderTreeNode& outMin)
{
    if (!t->left)
    {
        outMin = *t;
        return t->right;
    }
    return otBalance(
        t->dirRoot, t->insertSeq, t->offerKey, otDeleteMin(t->left, outMin), t->right);
}

// Join two subtrees (all keys in l < all keys in r) by promoting r's minimum.
[[nodiscard]] inline OrderTreePtr
otGlue(OrderTreePtr const& l, OrderTreePtr const& r)
{
    if (!l)
        return r;
    if (!r)
        return l;
    OrderTreeNode minN;
    auto const r2 = otDeleteMin(r, minN);
    return otBalance(minN.dirRoot, minN.insertSeq, minN.offerKey, l, r2);
}

[[nodiscard]] inline OrderTreePtr
otDelete(OrderTreePtr const& t, uint256 const& dir, std::uint64_t seq)
{
    if (!t)
        return nullptr;
    int const c = otCmp(dir, seq, t->dirRoot, t->insertSeq);
    if (c < 0)
        return otBalance(
            t->dirRoot, t->insertSeq, t->offerKey, otDelete(t->left, dir, seq), t->right);
    if (c > 0)
        return otBalance(
            t->dirRoot, t->insertSeq, t->offerKey, t->left, otDelete(t->right, dir, seq));
    return otGlue(t->left, t->right);
}

// In-order traversal: appends offer keys best-quality-first, append order
// within a level.
inline void
otInorder(OrderTreePtr const& t, std::vector<uint256>& out)
{
    if (!t)
        return;
    otInorder(t->left, out);
    out.push_back(t->offerKey);
    otInorder(t->right, out);
}

// Leftmost (best) offer key.
[[nodiscard]] inline std::optional<uint256>
otFirst(OrderTreePtr t)
{
    if (!t)
        return std::nullopt;
    while (t->left)
        t = t->left;
    return t->offerKey;
}

// Find the insertSeq for (dirRoot, offerKey). All nodes sharing a dirRoot form
// a contiguous in-order range that may straddle a node's two children, so when
// dirRoot matches we must check the node and both subtrees. O(level-size) worst
// case; effectively O(log n) for front deletions (crossing consumes front-first
// and the target is then the level's leftmost remaining node).
[[nodiscard]] inline std::optional<std::uint64_t>
otFindSeq(OrderTreePtr const& t, uint256 const& dir, uint256 const& off)
{
    if (!t)
        return std::nullopt;
    if (dir < t->dirRoot)
        return otFindSeq(t->left, dir, off);
    if (t->dirRoot < dir)
        return otFindSeq(t->right, dir, off);
    if (t->offerKey == off)
        return t->insertSeq;
    if (auto const l = otFindSeq(t->left, dir, off))
        return l;
    return otFindSeq(t->right, dir, off);
}

}  // namespace xrpl::detail
