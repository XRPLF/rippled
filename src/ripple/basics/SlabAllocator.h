//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright 2022, Nikolaos D. Bougalis <nikb@bougalis.net>

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

#ifndef RIPPLE_BASICS_SLABALLOCATOR_H_INCLUDED
#define RIPPLE_BASICS_SLABALLOCATOR_H_INCLUDED

#include <ripple/basics/ByteUtilities.h>
#include <ripple/beast/type_name.h>

#include <boost/align.hpp>
#include <boost/container/static_vector.hpp>
#include <boost/predef.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <mutex>

#if BOOST_OS_LINUX
#include <sys/mman.h>
#endif

namespace ripple {

template <typename Type>
class SlabAllocator
{
    static_assert(
        sizeof(Type) >= sizeof(std::uint8_t*),
        "SlabAllocator: the requested object must be larger than a pointer.");

    static_assert(alignof(Type) == 8 || alignof(Type) == 4);

    /** A block of memory that is owned by a slab allocator */
    struct SlabBlock
    {
        // A mutex to protect the freelist for this block:
        std::mutex m_;

        // A linked list of appropriately sized free buffers:
        std::uint8_t* list_ = nullptr;

        // The next block:
        SlabBlock* next_;

        // The underlying memory block:
        std::uint8_t* data_ = nullptr;

        // The number of items available in this block
        std::size_t avail_{0};

        explicit SlabBlock(SlabBlock* next) : next_(next)
        {
        }

        // We don't release the allocated memory here: slabs are only
        // destroyed when the process is shutting down, and we cannot
        // guarantee that this isn't called before the destruction of
        // all the objects that are allocated in the buffer we have.
        ~SlabBlock() = default;

        std::uint8_t*
        assign_and_allocate(
            std::uint8_t* data,
            std::size_t size,
            std::size_t item,
            std::size_t count)
        {
            assert(
                reinterpret_cast<std::uintptr_t>(data) %
                    alignof(decltype(data_)) ==
                0);

            assert(count == (size / item));

            assert(data != nullptr);

            std::lock_guard _(m_);

            // It's possible that this block has an underlying buffer. If it
            // does, and it has space available, use it.
            if (data_ != nullptr)
            {
                auto ret = list_;

                if (ret != nullptr)
                {
                    // We use memcpy to avoid UB for unaligned accesses
                    std::memcpy(&list_, ret, sizeof(std::uint8_t*));
                    --avail_;
                }

                return ret;
            }

            assert(data_ == nullptr);

            data_ = data;
            list_ = nullptr;

            // We're needing an allocation, so grab it now.
            auto ret = data;

            for (std::size_t i = 0; i != count - 1; ++i)
            {
                data += item;
                // We use memcpy to avoid UB for unaligned accesses
                std::memcpy(data, &list_, sizeof(std::uint8_t*));
                list_ = data;
            }

            avail_ = count - 1;

            return ret;
        }

        SlabBlock(SlabBlock const& other) = delete;
        SlabBlock&
        operator=(SlabBlock const& other) = delete;

        SlabBlock(SlabBlock&& other) = delete;
        SlabBlock&
        operator=(SlabBlock&& other) = delete;
    };

    // The first slab, which is always present.
    SlabBlock slab_;

    // A linked list of slabs
    std::atomic<SlabBlock*> slabs_ = nullptr;

    // A pointer to the slab we're attempting to allocate from
    std::atomic<SlabBlock*> active_ = nullptr;

    // The size of an item, including the extra bytes requested and
    // any padding needed for alignment purposes between items:
    std::size_t const itemSize_;

    // The maximum number of items that this slab can allocate
    std::size_t const itemCount_;

    // The size of each individual slab:
    std::size_t const slabSize_;

    std::uint8_t*
    allocate(SlabBlock* slab) noexcept
    {
        std::lock_guard l(slab->m_);

        std::uint8_t* ret = slab->list_;

        if (ret)
        {
            assert(
                (slab->avail_ != 0) && (slab->avail_ <= itemCount_) &&
                (ret >= slab->data_) &&
                (ret + itemSize_ <= slab->data_ + slabSize_));

            // We use memcpy to avoid UB for unaligned accesses
            std::memcpy(&slab->list_, ret, sizeof(std::uint8_t*));
            --slab->avail_;
        }

        return ret;
    }

