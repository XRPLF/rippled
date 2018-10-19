//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

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
#include <ripple/protocol/SeqProxy.h>
#include <limits>
#include <sstream>

namespace ripple {

struct SeqProxy_test : public beast::unit_test::suite
{
    // Exercise value(), isSeq(), and isTicket().
    static constexpr bool
    expectValues(SeqProxy seqProx, std::uint32_t value, SeqProxy::Type type)
    {
        bool const expectSeq{type == SeqProxy::seq};
        return (seqProx.value() == value) && (seqProx.isSeq() == expectSeq) &&
            (seqProx.isTicket() == !expectSeq);
    }

    // Exercise all SeqProxy comparison operators expecting lhs < rhs.
    static constexpr bool
    expectLt(SeqProxy lhs, SeqProxy rhs)
    {
        return (lhs < rhs) && (lhs <= rhs) && (!(lhs == rhs)) && (lhs != rhs) &&
            (!(lhs >= rhs)) && (!(lhs > rhs));
    }

    // Exercise all SeqProxy comparison operators expecting lhs == rhs.
    static constexpr bool
    expectEq(SeqProxy lhs, SeqProxy rhs)
    {
        return (!(lhs < rhs)) && (lhs <= rhs) && (lhs == rhs) &&
            (!(lhs != rhs)) && (lhs >= rhs) && (!(lhs > rhs));
    }

    // Exercise all SeqProxy comparison operators expecting lhs > rhs.
    static constexpr bool
    expectGt(SeqProxy lhs, SeqProxy rhs)
    {
        return (!(lhs < rhs)) && (!(lhs <= rhs)) && (!(lhs == rhs)) &&
            (lhs != rhs) && (lhs >= rhs) && (lhs > rhs);
    }

    // Verify streaming.
    bool
    streamTest(SeqProxy seqProx)
    {
        std::string const type{seqProx.isSeq() ? "sequence" : "ticket"};
        std::string const value{std::to_string(seqProx.value())};

        std::stringstream ss;
        ss << seqProx;
        std::string str{ss.str()};

        return str.find(type) == 0 && str[type.size()] == ' ' &&
            str.find(value) == (type.size() + 1);
    }

