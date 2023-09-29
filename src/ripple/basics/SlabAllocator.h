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

#include <ripple/beast/type_name.h>

#include <boost/align.hpp>
#include <boost/container/static_vector.hpp>
#include <boost/predef.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <mutex>

namespace ripple {

template <typename Type>
class SlabAllocator
{
    // The code keeps track of the free list using a singly-linked list, that
    // is allocated in and maintained within the unused memory of a slab. The
    // assert ensures that we can store the 'next' pointer.
    static_assert(
        sizeof(Type) >= sizeof(std::uint8_t*),
        "SlabAllocator: the requested object must be larger than a pointer.");

    static_assert(alignof(Type) == 8 || alignof(Type) == 4);

    // A simple linked list node to keep track of the free list.
    //
    // The nodes for this linked list are constructed inside unused memory
    // in the slab's allocated block.
    //
    // By way of example, on a system with 1 byte long pointers, where the
    // slab allocator allocates 4 byte long items from a 16 byte long slab
    // the memory layout is as follows:
    //
    //     slot: 0      1      2      3
    //           [ .... | .... | .... | ....]
    //
    // We don't need anything too complicated: the only operations that we
    // perform are insertion and removal, but only at the head of the list
    // which means that all we need is very basic CAS.
    //
    // Normally, to keep a linked list we need a "node" structure, similar
    // to this. And, if we're going to have a node, we need some memory in
    // which to "store" it.
    //
    // But remember that we only need a "node" when an item is going to be
    // on the free list, and something is on the free list because there's
    // some memory that is otherwise unused.
    //
    // So if the size of the unused memory is greater than the size of our
    // node (which is the size of a pointer) then we can use that directly
    // and overlay a singly linked list of free objects onto the memory of
    // those free objects.
    //
    // Here's what the memory may look like if slots 1 and 3 are allocated
    // and slots 0 and 2 are free, with the free list overlaid on top:
    //
    //     list_ --------------+
    //                         |
    //                         v
    //     slot: 0      1      2      3
    //           [ N... | .... | N... | .... ]
    //           ^ |             |
    //           | '---> NULL    |
    //           |               |
    //           '---------------'
    //
    // Notice that although the first empty slot in memory order is slot 0
    // the free list points to slot 2 as the first available slot and slot
    // 2 points to slot 0, which points to NULL, as it is it the last free
    // slot.
    //
    // Now imagine that the object in slot 1 is released. The free list is
    // updated to point to slot 1 as the first available slot. The  memory
    // now looks like this:
    //
    //  list_ ----------+
    //                  |
    //                  v
    //     slot: 0      1      2      3
    //           [ N... | N... | N... | .... ]
    //           ^ |      |    ^ |
    //           | |      |    | |
    //           | |      '----' |
    //           | '---> NULL    |
    //           |               |
    //           '---------------'
    //
    // Note that the order of slots within the free list at any given time
    // is random, as it depends on the pattern and timing of memory access
    // of the application.
    struct alignas(Type) FreeItem
    {
        FreeItem* next;

        FreeItem(FreeItem* n = nullptr) noexcept : next(n)
        {
        }

        ~FreeItem() noexcept = default;
    };

    static_assert(sizeof(Type) >= sizeof(FreeItem));

    /** A block of memory that is owned by a slab allocator */
    struct SlabBlock
    {
        // A mutex to protect this block:
        std::mutex m_;

        // A linked list of appropriately sized free buffers:
        FreeItem* list_ = nullptr;

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

            assert(
                (avail_ == 0 && list_ == nullptr) ||
                (avail_ != 0 && list_ != nullptr));

            // It's possible that this block has an underlying buffer. If
            // it does, and it has space available, we use it.
            if (data_ != nullptr)
            {
                auto ret = list_;

                if (ret != nullptr)
                {
                    list_ = ret->next;
                    std::destroy_at(ret);
                    --avail_;
                }

                return reinterpret_cast<std::uint8_t*>(ret);
            }

