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

#ifndef RIPPLE_SHAMAP_SHAMAPITEM_H_INCLUDED
#define RIPPLE_SHAMAP_SHAMAPITEM_H_INCLUDED

#include <ripple/basics/ByteUtilities.h>
#include <ripple/basics/CountedObject.h>
#include <ripple/basics/SlabAllocator.h>
#include <ripple/basics/Slice.h>
#include <ripple/basics/base_uint.h>
#include <boost/smart_ptr/intrusive_ptr.hpp>
#include <cassert>

namespace ripple {

// an item stored in a SHAMap
class SHAMapItem : public CountedObject<SHAMapItem>
{
    // These are used to support boost::intrusive_ptr reference counting
    // These functions are used internally by boost::intrusive_ptr to handle
    // lifetime management.
    friend void
    intrusive_ptr_add_ref(SHAMapItem const* x);

    friend void
    intrusive_ptr_release(SHAMapItem const* x);

    // This is the interface for creating new instances of this class.
    friend boost::intrusive_ptr<SHAMapItem>
    make_shamapitem(uint256 const& tag, Slice data);

private:
    uint256 const tag_;

    // We use std::uint32_t to minimize the size; there's no SHAMapItem whose
    // size exceeds 4GB and there won't ever be (famous last words?), so this
    // is safe.
    std::uint32_t const size_;

    // This is the reference count used to support boost::intrusive_ptr
    mutable std::atomic<std::uint32_t> refcount_ = 1;

    // Because of the unusual way in which SHAMapItem objects are constructed
    // the only way to properly create one is to first allocate enough memory
    // so we limit this constructor to codepaths that do this right and limit
    // arbitrary construction.
    SHAMapItem(uint256 const& tag, Slice data)
        : tag_(tag), size_(static_cast<std::uint32_t>(data.size()))
    {
        std::memcpy(
            reinterpret_cast<std::uint8_t*>(this) + sizeof(*this),
            data.data(),
            data.size());
    }

public:
    SHAMapItem() = delete;

    SHAMapItem(SHAMapItem const& other) = delete;

    SHAMapItem&
    operator=(SHAMapItem const& other) = delete;

    SHAMapItem(SHAMapItem&& other) = delete;

    SHAMapItem&
    operator=(SHAMapItem&&) = delete;

    uint256 const&
    key() const
    {
        return tag_;
    }

    std::size_t
    size() const
    {
        return size_;
    }

    void const*
    data() const
    {
        return reinterpret_cast<std::uint8_t const*>(this) + sizeof(*this);
    }

    Slice
    slice() const
    {
        return {data(), size()};
    }
};

namespace detail {

// clang-format off
// The slab cutoffs and the number of megabytes per allocation are customized
// based on the number of objects of each size we expect to need at any point
// in time and with an eye to minimize the number of slack bytes in a block.
inline SlabAllocatorSet<SHAMapItem> slabber({
    {  128, megabytes(std::size_t(60)) },
    {  192, megabytes(std::size_t(46)) },
    {  272, megabytes(std::size_t(60)) },
    {  384, megabytes(std::size_t(56)) },
    {  564, megabytes(std::size_t(40)) },
    {  772, megabytes(std::size_t(46)) },
    { 1052, megabytes(std::size_t(60)) },
});
// clang-format on

}  // namespace detail

inline void
intrusive_ptr_add_ref(SHAMapItem const* x)
{
    // This can only happen if someone releases the last reference to the
    // item while we were trying to increment the refcount.
    if (x->refcount_++ == 0)
        LogicError("SHAMapItem: the reference count is 0!");
}

inline void
intrusive_ptr_release(SHAMapItem const* x)
{
    if (--x->refcount_ == 0)
    {
        auto p = reinterpret_cast<std::uint8_t const*>(x);

        // The SHAMapItem constuctor isn't trivial (because the destructor
        // for CountedObject isn't) so we can't avoid calling it here, but
        // plan for a future where we might not need to.
        if constexpr (!std::is_trivially_destructible_v<SHAMapItem>)
            std::destroy_at(x);

        // If the slabber doens't claim this pointer, it was allocated
        // manually, so we free it manually.
        if (!detail::slabber.deallocate(const_cast<std::uint8_t*>(p)))
            delete[] p;
    }
}

inline boost::intrusive_ptr<SHAMapItem>
make_shamapitem(uint256 const& tag, Slice data)
{
    assert(data.size() <= megabytes<std::size_t>(16));

    std::uint8_t* raw = detail::slabber.allocate(data.size());

    // If we can't grab memory from the slab allocators, we fall back to
    // the standard library and try to grab a precisely-sized memory block:
    if (raw == nullptr)
        raw = new std::uint8_t[sizeof(SHAMapItem) + data.size()];

    // We do not increment the reference count here on purpose: the
    // constructor of SHAMapItem explicitly sets it to 1. We use the fact
    // that the refcount can never be zero before incrementing as an
    // invariant.
    return {new (raw) SHAMapItem{tag, data}, false};
}

static_assert(alignof(SHAMapItem) != 40);
static_assert(alignof(SHAMapItem) == 8 || alignof(SHAMapItem) == 4);

inline boost::intrusive_ptr<SHAMapItem>
make_shamapitem(SHAMapItem const& other)
{
    return make_shamapitem(other.key(), other.slice());
}

}  // namespace ripple

#endif
