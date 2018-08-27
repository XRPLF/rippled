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

#include <ripple/beast/unit_test.h>
#include <ripple/nodestore/impl/varint.h>
#include <array>
#include <vector>

namespace ripple {
namespace NodeStore {
namespace tests {

class varint_test : public beast::unit_test::suite
{
public:
    void
    test_varints (std::vector<std::size_t> vv)
    {
        testcase("encode, decode");
        for (auto const v : vv)
        {
            std::array<std::uint8_t, varint_traits<std::size_t>::max> vi;
            auto const n0 = write_varint(vi.data(), v);
            expect (n0 > 0, "write error");
            expect(n0 == size_varint(v), "size error");
            std::size_t v1;
            auto const n1 = read_varint(vi.data(), n0, v1);
            expect(n1 == n0, "read error");
            expect(v == v1, "wrong value");
        }
    }

    void
    run() override
    {
        test_varints({
                0,     1,     2,
              126,   127,   128,
              253,   254,   255,
            16127, 16128, 16129,
            0xff,
            0xffff,
            0xffffffff,
            0xffffffffffffUL,
            0xffffffffffffffffUL});
    }
};

BEAST_DEFINE_TESTSUITE(varint,NodeStore,ripple);

} // test
} // NodeStore
} // ripple
