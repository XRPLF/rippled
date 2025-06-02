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

#include <xrpl/beast/core/FunctionProfiler.h>

#include <boost/endian/conversion.hpp>

#include <xxhash.h>

#include <cstddef>
#include <iostream>
#include <new>
#include <span>
#include <type_traits>

#define ORIGINAL_HASH 1
#define BIT_SHIFT_HASH 0

namespace beast {

class xxhasher
{
public:
    using HashType = std::size_t;

private:
    // requires 64-bit std::size_t
    static_assert(sizeof(std::size_t) == 8, "");

#if ORIGINAL_HASH
    XXH3_state_t* state_;
#endif

#if PROFILING
    std::size_t totalSize_ = 0;
    std::chrono::nanoseconds duration_{};
    std::uint64_t cpuCycles = 0;
#endif
    XXH64_hash_t seed_ = 0;

    std::array<std::uint8_t, 40> buffer_;
    std::span<std::uint8_t> readBuffer_;
    std::span<std::uint8_t> writeBuffer_;

#if ORIGINAL_HASH
    static XXH3_state_t*
    allocState()
    {
        auto ret = XXH3_createState();
        if (ret == nullptr)
            throw std::bad_alloc();
        return ret;
    }
#endif

    void
    setupBuffers()
    {
        writeBuffer_ = std::span{buffer_};
    }

    void
    writeBuffer(void const* data, std::size_t len)
    {
        auto bytesToWrite = std::min(len, writeBuffer_.size());

        std::memcpy(writeBuffer_.data(), data, bytesToWrite);
        writeBuffer_ = writeBuffer_.subspan(bytesToWrite);
        readBuffer_ = std::span{
            std::begin(buffer_), buffer_.size() - writeBuffer_.size()};
    }

public:
    using result_type = std::size_t;

    static constexpr auto const endian = boost::endian::order::native;

    xxhasher(xxhasher const&) = delete;
    xxhasher&
    operator=(xxhasher const&) = delete;

    xxhasher()
    {
#if ORIGINAL_HASH
        auto start = std::chrono::steady_clock::now();
        auto cpuCyclesStart = __rdtsc();
        state_ = allocState();
        XXH3_64bits_reset(state_);
        duration_ += std::chrono::steady_clock::now() - start;
        cpuCycles += (__rdtsc() - cpuCyclesStart);
#else
        setupBuffers();
#endif
    }

#if ORIGINAL_HASH
    ~xxhasher() noexcept
    {
        XXH3_freeState(state_);
    }
#endif
    template <
        class Seed,
        std::enable_if_t<std::is_unsigned<Seed>::value>* = nullptr>
    explicit xxhasher(Seed seed)
    {
        seed_ = seed;


#if ORIGINAL_HASH
        auto start = std::chrono::steady_clock::now();
        auto cpuCyclesStart = __rdtsc();
        state_ = allocState();
        XXH3_64bits_reset_withSeed(state_, seed);
        duration_ += std::chrono::steady_clock::now() - start;
        cpuCycles += (__rdtsc() - cpuCyclesStart);
#else
        setupBuffers();
#endif
    }

    template <
        class Seed,
        std::enable_if_t<std::is_unsigned<Seed>::value>* = nullptr>
    xxhasher(Seed seed, Seed)
    {
        seed_ = seed;

#if ORIGINAL_HASH
        auto start = std::chrono::steady_clock::now();
        auto cpuCyclesStart = __rdtsc();
        state_ = allocState();
        XXH3_64bits_reset_withSeed(state_, seed);
        duration_ += std::chrono::steady_clock::now() - start;
        cpuCycles += (__rdtsc() - cpuCyclesStart);
#else
        setupBuffers();
#endif
    }

    void
    operator()(void const* key, std::size_t len) noexcept
    {
#if PROFILING
        totalSize_ += len;
        auto start = std::chrono::steady_clock::now();
        auto cpuCyclesStart = __rdtsc();
#endif
#if ORIGINAL_HASH
        XXH3_64bits_update(state_, key, len);
#else
        writeBuffer(key, len);
#endif

#if PROFILING
        duration_ += std::chrono::steady_clock::now() - start;
        cpuCycles += (__rdtsc() - cpuCyclesStart);
#endif
    }

    explicit
    operator HashType() noexcept
    {
        if (readBuffer_.size() == 0) return 0;
        
#if PROFILING
        auto start = std::chrono::steady_clock::now();
        auto cpuCyclesStart = __rdtsc();
#endif

#if BIT_SHIFT_HASH
        const size_t bit_width = readBuffer_.size() * 8;
        const size_t shift = seed_ % bit_width;

        // Copy input into a buffer long enough to safely extract 64 bits with wraparound
        std::uint64_t buffer = 0;

        // Load the first 8 bytes (or wrap if input < 8 bytes)
        for (size_t i = 0; i < 8; ++i) {
            size_t index = readBuffer_.size() - 1 - (i % readBuffer_.size());
            buffer <<= 8;
            buffer |= readBuffer_[index];
        }

        // Rotate and return
        auto result = (buffer << shift) | (buffer >> (64 - shift));
#else
        auto result = seed_ == 0 ? 
            XXH3_64bits(readBuffer_.data(), readBuffer_.size()) : 
            XXH3_64bits_withSeed(readBuffer_.data(), readBuffer_.size(), seed_);
#endif
#if PROFILING
        duration_ += std::chrono::steady_clock::now() - start;
        cpuCycles += (__rdtsc() - cpuCyclesStart);

        std::lock_guard<std::mutex> lock{FunctionProfiler::mutex_};
        FunctionProfiler::funcionDurations
            ["xxhasher-" + std::to_string(totalSize_)]
                .time.emplace_back(duration_);
        FunctionProfiler::funcionDurations
            ["xxhasher-" + std::to_string(totalSize_)]
                .cpuCycles.emplace_back(cpuCycles);
#endif
        return result;
    }
};

}  // namespace beast

#endif