    void
    run() override
    {
        // While SeqProxy supports values of zero, they are not
        // expected in the wild.  Nevertheless they are tested here.
        // But so are values of 1, which are expected to occur in the wild.
        static constexpr std::uint32_t uintMax{
            std::numeric_limits<std::uint32_t>::max()};
        static constexpr SeqProxy::Type seq{SeqProxy::seq};
        static constexpr SeqProxy::Type ticket{SeqProxy::ticket};

        static constexpr SeqProxy seqZero{seq, 0};
        static constexpr SeqProxy seqSmall{seq, 1};
        static constexpr SeqProxy seqMid0{seq, 2};
        static constexpr SeqProxy seqMid1{seqMid0};
        static constexpr SeqProxy seqBig{seq, uintMax};

        static constexpr SeqProxy ticZero{ticket, 0};
        static constexpr SeqProxy ticSmall{ticket, 1};
        static constexpr SeqProxy ticMid0{ticket, 2};
        static constexpr SeqProxy ticMid1{ticMid0};
        static constexpr SeqProxy ticBig{ticket, uintMax};

        // Verify operation of value(), isSeq() and isTicket().
        static_assert(expectValues(seqZero, 0, seq), "");
        static_assert(expectValues(seqSmall, 1, seq), "");
        static_assert(expectValues(seqMid0, 2, seq), "");
        static_assert(expectValues(seqMid1, 2, seq), "");
        static_assert(expectValues(seqBig, uintMax, seq), "");

        static_assert(expectValues(ticZero, 0, ticket), "");
        static_assert(expectValues(ticSmall, 1, ticket), "");
        static_assert(expectValues(ticMid0, 2, ticket), "");
        static_assert(expectValues(ticMid1, 2, ticket), "");
        static_assert(expectValues(ticBig, uintMax, ticket), "");

        // Verify expected behavior of comparison operators.
        static_assert(expectEq(seqZero, seqZero), "");
        static_assert(expectLt(seqZero, seqSmall), "");
        static_assert(expectLt(seqZero, seqMid0), "");
        static_assert(expectLt(seqZero, seqMid1), "");
        static_assert(expectLt(seqZero, seqBig), "");
        static_assert(expectLt(seqZero, ticZero), "");
        static_assert(expectLt(seqZero, ticSmall), "");
        static_assert(expectLt(seqZero, ticMid0), "");
        static_assert(expectLt(seqZero, ticMid1), "");
        static_assert(expectLt(seqZero, ticBig), "");

        static_assert(expectGt(seqSmall, seqZero), "");
        static_assert(expectEq(seqSmall, seqSmall), "");
        static_assert(expectLt(seqSmall, seqMid0), "");
        static_assert(expectLt(seqSmall, seqMid1), "");
        static_assert(expectLt(seqSmall, seqBig), "");
        static_assert(expectLt(seqSmall, ticZero), "");
        static_assert(expectLt(seqSmall, ticSmall), "");
        static_assert(expectLt(seqSmall, ticMid0), "");
        static_assert(expectLt(seqSmall, ticMid1), "");
        static_assert(expectLt(seqSmall, ticBig), "");

        static_assert(expectGt(seqMid0, seqZero), "");
        static_assert(expectGt(seqMid0, seqSmall), "");
        static_assert(expectEq(seqMid0, seqMid0), "");
        static_assert(expectEq(seqMid0, seqMid1), "");
        static_assert(expectLt(seqMid0, seqBig), "");
        static_assert(expectLt(seqMid0, ticZero), "");
        static_assert(expectLt(seqMid0, ticSmall), "");
        static_assert(expectLt(seqMid0, ticMid0), "");
        static_assert(expectLt(seqMid0, ticMid1), "");
        static_assert(expectLt(seqMid0, ticBig), "");

        static_assert(expectGt(seqMid1, seqZero), "");
        static_assert(expectGt(seqMid1, seqSmall), "");
        static_assert(expectEq(seqMid1, seqMid0), "");
        static_assert(expectEq(seqMid1, seqMid1), "");
        static_assert(expectLt(seqMid1, seqBig), "");
        static_assert(expectLt(seqMid1, ticZero), "");
        static_assert(expectLt(seqMid1, ticSmall), "");
        static_assert(expectLt(seqMid1, ticMid0), "");
        static_assert(expectLt(seqMid1, ticMid1), "");
        static_assert(expectLt(seqMid1, ticBig), "");

        static_assert(expectGt(seqBig, seqZero), "");
        static_assert(expectGt(seqBig, seqSmall), "");
        static_assert(expectGt(seqBig, seqMid0), "");
        static_assert(expectGt(seqBig, seqMid1), "");
        static_assert(expectEq(seqBig, seqBig), "");
        static_assert(expectLt(seqBig, ticZero), "");
        static_assert(expectLt(seqBig, ticSmall), "");
        static_assert(expectLt(seqBig, ticMid0), "");
        static_assert(expectLt(seqBig, ticMid1), "");
        static_assert(expectLt(seqBig, ticBig), "");

        static_assert(expectGt(ticZero, seqZero), "");
        static_assert(expectGt(ticZero, seqSmall), "");
        static_assert(expectGt(ticZero, seqMid0), "");
        static_assert(expectGt(ticZero, seqMid1), "");
        static_assert(expectGt(ticZero, seqBig), "");
        static_assert(expectEq(ticZero, ticZero), "");
        static_assert(expectLt(ticZero, ticSmall), "");
        static_assert(expectLt(ticZero, ticMid0), "");
        static_assert(expectLt(ticZero, ticMid1), "");
        static_assert(expectLt(ticZero, ticBig), "");

        static_assert(expectGt(ticSmall, seqZero), "");
        static_assert(expectGt(ticSmall, seqSmall), "");
        static_assert(expectGt(ticSmall, seqMid0), "");
        static_assert(expectGt(ticSmall, seqMid1), "");
        static_assert(expectGt(ticSmall, seqBig), "");
        static_assert(expectGt(ticSmall, ticZero), "");
        static_assert(expectEq(ticSmall, ticSmall), "");
        static_assert(expectLt(ticSmall, ticMid0), "");
        static_assert(expectLt(ticSmall, ticMid1), "");
        static_assert(expectLt(ticSmall, ticBig), "");

        static_assert(expectGt(ticMid0, seqZero), "");
        static_assert(expectGt(ticMid0, seqSmall), "");
        static_assert(expectGt(ticMid0, seqMid0), "");
        static_assert(expectGt(ticMid0, seqMid1), "");
        static_assert(expectGt(ticMid0, seqBig), "");
        static_assert(expectGt(ticMid0, ticZero), "");
        static_assert(expectGt(ticMid0, ticSmall), "");
        static_assert(expectEq(ticMid0, ticMid0), "");
        static_assert(expectEq(ticMid0, ticMid1), "");
        static_assert(expectLt(ticMid0, ticBig), "");

        static_assert(expectGt(ticMid1, seqZero), "");
        static_assert(expectGt(ticMid1, seqSmall), "");
        static_assert(expectGt(ticMid1, seqMid0), "");
        static_assert(expectGt(ticMid1, seqMid1), "");
        static_assert(expectGt(ticMid1, seqBig), "");
        static_assert(expectGt(ticMid1, ticZero), "");
        static_assert(expectGt(ticMid1, ticSmall), "");
        static_assert(expectEq(ticMid1, ticMid0), "");
        static_assert(expectEq(ticMid1, ticMid1), "");
        static_assert(expectLt(ticMid1, ticBig), "");

        static_assert(expectGt(ticBig, seqZero), "");
        static_assert(expectGt(ticBig, seqSmall), "");
        static_assert(expectGt(ticBig, seqMid0), "");
        static_assert(expectGt(ticBig, seqMid1), "");
        static_assert(expectGt(ticBig, seqBig), "");
        static_assert(expectGt(ticBig, ticZero), "");
        static_assert(expectGt(ticBig, ticSmall), "");
        static_assert(expectGt(ticBig, ticMid0), "");
        static_assert(expectGt(ticBig, ticMid1), "");
        static_assert(expectEq(ticBig, ticBig), "");

        // Verify streaming.
        BEAST_EXPECT(streamTest(seqZero));
        BEAST_EXPECT(streamTest(seqSmall));
        BEAST_EXPECT(streamTest(seqMid0));
        BEAST_EXPECT(streamTest(seqMid1));
        BEAST_EXPECT(streamTest(seqBig));
        BEAST_EXPECT(streamTest(ticZero));
        BEAST_EXPECT(streamTest(ticSmall));
        BEAST_EXPECT(streamTest(ticMid0));
        BEAST_EXPECT(streamTest(ticMid1));
        BEAST_EXPECT(streamTest(ticBig));
    }
};

BEAST_DEFINE_TESTSUITE(SeqProxy, protocol, ripple);

}  // namespace ripple
