#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/SeqProxy.h>

#include <cstdint>
#include <limits>
#include <sstream>
#include <string>

namespace xrpl {

struct SeqProxy_test : public beast::unit_test::Suite
{
    // Exercise value(), isSeq(), and isTicket().
    static constexpr bool
    expectValues(SeqProxy seqProx, std::uint32_t value, SeqProxy::Type type)
    {
        bool const expectSeq{type == SeqProxy::Type::Seq};
        return (seqProx.value() == value) && (seqProx.isSeq() == expectSeq) &&
            (seqProx.isTicket() == !expectSeq);
    }

    // Exercise all SeqProxy comparison operators expecting lhs < rhs.
    static constexpr bool
    expectLt(SeqProxy lhs, SeqProxy rhs)
    {
        return (lhs < rhs) && (lhs <= rhs) && (!(lhs == rhs)) && (lhs != rhs) && (!(lhs >= rhs)) &&
            (!(lhs > rhs));
    }

    // Exercise all SeqProxy comparison operators expecting lhs == rhs.
    static constexpr bool
    expectEq(SeqProxy lhs, SeqProxy rhs)
    {
        return (!(lhs < rhs)) && (lhs <= rhs) && (lhs == rhs) && (!(lhs != rhs)) && (lhs >= rhs) &&
            (!(lhs > rhs));
    }

    // Exercise all SeqProxy comparison operators expecting lhs > rhs.
    static constexpr bool
    expectGt(SeqProxy lhs, SeqProxy rhs)
    {
        return (!(lhs < rhs)) && (!(lhs <= rhs)) && (!(lhs == rhs)) && (lhs != rhs) &&
            (lhs >= rhs) && (lhs > rhs);
    }

    // Verify streaming.
    static bool
    streamTest(SeqProxy seqProx)
    {
        std::string const type{seqProx.isSeq() ? "sequence" : "ticket"};
        std::string const value{std::to_string(seqProx.value())};

        std::stringstream ss;
        ss << seqProx;
        std::string str{ss.str()};

        return str.starts_with(type) && str[type.size()] == ' ' &&
            str.find(value) == (type.size() + 1);
    }