            // We are going to take the passed-in memory block and use it
            // to service allocations. The first item will be returned to
            // the caller, and we chain the rest into a linked list.
            for (std::size_t i = 1; i != count; ++i)
                list_ = new (data + (item * i)) FreeItem(list_);

            data_ = data;
            avail_ = count - 1;

            return data;
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

    [[nodiscard]] std::uint8_t*
    allocate(SlabBlock* slab) noexcept
    {
        std::lock_guard l(slab->m_);

        auto ret = slab->list_;

        if (ret != nullptr)
        {
            slab->list_ = ret->next;
            --slab->avail_;

            // We want to invoke the destructor for the FreeItem object
            // before we use its memory.
            std::destroy_at(ret);
        }

        return reinterpret_cast<std::uint8_t*>(ret);
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

        FreeItem* flp;

        {
            std::lock_guard l(slab->m_);

            if (auto const d = slab->data_; (ptr < d) || (ptr >= d + slabSize_))
                return false;

            // It looks like this objects belongs to us. Ensure that it
            // doesn't extend past the end of our buffer:
            assert(ptr + itemSize_ <= slab->data_ + slabSize_);

            slab->list_ = new (std::assume_aligned<alignof(FreeItem)>(ptr))
                FreeItem(slab->list_);

            if (++slab->avail_ != itemCount_)
                return true;

            // The slab is now fully unused and doesn't need its data block:
            ptr = std::exchange(slab->data_, nullptr);
            flp = std::exchange(slab->list_, nullptr);
            slab->avail_ = 0;
        }

        // We can now destroy the free list and return the fully unused memory
        // block back to the system without holding a lock.
        while (flp != nullptr)
        {
            auto curr = flp;
            flp = curr->next;
            std::destroy_at(curr);
        }

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
        auto active = active_.load(std::memory_order::acquire);

        if (active)
        {
            if (auto ret = allocate(active))
                return ret;
        }

        for (auto slab = slabs_.load(std::memory_order::acquire); slab;
             slab = slab->next_)
        {
            if (auto ret = allocate(slab))
            {
                // This may fail, but that's OK.
                active_.compare_exchange_weak(
                    active, slab, std::memory_order::release);
                return ret;
            }
        }

        // No slab can satisfy our request, so we attempt to allocate a new
        // one. We align the block at a 2 MiB boundary to allow transparent
        // hugepage support on Linux.
        auto buf =
            reinterpret_cast<std::uint8_t*>(boost::alignment::aligned_alloc(
                megabytes(std::size_t(2)), slabSize_));

        if (!buf)
            return nullptr;

        // Check whether there's an existing slab with no associated buffer
        // that we can give our newly allocated buffer to:
        for (auto slab = slabs_.load(std::memory_order::acquire);
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
                    active, slab, std::memory_order::release);

                return ret;
            }
        }

        auto slab = new (std::nothrow)
            SlabBlock(slabs_.load(std::memory_order_relaxed));

        if (slab == nullptr)
        {
            boost::alignment::aligned_free(buf);
            return nullptr;
        }

        auto ret =
            slab->assign_and_allocate(buf, slabSize_, itemSize_, itemCount_);
        assert(ret == buf);

        // Finally, link the new slab
        while (!slabs_.compare_exchange_weak(
            slab->next_, slab, std::memory_order::release))
        {
            // Nothing to do
        }

        active_.store(slab, std::memory_order::release);
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

        for (auto slab = slabs_.load(std::memory_order::acquire);
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
        /**
         * @param extra_ The number of additional bytes to allocate per item.
         * @param alloc_ The number of bytes to allocate for the slab.
         * @param align_ The alignment of returned pointers.
         */
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
        if (cfg.size() > allocators_.capacity())
        {
            throw std::runtime_error(
                "SlabAllocatorSet<" + beast::type_name<Type>() +
                ">: too many slab config options");
        }

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