    /** Return an item to this allocator's freelist.

        @param slab The slab in which to attempt to return the memory
        @param ptr The pointer to the chunk of memory being deallocated.

        @return true if the item belongs to the given slab and was freed, false
                     otherwise.
    */
    [[nodiscard]] bool
    deallocate(SlabBlock* slab, std::uint8_t* ptr) noexcept
    {
        assert(ptr != nullptr);

        {
            std::lock_guard l(slab->m_);

            if (auto const d = slab->data_; (ptr < d) || (ptr >= d + slabSize_))
                return false;

            // It looks like this objects belongs to us. Ensure that it
            // doesn't extend past the end of our buffer:
            assert(ptr + itemSize_ <= slab->data_ + slabSize_);

            // We use memcpy to avoid UB for unaligned accesses
            std::memcpy(ptr, &slab->list_, sizeof(std::uint8_t*));
            slab->list_ = ptr;

            if (++slab->avail_ != itemCount_)
                return true;

            // The slab is now fully unused and doesn't need its data block:
            ptr = std::exchange(slab->data_, nullptr);
            slab->list_ = nullptr;
            slab->avail_ = 0;
        }

        // Release the memory of this fully unused block back to the system
        // without holding a lock.
        boost::alignment::aligned_free(ptr);
        return true;
    }

public:
    /** Constructs a slab allocator able to allocate objects of a fixed size

        @param extra The number of extra bytes per item, on top of sizeof(Type)
        @param alloc The number of bytes to allocate for this slab
        @param align The alignment of returned pointers, normally alignof(Type)
     */
    constexpr explicit SlabAllocator(
        std::size_t extra,
        std::size_t alloc = 0,
        std::size_t align = alignof(Type))
        : slab_(nullptr)
        , slabs_(&slab_)
        , itemSize_(boost::alignment::align_up(sizeof(Type) + extra, align))
        , itemCount_(alloc / itemSize_)
        , slabSize_(alloc)
    {
        assert(std::has_single_bit(align));
    }

    SlabAllocator(SlabAllocator const& other) = delete;
    SlabAllocator&
    operator=(SlabAllocator const& other) = delete;

    SlabAllocator(SlabAllocator&& other) = delete;
    SlabAllocator&
    operator=(SlabAllocator&& other) = delete;

    // FIXME: We can't destroy the memory blocks we've allocated, because
    //        we can't be sure that they are not being used. Cleaning the
    //        shutdown process up could make this possible.
    ~SlabAllocator() = default;

    /** Returns the size of the memory block this allocator returns. */
    [[nodiscard]] constexpr std::size_t
    size() const noexcept
    {
        return itemSize_;
    }

    /** Returns a suitably aligned pointer, if one is available.

        @return a pointer to a block of memory from the allocator, or
                nullptr if the allocator can't satisfy this request.
     */
    [[nodiscard]] std::uint8_t*
    allocate() noexcept
    {
        auto active = active_.load();

        if (active)
        {
            if (auto ret = allocate(active))
                return ret;
        }

        for (auto slab = slabs_.load(); slab; slab = slab->next_)
        {
            if (auto ret = allocate(slab))
            {
                // This may fail, but that's OK.
                active_.compare_exchange_strong(active, slab);
                return ret;
            }
        }

        // No slab can satisfy our request, so we attempt to allocate a new
        // one. We align the block at a 2 MiB boundary to allow transparent
        // hugepage support on Linux.
        auto buf =
            reinterpret_cast<std::uint8_t*>(boost::alignment::aligned_alloc(
                megabytes(std::size_t(2)), slabSize_));

        if (!buf) [[unlikely]]
            return nullptr;

#if BOOST_OS_LINUX
        if (slabSize_ >= megabytes(std::size_t(4)))
            madvise(buf, slabSize_, MADV_HUGEPAGE);
#endif

        // Check whether there's an existing slab with no associated buffer
        // that we can give our newly allocated buffer to:
        for (auto slab = slabs_.load(std::memory_order_relaxed);
             slab != nullptr;
             slab = slab->next_)
        {
            if (auto ret = slab->assign_and_allocate(
                    buf, slabSize_, itemSize_, itemCount_))
            {
                // This slab had available space and we used it. The block
                // of memory we allocated is no longer needed.
                if (ret != buf)
                    boost::alignment::aligned_free(buf);

                // This may fail, but that's OK.
                active_.compare_exchange_weak(
                    active,
                    slab,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed);

                return ret;
            }
        }

        auto slab = new (std::nothrow)
            SlabBlock(slabs_.load(std::memory_order_relaxed));

        if (slab == nullptr) [[unlikely]]
        {
            boost::alignment::aligned_free(buf);
            return nullptr;
        }

        auto ret =
            slab->assign_and_allocate(buf, slabSize_, itemSize_, itemCount_);
        assert(ret == buf);

        // Finally, link the new slab
        while (!slabs_.compare_exchange_weak(
            slab->next_,
            slab,
            std::memory_order_release,
            std::memory_order_relaxed))
        {
            // Nothing to do
        }

        active_.store(slab, std::memory_order_relaxed);
        return ret;
    }

