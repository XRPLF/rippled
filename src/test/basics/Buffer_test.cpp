//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github0.com/ripple/rippled
    Copyright (c) 2012-2016 Ripple Labs Inc.

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

#include <ripple/basics/Buffer.h>
#include <ripple/basics/random.h>
#include <ripple/beast/unit_test.h>
#include <ripple/beast/xor_shift_engine.h>
#include <cstdint>
#include <type_traits>

namespace ripple {
namespace test {

struct Buffer_test : beast::unit_test::suite
{
    void
    run() override
    {
        std::uint8_t const data[] = {
            0xa8, 0xa1, 0x38, 0x45, 0x23, 0xec, 0xe4, 0x23, 0x71, 0x6d, 0x2a,
            0x18, 0xb4, 0x70, 0xcb, 0xf5, 0xac, 0x2d, 0x89, 0x4d, 0x19, 0x9c,
            0xf0, 0x2c, 0x15, 0xd1, 0xf9, 0x9b, 0x66, 0xd2, 0x30, 0xd3};

        Buffer b0;
        BEAST_EXPECT(b0.empty());

        Buffer b1{0};
        BEAST_EXPECT(b1.empty());
        std::memcpy(b1.alloc(16), data, 16);
        BEAST_EXPECT(!b1.empty());
        BEAST_EXPECT(b1.size() == 16);

        Buffer b2{b1.size()};
        BEAST_EXPECT(!b2.empty());
        BEAST_EXPECT(b2.size() == b1.size());
        std::memcpy(b2.data(), data + 16, 16);

        Buffer b3{data, sizeof(data)};
        BEAST_EXPECT(!b3.empty());
        BEAST_EXPECT(b3.size() == sizeof(data));
        BEAST_EXPECT(std::memcmp(b3.data(), data, b3.size()) == 0);

        // Check equality and inequality comparisons
        BEAST_EXPECT(b0 == b0);
        BEAST_EXPECT(b0 != b1);
        BEAST_EXPECT(b1 == b1);
        BEAST_EXPECT(b1 != b2);
        BEAST_EXPECT(b2 != b3);

        // Check copy constructors and copy assignments:
        {
            testcase("Copy Construction / Assignment");

            Buffer x{b0};
            BEAST_EXPECT(x == b0);
            Buffer y{b1};
            BEAST_EXPECT(y == b1);
            x = b2;
            BEAST_EXPECT(x == b2);
            x = y;
            BEAST_EXPECT(x == y);
            y = b3;
            BEAST_EXPECT(y == b3);
            x = b0;
            BEAST_EXPECT(x == b0);
#if defined(__clang__) && (!defined(__APPLE__) && (__clang_major__ >= 7)) || \
    (defined(__APPLE__) && (__apple_build_version__ >= 10010043))
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-assign-overloaded"
#endif

            x = x;
            BEAST_EXPECT(x == b0);
            y = y;
            BEAST_EXPECT(y == b3);

#if defined(__clang__) && (!defined(__APPLE__) && (__clang_major__ >= 7)) || \
    (defined(__APPLE__) && (__apple_build_version__ >= 10010043))
#pragma clang diagnostic pop
#endif
        }

        // Check move constructor & move assignments:
        {
            testcase("Move Construction / Assignment");

            static_assert(
                std::is_nothrow_move_constructible<Buffer>::value, "");
            static_assert(std::is_nothrow_move_assignable<Buffer>::value, "");

            {  // Move-construct from empty buf
                Buffer x;
                Buffer y{std::move(x)};
                BEAST_EXPECT(x.empty());
                BEAST_EXPECT(y.empty());
                BEAST_EXPECT(x == y);
            }

            {  // Move-construct from non-empty buf
                Buffer x{b1};
                Buffer y{std::move(x)};
                BEAST_EXPECT(x.empty());
                BEAST_EXPECT(y == b1);
            }

            {  // Move assign empty buf to empty buf
                Buffer x;
                Buffer y;

                x = std::move(y);
                BEAST_EXPECT(x.empty());
                BEAST_EXPECT(y.empty());
            }

            {  // Move assign non-empty buf to empty buf
                Buffer x;
                Buffer y{b1};

                x = std::move(y);
                BEAST_EXPECT(x == b1);
                BEAST_EXPECT(y.empty());
            }

            {  // Move assign empty buf to non-empty buf
                Buffer x{b1};
                Buffer y;

                x = std::move(y);
                BEAST_EXPECT(x.empty());
                BEAST_EXPECT(y.empty());
            }

            {  // Move assign non-empty buf to non-empty buf
                Buffer x{b1};
                Buffer y{b2};
                Buffer z{b3};

                x = std::move(y);
                BEAST_EXPECT(!x.empty());
                BEAST_EXPECT(y.empty());

                x = std::move(z);
                BEAST_EXPECT(!x.empty());
                BEAST_EXPECT(z.empty());
            }

            { // Copy and move self-assignment
                Buffer x{b1};
                Buffer y{b2};

                BEAST_EXPECT(x == b1);
                BEAST_EXPECT(y == b2);

                x = x;
                BEAST_EXPECT(x == b1);

                y = std::move(y);
                BEAST_EXPECT(y == b2);
            }

            {
                auto const testdata = []() {
                    std::array<std::uint8_t, 1024> ret{};

                    for (std::size_t i = 0; i != ret.size(); ++i)
                        ret[i] = rand_byte<std::uint8_t>();

                    return ret;
                }();

                auto makeBuffer = [&testdata](std::size_t n) {
                    assert(n < 512);

                    return Slice{testdata.data() + n, n};
                };

                auto testCopy = [this, &makeBuffer](
                                    std::size_t n1, std::size_t n2) {
                    Buffer const x{makeBuffer(n1)};
                    Buffer const y{x};
                    Buffer z{makeBuffer(n2)};
                    BEAST_EXPECT(x == y && x.size() == n1);
                    BEAST_EXPECT(z.size() == n2);
                    z = y;
                    BEAST_EXPECT(z == x);
                    BEAST_EXPECT(z == y);
                };

                auto testMove = [this, &makeBuffer](
                                    std::size_t n1, std::size_t n2) {
                    Buffer const x{makeBuffer(n1)};
                    Buffer y{x};
                    Buffer z{makeBuffer(n2)};
                    BEAST_EXPECT(x == y && x.size() == n1);
                    BEAST_EXPECT(z.size() == n2);
                    z = std::move(y);
                    BEAST_EXPECT(z == x);
                    BEAST_EXPECT(y.size() == 0);
                };

                for (std::size_t n1 = 0; n1 != 7; ++n1)
                {
                    for (std::size_t n2 = 0; n2 != 7; ++n2)
                    {
                        testCopy(n1 * 61, n2 * 43);
                        testCopy(n1 * 41, n2 * 71);
                        testMove(n1 * 53, n2 * 67);
                        testMove(n1 * 83, n2 * 59);
                    }
                }
            }
        }

        {
            testcase("Slice Conversion / Construction / Assignment");

            Buffer w{static_cast<Slice>(b0)};
            BEAST_EXPECT(w == b0);

            Buffer x{static_cast<Slice>(b1)};
            BEAST_EXPECT(x == b1);

            Buffer y{static_cast<Slice>(b2)};
            BEAST_EXPECT(y == b2);

            Buffer z{static_cast<Slice>(b3)};
            BEAST_EXPECT(z == b3);

            // Assign empty slice to empty buffer
            w = static_cast<Slice>(b0);
            BEAST_EXPECT(w == b0);

            // Assign non-empty slice to empty buffer
            w = static_cast<Slice>(b1);
            BEAST_EXPECT(w == b1);

            // Assign non-empty slice to non-empty buffer
            x = static_cast<Slice>(b2);
            BEAST_EXPECT(x == b2);

            // Assign non-empty slice to non-empty buffer
            y = static_cast<Slice>(z);
            BEAST_EXPECT(y == z);

            // Assign empty slice to non-empty buffer:
            z = static_cast<Slice>(b0);
            BEAST_EXPECT(z == b0);
        }

        {
            testcase("Allocation, Deallocation and Clearing");

            auto test = [this](Buffer const& b, std::size_t i) {
                Buffer x{b};

                // Try to allocate the same number of bytes:
                BEAST_EXPECT(x.alloc(b.size()));
                BEAST_EXPECT(x.size() == b.size());

                // Try to allocate some number of bytes, possibly
                // zero (which means clear) and sanity check
                BEAST_EXPECT(x.alloc(i));
                BEAST_EXPECT(x.size() == i);
                BEAST_EXPECT((i == 0 && x.empty()) || (i != 0 && !x.empty()));

                // Try to allocate some more data (always non-zero)
                BEAST_EXPECT(x.alloc(i + 1));
                BEAST_EXPECT(x.size() == i + 1);
                BEAST_EXPECT(!x.empty());

                // Try to clear:
                x.clear();
                BEAST_EXPECT(x.empty() && (x.size() == 0));

                // Try to clear again:
                x.clear();
                BEAST_EXPECT(x.empty() && (x.size() == 0));
            };

            for (std::size_t i = 0; i < 256; ++i)
            {
                test(b0, i);
                test(b1, i);
            }
        }
    }
};

BEAST_DEFINE_TESTSUITE(Buffer, ripple_basics, ripple);

}  // namespace test
}  // namespace ripple
