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
        static constexpr std::uint32_t kUINT_MAX{std::numeric_limits<std::uint32_t>::max()};
        static constexpr SeqProxy::Type kSEQ{SeqProxy::Type::Seq};
        static constexpr SeqProxy::Type kTICKET{SeqProxy::Type::Ticket};

        static constexpr SeqProxy kSEQ_ZERO{kSEQ, 0};
        static constexpr SeqProxy kSEQ_SMALL{kSEQ, 1};
        static constexpr SeqProxy kSEQ_MID0{kSEQ, 2};
        static constexpr SeqProxy kSEQ_MID1{kSEQ_MID0};
        static constexpr SeqProxy kSEQ_BIG{kSEQ, kUINT_MAX};

        static constexpr SeqProxy kTIC_ZERO{kTICKET, 0};
        static constexpr SeqProxy kTIC_SMALL{kTICKET, 1};
        static constexpr SeqProxy kTIC_MID0{kTICKET, 2};
        static constexpr SeqProxy kTIC_MID1{kTIC_MID0};
        static constexpr SeqProxy kTIC_BIG{kTICKET, kUINT_MAX};

        // Verify operation of value(), isSeq() and isTicket().
        static_assert(expectValues(kSEQ_ZERO, 0, kSEQ), "");
        static_assert(expectValues(kSEQ_SMALL, 1, kSEQ), "");
        static_assert(expectValues(kSEQ_MID0, 2, kSEQ), "");
        static_assert(expectValues(kSEQ_MID1, 2, kSEQ), "");
        static_assert(expectValues(kSEQ_BIG, kUINT_MAX, kSEQ), "");

        static_assert(expectValues(kTIC_ZERO, 0, kTICKET), "");
        static_assert(expectValues(kTIC_SMALL, 1, kTICKET), "");
        static_assert(expectValues(kTIC_MID0, 2, kTICKET), "");
        static_assert(expectValues(kTIC_MID1, 2, kTICKET), "");
        static_assert(expectValues(kTIC_BIG, kUINT_MAX, kTICKET), "");

        // Verify expected behavior of comparison operators.
        static_assert(expectEq(kSEQ_ZERO, kSEQ_ZERO), "");
        static_assert(expectLt(kSEQ_ZERO, kSEQ_SMALL), "");
        static_assert(expectLt(kSEQ_ZERO, kSEQ_MID0), "");
        static_assert(expectLt(kSEQ_ZERO, kSEQ_MID1), "");
        static_assert(expectLt(kSEQ_ZERO, kSEQ_BIG), "");
        static_assert(expectLt(kSEQ_ZERO, kTIC_ZERO), "");
        static_assert(expectLt(kSEQ_ZERO, kTIC_SMALL), "");
        static_assert(expectLt(kSEQ_ZERO, kTIC_MID0), "");
        static_assert(expectLt(kSEQ_ZERO, kTIC_MID1), "");
        static_assert(expectLt(kSEQ_ZERO, kTIC_BIG), "");

        static_assert(expectGt(kSEQ_SMALL, kSEQ_ZERO), "");
        static_assert(expectEq(kSEQ_SMALL, kSEQ_SMALL), "");
        static_assert(expectLt(kSEQ_SMALL, kSEQ_MID0), "");
        static_assert(expectLt(kSEQ_SMALL, kSEQ_MID1), "");
        static_assert(expectLt(kSEQ_SMALL, kSEQ_BIG), "");
        static_assert(expectLt(kSEQ_SMALL, kTIC_ZERO), "");
        static_assert(expectLt(kSEQ_SMALL, kTIC_SMALL), "");
        static_assert(expectLt(kSEQ_SMALL, kTIC_MID0), "");
        static_assert(expectLt(kSEQ_SMALL, kTIC_MID1), "");
        static_assert(expectLt(kSEQ_SMALL, kTIC_BIG), "");

        static_assert(expectGt(kSEQ_MID0, kSEQ_ZERO), "");
        static_assert(expectGt(kSEQ_MID0, kSEQ_SMALL), "");
        static_assert(expectEq(kSEQ_MID0, kSEQ_MID0), "");
        static_assert(expectEq(kSEQ_MID0, kSEQ_MID1), "");
        static_assert(expectLt(kSEQ_MID0, kSEQ_BIG), "");
        static_assert(expectLt(kSEQ_MID0, kTIC_ZERO), "");
        static_assert(expectLt(kSEQ_MID0, kTIC_SMALL), "");
        static_assert(expectLt(kSEQ_MID0, kTIC_MID0), "");
        static_assert(expectLt(kSEQ_MID0, kTIC_MID1), "");
        static_assert(expectLt(kSEQ_MID0, kTIC_BIG), "");

        static_assert(expectGt(kSEQ_MID1, kSEQ_ZERO), "");
        static_assert(expectGt(kSEQ_MID1, kSEQ_SMALL), "");
        static_assert(expectEq(kSEQ_MID1, kSEQ_MID0), "");
        static_assert(expectEq(kSEQ_MID1, kSEQ_MID1), "");
        static_assert(expectLt(kSEQ_MID1, kSEQ_BIG), "");
        static_assert(expectLt(kSEQ_MID1, kTIC_ZERO), "");
        static_assert(expectLt(kSEQ_MID1, kTIC_SMALL), "");
        static_assert(expectLt(kSEQ_MID1, kTIC_MID0), "");
        static_assert(expectLt(kSEQ_MID1, kTIC_MID1), "");
        static_assert(expectLt(kSEQ_MID1, kTIC_BIG), "");

        static_assert(expectGt(kSEQ_BIG, kSEQ_ZERO), "");
        static_assert(expectGt(kSEQ_BIG, kSEQ_SMALL), "");
        static_assert(expectGt(kSEQ_BIG, kSEQ_MID0), "");
        static_assert(expectGt(kSEQ_BIG, kSEQ_MID1), "");
        static_assert(expectEq(kSEQ_BIG, kSEQ_BIG), "");
        static_assert(expectLt(kSEQ_BIG, kTIC_ZERO), "");
        static_assert(expectLt(kSEQ_BIG, kTIC_SMALL), "");
        static_assert(expectLt(kSEQ_BIG, kTIC_MID0), "");
        static_assert(expectLt(kSEQ_BIG, kTIC_MID1), "");
        static_assert(expectLt(kSEQ_BIG, kTIC_BIG), "");

        static_assert(expectGt(kTIC_ZERO, kSEQ_ZERO), "");
        static_assert(expectGt(kTIC_ZERO, kSEQ_SMALL), "");
        static_assert(expectGt(kTIC_ZERO, kSEQ_MID0), "");
        static_assert(expectGt(kTIC_ZERO, kSEQ_MID1), "");
        static_assert(expectGt(kTIC_ZERO, kSEQ_BIG), "");
        static_assert(expectEq(kTIC_ZERO, kTIC_ZERO), "");
        static_assert(expectLt(kTIC_ZERO, kTIC_SMALL), "");
        static_assert(expectLt(kTIC_ZERO, kTIC_MID0), "");
        static_assert(expectLt(kTIC_ZERO, kTIC_MID1), "");
        static_assert(expectLt(kTIC_ZERO, kTIC_BIG), "");

        static_assert(expectGt(kTIC_SMALL, kSEQ_ZERO), "");
        static_assert(expectGt(kTIC_SMALL, kSEQ_SMALL), "");
        static_assert(expectGt(kTIC_SMALL, kSEQ_MID0), "");
        static_assert(expectGt(kTIC_SMALL, kSEQ_MID1), "");
        static_assert(expectGt(kTIC_SMALL, kSEQ_BIG), "");
        static_assert(expectGt(kTIC_SMALL, kTIC_ZERO), "");
        static_assert(expectEq(kTIC_SMALL, kTIC_SMALL), "");
        static_assert(expectLt(kTIC_SMALL, kTIC_MID0), "");
        static_assert(expectLt(kTIC_SMALL, kTIC_MID1), "");
        static_assert(expectLt(kTIC_SMALL, kTIC_BIG), "");

        static_assert(expectGt(kTIC_MID0, kSEQ_ZERO), "");
        static_assert(expectGt(kTIC_MID0, kSEQ_SMALL), "");
        static_assert(expectGt(kTIC_MID0, kSEQ_MID0), "");
        static_assert(expectGt(kTIC_MID0, kSEQ_MID1), "");
        static_assert(expectGt(kTIC_MID0, kSEQ_BIG), "");
        static_assert(expectGt(kTIC_MID0, kTIC_ZERO), "");
        static_assert(expectGt(kTIC_MID0, kTIC_SMALL), "");
        static_assert(expectEq(kTIC_MID0, kTIC_MID0), "");
        static_assert(expectEq(kTIC_MID0, kTIC_MID1), "");
        static_assert(expectLt(kTIC_MID0, kTIC_BIG), "");

        static_assert(expectGt(kTIC_MID1, kSEQ_ZERO), "");
        static_assert(expectGt(kTIC_MID1, kSEQ_SMALL), "");
        static_assert(expectGt(kTIC_MID1, kSEQ_MID0), "");
        static_assert(expectGt(kTIC_MID1, kSEQ_MID1), "");
        static_assert(expectGt(kTIC_MID1, kSEQ_BIG), "");
        static_assert(expectGt(kTIC_MID1, kTIC_ZERO), "");
        static_assert(expectGt(kTIC_MID1, kTIC_SMALL), "");
        static_assert(expectEq(kTIC_MID1, kTIC_MID0), "");
        static_assert(expectEq(kTIC_MID1, kTIC_MID1), "");
        static_assert(expectLt(kTIC_MID1, kTIC_BIG), "");

        static_assert(expectGt(kTIC_BIG, kSEQ_ZERO), "");
        static_assert(expectGt(kTIC_BIG, kSEQ_SMALL), "");
        static_assert(expectGt(kTIC_BIG, kSEQ_MID0), "");
        static_assert(expectGt(kTIC_BIG, kSEQ_MID1), "");
        static_assert(expectGt(kTIC_BIG, kSEQ_BIG), "");
        static_assert(expectGt(kTIC_BIG, kTIC_ZERO), "");
        static_assert(expectGt(kTIC_BIG, kTIC_SMALL), "");
        static_assert(expectGt(kTIC_BIG, kTIC_MID0), "");
        static_assert(expectGt(kTIC_BIG, kTIC_MID1), "");
        static_assert(expectEq(kTIC_BIG, kTIC_BIG), "");

        // Verify streaming.
        BEAST_EXPECT(streamTest(kSEQ_ZERO));
        BEAST_EXPECT(streamTest(kSEQ_SMALL));
        BEAST_EXPECT(streamTest(kSEQ_MID0));
        BEAST_EXPECT(streamTest(kSEQ_MID1));
        BEAST_EXPECT(streamTest(kSEQ_BIG));
        BEAST_EXPECT(streamTest(kTIC_ZERO));
        BEAST_EXPECT(streamTest(kTIC_SMALL));
        BEAST_EXPECT(streamTest(kTIC_MID0));
        BEAST_EXPECT(streamTest(kTIC_MID1));
        BEAST_EXPECT(streamTest(kTIC_BIG));
    }
};

BEAST_DEFINE_TESTSUITE(SeqProxy, protocol, xrpl);

}  // namespace xrpl
