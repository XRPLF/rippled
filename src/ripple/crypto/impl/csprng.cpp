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

#include <BeastConfig.h>
#include <ripple/basics/contract.h>
#include <ripple/crypto/csprng.h>
#include <openssl/rand.h>
#include <array>
#include <cassert>
#include <random>
#include <stdexcept>

namespace ripple {

void
csprng_engine::mix (
    void* data, std::size_t size, double bitsPerByte)
{
    assert (data != nullptr);
    assert (size != 0);
    assert (bitsPerByte != 0);

    std::lock_guard<std::mutex> lock (mutex_);
    RAND_add (data, size, (size * bitsPerByte) / 8.0);
}

csprng_engine::csprng_engine ()
{
    mix_entropy ();
}

csprng_engine::~csprng_engine ()
{
    RAND_cleanup ();
}

void
csprng_engine::load_state (std::string const& file)
{
    if (!file.empty())
    {
        std::lock_guard<std::mutex> lock (mutex_);
        RAND_load_file (file.c_str (), 1024);
        RAND_write_file (file.c_str ());
    }
}

void
csprng_engine::save_state (std::string const& file)
{
    if (!file.empty())
    {
        std::lock_guard<std::mutex> lock (mutex_);
        RAND_write_file (file.c_str ());
    }
}

void
csprng_engine::mix_entropy (void* buffer, std::size_t count)
{
    std::array<std::random_device::result_type, 128> entropy;

    {
        // On every platform we support, std::random_device
        // is non-deterministic and should provide some good
        // quality entropy.
        std::random_device rd;

        for (auto& e : entropy)
            e = rd();
    }

    // Assume 2 bits per byte for the system entropy:
    mix (
        entropy.data(),
        entropy.size() * sizeof(std::random_device::result_type),
        2.0);

    // We want to be extremely conservative about estimating
    // how much entropy the buffer the user gives us contains
    // and assume only 0.5 bits of entropy per byte:
    if (buffer != nullptr && count != 0)
        mix (buffer, count, 0.5);
}

csprng_engine::result_type
csprng_engine::operator()()
{
    result_type ret;

    std::lock_guard<std::mutex> lock (mutex_);

    auto const result = RAND_bytes (
        reinterpret_cast<unsigned char*>(&ret),
        sizeof(ret));

    if (result == 0)
        Throw<std::runtime_error> ("Insufficient entropy");

    return ret;
}

void
csprng_engine::operator()(void *ptr, std::size_t count)
{
    std::lock_guard<std::mutex> lock (mutex_);

    auto const result = RAND_bytes (
        reinterpret_cast<unsigned char*>(ptr),
        count);

    if (result != 1)
        Throw<std::runtime_error> ("Insufficient entropy");
}

csprng_engine& crypto_prng()
{
    static csprng_engine engine;
    return engine;
}

}
