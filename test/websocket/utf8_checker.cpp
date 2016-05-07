﻿//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/websocket/detail/utf8_checker.hpp>

#include <beast/streambuf.hpp>
#include <beast/unit_test/suite.hpp>
#include <array>

namespace beast {
namespace websocket {

class utf8_checker_test : public beast::unit_test::suite
{
public:
    void
    testOneByteSequence()
    {
        detail::utf8_checker utf8;
        std::array<std::uint8_t, 256> const buf =
            ([]()
            {
                std::array<std::uint8_t, 256> values;
                std::uint8_t i = 0;
                for (auto& c : values)
                    c = i++;
                return values;
            })();

        // Valid range 0-127
        expect(utf8.write(buf.data(), 128));
        expect(utf8.finish());

        // Invalid range 128-193
        for (auto it = std::next(buf.begin(), 128);
            it != std::next(buf.begin(), 194); ++it)
                expect(! utf8.write(&(*it), 1));

        // Invalid range 245-255
        for (auto it = std::next(buf.begin(), 245);
            it != buf.end(); ++it)
                expect(! utf8.write(&(*it), 1));
    }

    void
    testTwoByteSequence()
    {
        detail::utf8_checker utf8;
        std::uint8_t buf[2];
        for(auto i = 194; i <= 223; ++i)
        {
            // First byte valid range 194-223
            buf[0] = static_cast<std::uint8_t>(i);

            for (auto j = 128; j <= 191; ++j)
            {
                // Second byte valid range 128-191
                buf[1] = static_cast<std::uint8_t>(j);
                expect(utf8.write(buf, 2));
                expect(utf8.finish());
            }

            for (auto j = 0; j <= 127; ++j)
            {
                // Second byte invalid range 0-127
                buf[1] = static_cast<std::uint8_t>(j);
                expect(! utf8.write(buf, 2));
            }

            for (auto j = 192; j <= 255; ++j)
            {
                // Second byte invalid range 192-255
                buf[1] = static_cast<std::uint8_t>(j);
                expect(! utf8.write(buf, 2));
            }
        }
    }

    void
    testThreeByteSequence()
    {
        detail::utf8_checker utf8;
        std::uint8_t buf[3];
        for (auto i = 224; i <= 239; ++i)
        {
            // First byte valid range 224-239
            buf[0] = static_cast<std::uint8_t>(i);

            std::int32_t const b = (i == 224 ? 160 : 128);
            std::int32_t const e = (i == 237 ? 159 : 191);
            for (auto j = b; j <= e; ++j)
            {
                // Second byte valid range 128-191 or 160-191 or 128-159
                buf[1] = static_cast<std::uint8_t>(j);

                for (auto k = 128; k <= 191; ++k)
                {
                    // Third byte valid range 128-191
                    buf[2] = static_cast<std::uint8_t>(k);
                    expect(utf8.write(buf, 3));
                    expect(utf8.finish());
                }

                for (auto k = 0; k <= 127; ++k)
                {
                    // Third byte invalid range 0-127
                    buf[2] = static_cast<std::uint8_t>(k);
                    expect(! utf8.write(buf, 3));
                }

                for (auto k = 192; k <= 255; ++k)
                {
                    // Third byte invalid range 192-255
                    buf[2] = static_cast<std::uint8_t>(k);
                    expect(! utf8.write(buf, 3));
                }
            }

            for (auto j = 0; j < b; ++j)
            {
                // Second byte invalid range 0-127 or 0-159
                buf[1] = static_cast<std::uint8_t>(j);
                expect(! utf8.write(buf, 3));
            }

            for (auto j = e + 1; j <= 255; ++j)
            {
                // Second byte invalid range 160-255 or 192-255
                buf[1] = static_cast<std::uint8_t>(j);
                expect(! utf8.write(buf, 3));
            }
        }
    }

