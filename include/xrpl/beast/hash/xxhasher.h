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


namespace beast {

class xxhasher
{
public:
    using HashType = std::size_t;

private:
    // requires 64-bit std::size_t
    static_assert(sizeof(std::size_t) == 8, "");

    struct state_wrapper
    {
        XXH3_state_t* state;
        state_wrapper()
        {
            state = XXH3_createState();
        }
        ~state_wrapper()
        {
            XXH3_freeState(state);
        }
    };

    // XXH3_state_t* state_;
#if PROFILING
    inline static thread_local state_wrapper wrapper{};
    std::size_t totalSize_ = 0;
    std::chrono::nanoseconds duration_{};
    std::uint64_t cpuCycles = 0;
#endif
    std::uint8_t seed_ = 0;

    std::array<std::uint8_t, 40> buffer_;
    std::span<std::uint8_t> readBuffer_;
    std::span<std::uint8_t> writeBuffer_;

    // static XXH3_state_t*
    // allocState()
    // {
    //     FunctionProfiler _{"-alloc"};
    //     auto ret = XXH3_createState();
    //     if (ret == nullptr)
    //         throw std::bad_alloc();
    //     return ret;
    // }

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
        setupBuffers();
        //     auto start = std::chrono::steady_clock::now();
        //     auto cpuCyclesStart = __rdtsc();
        //     // state_ = allocState();
        //     // XXH3_64bits_reset(state_);
        //     XXH3_64bits_reset(wrapper.state);
        //     duration_ += std::chrono::steady_clock::now() - start;
        //     cpuCycles += (__rdtsc() - cpuCyclesStart);
    }

    // ~xxhasher() noexcept
    // {
    //     // profiler_.functionName = "xxhasher-" + std::to_string(totalSize_);
    //     // auto start = std::chrono::steady_clock::now();
    //     if (0)
    //     {
    //         FunctionProfiler _{"-free"};
    //         XXH3_freeState(state_);
    //     }
    // }

    template <
        class Seed,
        std::enable_if_t<std::is_unsigned<Seed>::value>* = nullptr>
    explicit xxhasher(Seed seed)
    {
        seed_ = seed % std::numeric_limits<std::uint8_t>::max();

        setupBuffers();

        // auto start = std::chrono::steady_clock::now();
        // auto cpuCyclesStart = __rdtsc();
        // state_ = allocState();
        // XXH3_64bits_reset_withSeed(state_, seed);
        // XXH3_64bits_reset_withSeed(wrapper.state, seed);
        // duration_ += std::chrono::steady_clock::now() - start;
        // cpuCycles += (__rdtsc() - cpuCyclesStart);
    }

    template <
        class Seed,
        std::enable_if_t<std::is_unsigned<Seed>::value>* = nullptr>
    xxhasher(Seed seed, Seed)
    {
        seed_ = seed % std::numeric_limits<std::uint8_t>::max();

        setupBuffers();
        // auto start = std::chrono::steady_clock::now();
        // auto cpuCyclesStart = __rdtsc();
        // // state_ = allocState();
        // // XXH3_64bits_reset_withSeed(state_, seed);
        // XXH3_64bits_reset_withSeed(wrapper.state, seed);
        // duration_ += std::chrono::steady_clock::now() - start;
        // cpuCycles += (__rdtsc() - cpuCyclesStart);
    }

    void
    operator()(void const* key, std::size_t len) noexcept
    {
#if PROFILING
        totalSize_ += len;
        auto start = std::chrono::steady_clock::now();
        auto cpuCyclesStart = __rdtsc();
#endif
        // FunctionProfiler _{"-size-" + std::to_string(len)};
        // XXH3_64bits_update(state_, key, len);
        // XXH3_64bits_update(wrapper.state, key, len);

        writeBuffer(key, len);

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

    if (readBuffer_.size() == 0) return 0;

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
        // auto start = std::chrono::steady_clock::now();
        // // auto ret =  XXH3_64bits_digest(state_);
        // auto ret = XXH3_64bits_digest(wrapper.state);
        // duration_ += std::chrono::steady_clock::now() - start;

        // std::lock_guard<std::mutex> lock{FunctionProfiler::mutex_};
        // FunctionProfiler::funcionDurations
        //     ["xxhasher-" + std::to_string(totalSize_)]
        //         .time.emplace_back(duration_);
        // FunctionProfiler::funcionDurations
        //     ["xxhasher-" + std::to_string(totalSize_)]
        //         .cpuCycles.emplace_back(cpuCycles);
        // return ret;
    }
};

}  // namespace beast

#endif