    /** Returns the memory block to the allocator.

        @param ptr A pointer to a memory block.
        @return true if this memory block belonged to the allocator and has
                     been released; false otherwise.
     */
    [[nodiscard]] bool
    deallocate(std::uint8_t* ptr) noexcept
    {
        assert(ptr);

        for (auto slab = slabs_.load(std::memory_order_relaxed);
             slab != nullptr;
             slab = slab->next_)
        {
            if (deallocate(slab, ptr))
                return true;
        }

        return false;
    }
};

/** A collection of slab allocators of various sizes for a given type. */
template <typename Type>
class SlabAllocatorSet
{
private:
    // The list of allocators that belong to this set
    boost::container::static_vector<SlabAllocator<Type>, 64> allocators_;

    std::size_t maxSize_ = 0;

public:
    class SlabConfig
    {
        friend class SlabAllocatorSet;

        std::size_t extra;
        std::size_t alloc;
        std::size_t align;

    public:
        constexpr SlabConfig(
            std::size_t extra_,
            std::size_t alloc_ = 0,
            std::size_t align_ = alignof(Type))
            : extra(extra_), alloc(alloc_), align(align_)
        {
        }
    };

    constexpr SlabAllocatorSet(std::vector<SlabConfig> cfg)
    {
        // Ensure that the specified allocators are sorted from smallest to
        // largest by size:
        std::sort(
            std::begin(cfg),
            std::end(cfg),
            [](SlabConfig const& a, SlabConfig const& b) {
                return a.extra < b.extra;
            });

        // We should never have two slabs of the same size
        if (std::adjacent_find(
                std::begin(cfg),
                std::end(cfg),
                [](SlabConfig const& a, SlabConfig const& b) {
                    return a.extra == b.extra;
                }) != cfg.end())
        {
            throw std::runtime_error(
                "SlabAllocatorSet<" + beast::type_name<Type>() +
                ">: duplicate slab size");
        }

        for (auto const& c : cfg)
        {
            auto& a = allocators_.emplace_back(c.extra, c.alloc, c.align);

            if (a.size() > maxSize_)
                maxSize_ = a.size();
        }
    }

    SlabAllocatorSet(SlabAllocatorSet const& other) = delete;
    SlabAllocatorSet&
    operator=(SlabAllocatorSet const& other) = delete;

    SlabAllocatorSet(SlabAllocatorSet&& other) = delete;
    SlabAllocatorSet&
    operator=(SlabAllocatorSet&& other) = delete;

    ~SlabAllocatorSet() = default;

    /** Returns a suitably aligned pointer, if one is available.

        @param extra The number of extra bytes, above and beyond the size of
                     the object, that should be returned by the allocator.

        @return a pointer to a block of memory, or nullptr if the allocator
                can't satisfy this request.
     */
    std::uint8_t*
    allocate(std::size_t extra) noexcept
    {
        if (auto const size = sizeof(Type) + extra; size <= maxSize_)
        {
            for (auto& a : allocators_)
            {
                if (a.size() >= size)
                    return a.allocate();
            }
        }

        return nullptr;
    }

    /** Returns the memory block to the allocator.

        @param ptr A pointer to a memory block.

        @return true if this memory block belonged to one of the allocators
                     in this set and has been released; false otherwise.
     */
    bool
    deallocate(std::uint8_t* ptr) noexcept
    {
        for (auto& a : allocators_)
        {
            if (a.deallocate(ptr))
                return true;
        }

        return false;
    }
};

}  // namespace ripple

#endif  // RIPPLE_BASICS_SLABALLOCATOR_H_INCLUDED
