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
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace ripple {

/** A dynamically sized block of memory.

    Meets the requirements of BufferFactory.
*/
class Buffer
{
private:
    // Small buffer optimization
    alignas(std::max_align_t) std::array<std::uint8_t, 112> buff_;
    std::size_t size_;
    std::uint8_t* data_;

public:
    using const_iterator = decltype(buff_)::const_iterator;
    using iterator = decltype(buff_)::iterator;

    ~Buffer()
    {
        assert(data_ != nullptr);

        if (data_ != buff_.data())
            delete[] data_;
    }

    /** Create a buffer with the given size.

        @note The contents of the buffer are unspecified.
     */
    explicit Buffer(std::size_t size)
        : size_(size)
        , data_(size <= buff_.size() ? buff_.data() : new std::uint8_t[size])
    {
        assert(data_ != nullptr);
    }

    Buffer() : Buffer(0)
    {
    }

    /** Create a buffer and copy existing data into it.

        @param data a pointer to the existing memory. If
                    size is non-zero, it must not be null.
        @param size size of the existing memory block.
    */
    Buffer(void const* data, std::size_t size) : Buffer(size)
    {
        assert(size == 0 || data != nullptr);

        std::copy_n(reinterpret_cast<std::uint8_t const*>(data), size, data_);
    }

    /** Copy-construct */
    Buffer(Buffer const& other) : Buffer(other.data_, other.size_)
    {
    }

    /** Copy assign */
    Buffer&
    operator=(Buffer const& other)
    {
        if (this != &other) [[likely]]
            std::copy_n(other.data_, other.size_, alloc(other.size_));

        return *this;
    }

    /** Move-construct.
        The other buffer is reset.
    */
    Buffer(Buffer&& other) noexcept : Buffer(0)
    {
        *this = std::move(other);
    }

    /** Move-assign.
        The other buffer is reset.
    */
    Buffer&
    operator=(Buffer&& other) noexcept
    {
        if (this != &other) [[likely]]
        {
            if (other.size_ > other.buff_.size())
            {  // We take the memory from the other buffer:
                if (data_ != buff_.data())
                    delete[] data_;

                data_ = other.data_;
                size_ = other.size_;

                // Can't use clear here: it would release the memory block
                // we just saved.
                other.data_ = other.buff_.data();
                other.size_ = 0;
            }
            else
            {
                // Notice that in this codepath, we "allocate" data from
                // the internal buffer, so no exception can be raised.
                assert(other.size_ <= buff_.size());

                std::copy_n(other.data_, other.size_, alloc(other.size_));

                other.size_ = 0;
            }
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
        assert(
            s.size() == 0 || size_ == 0 || s.data() < data_ ||
            s.data() >= data_ + size_);

        std::copy_n(s.data(), s.size(), alloc(s.size()));

        return *this;
    }

    /** Returns the number of bytes in the buffer. */
    [[nodiscard]] std::size_t
    size() const noexcept
    {
        return size_;
    }

    [[nodiscard]] bool
    empty() const noexcept
    {
        return 0 == size_;
    }

    operator Slice() const noexcept
    {
        return Slice{data_, size_};
    }

    /** Return a pointer to the storage.
        @note The return type is guaranteed to be a pointer
              to a single byte, to facilitate pointer arithmetic.
    */
    /** @{ */
    [[nodiscard]] std::uint8_t const*
    data() const noexcept
    {
        return data_;
    }

    [[nodiscard]] std::uint8_t*
    data() noexcept
    {
        return data_;
    }
    /** @} */

    /** Resizes the container to the requested number of elements. */
    void
    resize(std::size_t size)
    {
        if (size_ <= buff_.size() && size <= buff_.size())
        {  // Just update the size if we can still use our internal buffer
            size_ = size;
            return;
        }

        if (size_ != size)
        {
            auto d2 =
                (size <= buff_.size()) ? buff_.data() : new std::uint8_t[size];
            assert(d2 != data_);

            std::copy_n(data_, std::min(size, size_), d2);

            if (data_ != buff_.data())
                delete[] data_;

            data_ = d2;
            size_ = size;
        }
    }

    /** Mark the buffer as empty and release allocated memory. */
    void
    clear() noexcept
    {
        if (size_ != 0 && data_ != buff_.data())
            delete[] data_;

        data_ = buff_.data();
        size_ = 0;
    }

    /** Reallocate the storage.
        Existing data, if any, is discarded.
    */
    [[nodiscard]] std::uint8_t*
    alloc(std::size_t size)
    {
        if (size_ != size)
        {
            if (data_ != buff_.data())
                delete[] data_;

            size_ = size;

            if (size_ <= buff_.size())
                data_ = buff_.data();
            else
                data_ = new std::uint8_t[size_];
        }

        return data_;
    }

    [[nodiscard]] iterator
    begin() noexcept
    {
        return data_;
    }

    [[nodiscard]] iterator
    end() noexcept
    {
        return data_ + size_;
    }

    [[nodiscard]] const_iterator
    begin() const noexcept
    {
        return data_;
    }

    [[nodiscard]] const_iterator
    cbegin() const noexcept
    {
        return data_;
    }

    [[nodiscard]] const_iterator
    end() const noexcept
    {
        return data_ + size_;
    }

    [[nodiscard]] const_iterator
    cend() const noexcept
    {
        return data_ + size_;
    }
};

inline bool
operator==(Buffer const& lhs, Buffer const& rhs) noexcept
{
    return std::equal(lhs.cbegin(), lhs.cend(), rhs.cbegin(), rhs.cend());
}

inline bool
operator!=(Buffer const& lhs, Buffer const& rhs) noexcept
{
    return !(lhs == rhs);
}

}  // namespace ripple

#endif
