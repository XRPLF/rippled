#include <test/formal_verification/common/LeanSuite.h>
#include <test/formal_verification/numbers/helpers/NumberGenerators.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test.h>

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// Exported by the Lean model. See formal_verification/XRPL/FFI/Protocol/NumberFFI.lean.
extern "C" {
uint8_t
lean_number_lt(uint8_t, uint64_t, uint64_t, uint8_t, uint64_t, uint64_t);
}

namespace xrpl::test {

using namespace formal_verification;

class LeanNumber_test : public LeanSuite
{
    // Formats a value like "-123e-5" for failure messages.
    static std::string
    fmtNum(bool neg, uint64_t m, int e)
    {
        std::stringstream ss;
        ss << (neg ? "-" : "") << m << "e" << e;
        return ss.str();
    }

    static std::string
    fmtNum(NumberPair const& p)
    {
        return fmtNum(p.leanNum.negative != 0, p.leanNum.mantissa, p.cppNum.exponent());
    }

    using LeanCmpOp = uint8_t (*)(uint8_t, uint64_t, uint64_t, uint8_t, uint64_t, uint64_t);

    // Verify Lean and C++ agree on operator< for one pair.
    // Comparisons can't error, so we compare the raw bool results directly.
    bool
    checkCompare(std::string const& label, NumberPair const& a, NumberPair const& b)
    {
        auto leanCmp = [&](LeanCmpOp op) {
            return op(a.leanNum.negative,
                      a.leanNum.mantissa,
                      a.leanNum.exponent,
                      b.leanNum.negative,
                      b.leanNum.mantissa,
                      b.leanNum.exponent) != 0;
        };
        struct Case
        {
            char const* sym;
            bool lean;
            bool cpp;
        };
        Case const cases[] = {
            {"<", leanCmp(lean_number_lt), a.cppNum < b.cppNum},
        };
        bool ok = true;
        for (auto const& c : cases)
        {
            if (c.lean != c.cpp)
            {
                std::stringstream ss;
                ss << label << ' ' << c.sym << ": lean=" << c.lean << " cpp=" << c.cpp;
                fail(ss.str());
                ok = false;
            }
        }
        if (ok)
            pass();
        return ok;
    }

    std::vector<uint64_t>
    edgeMantissas() const
    {
        uint64_t const minM = Number::minMantissa();
        uint64_t const maxRep = Number::kMaxRep;
        uint64_t const maxM = Number::maxMantissa();
        return {
            minM,
            minM + 1,
            maxRep - 1,
            maxRep,
            Number::kMaxRep + 3,
            maxM - maxM % 10,
        };
    }

    std::vector<std::pair<int, int>>
    edgeExpPairs() const
    {
        return {
            {0, 0}, {0, 1}, {0, -1}, {0, 18}, {0, -40}, {40, 40}, {-40, -40}, {80, -96}, {-96, 80}};
    }

    std::vector<int>
    edgeExps() const
    {
        return {0, 1, -1, 18, -18, 40, -40, 80, -96};
    }

    // One operand: ~1/3 a random edge value, else a uniform interior value.
    NumberPair
    randomOperand()
    {
        constexpr double edgeBias = 1.0 / 3;
        auto& rng = nextRng();
        std::bernoulli_distribution useEdge(edgeBias);
        if (useEdge(rng))
        {
            auto const ms = edgeMantissas();
            auto const es = edgeExps();
            std::uniform_int_distribution<std::size_t> mi(0, ms.size() - 1);
            std::uniform_int_distribution<std::size_t> ei(0, es.size() - 1);
            std::bernoulli_distribution sign(0.5);
            return makeNumberPair(sign(rng), ms[mi(rng)], es[ei(rng)]);
        }
        return randomNumberPair(-96, 80);
    }

public:
    void
    testCompare()
    {
        beginCase("LeanNumber.compare", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        auto const ms = edgeMantissas();
        auto const exps = edgeExpPairs();

        for (uint64_t ma : ms)
            for (bool na : {false, true})
                for (uint64_t mb : ms)
                    for (bool nb : {false, true})
                        for (auto const& [ea, eb] : exps)
                        {
                            auto a = makeNumberPair(na, ma, ea);
                            auto b = makeNumberPair(nb, mb, eb);
                            checkCompare(fmtNum(a) + " ? " + fmtNum(b), a, b);
                        }
        for (uint64_t m : ms)
            for (bool n : {false, true})
                checkCompare(
                    "0 ? " + fmtNum(makeNumberPair(n, m, 0)),
                    makeNumberPair(false, 0, 0),
                    makeNumberPair(n, m, 0));

        runFuzz(10'000, [&] {
            auto a = randomOperand();
            auto b = randomOperand();
            bool ok = checkCompare(fmtNum(a) + " ? " + fmtNum(b), a, b);
            ok = checkCompare(fmtNum(a) + " ? self", a, a) && ok;
            return ok;
        });
    }

    // Regression for XRPLF/rippled#7406: equal-exponent negative comparison.
    void
    testNegativeComparison()
    {
        beginCase("LeanNumber.negative_comparison");
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);

        auto a = makeNumberPair(true, 3'000'000'000'000'000'000ULL, 0);
        auto b = makeNumberPair(true, 7'000'000'000'000'000'000ULL, 0);

        checkCompare("(-3e18) ? (-7e18)", a, b);
        checkCompare("(-7e18) ? (-3e18)", b, a);

        auto lt = [](NumberPair const& l, NumberPair const& r) {
            return lean_number_lt(
                       l.leanNum.negative,
                       l.leanNum.mantissa,
                       l.leanNum.exponent,
                       r.leanNum.negative,
                       r.leanNum.mantissa,
                       r.leanNum.exponent) != 0;
        };

        BEAST_EXPECT(!lt(a, b) && !(a.cppNum < b.cppNum));  // -3e18 < -7e18 is false
        BEAST_EXPECT(lt(b, a) && (b.cppNum < a.cppNum));    // -7e18 < -3e18 is true
    }

    void
    runTests() override
    {
        testCompare();

        // Issues identified during formal verification.
        // Kept here to ensure there are no regression bugs.
        testNegativeComparison();
    }
};

BEAST_DEFINE_TESTSUITE(LeanNumber, formal_verification, xrpl);

}  // namespace xrpl::test
