#include <xrpl/ledger/detail/PersistentOrderTree.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <map>
#include <random>
#include <utility>
#include <vector>

namespace xrpl::detail {

namespace {

// 256-bit value whose numeric order matches integer order (n written
// big-endian into the low 8 bytes; base_uint compares MSB-first).
uint256
u256(std::uint64_t n)
{
    uint256 k;
    std::memset(k.data(), 0, k.size());
    auto* end = k.data() + k.size();
    for (int i = 0; i < 8; ++i)
        end[-1 - i] = static_cast<unsigned char>((n >> (8 * i)) & 0xff);
    return k;
}

using RefKey = std::pair<uint256, std::uint64_t>;  // (dirRoot, insertSeq)

// Reference inorder: std::map orders by (dirRoot, insertSeq); collect offers.
std::vector<uint256>
refInorder(std::map<RefKey, uint256> const& ref)
{
    std::vector<uint256> out;
    out.reserve(ref.size());
    for (auto const& [k, off] : ref)
        out.push_back(off);
    return out;
}

std::vector<uint256>
treeInorder(OrderTreePtr const& t)
{
    std::vector<uint256> out;
    otInorder(t, out);
    return out;
}

int
height(OrderTreePtr const& t)
{
    if (!t)
        return 0;
    return 1 + std::max(height(t->left), height(t->right));
}

// Verify subtree size fields are consistent.
std::uint32_t
checkSize(OrderTreePtr const& t)
{
    if (!t)
        return 0;
    auto const s = checkSize(t->left) + checkSize(t->right) + 1;
    EXPECT_EQ(s, t->size);
    return s;
}

}  // namespace

TEST(PersistentOrderTree, MatchesStdMapUnderRandomOps)
{
    std::mt19937_64 rng(0xC0FFEEu);  // fixed seed → deterministic
    std::map<RefKey, uint256> ref;
    OrderTreePtr tree;

    // A small set of dirRoots (quality levels) so levels hold multiple offers,
    // exercising within-level ordering and the dirRoot-range delete search.
    constexpr std::uint64_t kDirs = 8;
    std::uint64_t seqCounter = 0;
    std::uint64_t offerCounter = 0;
    std::vector<RefKey> live;

    for (int op = 0; op < 4000; ++op)
    {
        bool const doInsert = live.empty() || (rng() % 100) < 60;
        if (doInsert)
        {
            uint256 const dir = u256(rng() % kDirs);
            std::uint64_t const seq = ++seqCounter;  // unique → unique key
            uint256 const off = u256(1'000'000 + (++offerCounter));
            RefKey const key{dir, seq};
            ref.emplace(key, off);
            tree = otInsert(tree, dir, seq, off);
            live.push_back(key);
        }
        else
        {
            // Delete a random live key by (dirRoot, offerKey) lookup, exactly
            // like OrderBookIndex::deleteOffer does.
            auto const idx = rng() % live.size();
            RefKey const key = live[idx];
            uint256 const off = ref.at(key);

            auto const foundSeq = otFindSeq(tree, key.first, off);
            ASSERT_TRUE(foundSeq.has_value());
            EXPECT_EQ(*foundSeq, key.second);

            tree = otDelete(tree, key.first, *foundSeq);
            ref.erase(key);
            live[idx] = live.back();
            live.pop_back();
        }

        // Inorder equivalence after every op.
        ASSERT_EQ(treeInorder(tree), refInorder(ref));
        // Size field integrity + element count.
        EXPECT_EQ(otSize(tree), ref.size());
        checkSize(tree);
        // first == reference begin's offer.
        if (ref.empty())
            EXPECT_FALSE(otFirst(tree).has_value());
        else
            EXPECT_EQ(otFirst(tree), ref.begin()->second);
    }

    // Weight-balanced height stays logarithmic (loose bound).
    auto const n = otSize(tree);
    if (n > 0)
        EXPECT_LE(height(tree), 3 * (static_cast<int>(std::log2(n)) + 1) + 3);
}

TEST(PersistentOrderTree, StructuralSharingImmutability)
{
    OrderTreePtr base;
    for (std::uint64_t i = 0; i < 200; ++i)
        base = otInsert(base, u256(i % 4), i + 1, u256(10'000 + i));

    auto const before = treeInorder(base);

    // Mutate copies; the captured `base` must be unaffected (immutable nodes).
    auto inserted = otInsert(base, u256(2), 99'999, u256(42));
    auto deleted = otDelete(base, u256(0), 1);

    EXPECT_EQ(treeInorder(base), before);             // base unchanged by insert
    EXPECT_EQ(otSize(inserted), otSize(base) + 1);    // derived tree grew
    EXPECT_EQ(otSize(deleted), otSize(base) - 1);     // derived tree shrank
    EXPECT_EQ(treeInorder(base), before);             // base unchanged by delete
}

}  // namespace xrpl::detail
