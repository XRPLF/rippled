//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2014, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_HASH_XXHASHER_H_INCLUDED
#define BEAST_HASH_XXHASHER_H_INCLUDED

#include <boost/endian/conversion.hpp>

#include <xxhash.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace beast {

class xxhasher
{
public:
    using HashType = std::size_t;

private:
    // requires 64-bit std::size_t
    static_assert(sizeof(std::size_t) == 8, "");
    // Have an internal buffer to avoid the streaming API
    // A 40-byte buffer should to be big enough for us
    static constexpr std::size_t INTERNAL_BUFFER_SIZE = 40;

    std::optional<XXH64_hash_t> seed_;
    XXH3_state_t* state_ = nullptr;

    std::array<std::uint8_t, INTERNAL_BUFFER_SIZE> buffer_;
    std::span<std::uint8_t> readBuffer_;
    std::span<std::uint8_t> writeBuffer_;

    void
    resetBuffers()
    {
        writeBuffer_ = std::span{buffer_};
        readBuffer_ = {};
    }

    void
    updateHash(void const* data, std::size_t len)
    {
        if (writeBuffer_.size() < len)
        {
            flushToState(data, len);
        }
        else
        {
            std::memcpy(writeBuffer_.data(), data, len);
            writeBuffer_ = writeBuffer_.subspan(len);
            readBuffer_ = std::span{
                std::begin(buffer_), buffer_.size() - writeBuffer_.size()};
        }
    }

    static XXH3_state_t*
    allocState()
    {
        auto ret = XXH3_createState();
        if (ret == nullptr)
            throw std::bad_alloc();  // LCOV_EXCL_LINE
        return ret;
    }

    void
    flushToState(void const* data, std::size_t len)
    {
        if (!state_)
        {
            state_ = allocState();
            if (seed_.has_value())
            {
                XXH3_64bits_reset_withSeed(state_, *seed_);
            }
            else
            {
                XXH3_64bits_reset(state_);
            }
        }
        XXH3_64bits_update(state_, readBuffer_.data(), readBuffer_.size());
        resetBuffers();
        if (data && len)
        {
            XXH3_64bits_update(state_, data, len);
        }
    }

    HashType
    retrieveHash()
    {
        if (state_)
        {
            flushToState(nullptr, 0);
            return XXH3_64bits_digest(state_);
        }
        else
        {
            if (seed_.has_value())
            {
                return XXH3_64bits_withSeed(
                    readBuffer_.data(), readBuffer_.size(), *seed_);
            }
            else
            {
                return XXH3_64bits(readBuffer_.data(), readBuffer_.size());
            }
        }
    }

public:
    using result_type = std::size_t;

    static constexpr auto const endian = boost::endian::order::native;

    xxhasher(xxhasher const&) = delete;
    xxhasher&
    operator=(xxhasher const&) = delete;

    xxhasher()
    {
        resetBuffers();
    }

    ~xxhasher() noexcept
    {
        if (state_)
        {
            XXH3_freeState(state_);
        }
    }

    template <
        class Seed,
        std::enable_if_t<std::is_unsigned<Seed>::value>* = nullptr>
    explicit xxhasher(Seed seed) : seed_(seed)
    {
        resetBuffers();
    }

    template <
        class Seed,
        std::enable_if_t<std::is_unsigned<Seed>::value>* = nullptr>
    xxhasher(Seed seed, Seed) : seed_(seed)
    {
        resetBuffers();
    }

    void
    operator()(void const* key, std::size_t len) noexcept
    {
        updateHash(key, len);
    }

    explicit
    operator HashType() noexcept
    {
        return retrieveHash();
    }
};

}  // namespace beast

#endif
