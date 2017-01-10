//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
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

#include <BeastConfig.h>
#include <ripple/basics/base_uint.h>
#include <ripple/basics/hardened_hash.h>
#include <ripple/beast/unit_test.h>
#include <boost/algorithm/string.hpp>

namespace ripple {
namespace test {

struct base_uint_test : beast::unit_test::suite
{
    using test96 = base_uint<96>;

    void run()
    {
        // used to verify set insertion (hashing required)
        std::unordered_set<test96, hardened_hash<>> uset;

        Blob raw { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
        BEAST_EXPECT(test96::bytes == raw.size());

        test96 u { raw };
        uset.insert(u);
        BEAST_EXPECT(raw.size() == u.size());
        BEAST_EXPECT(to_string(u) == "0102030405060708090A0B0C");
        BEAST_EXPECT(*u.data() == 1);
        BEAST_EXPECT(u.signum() == 1);
        BEAST_EXPECT(!!u);
        BEAST_EXPECT(!u.isZero());
        BEAST_EXPECT(u.isNonZero());
        unsigned char t = 0;
        for (auto& d : u)
        {
            BEAST_EXPECT(d == ++t);
        }

        test96 v { ~u };
        uset.insert(v);
        BEAST_EXPECT(to_string(v) == "FEFDFCFBFAF9F8F7F6F5F4F3");
        BEAST_EXPECT(*v.data() == 0xfe);
        BEAST_EXPECT(v.signum() == 1);
        BEAST_EXPECT(!!v);
        BEAST_EXPECT(!v.isZero());
        BEAST_EXPECT(v.isNonZero());
        t = 0xff;
        for (auto& d : v)
        {
            BEAST_EXPECT(d == --t);
        }

        BEAST_EXPECT(compare(u, v) < 0);
        BEAST_EXPECT(compare(v, u) > 0);

        v = u;
        BEAST_EXPECT(v == u);

        test96 z { beast::zero };
        uset.insert(z);
        BEAST_EXPECT(to_string(z) == "000000000000000000000000");
        BEAST_EXPECT(*z.data() == 0);
        BEAST_EXPECT(*z.begin() == 0);
        BEAST_EXPECT(*std::prev(z.end(), 1) == 0);
        BEAST_EXPECT(z.signum() == 0);
        BEAST_EXPECT(!z);
        BEAST_EXPECT(z.isZero());
        BEAST_EXPECT(!z.isNonZero());
        for (auto& d : z)
        {
            BEAST_EXPECT(d == 0);
        }

        test96 n { z };
        n++;
        BEAST_EXPECT(n == test96(1));
        n--;
        BEAST_EXPECT(n == beast::zero);
        BEAST_EXPECT(n == z);
        n--;
        BEAST_EXPECT(to_string(n) == "FFFFFFFFFFFFFFFFFFFFFFFF");
        n = beast::zero;
        BEAST_EXPECT(n == z);

        test96 x { (--test96 { z }) ^ (++test96 { z }) };
        uset.insert(x);
        BEAST_EXPECT(to_string(x) == "FFFFFFFFFFFFFFFFFFFFFFFE");

        BEAST_EXPECT(uset.size() == 4);

        // SetHex tests...
        test96 fromHex;
        BEAST_EXPECT(fromHex.SetHexExact(to_string(u)));
        BEAST_EXPECT(fromHex == u);
        fromHex = z;

        // fails with extra char
        BEAST_EXPECT(! fromHex.SetHexExact("A" + to_string(u)));
        BEAST_EXPECT(fromHex != u);
        fromHex = z;

        // fails with extra char at end, but the value is still parsed (?)
        BEAST_EXPECT(! fromHex.SetHexExact(to_string(u) + "A"));
        BEAST_EXPECT(fromHex == u);
        fromHex = z;

        BEAST_EXPECT(fromHex.SetHex(to_string(u)));
        BEAST_EXPECT(fromHex == u);
        fromHex = z;

        // leading space/0x allowed if not strict
        BEAST_EXPECT(fromHex.SetHex("  0x" + to_string(u)));
        BEAST_EXPECT(fromHex == u);
        fromHex = z;

        // other leading chars also allowed (ignored)
        BEAST_EXPECT(fromHex.SetHex("FEFEFE" + to_string(u)));
        BEAST_EXPECT(fromHex == u);
        fromHex = z;

        // invalid hex chars should fail (0 replaced with Z here)
        BEAST_EXPECT(! fromHex.SetHex(
            boost::algorithm::replace_all_copy(to_string(u), "0", "Z")));
        BEAST_EXPECT(fromHex != u);
        fromHex = z;

        BEAST_EXPECT(fromHex.SetHex(to_string(u), true));
        BEAST_EXPECT(fromHex == u);
        fromHex = z;

        // strict mode fails with leading chars
        BEAST_EXPECT(! fromHex.SetHex("  0x" + to_string(u), true));
        BEAST_EXPECT(fromHex != u);
        fromHex = z;
    }
};

BEAST_DEFINE_TESTSUITE(base_uint, ripple_basics, ripple);

}  // namespace test
}  // namespace ripple
