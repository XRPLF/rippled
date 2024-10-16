//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/Serializer.h>

#include <limits>

namespace ripple {

struct Serializer_test : public beast::unit_test::suite
{
    void
    run() override
    {
        {
            std::initializer_list<std::int32_t> const values = {
                std::numeric_limits<std::int32_t>::min(),
                -1,
                0,
                1,
                std::numeric_limits<std::int32_t>::max()};
            for (std::int32_t value : values)
            {
                Serializer s;
                s.add32(value);
                BEAST_EXPECT(s.size() == 4);
                SerialIter sit(s.slice());
                BEAST_EXPECT(sit.geti32() == value);
            }
        }
        {
            std::initializer_list<std::int64_t> const values = {
                std::numeric_limits<std::int64_t>::min(),
                -1,
                0,
                1,
                std::numeric_limits<std::int64_t>::max()};
            for (std::int64_t value : values)
            {
                Serializer s;
                s.add64(value);
                BEAST_EXPECT(s.size() == 8);
                SerialIter sit(s.slice());
                BEAST_EXPECT(sit.geti64() == value);
            }
        }
    }
};

BEAST_DEFINE_TESTSUITE(Serializer, protocol, ripple);

}  // namespace ripple
