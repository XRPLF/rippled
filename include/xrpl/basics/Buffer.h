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

#ifndef RIPPLE_BASICS_BUFFER_H_INCLUDED
#define RIPPLE_BASICS_BUFFER_H_INCLUDED

#include <xrpl/basics/Slice.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>

namespace ripple {

/** Like std::vector<char> but better.
    Meets the requirements of BufferFactory.
*/
class Buffer
{
private:
    std::unique_ptr<std::uint8_t[]> p_;
    std::size_t size_ = 0;

public:
    using const_iterator = std::uint8_t const*;

    Buffer() = default;

    /** Create an uninitialized buffer with the given size. */
    explicit Buffer(std::size_t size)
        : p_(size ? new std::uint8_t[size] : nullptr), size_(size)
    {
    }

    /** Create a buffer as a copy of existing memory.

        @param data a pointer to the existing memory. If
                    size is non-zero, it must not be null.
        @param size size of the existing memory block.
    */
    Buffer(void const* data, std::size_t size) : Buffer(size)
    {
        if (size)
            std::memcpy(p_.get(), data, size);
    }

    /** Copy-construct */
    Buffer(Buffer const& other) : Buffer(other.p_.get(), other.size_)
    {
    }

    /** Copy assign */
    Buffer&
    operator=(Buffer const& other)
    {
        if (this != &other)
        {
            if (auto p = alloc(other.size_))
                std::memcpy(p, other.p_.get(), size_);
        }
        return *this;
    }

    /** Move-construct.
        The other buffer is reset.
    */
    Buffer(Buffer&& other) noexcept
        : p_(std::move(other.p_)), size_(other.size_)
    {
        other.size_ = 0;
    }

    /** Move-assign.
        The other buffer is reset.
    */
    Buffer&
    operator=(Buffer&& other) noexcept
    {
        if (this != &other)
        {
            p_ = std::move(other.p_);
            size_ = other.size_;
            other.size_ = 0;
        }
        return *this;
    }

    /** Construct from a slice */
    explicit Buffer(Slice s) : Buffer(s.data(), s.size())
    {
    }

    /** Assign from slice */
    Buffer&
    operator=(Slice s)
    {
        // Ensure the slice isn't a subset of the buffer.
        ASSERT(
            s.size() == 0 || size_ == 0 || s.data() < p_.get() ||
                s.data() >= p_.get() + size_,
            "ripple::Buffer::operator=(Slice) : input not a subset");

        if (auto p = alloc(s.size()))
            std::memcpy(p, s.data(), s.size());
        return *this;
    }

    /** Returns the number of bytes in the buffer. */
    std::size_t
    size() const noexcept
    {
        return size_;
    }

    bool
    empty() const noexcept
    {
        return 0 == size_;
    }

    operator Slice() const noexcept
    {
        if (!size_)
            return Slice{};
        return Slice{p_.get(), size_};
    }

    /** Return a pointer to beginning of the storage.
        @note The return type is guaranteed to be a pointer
              to a single byte, to facilitate pointer arithmetic.
    */
    /** @{ */
    std::uint8_t const*
    data() const noexcept
    {
        return p_.get();
    }

    std::uint8_t*
    data() noexcept
    {
        return p_.get();
    }
    /** @} */

    /** Reset the buffer.
        All memory is deallocated. The resulting size is 0.
    */
    void
    clear() noexcept
    {
        p_.reset();
        size_ = 0;
    }

    /** Reallocate the storage.
        Existing data, if any, is discarded.
    */
    std::uint8_t*
    alloc(std::size_t n)
    {
        if (n != size_)
        {
            p_.reset(n ? new std::uint8_t[n] : nullptr);
            size_ = n;
        }
        return p_.get();
    }

    // Meet the requirements of BufferFactory
    void*
    operator()(std::size_t n)
    {
        return alloc(n);
    }

    const_iterator
    begin() const noexcept
    {
        return p_.get();
    }

    const_iterator
    cbegin() const noexcept
    {
        return p_.get();
    }

    const_iterator
    end() const noexcept
    {
        return p_.get() + size_;
    }

    const_iterator
    cend() const noexcept
    {
        return p_.get() + size_;
    }
};

inline bool
operator==(Buffer const& lhs, Buffer const& rhs) noexcept
{
    if (lhs.size() != rhs.size())
        return false;

    if (lhs.size() == 0)
        return true;

    return std::memcmp(lhs.data(), rhs.data(), lhs.size()) == 0;
}

inline bool
operator!=(Buffer const& lhs, Buffer const& rhs) noexcept
{
    return !(lhs == rhs);
}

}  // namespace ripple

#endif
