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

#include <ripple/basics/Slice.h>
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
    std::unique_ptr<
        std::uint8_t[]> p_;
    std::size_t size_ = 0;

public:
    Buffer() = default;
    Buffer (Buffer const&) = delete;
    Buffer& operator= (Buffer const&) = delete;

    /** Move-construct.
        The other buffer is reset.
    */
    Buffer (Buffer&& other)
        : p_ (std::move(other.p_))
        , size_ (other.size_)
    {
        other.size_ = 0;
    }

    /** Move-assign.
        The other buffer is reset.
    */
    Buffer& operator= (Buffer&& other)
    {
        p_ = std::move(other.p_);
        size_ = other.size_;
        other.size_ = 0;
        return *this;
    }

    /** Create an uninitialized buffer with the given size. */
    explicit
    Buffer (std::size_t size)
        : p_ (size ?
            new std::uint8_t[size] : nullptr)
        , size_ (size)
    {
    }

    /** Create a buffer as a copy of existing memory. */
    Buffer (void const* data, std::size_t size)
        : Buffer (size)
    {
        std::memcpy(p_.get(), data, size);
    }

    /** Create a buffer from a copy of existing memory. */
    explicit
    Buffer (Slice const& slice)
        : Buffer(slice.data(), slice.size())
    {
    }

    /** Returns the number of bytes in the buffer. */
    std::size_t
    size() const noexcept
    {
        return size_;
    }

    bool
    empty () const noexcept
    {
        return 0 == size_;
    }

    operator Slice() const noexcept
    {
        if (! size_)
            return Slice{};
        return Slice{ p_.get(), size_ };
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
    alloc (std::size_t n)
    {
        if (n == 0)
        {
            clear();
            return nullptr;
        }
        if (n != size_)
        {
            p_.reset(new std::uint8_t[n]);
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
};

inline bool operator==(Buffer const& lhs, Buffer const& rhs) noexcept
{
    if (lhs.size () != rhs.size ())
        return false;
    return !std::memcmp (lhs.data (), rhs.data (), lhs.size ());
}

inline bool operator!=(Buffer const& lhs, Buffer const& rhs) noexcept
{
    return !(lhs == rhs);
}

} // ripple

#endif
