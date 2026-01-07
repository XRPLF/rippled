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
    using result_type = std::size_t;

private:
    static_assert(sizeof(std::size_t) == 8, "requires 64-bit std::size_t");
    // Have an internal buffer to avoid the streaming API
    // A 64-byte buffer should to be big enough for us
    static constexpr std::size_t INTERNAL_BUFFER_SIZE = 64;

    alignas(64) std::array<std::uint8_t, INTERNAL_BUFFER_SIZE> buffer_;
    std::span<std::uint8_t> readBuffer_;
    std::span<std::uint8_t> writeBuffer_;

    std::optional<XXH64_hash_t> seed_;
    XXH3_state_t* state_ = nullptr;

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

    result_type
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
    operator result_type() noexcept
    {
        return retrieveHash();
    }
};

}  // namespace beast

#endif
