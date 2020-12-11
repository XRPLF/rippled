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

#ifndef RIPPLE_SHAMAP_TAGGEDPOINTER_H_INCLUDED
#define RIPPLE_SHAMAP_TAGGEDPOINTER_H_INCLUDED

#include <ripple/shamap/SHAMapTreeNode.h>

#include <cstdint>
#include <optional>

namespace ripple {

/** TaggedPointer is a combination of a pointer and a mask stored in the
    lowest two bits.

    Since pointers do not have arbitrary alignment, the lowest bits in the
    pointer are guaranteed to be zero. TaggedPointer stores information in these
    low bits. When dereferencing the pointer, these low "tag" bits are set to
    zero. When accessing the tag bits, the high "pointer" bits are set to zero.

    The "pointer" part points to to the equivalent to an array of
    `SHAMapHash` followed immediately by an array of
    `shared_ptr<SHAMapTreeNode>`. The sizes of these arrays are
    determined by the tag. The tag is an index into an array (`boundaries`,
    defined in the cpp file) that specifies the size. Both arrays are the
    same size. Note that the sizes may be smaller than the full 16 elements
    needed to explicitly store all the children. In this case, the arrays
    only store the non-empty children. The non-empty children are stored in
    index order. For example, if only children `2` and `14` are non-empty, a
    two-element array would store child `2` in array index 0 and child `14`
    in array index 1. There are functions to convert between a child's tree
    index and the child's index in a sparse array.

    The motivation for this class is saving RAM. A large percentage of inner
    nodes only store a small number of children. Memory can be saved by
    storing the inner node's children in sparse arrays. Measurements show
    that on average a typical SHAMap's inner nodes can be stored using only
    25% of the original space.
*/
class TaggedPointer
{
    static_assert(
        alignof(SHAMapHash) >= 4,
        "Bad alignment: Tag pointer requires low two bits to be zero.");
    /** Upper bits are the pointer, lowest two bits are the tag
        A moved-from object will have a tp_ of zero.
    */
    std::uintptr_t tp_ = 0;
    /** bit-and with this mask to get the tag bits (lowest two bits) */
    static constexpr std::uintptr_t tagMask = 3;
    /** bit-and with this mask to get the pointer bits (mask out the tag) */
    static constexpr std::uintptr_t ptrMask = ~tagMask;

    /** Deallocate memory and run destructors */
    void
    destroyHashesAndChildren();

    struct RawAllocateTag
    {
    };
    /** This constructor allocates space for the hashes and children, but
        does not run constructors.

        @param RawAllocateTag used to select overload only

        @param numChildren allocate space for at least this number of children
        (must be <= branchFactor)

        @note Since the hashes/children destructors are always run in the
        TaggedPointer destructor, this means those constructors _must_ be run
        after this constructor is run. This constructor is private and only used
        in places where the hashes/children constructor are subsequently run.
    */
    explicit TaggedPointer(RawAllocateTag, std::uint8_t numChildren);

public:
    TaggedPointer() = delete;
    explicit TaggedPointer(std::uint8_t numChildren);

    /** Constructor is used change the number of allocated children.

        Existing children from `other` are copied (toAllocate must be >= the
        number of children). The motivation for making this a constructor is it
        saves unneeded copying and zeroing out of hashes if this were
        implemented directly in the SHAMapInnerNode class.

        @param other children and hashes are moved from this param

        @param isBranch bitset of non-empty children in `other`

        @param toAllocate allocate space for at least this number of children
        (must be <= branchFactor)
    */
    explicit TaggedPointer(
        TaggedPointer&& other,
        std::uint16_t isBranch,
        std::uint8_t toAllocate);

    /** Given `other` with the specified children in `srcBranches`, create a
        new TaggedPointer with the allocated number of children and the
        children specified in `dstBranches`.

        @param other children and hashes are moved from this param

        @param srcBranches bitset of non-empty children in `other`

        @param dstBranches bitset of children to copy from `other` (or space to
        leave in a sparse array - see note below)

        @param toAllocate allocate space for at least this number of children
        (must be <= branchFactor)

        @note a child may be absent in srcBranches but present in dstBranches
        (if dst has a sparse representation, space for the new child will be
        left in the sparse array). Typically, srcBranches and dstBranches will
        differ by at most one bit. The function works correctly if they differ
        by more, but there are likely more efficient algorithms to consider if
        this becomes a common use-case.
    */
    explicit TaggedPointer(
        TaggedPointer&& other,
        std::uint16_t srcBranches,
        std::uint16_t dstBranches,
        std::uint8_t toAllocate);

    TaggedPointer(TaggedPointer const&) = delete;

    TaggedPointer(TaggedPointer&&);

    TaggedPointer&
    operator=(TaggedPointer&&);

    ~TaggedPointer();

    /** Decode the tagged pointer into its tag and pointer */
    [[nodiscard]] std::pair<std::uint8_t, void*>
    decode() const;

    /** Get the number of elements allocated for each array */
    [[nodiscard]] std::uint8_t
    capacity() const;

    /** Check if the arrays have a dense format.

        @note The dense format is when there is an array element for all 16
        (branchFactor) possible children.
    */
    [[nodiscard]] bool
    isDense() const;

    /** Get the number of elements in each array and a pointer to the start
        of each array.
    */
    [[nodiscard]] std::
        tuple<std::uint8_t, SHAMapHash*, std::shared_ptr<SHAMapTreeNode>*>
        getHashesAndChildren() const;

    /** Get the `hashes` array */
    [[nodiscard]] SHAMapHash*
    getHashes() const;

    /** Get the `children` array */
    [[nodiscard]] std::shared_ptr<SHAMapTreeNode>*
    getChildren() const;

    /** Call the `f` callback for all 16 (branchFactor) branches - even if
        the branch is empty.

        @param isBranch bitset of non-empty children

        @param f a one parameter callback function. The parameter is the
        child's hash.
     */
    template <class F>
    void
    iterChildren(std::uint16_t isBranch, F&& f) const;

    /** Call the `f` callback for all non-empty branches.

        @param isBranch bitset of non-empty children

        @param f a two parameter callback function. The first parameter is
        the branch number, the second parameter is the index into the array.
        For dense formats these are the same, for sparse they may be
        different.
     */
    template <class F>
    void
    iterNonEmptyChildIndexes(std::uint16_t isBranch, F&& f) const;

    /** Get the child's index inside the `hashes` or `children` array (which
        may or may not be sparse). The optional will be empty if an empty
        branch is requested and the children are sparse.

        @param isBranch bitset of non-empty children

        @param i index of the requested child
     */
    std::optional<int>
    getChildIndex(std::uint16_t isBranch, int i) const;
};

}  // namespace ripple

#endif