    void
    testFourByteSequence()
    {
        detail::utf8_checker utf8;
        std::uint8_t buf[4];
        for (auto i = 240; i <= 244; ++i)
        {
            // First byte valid range 240-244
            buf[0] = static_cast<std::uint8_t>(i);

            std::int32_t const b = (i == 240 ? 144 : 128);
            std::int32_t const e = (i == 244 ? 143 : 191);
            for (auto j = b; j <= e; ++j)
            {
                // Second byte valid range 128-191 or 144-191 or 128-143
                buf[1] = static_cast<std::uint8_t>(j);

                for (auto k = 128; k <= 191; ++k)
                {
                    // Third byte valid range 128-191
                    buf[2] = static_cast<std::uint8_t>(k);

                    for (auto n = 128; n <= 191; ++n)
                    {
                        // Fourth byte valid range 128-191
                        buf[3] = static_cast<std::uint8_t>(n);
                        expect(utf8.write(buf, 4));
                        expect(utf8.finish());
                    }

                    for (auto n = 0; n <= 127; ++n)
                    {
                        // Fourth byte invalid range 0-127
                        buf[3] = static_cast<std::uint8_t>(n);
                        expect(! utf8.write(buf, 4));
                    }

                    for (auto n = 192; n <= 255; ++n)
                    {
                        // Fourth byte invalid range 192-255
                        buf[3] = static_cast<std::uint8_t>(n);
                        expect(! utf8.write(buf, 4));
                    }
                }

                for (auto k = 0; k <= 127; ++k)
                {
                    // Third byte invalid range 0-127
                    buf[2] = static_cast<std::uint8_t>(k);
                    expect(! utf8.write(buf, 4));
                }

                for (auto k = 192; k <= 255; ++k)
                {
                    // Third byte invalid range 192-255
                    buf[2] = static_cast<std::uint8_t>(k);
                    expect(! utf8.write(buf, 4));
                }
            }

            for (auto j = 0; j < b; ++j)
            {
                // Second byte invalid range 0-127 or 0-143
                buf[1] = static_cast<std::uint8_t>(j);
                expect(! utf8.write(buf, 3));
            }

            for (auto j = e + 1; j <= 255; ++j)
            {
                // Second byte invalid range 144-255 or 192-255
                buf[1] = static_cast<std::uint8_t>(j);
                expect(! utf8.write(buf, 3));
            }
        }
    }

    void
    testWithStreamBuffer()
    {
        using namespace boost::asio;
        // Valid UTF8 encoded text
        std::vector<std::vector<std::uint8_t>> const data{
            {0x48,0x65,0x69,0x7A,0xC3,0xB6,0x6C,0x72,0xC3,0xBC,0x63,0x6B,
            0x73,0x74,0x6F,0xC3,0x9F,0x61,0x62,0x64,0xC3,0xA4,0x6D,0x70,
            0x66,0x75,0x6E,0x67},
            {0xCE,0x93,0xCE,0xB1,0xCE,0xB6,0xCE,0xAD,0xCE,0xB5,0xCF,0x82,
            0x20,0xCE,0xBA,0xCE,0xB1,0xE1,0xBD,0xB6,0x20,0xCE,0xBC,0xCF,
            0x85,0xCF,0x81,0xCF,0x84,0xCE,0xB9,0xE1,0xBD,0xB2,0xCF,0x82,
            0x20,0xCE,0xB4,0xE1,0xBD,0xB2,0xCE,0xBD,0x20,0xCE,0xB8,0xE1,
            0xBD,0xB0,0x20,0xCE,0xB2,0xCF,0x81,0xE1,0xBF,0xB6,0x20,0xCF,
            0x80,0xCE,0xB9,0xE1,0xBD,0xB0,0x20,0xCF,0x83,0xCF,0x84,0xE1,
            0xBD,0xB8,0x20,0xCF,0x87,0xCF,0x81,0xCF,0x85,0xCF,0x83,0xCE,
            0xB1,0xCF,0x86,0xE1,0xBD,0xB6,0x20,0xCE,0xBE,0xCE,0xAD,0xCF,
            0x86,0xCF,0x89,0xCF,0x84,0xCE,0xBF},
            {0xC3,0x81,0x72,0x76,0xC3,0xAD,0x7A,0x74,0xC5,0xB1,0x72,0xC5,
            0x91,0x20,0x74,0xC3,0xBC,0x6B,0xC3,0xB6,0x72,0x66,0xC3,0xBA,
            0x72,0xC3,0xB3,0x67,0xC3,0xA9,0x70}
        };
        detail::utf8_checker utf8;
        for(auto const& s : data)
        {
            beast::streambuf sb(
                s.size() / 4); // Force split across blocks
            sb.commit(buffer_copy(
                sb.prepare(s.size()),
                    const_buffer(s.data(), s.size())));
            expect(utf8.write(sb.data()));
            expect(utf8.finish());
        }
    }

    void run() override
    {
        testOneByteSequence();
        testTwoByteSequence();
        testThreeByteSequence();
        testFourByteSequence();
        testWithStreamBuffer();
    }
};

BEAST_DEFINE_TESTSUITE(utf8_checker,websocket,beast);

} // websocket
} // beast