    void
    run() override
    {
        // While SeqProxy supports values of zero, they are not
        // expected in the wild.  Nevertheless they are tested here.
        // But so are values of 1, which are expected to occur in the wild.
        static constexpr std::uint32_t kUintMax{std::numeric_limits<std::uint32_t>::max()};
        static constexpr SeqProxy::Type kSEQ{SeqProxy::Type::Seq};
        static constexpr SeqProxy::Type kTICKET{SeqProxy::Type::Ticket};

        static constexpr SeqProxy kSeqZero{kSEQ, 0};
        static constexpr SeqProxy kSeqSmall{kSEQ, 1};
        static constexpr SeqProxy kSeqMiD0{kSEQ, 2};
        static constexpr SeqProxy kSeqMiD1{kSeqMiD0};
        static constexpr SeqProxy kSeqBig{kSEQ, kUintMax};

        static constexpr SeqProxy kTicZero{kTICKET, 0};
        static constexpr SeqProxy kTicSmall{kTICKET, 1};
        static constexpr SeqProxy kTicMiD0{kTICKET, 2};
        static constexpr SeqProxy kTicMiD1{kTicMiD0};
        static constexpr SeqProxy kTicBig{kTICKET, kUintMax};

        // Verify operation of value(), isSeq() and isTicket().
        static_assert(expectValues(kSeqZero, 0, kSEQ), "");
        static_assert(expectValues(kSeqSmall, 1, kSEQ), "");
        static_assert(expectValues(kSeqMiD0, 2, kSEQ), "");
        static_assert(expectValues(kSeqMiD1, 2, kSEQ), "");
        static_assert(expectValues(kSeqBig, kUintMax, kSEQ), "");

        static_assert(expectValues(kTicZero, 0, kTICKET), "");
        static_assert(expectValues(kTicSmall, 1, kTICKET), "");
        static_assert(expectValues(kTicMiD0, 2, kTICKET), "");
        static_assert(expectValues(kTicMiD1, 2, kTICKET), "");
        static_assert(expectValues(kTicBig, kUintMax, kTICKET), "");

        // Verify expected behavior of comparison operators.
        static_assert(expectEq(kSeqZero, kSeqZero), "");
        static_assert(expectLt(kSeqZero, kSeqSmall), "");
        static_assert(expectLt(kSeqZero, kSeqMiD0), "");
        static_assert(expectLt(kSeqZero, kSeqMiD1), "");
        static_assert(expectLt(kSeqZero, kSeqBig), "");
        static_assert(expectLt(kSeqZero, kTicZero), "");
        static_assert(expectLt(kSeqZero, kTicSmall), "");
        static_assert(expectLt(kSeqZero, kTicMiD0), "");
        static_assert(expectLt(kSeqZero, kTicMiD1), "");
        static_assert(expectLt(kSeqZero, kTicBig), "");

        static_assert(expectGt(kSeqSmall, kSeqZero), "");
        static_assert(expectEq(kSeqSmall, kSeqSmall), "");
        static_assert(expectLt(kSeqSmall, kSeqMiD0), "");
        static_assert(expectLt(kSeqSmall, kSeqMiD1), "");
        static_assert(expectLt(kSeqSmall, kSeqBig), "");
        static_assert(expectLt(kSeqSmall, kTicZero), "");
        static_assert(expectLt(kSeqSmall, kTicSmall), "");
        static_assert(expectLt(kSeqSmall, kTicMiD0), "");
        static_assert(expectLt(kSeqSmall, kTicMiD1), "");
        static_assert(expectLt(kSeqSmall, kTicBig), "");

        static_assert(expectGt(kSeqMiD0, kSeqZero), "");
        static_assert(expectGt(kSeqMiD0, kSeqSmall), "");
        static_assert(expectEq(kSeqMiD0, kSeqMiD0), "");
        static_assert(expectEq(kSeqMiD0, kSeqMiD1), "");
        static_assert(expectLt(kSeqMiD0, kSeqBig), "");
        static_assert(expectLt(kSeqMiD0, kTicZero), "");
        static_assert(expectLt(kSeqMiD0, kTicSmall), "");
        static_assert(expectLt(kSeqMiD0, kTicMiD0), "");
        static_assert(expectLt(kSeqMiD0, kTicMiD1), "");
        static_assert(expectLt(kSeqMiD0, kTicBig), "");

        static_assert(expectGt(kSeqMiD1, kSeqZero), "");
        static_assert(expectGt(kSeqMiD1, kSeqSmall), "");
        static_assert(expectEq(kSeqMiD1, kSeqMiD0), "");
        static_assert(expectEq(kSeqMiD1, kSeqMiD1), "");
        static_assert(expectLt(kSeqMiD1, kSeqBig), "");
        static_assert(expectLt(kSeqMiD1, kTicZero), "");
        static_assert(expectLt(kSeqMiD1, kTicSmall), "");
        static_assert(expectLt(kSeqMiD1, kTicMiD0), "");
        static_assert(expectLt(kSeqMiD1, kTicMiD1), "");
        static_assert(expectLt(kSeqMiD1, kTicBig), "");

        static_assert(expectGt(kSeqBig, kSeqZero), "");
        static_assert(expectGt(kSeqBig, kSeqSmall), "");
        static_assert(expectGt(kSeqBig, kSeqMiD0), "");
        static_assert(expectGt(kSeqBig, kSeqMiD1), "");
        static_assert(expectEq(kSeqBig, kSeqBig), "");
        static_assert(expectLt(kSeqBig, kTicZero), "");
        static_assert(expectLt(kSeqBig, kTicSmall), "");
        static_assert(expectLt(kSeqBig, kTicMiD0), "");
        static_assert(expectLt(kSeqBig, kTicMiD1), "");
        static_assert(expectLt(kSeqBig, kTicBig), "");

        static_assert(expectGt(kTicZero, kSeqZero), "");
        static_assert(expectGt(kTicZero, kSeqSmall), "");
        static_assert(expectGt(kTicZero, kSeqMiD0), "");
        static_assert(expectGt(kTicZero, kSeqMiD1), "");
        static_assert(expectGt(kTicZero, kSeqBig), "");
        static_assert(expectEq(kTicZero, kTicZero), "");
        static_assert(expectLt(kTicZero, kTicSmall), "");
        static_assert(expectLt(kTicZero, kTicMiD0), "");
        static_assert(expectLt(kTicZero, kTicMiD1), "");
        static_assert(expectLt(kTicZero, kTicBig), "");

        static_assert(expectGt(kTicSmall, kSeqZero), "");
        static_assert(expectGt(kTicSmall, kSeqSmall), "");
        static_assert(expectGt(kTicSmall, kSeqMiD0), "");
        static_assert(expectGt(kTicSmall, kSeqMiD1), "");
        static_assert(expectGt(kTicSmall, kSeqBig), "");
        static_assert(expectGt(kTicSmall, kTicZero), "");
        static_assert(expectEq(kTicSmall, kTicSmall), "");
        static_assert(expectLt(kTicSmall, kTicMiD0), "");
        static_assert(expectLt(kTicSmall, kTicMiD1), "");
        static_assert(expectLt(kTicSmall, kTicBig), "");

        static_assert(expectGt(kTicMiD0, kSeqZero), "");
        static_assert(expectGt(kTicMiD0, kSeqSmall), "");
        static_assert(expectGt(kTicMiD0, kSeqMiD0), "");
        static_assert(expectGt(kTicMiD0, kSeqMiD1), "");
        static_assert(expectGt(kTicMiD0, kSeqBig), "");
        static_assert(expectGt(kTicMiD0, kTicZero), "");
        static_assert(expectGt(kTicMiD0, kTicSmall), "");
        static_assert(expectEq(kTicMiD0, kTicMiD0), "");
        static_assert(expectEq(kTicMiD0, kTicMiD1), "");
        static_assert(expectLt(kTicMiD0, kTicBig), "");

        static_assert(expectGt(kTicMiD1, kSeqZero), "");
        static_assert(expectGt(kTicMiD1, kSeqSmall), "");
        static_assert(expectGt(kTicMiD1, kSeqMiD0), "");
        static_assert(expectGt(kTicMiD1, kSeqMiD1), "");
        static_assert(expectGt(kTicMiD1, kSeqBig), "");
        static_assert(expectGt(kTicMiD1, kTicZero), "");
        static_assert(expectGt(kTicMiD1, kTicSmall), "");
        static_assert(expectEq(kTicMiD1, kTicMiD0), "");
        static_assert(expectEq(kTicMiD1, kTicMiD1), "");
        static_assert(expectLt(kTicMiD1, kTicBig), "");

        static_assert(expectGt(kTicBig, kSeqZero), "");
        static_assert(expectGt(kTicBig, kSeqSmall), "");
        static_assert(expectGt(kTicBig, kSeqMiD0), "");
        static_assert(expectGt(kTicBig, kSeqMiD1), "");
        static_assert(expectGt(kTicBig, kSeqBig), "");
        static_assert(expectGt(kTicBig, kTicZero), "");
        static_assert(expectGt(kTicBig, kTicSmall), "");
        static_assert(expectGt(kTicBig, kTicMiD0), "");
        static_assert(expectGt(kTicBig, kTicMiD1), "");
        static_assert(expectEq(kTicBig, kTicBig), "");

        // Verify streaming.
        BEAST_EXPECT(streamTest(kSeqZero));
        BEAST_EXPECT(streamTest(kSeqSmall));
        BEAST_EXPECT(streamTest(kSeqMiD0));
        BEAST_EXPECT(streamTest(kSeqMiD1));
        BEAST_EXPECT(streamTest(kSeqBig));
        BEAST_EXPECT(streamTest(kTicZero));
        BEAST_EXPECT(streamTest(kTicSmall));
        BEAST_EXPECT(streamTest(kTicMiD0));
        BEAST_EXPECT(streamTest(kTicMiD1));
        BEAST_EXPECT(streamTest(kTicBig));
    }
};

BEAST_DEFINE_TESTSUITE(SeqProxy, protocol, xrpl);

}  // namespace xrpl
