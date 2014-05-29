//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

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

#include <beast/unit_test/suite.h>

namespace beast {

Random::Random (const std::int64_t seedValue) noexcept
    : seed (seedValue)
{
    nextInt (); // fixes a bug where the first int is always 0
}

Random::Random()
    : seed (1)
{
    setSeedRandomly();
}

Random::~Random() noexcept
{
}

void Random::setSeed (const std::int64_t newSeed) noexcept
{
    seed = newSeed;

    nextInt (); // fixes a bug where the first int is always 0
}

void Random::combineSeed (const std::int64_t seedValue) noexcept
{
    seed ^= nextInt64() ^ seedValue;
}

void Random::setSeedRandomly()
{
    static std::int64_t globalSeed = 0;

    combineSeed (globalSeed ^ (std::int64_t) (std::intptr_t) this);
    combineSeed (Time::getMillisecondCounter());
    combineSeed (Time::getHighResolutionTicks());
    combineSeed (Time::getHighResolutionTicksPerSecond());
    combineSeed (Time::currentTimeMillis());
    globalSeed ^= seed;

    nextInt (); // fixes a bug where the first int is always 0
}

Random& Random::getSystemRandom() noexcept
{
    static Random sysRand;
    return sysRand;
}

//==============================================================================
int Random::nextInt() noexcept
{
    seed = (seed * 0x5deece66dLL + 11) & 0xffffffffffffULL;

    return (int) (seed >> 16);
}

int Random::nextInt (const int maxValue) noexcept
{
    bassert (maxValue > 0);
    return (int) ((((unsigned int) nextInt()) * (std::uint64_t) maxValue) >> 32);
}

std::int64_t Random::nextInt64() noexcept
{
    return (((std::int64_t) nextInt()) << 32) | (std::int64_t) (std::uint64_t) (std::uint32_t) nextInt();
}

bool Random::nextBool() noexcept
{
    return (nextInt() & 0x40000000) != 0;
}

float Random::nextFloat() noexcept
{
    return static_cast <std::uint32_t> (nextInt()) / (float) 0xffffffff;
}

double Random::nextDouble() noexcept
{
    return static_cast <std::uint32_t> (nextInt()) / (double) 0xffffffff;
}

void Random::fillBitsRandomly (void* const buffer, size_t bytes)
{
    int* d = static_cast<int*> (buffer);

    for (; bytes >= sizeof (int); bytes -= sizeof (int))
        *d++ = nextInt();

    if (bytes > 0)
    {
        const int lastBytes = nextInt();
        memcpy (d, &lastBytes, bytes);
    }
}

//==============================================================================

class Random_test  : public unit_test::suite
{
public:
    void run()
    {
        for (int j = 10; --j >= 0;)
        {
            Random r;
            r.setSeedRandomly();

            for (int i = 20; --i >= 0;)
            {
                expect (r.nextDouble() >= 0.0 && r.nextDouble() < 1.0);
                expect (r.nextFloat() >= 0.0f && r.nextFloat() < 1.0f);
                expect (r.nextInt (5) >= 0 && r.nextInt (5) < 5);
                expect (r.nextInt (1) == 0);

                int n = r.nextInt (50) + 1;
                expect (r.nextInt (n) >= 0 && r.nextInt (n) < n);

                n = r.nextInt (0x7ffffffe) + 1;
                expect (r.nextInt (n) >= 0 && r.nextInt (n) < n);
            }
        }
    }
};

BEAST_DEFINE_TESTSUITE(Random,beast_core,beast);

} // beast
