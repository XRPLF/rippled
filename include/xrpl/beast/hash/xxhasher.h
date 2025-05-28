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
    inline static thread_local state_wrapper wrapper{};
    std::size_t totalSize_ = 0;
    std::chrono::nanoseconds duration_{};
    std::uint64_t cpuCycles = 0;

    static XXH3_state_t*
    allocState()
    {
        FunctionProfiler _{"-alloc"};
        auto ret = XXH3_createState();
        if (ret == nullptr)
            throw std::bad_alloc();
        return ret;
    }

public:
    using result_type = std::size_t;

    static constexpr auto const endian = boost::endian::order::native;

    xxhasher(xxhasher const&) = delete;
    xxhasher&
    operator=(xxhasher const&) = delete;

    xxhasher()
    {
        auto start = std::chrono::steady_clock::now();
        auto cpuCyclesStart = __rdtsc();
        // state_ = allocState();
        // XXH3_64bits_reset(state_);
        XXH3_64bits_reset(wrapper.state);
        duration_ += std::chrono::steady_clock::now() - start;
        cpuCycles += (__rdtsc() - cpuCyclesStart);
    }

    ~xxhasher() noexcept
    {
        // profiler_.functionName = "xxhasher-" + std::to_string(totalSize_);
        // auto start = std::chrono::steady_clock::now();
        if (0)
        {
            FunctionProfiler _{"-free"};
            XXH3_freeState(state_);
        }
    }

    template <
        class Seed,
        std::enable_if_t<std::is_unsigned<Seed>::value>* = nullptr>
    explicit xxhasher(Seed seed)
    {
        auto start = std::chrono::steady_clock::now();
        auto cpuCyclesStart = __rdtsc();
        // state_ = allocState();
        // XXH3_64bits_reset_withSeed(state_, seed);
        XXH3_64bits_reset_withSeed(wrapper.state, seed);
        duration_ += std::chrono::steady_clock::now() - start;
        cpuCycles += (__rdtsc() - cpuCyclesStart);
    }

    template <
        class Seed,
        std::enable_if_t<std::is_unsigned<Seed>::value>* = nullptr>
    xxhasher(Seed seed, Seed)
    {
        auto start = std::chrono::steady_clock::now();
        auto cpuCyclesStart = __rdtsc();
        // state_ = allocState();
        // XXH3_64bits_reset_withSeed(state_, seed);
        XXH3_64bits_reset_withSeed(wrapper.state, seed);
        duration_ += std::chrono::steady_clock::now() - start;
        cpuCycles += (__rdtsc() - cpuCyclesStart);
    }

    void
    operator()(void const* key, std::size_t len) noexcept
    {
        auto start = std::chrono::steady_clock::now();
        auto cpuCyclesStart = __rdtsc();
        totalSize_ += len;
        // FunctionProfiler _{"-size-" + std::to_string(len)};
        // XXH3_64bits_update(state_, key, len);
        XXH3_64bits_update(wrapper.state, key, len);
        duration_ += std::chrono::steady_clock::now() - start;
        cpuCycles += (__rdtsc() - cpuCyclesStart);
    }

    explicit
    operator std::size_t() noexcept
    {
        auto start = std::chrono::steady_clock::now();
        // auto ret =  XXH3_64bits_digest(state_);
        auto ret = XXH3_64bits_digest(wrapper.state);
        duration_ += std::chrono::steady_clock::now() - start;

        std::lock_guard<std::mutex> lock{FunctionProfiler::mutex_};
        FunctionProfiler::funcionDurations
            ["xxhasher-" + std::to_string(totalSize_)]
                .time.emplace_back(duration_);
        FunctionProfiler::funcionDurations
            ["xxhasher-" + std::to_string(totalSize_)]
                .cpuCycles.emplace_back(cpuCycles);
        return ret;
    }
};

}  // namespace beast

#endif
