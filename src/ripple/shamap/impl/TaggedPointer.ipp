//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#include <ripple/shamap/impl/TaggedPointer.h>

#include <ripple/shamap/SHAMapInnerNode.h>

#include <array>

// #define FORCE_BOOST_POOL 1
#if FORCE_BOOST_POOL || !__has_include(<memory_resource>)
#define USE_BOOST_POOL 1
#else
#define USE_BOOST_POOL 0
#endif

#if USE_BOOST_POOL
#include <boost/pool/pool_alloc.hpp>
#else
#include <memory_resource>
#endif

namespace ripple {

namespace {
// Sparse array size boundaries.
// Given n children, an array of size `*std::lower_bound(boundaries.begin(),
// boundaries.end(), n);` is used to store the children. Note that the last
// element must be the number of children in a dense array.
constexpr std::array<std::uint8_t, 4> boundaries{
    2,
    4,
    6,
    SHAMapInnerNode::branchFactor};
static_assert(
    boundaries.size() <= 4,
    "The hashesAndChildren member uses a tagged array format with two bits "
    "reserved for the tag. This supports at most 4 values.");
static_assert(
    boundaries.back() == SHAMapInnerNode::branchFactor,
    "Last element of boundaries must be number of children in a dense array");

// Terminology: A chunk is the memory being allocated from a block. A block
// contains multiple chunks. This is the terminology the boost documentation
// uses. Pools use "Simple Segregated Storage" as their storage format.
constexpr size_t elementSizeBytes =
    (sizeof(SHAMapHash) + sizeof(std::shared_ptr<SHAMapTreeNode>));

constexpr size_t blockSizeBytes = kilobytes(512);

template <std::size_t... I>
constexpr std::array<size_t, boundaries.size()> initArrayChunkSizeBytes(
    std::index_sequence<I...>)
{
    return std::array<size_t, boundaries.size()>{
        boundaries[I] * elementSizeBytes...,
    };
}
constexpr auto arrayChunkSizeBytes =
    initArrayChunkSizeBytes(std::make_index_sequence<boundaries.size()>{});

template <std::size_t... I>
constexpr std::array<size_t, boundaries.size()> initArrayChunksPerBlock(
    std::index_sequence<I...>)
{
    return std::array<size_t, boundaries.size()>{
        blockSizeBytes / arrayChunkSizeBytes[I]...,
    };
}
constexpr auto chunksPerBlock =
    initArrayChunksPerBlock(std::make_index_sequence<boundaries.size()>{});

[[nodiscard]] inline std::uint8_t
numAllocatedChildren(std::uint8_t n)
{
    assert(n <= SHAMapInnerNode::branchFactor);
    return *std::lower_bound(boundaries.begin(), boundaries.end(), n);
}

[[nodiscard]] inline std::size_t
boundariesIndex(std::uint8_t numChildren)
{
    assert(numChildren <= SHAMapInnerNode::branchFactor);
    return std::distance(
        boundaries.begin(),
        std::lower_bound(boundaries.begin(), boundaries.end(), numChildren));
}

#if USE_BOOST_POOL

template <std::size_t... I>
std::array<std::function<void*()>, boundaries.size()> initAllocateArrayFuns(
    std::index_sequence<I...>)
{
    return std::array<std::function<void*()>, boundaries.size()>{
        boost::singleton_pool<
            boost::fast_pool_allocator_tag,
            arrayChunkSizeBytes[I],
            boost::default_user_allocator_new_delete,
            std::mutex,
            chunksPerBlock[I],
            chunksPerBlock[I]>::malloc...,
    };
}
std::array<std::function<void*()>, boundaries.size()> const allocateArrayFuns =
    initAllocateArrayFuns(std::make_index_sequence<boundaries.size()>{});

template <std::size_t... I>
std::array<std::function<void(void*)>, boundaries.size()> initFreeArrayFuns(
    std::index_sequence<I...>)
{
    return std::array<std::function<void(void*)>, boundaries.size()>{
        static_cast<void (*)(void*)>(boost::singleton_pool<
                                     boost::fast_pool_allocator_tag,
                                     arrayChunkSizeBytes[I],
                                     boost::default_user_allocator_new_delete,
                                     std::mutex,
                                     chunksPerBlock[I],
                                     chunksPerBlock[I]>::free)...,
    };
}
std::array<std::function<void(void*)>, boundaries.size()> const freeArrayFuns =
    initFreeArrayFuns(std::make_index_sequence<boundaries.size()>{});

template <std::size_t... I>
std::array<std::function<bool(void*)>, boundaries.size()> initIsFromArrayFuns(
    std::index_sequence<I...>)
{
    return std::array<std::function<bool(void*)>, boundaries.size()>{
        boost::singleton_pool<
            boost::fast_pool_allocator_tag,
            arrayChunkSizeBytes[I],
            boost::default_user_allocator_new_delete,
            std::mutex,
            chunksPerBlock[I],
            chunksPerBlock[I]>::is_from...,
    };
}
std::array<std::function<bool(void*)>, boundaries.size()> const
    isFromArrayFuns =
        initIsFromArrayFuns(std::make_index_sequence<boundaries.size()>{});

// This function returns an untagged pointer
[[nodiscard]] inline std::pair<std::uint8_t, void*>
allocateArrays(std::uint8_t numChildren)
{
    auto const i = boundariesIndex(numChildren);
    return {i, allocateArrayFuns[i]()};
}

// This function takes an untagged pointer
inline void
deallocateArrays(std::uint8_t boundaryIndex, void* p)
{
    assert(isFromArrayFuns[boundaryIndex](p));
    freeArrayFuns[boundaryIndex](p);
}
#else

template <std::size_t... I>
std::array<std::pmr::synchronized_pool_resource, boundaries.size()>
    initPmrArrayFuns(std::index_sequence<I...>)
{
    return std::array<std::pmr::synchronized_pool_resource, boundaries.size()>{
        std::pmr::synchronized_pool_resource{std::pmr::pool_options{
            /* max_blocks_per_chunk */ chunksPerBlock[I],
            /* largest_required_pool_block */ chunksPerBlock[I]}}...,
    };
}
std::array<std::pmr::synchronized_pool_resource, boundaries.size()>
    pmrArrayFuns =
        initPmrArrayFuns(std::make_index_sequence<boundaries.size()>{});

// This function returns an untagged pointer
[[nodiscard]] inline std::pair<std::uint8_t, void*>
allocateArrays(std::uint8_t numChildren)
{
    auto const i = boundariesIndex(numChildren);
    return {i, pmrArrayFuns[i].allocate(arrayChunkSizeBytes[i])};
}

// This function takes an untagged pointer
inline void
deallocateArrays(std::uint8_t boundaryIndex, void* p)
{
    return pmrArrayFuns[boundaryIndex].deallocate(
        p, arrayChunkSizeBytes[boundaryIndex]);
}
#endif

[[nodiscard]] inline int
popcnt16(std::uint16_t a)
{
#if defined(__clang__) || defined(__GNUC__)
    return __builtin_popcount(a);
#else
    // fallback to table lookup
    static auto constexpr const tbl = []() {
        std::array<std::uint8_t, 256> ret{};
        for (int i = 0; i != 256; ++i)
        {
            for (int j = 0; j != 8; ++j)
            {
                if (i & (1 << j))
                    ret[i]++;
            }
        }
        return ret;
    }();
    return tbl[a & 0xff] + tbl[a >> 8];
#endif
}

// Used in `iterChildren` and elsewhere as the hash value for sparse arrays when
// the hash isn't actually stored in the array.
static SHAMapHash const zeroSHAMapHash;

}  // namespace

template <class F>
void
TaggedPointer::iterChildren(std::uint16_t isBranch, F&& f) const
{
    auto [numAllocated, hashes, _] = getHashesAndChildren();
    if (numAllocated == SHAMapInnerNode::branchFactor)
    {
        // dense case
        for (int i = 0; i < SHAMapInnerNode::branchFactor; ++i)
            f(hashes[i]);
    }
    else
    {
        // sparse case
        int curHashI = 0;
        for (int i = 0; i < SHAMapInnerNode::branchFactor; ++i)
        {
            if ((1 << i) & isBranch)
            {
                f(hashes[curHashI++]);
            }
            else
            {
                f(zeroSHAMapHash);
            }
        }
    }
}

template <class F>
void
TaggedPointer::iterNonEmptyChildIndexes(std::uint16_t isBranch, F&& f) const
{
    if (capacity() == SHAMapInnerNode::branchFactor)
    {
        // dense case
        for (int i = 0; i < SHAMapInnerNode::branchFactor; ++i)
        {
            if ((1 << i) & isBranch)
            {
                f(i, i);
            }
        }
    }
    else
    {
        // sparse case
        int curHashI = 0;
        for (int i = 0; i < SHAMapInnerNode::branchFactor; ++i)
        {
            if ((1 << i) & isBranch)
            {
                f(i, curHashI++);
            }
        }
    }
}

inline void
TaggedPointer::destroyHashesAndChildren()
{
    if (!tp_)
        return;

    auto [numAllocated, hashes, children] = getHashesAndChildren();
    for (std::size_t i = 0; i < numAllocated; ++i)
    {
        hashes[i].~SHAMapHash();
        children[i].~shared_ptr<SHAMapTreeNode>();
    }

    auto [tag, ptr] = decode();
    deallocateArrays(tag, ptr);
}

inline std::optional<int>
TaggedPointer::getChildIndex(std::uint16_t isBranch, int i) const
{
    if (isDense())
        return i;

    // Sparse case
    if ((isBranch & (1 << i)) == 0)
    {
        // Empty branch. Sparse children do not store empty branches
        return {};
    }

    // Sparse children are stored sorted. This means the index
    // of a child in the array is the number of non-empty children
    // before it. Since `isBranch_` is a bitset of the stored
    // children, we simply need to mask out (and set to zero) all
    // the bits in `isBranch_` equal to to higher than `i` and count
    // the bits.

    // mask sets all the bits >=i to zero and all the bits <i to
    // one.
    auto const mask = (1 << i) - 1;
    return popcnt16(isBranch & mask);
}

inline TaggedPointer::TaggedPointer(RawAllocateTag, std::uint8_t numChildren)
{
    auto [tag, p] = allocateArrays(numChildren);
    assert(tag < boundaries.size());
    assert(
        (reinterpret_cast<std::uintptr_t>(p) & ptrMask) ==
        reinterpret_cast<std::uintptr_t>(p));
    tp_ = reinterpret_cast<std::uintptr_t>(p) + tag;
}

inline TaggedPointer::TaggedPointer(
    TaggedPointer&& other,
    std::uint16_t srcBranches,
    std::uint16_t dstBranches,
    std::uint8_t toAllocate)
{
    assert(toAllocate >= popcnt16(dstBranches));

    if (other.capacity() == numAllocatedChildren(toAllocate))
    {
        // in place
        *this = std::move(other);
        auto [srcDstNumAllocated, srcDstHashes, srcDstChildren] =
            getHashesAndChildren();
        bool const srcDstIsDense = isDense();
        int srcDstIndex = 0;
        for (int i = 0; i < SHAMapInnerNode::branchFactor; ++i)
        {
            auto const mask = (1 << i);
            bool const inSrc = (srcBranches & mask);
            bool const inDst = (dstBranches & mask);
            if (inSrc && inDst)
            {
                // keep
                ++srcDstIndex;
            }
            else if (inSrc && !inDst)
            {
                // remove
                if (srcDstIsDense)
                {
                    srcDstHashes[srcDstIndex].zero();
                    srcDstChildren[srcDstIndex].reset();
                    ++srcDstIndex;
                }
                else
                {
                    // sparse
                    // need to shift all the elements to the left by
                    // one
                    for (int c = srcDstIndex; c < srcDstNumAllocated - 1; ++c)
                    {
                        srcDstHashes[c] = srcDstHashes[c + 1];
                        srcDstChildren[c] = std::move(srcDstChildren[c + 1]);
                    }
                    srcDstHashes[srcDstNumAllocated - 1].zero();
                    srcDstChildren[srcDstNumAllocated - 1].reset();
                    // do not increment the index
                }
            }
            else if (!inSrc && inDst)
            {
                // add
                if (srcDstIsDense)
                {
                    // nothing to do, child is already present in the dense rep
                    ++srcDstIndex;
                }
                else
                {
                    // sparse
                    // need to create a hole by shifting all the elements to the
                    // right by one
                    for (int c = srcDstNumAllocated - 1; c > srcDstIndex; --c)
                    {
                        srcDstHashes[c] = srcDstHashes[c - 1];
                        srcDstChildren[c] = std::move(srcDstChildren[c - 1]);
                    }
                    srcDstHashes[srcDstIndex].zero();
                    srcDstChildren[srcDstIndex].reset();
                    ++srcDstIndex;
                }
            }
            else if (!inDst && !inDst)
            {
                // in neither
                if (srcDstIsDense)
                {
                    ++srcDstIndex;
                }
            }
        }
    }
    else
    {
        // not in place
        TaggedPointer dst{RawAllocateTag{}, toAllocate};
        auto [dstNumAllocated, dstHashes, dstChildren] =
            dst.getHashesAndChildren();
        // Move `other` into a local var so it's not in a partially moved from
        // state after this function runs
        TaggedPointer src(std::move(other));
        auto [srcNumAllocated, srcHashes, srcChildren] =
            src.getHashesAndChildren();
        bool const srcIsDense = src.isDense();
        bool const dstIsDense = dst.isDense();
        int srcIndex = 0, dstIndex = 0;
        for (int i = 0; i < SHAMapInnerNode::branchFactor; ++i)
        {
            auto const mask = (1 << i);
            bool const inSrc = (srcBranches & mask);
            bool const inDst = (dstBranches & mask);
            if (inSrc && inDst)
            {
                // keep
                new (&dstHashes[dstIndex]) SHAMapHash{srcHashes[srcIndex]};
                new (&dstChildren[dstIndex]) std::shared_ptr<SHAMapTreeNode>{
                    std::move(srcChildren[srcIndex])};
                ++dstIndex;
                ++srcIndex;
            }
            else if (inSrc && !inDst)
            {
                // remove
                ++srcIndex;
                if (dstIsDense)
                {
                    new (&dstHashes[dstIndex]) SHAMapHash{};
                    new (&dstChildren[dstIndex])
                        std::shared_ptr<SHAMapTreeNode>{};
                    ++dstIndex;
                }
            }
            else if (!inSrc && inDst)
            {
                // add
                new (&dstHashes[dstIndex]) SHAMapHash{};
                new (&dstChildren[dstIndex]) std::shared_ptr<SHAMapTreeNode>{};
                ++dstIndex;
                if (srcIsDense)
                {
                    ++srcIndex;
                }
            }
            else if (!inDst && !inDst)
            {
                // in neither
                if (dstIsDense)
                {
                    new (&dstHashes[dstIndex]) SHAMapHash{};
                    new (&dstChildren[dstIndex])
                        std::shared_ptr<SHAMapTreeNode>{};
                    ++dstIndex;
                }
                if (srcIsDense)
                {
                    ++srcIndex;
                }
            }
        }
        // If sparse, may need to run additional constructors
        assert(!dstIsDense || dstIndex == dstNumAllocated);
        for (int i = dstIndex; i < dstNumAllocated; ++i)
        {
            new (&dstHashes[i]) SHAMapHash{};
            new (&dstChildren[i]) std::shared_ptr<SHAMapTreeNode>{};
        }
        *this = std::move(dst);
    }
}

inline TaggedPointer::TaggedPointer(
    TaggedPointer&& other,
    std::uint16_t isBranch,
    std::uint8_t toAllocate)
    : TaggedPointer(std::move(other))
{
    auto const oldNumAllocated = capacity();
    toAllocate = numAllocatedChildren(toAllocate);
    if (toAllocate == oldNumAllocated)
        return;

    // allocate hashes and children, but do not run constructors
    TaggedPointer newHashesAndChildren{RawAllocateTag{}, toAllocate};
    SHAMapHash *newHashes, *oldHashes;
    std::shared_ptr<SHAMapTreeNode>*newChildren, *oldChildren;
    std::uint8_t newNumAllocated;
    // structured bindings can't be captured in c++ 17; use tie instead
    std::tie(newNumAllocated, newHashes, newChildren) =
        newHashesAndChildren.getHashesAndChildren();
    std::tie(std::ignore, oldHashes, oldChildren) = getHashesAndChildren();

    if (newNumAllocated == SHAMapInnerNode::branchFactor)
    {
        // new arrays are dense, old arrays are sparse
        iterNonEmptyChildIndexes(isBranch, [&](auto branchNum, auto indexNum) {
            new (&newHashes[branchNum]) SHAMapHash{oldHashes[indexNum]};
            new (&newChildren[branchNum]) std::shared_ptr<SHAMapTreeNode>{
                std::move(oldChildren[indexNum])};
        });
        // Run the constructors for the remaining elements
        for (int i = 0; i < SHAMapInnerNode::branchFactor; ++i)
        {
            if ((1 << i) & isBranch)
                continue;
            new (&newHashes[i]) SHAMapHash{};
            new (&newChildren[i]) std::shared_ptr<SHAMapTreeNode>{};
        }
    }
    else
    {
        // new arrays are sparse, old arrays may be sparse or dense
        int curCompressedIndex = 0;
        iterNonEmptyChildIndexes(isBranch, [&](auto branchNum, auto indexNum) {
            new (&newHashes[curCompressedIndex])
                SHAMapHash{oldHashes[indexNum]};
            new (&newChildren[curCompressedIndex])
                std::shared_ptr<SHAMapTreeNode>{
                    std::move(oldChildren[indexNum])};
            ++curCompressedIndex;
        });
        // Run the constructors for the remaining elements
        for (int i = curCompressedIndex; i < newNumAllocated; ++i)
        {
            new (&newHashes[i]) SHAMapHash{};
            new (&newChildren[i]) std::shared_ptr<SHAMapTreeNode>{};
        }
    }

    *this = std::move(newHashesAndChildren);
}

inline TaggedPointer::TaggedPointer(std::uint8_t numChildren)
    : TaggedPointer(TaggedPointer::RawAllocateTag{}, numChildren)
{
    auto [numAllocated, hashes, children] = getHashesAndChildren();
    for (std::size_t i = 0; i < numAllocated; ++i)
    {
        new (&hashes[i]) SHAMapHash{};
        new (&children[i]) std::shared_ptr<SHAMapTreeNode>{};
    }
}

inline TaggedPointer::TaggedPointer(TaggedPointer&& other) : tp_{other.tp_}
{
    other.tp_ = 0;
}

inline TaggedPointer&
TaggedPointer::operator=(TaggedPointer&& other)
{
    if (this == &other)
        return *this;
    destroyHashesAndChildren();
    tp_ = other.tp_;
    other.tp_ = 0;
    return *this;
}

[[nodiscard]] inline std::pair<std::uint8_t, void*>
TaggedPointer::decode() const
{
    return {tp_ & tagMask, reinterpret_cast<void*>(tp_ & ptrMask)};
}

[[nodiscard]] inline std::uint8_t
TaggedPointer::capacity() const
{
    return boundaries[tp_ & tagMask];
}

[[nodiscard]] inline bool
TaggedPointer::isDense() const
{
    return (tp_ & tagMask) == boundaries.size() - 1;
}

[[nodiscard]] inline std::
    tuple<std::uint8_t, SHAMapHash*, std::shared_ptr<SHAMapTreeNode>*>
    TaggedPointer::getHashesAndChildren() const
{
    auto const [tag, ptr] = decode();
    auto const hashes = reinterpret_cast<SHAMapHash*>(ptr);
    std::uint8_t numAllocated = boundaries[tag];
    auto const children = reinterpret_cast<std::shared_ptr<SHAMapTreeNode>*>(
        hashes + numAllocated);
    return {numAllocated, hashes, children};
};

[[nodiscard]] inline SHAMapHash*
TaggedPointer::getHashes() const
{
    return reinterpret_cast<SHAMapHash*>(tp_ & ptrMask);
};

[[nodiscard]] inline std::shared_ptr<SHAMapTreeNode>*
TaggedPointer::getChildren() const
{
    auto [unused1, unused2, result] = getHashesAndChildren();
    return result;
};

}  // namespace ripple
