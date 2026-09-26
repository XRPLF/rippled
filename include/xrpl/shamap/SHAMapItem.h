#pragma once

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/SlabAllocator.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <boost/smart_ptr/intrusive_ptr.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>

namespace xrpl {

// an item stored in a SHAMap
class alignas(8) SHAMapItem : public CountedObject<SHAMapItem>
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
    makeShamapitem(uint256 const& tag, Slice data);

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
            reinterpret_cast<std::uint8_t*>(this) + sizeof(*this), data.data(), data.size());
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
inline constinit slab::Allocator<SHAMapItem, slab::HeapFallback,
    slab::Config<1000000, 128>,
    slab::Config<1000000, 296>,
    slab::Config<125000, 392>,
    slab::Config<125000, 520>,
    slab::Config<62500, 760>,
    slab::Config<62500, 856>,
    slab::Config<31250, 1048>
> slabber;
// clang-format on

}  // namespace detail

inline void
intrusive_ptr_add_ref(SHAMapItem const* x)
{
    // This can only happen if someone releases the last reference to the
    // item while we were trying to increment the refcount.
    if (x->refcount_++ == 0)
        logicError("SHAMapItem: the reference count is 0!");
}

inline void
intrusive_ptr_release(SHAMapItem const* x)
{
    if (--x->refcount_ == 0)
    {
        auto p = reinterpret_cast<std::uint8_t const*>(x);

        // The SHAMapItem constructor isn't trivial (because the destructor
        // for CountedObject isn't) so we can't avoid calling it here, but
        // plan for a future where we might not need to.
        if constexpr (!std::is_trivially_destructible_v<SHAMapItem>)
            std::destroy_at(x);

        // If the slabber doesn't claim this pointer, it was allocated
        // manually, so we free it manually.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        if (!detail::slabber.deallocate(const_cast<std::uint8_t*>(p)))
            delete[] p;
    }
}

inline boost::intrusive_ptr<SHAMapItem>
makeShamapitem(uint256 const& tag, Slice data)
{
    XRPL_ASSERT(
        data.size() <= megabytes<std::size_t>(16), "xrpl::makeShamapitem : maximum input size");

    // We do not increment the reference count here on purpose: the
    // constructor of SHAMapItem explicitly sets it to 1.
    if (auto raw = detail::slabber.allocate(data.size())) [[likely]]
        return {new (raw) SHAMapItem{tag, data}, false};

    return nullptr;
}

static_assert(alignof(SHAMapItem) != 40);
static_assert(alignof(SHAMapItem) == 8 || alignof(SHAMapItem) == 4);

inline boost::intrusive_ptr<SHAMapItem>
makeShamapitem(SHAMapItem const& other)
{
    return makeShamapitem(other.key(), other.slice());
}

}  // namespace xrpl
