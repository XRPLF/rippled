#include <test/formal_verification/common/LeanSuite.h>
#include <test/formal_verification/ffi/protocol/NumberFFI.h>
#include <test/formal_verification/protocol/helpers/Generators.h>
#include <test/formal_verification/protocol/helpers/Helpers.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test.h>

#include <functional>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>

// Lean Number ops (largeRange, exported by formal_verification/XRPL/FFI/Protocol/NumberFFI.lean).
extern "C" {
lean_object*
lean_number_mul(uint8_t, uint64_t, uint64_t, uint8_t, uint64_t, uint64_t, uint8_t);
lean_object*
lean_number_div(uint8_t, uint64_t, uint64_t, uint8_t, uint64_t, uint64_t, uint8_t);
lean_object*
lean_number_add(uint8_t, uint64_t, uint64_t, uint8_t, uint64_t, uint64_t, uint8_t);
lean_object*
lean_number_sub(uint8_t, uint64_t, uint64_t, uint8_t, uint64_t, uint64_t, uint8_t);
lean_object*
lean_number_neg(uint8_t, uint64_t, uint64_t);
// signum returns -1/0/1 (no error channel).
int64_t
lean_number_signum(uint8_t, uint64_t, uint64_t);
lean_object*
lean_number_normalize(uint8_t, uint64_t, uint64_t, uint8_t);
// Comparison ops return 1/0 (no error channel).
uint8_t
lean_number_eq(uint8_t, uint64_t, uint64_t, uint8_t, uint64_t, uint64_t);
uint8_t
lean_number_ne(uint8_t, uint64_t, uint64_t, uint8_t, uint64_t, uint64_t);
uint8_t
lean_number_lt(uint8_t, uint64_t, uint64_t, uint8_t, uint64_t, uint64_t);
uint8_t
lean_number_le(uint8_t, uint64_t, uint64_t, uint8_t, uint64_t, uint64_t);
uint8_t
lean_number_gt(uint8_t, uint64_t, uint64_t, uint8_t, uint64_t, uint64_t);
uint8_t
lean_number_ge(uint8_t, uint64_t, uint64_t, uint8_t, uint64_t, uint64_t);
}

namespace xrpl::test {

class LeanNumber_test : public formal_verification::LeanSuite
{
    using Pair = formal_verification::Pair;
    using LeanNumberResult = formal_verification::LeanNumberResult;

    static std::string
    fmtNum(bool neg, uint64_t m, int e)
    {
        std::stringstream ss;
        ss << (neg ? "-" : "") << m << "e" << e;
        return ss.str();
    }

    static std::string
    fmtNum(Pair const& p)
    {
        return fmtNum(p.leanNum.negative != 0, p.leanNum.mantissa, p.cppNum.exponent());
    }

    bool
    checkResult(
        std::string const& label,
        LeanNumberResult const& lean,
        Number const& cpp,
        bool cppThrew)
    {
        using namespace formal_verification;

        if (lean.ok == cppThrew)
        {
            std::stringstream ss;
            ss << label << ": error mismatch (lean." << (lean.ok ? "ok" : "error") << ", cpp."
               << (cppThrew ? "threw" : "ok") << ")";
            fail(ss.str());
            return false;
        }
        if (!lean.ok)
        {
            pass();
            return true;
        }
        if (!fieldsEqual(lean, cpp))
        {
            std::stringstream ss;
            ss << label << ": result mismatch lean=" << format(lean) << " cpp=" << format(cpp);
            fail(ss.str());
            return false;
        }
        pass();
        return true;
    }

    // Lean/C++ disagree on the zero-exponent sentinel; compare mantissa only.
    bool
    checkZero(std::string const& label, LeanNumberResult const& lean, Number const& cpp)
    {
        if (!lean.ok || lean.mantissa != 0 || cpp.mantissa() != 0)
        {
            std::stringstream ss;
            ss << label << ": expected zero, lean.ok=" << lean.ok
               << " lean.mantissa=" << lean.mantissa << " cpp.mantissa=" << cpp.mantissa();
            fail(ss.str());
            return false;
        }
        pass();
        return true;
    }

    using LeanBinOp =
        lean_object* (*)(uint8_t, uint64_t, uint64_t, uint8_t, uint64_t, uint64_t, uint8_t);
    using LeanUnaryOp = lean_object* (*)(uint8_t, uint64_t, uint64_t);
    using CppBinOp = std::function<Number(Number const&, Number const&)>;
    using CppUnaryOp = std::function<Number(Number const&)>;

    bool
    checkBinOp(
        std::string const& label,
        LeanBinOp leanOp,
        CppBinOp cppOp,
        Pair const& a,
        Pair const& b,
        Number::RoundingMode mode)
    {
        using namespace formal_verification;

        auto lean = LeanNumberResult::fromLean(leanOp(
            a.leanNum.negative,
            a.leanNum.mantissa,
            a.leanNum.exponent,
            b.leanNum.negative,
            b.leanNum.mantissa,
            b.leanNum.exponent,
            toLeanMode(mode)));
        bool cppThrew = false;
        Number cpp;
        try
        {
            cpp = cppOp(a.cppNum, b.cppNum);
        }
        catch (std::overflow_error const&)
        {
            cppThrew = true;
        }
        return checkResult(label, lean, cpp, cppThrew);
    }

    bool
    checkUnary(std::string const& label, LeanUnaryOp leanOp, CppUnaryOp cppOp, Pair const& x)
    {
        auto lean = LeanNumberResult::fromLean(
            leanOp(x.leanNum.negative, x.leanNum.mantissa, x.leanNum.exponent));
        bool cppThrew = false;
        Number cpp;
        try
        {
            cpp = cppOp(x.cppNum);
        }
        catch (std::overflow_error const&)
        {
            cppThrew = true;
        }
        return checkResult(label, lean, cpp, cppThrew);
    }

    bool
    checkNormalize(
        std::string const& label,
        bool neg,
        uint64_t mant,
        int exp,
        Number::RoundingMode mode)
    {
        using namespace formal_verification;

        uint8_t leanMode = toLeanMode(mode);
        auto lean = LeanNumberResult::fromLean(
            lean_number_normalize(neg ? 1 : 0, mant, static_cast<uint64_t>(exp), leanMode));
        bool cppThrew = false;
        Number cpp;
        try
        {
            cpp = Number{neg, mant, exp, Number::Normalized{}};
        }
        catch (std::overflow_error const&)
        {
            cppThrew = true;
        }
        return checkResult(label, lean, cpp, cppThrew);
    }

    void
    runFuzzBinOp(LeanBinOp leanOp, char opChar, CppBinOp cppOp)
    {
        using namespace formal_verification;

        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode save{Number::setround(mode)};
            runFuzz(100'000, [&] {
                auto a = randomPair(-96, 80);
                auto b = randomPair(-96, 80);
                std::string label = fmtNum(a) + ' ' + opChar + ' ' + fmtNum(b);
                return checkBinOp(label, leanOp, cppOp, a, b, mode);
            });
        }
    }

    bool
    compareMul(char const* label, Pair const& a, Pair const& b)
    {
        return checkBinOp(
            label,
            lean_number_mul,
            [](Number const& x, Number const& y) { return x * y; },
            a,
            b,
            Number::RoundingMode::ToNearest);
    }

    bool
    compareDiv(char const* label, Pair const& a, Pair const& b)
    {
        return checkBinOp(
            label,
            lean_number_div,
            [](Number const& x, Number const& y) { return x / y; },
            a,
            b,
            Number::RoundingMode::ToNearest);
    }

    bool
    compareAdd(char const* label, Pair const& a, Pair const& b)
    {
        return checkBinOp(
            label,
            lean_number_add,
            [](Number const& x, Number const& y) { return x + y; },
            a,
            b,
            Number::RoundingMode::ToNearest);
    }

    bool
    compareSub(char const* label, Pair const& a, Pair const& b)
    {
        return checkBinOp(
            label,
            lean_number_sub,
            [](Number const& x, Number const& y) { return x - y; },
            a,
            b,
            Number::RoundingMode::ToNearest);
    }

    // signum has no error channel; Lean returns Int64, C++ returns int.
    bool
    checkSignum(std::string const& label, Pair const& x)
    {
        int64_t lean =
            lean_number_signum(x.leanNum.negative, x.leanNum.mantissa, x.leanNum.exponent);
        int cpp = x.cppNum.signum();
        if (lean != cpp)
        {
            std::stringstream ss;
            ss << "signum(" << label << "): lean=" << lean << " cpp=" << cpp;
            fail(ss.str());
            return false;
        }
        pass();
        return true;
    }

    bool
    compareNormalize(char const* label, bool neg, uint64_t mant, int exp)
    {
        return checkNormalize(label, neg, mant, exp, Number::RoundingMode::ToNearest);
    }

    using LeanCmpOp = uint8_t (*)(uint8_t, uint64_t, uint64_t, uint8_t, uint64_t, uint64_t);

    // Verify Lean and C++ agree on every relational operator for one pair.
    // Comparisons can't error, so we compare the raw bool results directly.
    bool
    checkCompare(std::string const& label, Pair const& a, Pair const& b)
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
            {"==", leanCmp(lean_number_eq), a.cppNum == b.cppNum},
            {"!=", leanCmp(lean_number_ne), a.cppNum != b.cppNum},
            {"<", leanCmp(lean_number_lt), a.cppNum < b.cppNum},
            {"<=", leanCmp(lean_number_le), a.cppNum <= b.cppNum},
            {">", leanCmp(lean_number_gt), a.cppNum > b.cppNum},
            {">=", leanCmp(lean_number_ge), a.cppNum >= b.cppNum},
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

public:
    void
    testFuzzMul()
    {
        beginCase("LeanNumber.fuzz_mul", true);
        runFuzzBinOp(lean_number_mul, '*', [](Number const& a, Number const& b) { return a * b; });
    }

    void
    testFuzzDiv()
    {
        beginCase("LeanNumber.fuzz_div", true);
        runFuzzBinOp(lean_number_div, '/', [](Number const& a, Number const& b) { return a / b; });
    }

    void
    testFuzzAdd()
    {
        beginCase("LeanNumber.fuzz_add", true);
        runFuzzBinOp(lean_number_add, '+', [](Number const& a, Number const& b) { return a + b; });
    }

    void
    testFuzzSub()
    {
        beginCase("LeanNumber.fuzz_sub", true);
        runFuzzBinOp(lean_number_sub, '-', [](Number const& a, Number const& b) { return a - b; });
    }

    void
    testFuzzNeg()
    {
        using namespace formal_verification;

        beginCase("LeanNumber.fuzz_neg", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        runFuzz(100'000, [&] {
            auto a = randomPair(-96, 80);
            std::string label = "neg(" + fmtNum(a) + ")";
            return checkUnary(label, lean_number_neg, [](Number const& x) { return -x; }, a);
        });
    }

    void
    testFuzzSignum()
    {
        using namespace formal_verification;

        beginCase("LeanNumber.fuzz_signum", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);

        // signum 0 (mantissa 0) is never produced by the normalized generator
        // below, so cover it (and ±1) explicitly.
        auto const minMant = Number::minMantissa();
        checkSignum("+minMant", makePair(false, minMant, 0));
        checkSignum("-minMant", makePair(true, minMant, 0));
        checkSignum("zero", makePair(false, 0, 0));

        runFuzz(100'000, [&] {
            auto a = randomPair(-96, 80);
            return checkSignum(fmtNum(a), a);
        });
    }

    void
    testFuzzNormalize()
    {
        using namespace formal_verification;

        beginCase("LeanNumber.fuzz_normalize", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);

        // Test with unnormalized inputs: mantissa outside [10^18, maxRep]
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode save{Number::setround(mode)};
            runFuzz(100'000, [&] {
                auto p = randomPair(1, 9'999'999'999'999'999'999ULL, -132800, 132800);
                bool neg = p.leanNum.negative != 0;
                int exp = p.cppNum.exponent();
                std::string label = "normalize(" + fmtNum(neg, p.leanNum.mantissa, exp) + ")";
                return checkNormalize(label, neg, p.leanNum.mantissa, exp, mode);
            });
        }
    }

    void
    testFuzzCompare()
    {
        using namespace formal_verification;

        beginCase("LeanNumber.fuzz_compare", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        runFuzz(100'000, [&] {
            auto a = randomPair(-96, 80);
            auto b = randomPair(-96, 80);
            // Random pairs are essentially never equal; also compare against
            // self to exercise the ==/<=/>= "equal" branches.
            bool ok = checkCompare(fmtNum(a) + " ? " + fmtNum(b), a, b);
            ok = checkCompare(fmtNum(a) + " ? self", a, a) && ok;
            return ok;
        });
    }

    void
    testKnownComparison()
    {
        using namespace formal_verification;

        beginCase("LeanNumber.known_comparison");
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        auto const minMant = Number::minMantissa();
        auto const maxRep = Number::kMaxRep;

        checkCompare("equal", makePair(false, minMant, 0), makePair(false, minMant, 0));
        checkCompare("sign", makePair(true, minMant, 0), makePair(false, minMant, 0));
        checkCompare("exponent", makePair(false, minMant, 0), makePair(false, minMant, 5));
        checkCompare("mantissa", makePair(false, minMant, 0), makePair(false, maxRep, 0));
        checkCompare("neg-order", makePair(true, maxRep, 0), makePair(true, minMant, 0));
        checkCompare("zero-pos", makePair(false, 0, 0), makePair(false, minMant, 0));
        checkCompare("zero-neg", makePair(false, 0, 0), makePair(true, minMant, 0));
        checkCompare("zero-zero", makePair(false, 0, 0), makePair(false, 0, 0));
    }

    void
    testKnownValues()
    {
        using namespace formal_verification;

        beginCase("LeanNumber.known_values");
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        SaveNumberRoundMode save{Number::setround(Number::RoundingMode::ToNearest)};

        compareMul(
            "2e18 * 3e18",
            makePair(false, 2'000'000'000'000'000'000ULL, 0),
            makePair(false, 3'000'000'000'000'000'000ULL, 0));

        compareDiv(
            "1e18 / 3e18",
            makePair(false, 1'000'000'000'000'000'000ULL, 0),
            makePair(false, 3'000'000'000'000'000'000ULL, 0));

        compareAdd(
            "5e18 + 3e18",
            makePair(false, 5'000'000'000'000'000'000ULL, 0),
            makePair(false, 3'000'000'000'000'000'000ULL, 0));

        compareAdd(
            "7e18 + (-3e18)",
            makePair(false, 7'000'000'000'000'000'000ULL, 0),
            makePair(true, 3'000'000'000'000'000'000ULL, 0));

        compareSub(
            "5e18 - 3e18",
            makePair(false, 5'000'000'000'000'000'000ULL, 0),
            makePair(false, 3'000'000'000'000'000'000ULL, 0));

        compareSub(
            "3e18 - 5e18 (sign flip)",
            makePair(false, 3'000'000'000'000'000'000ULL, 0),
            makePair(false, 5'000'000'000'000'000'000ULL, 0));

        compareSub(
            "5e18 - (-3e18)",
            makePair(false, 5'000'000'000'000'000'000ULL, 0),
            makePair(true, 3'000'000'000'000'000'000ULL, 0));

        // Self-subtraction cancels to zero (Lean 0e-32768 vs C++ 0e-INT_MIN;
        // compare mantissa only).
        {
            auto lean = LeanNumberResult::fromLean(lean_number_sub(
                false, 5'000'000'000'000'000'000ULL, 0, false, 5'000'000'000'000'000'000ULL, 0, 0));
            Number cpp = Number{false, 5'000'000'000'000'000'000ULL, 0, Number::Unchecked{}} -
                Number{false, 5'000'000'000'000'000'000ULL, 0, Number::Unchecked{}};
            checkZero("5e18 - 5e18 (cancel)", lean, cpp);
        }

        checkUnary(
            "neg(4.2e18 exp=5)",
            lean_number_neg,
            [](Number const& x) { return -x; },
            makePair(false, 4'200'000'000'000'000'000ULL, 5));

        compareNormalize("normalize(42e0)", false, 42, 0);

        compareMul(
            "1234567890123456789e-37 * 9876543210987654321e-42",
            makePair(false, 1'234'567'890'123'456'789ULL, -37),
            makePair(false, 9'876'543'210'987'654'321ULL, -42));

        compareDiv(
            "7777777777777777777e-10 / 3333333333333333333e-20",
            makePair(false, 7'777'777'777'777'777'777ULL, -10),
            makePair(false, 3'333'333'333'333'333'333ULL, -20));

        compareAdd(
            "1111111111111111111e5 + 8372615940283746192e-3",
            makePair(false, 1'111'111'111'111'111'111ULL, 5),
            makePair(false, 8'372'615'940'283'746'192ULL, -3));

        compareMul(
            "(-5718293048271639405e-73) * 8294710385627194038e12",
            makePair(true, 5'718'293'048'271'639'405ULL, -73),
            makePair(false, 8'294'710'385'627'194'038ULL, 12));

        compareAdd(
            "1000000000000000001e0 + (-1000000000000000000e0)",
            makePair(false, 1'000'000'000'000'000'001ULL, 0),
            makePair(true, 1'000'000'000'000'000'000ULL, 0));

        compareNormalize(
            "normalize(maxRep=9223372036854775807e0)", false, 9'223'372'036'854'775'807ULL, 0);

        compareMul(
            "overflow mul (exp 32000+32000)",
            makePair(false, 1'000'000'000'000'000'000ULL, 32000),
            makePair(false, 1'000'000'000'000'000'000ULL, 32000));

        // Only the canonical zero (Number{}, exp=INT_MIN) is a zero divisor in
        // C++ operator/=. A non-canonical {mantissa=0, exp=-32768} (only
        // constructible via Unchecked) is not caught by that guard and reaches
        // an integer divide-by-zero.
        {
            auto lean = LeanNumberResult::fromLean(lean_number_div(
                false,
                1'000'000'000'000'000'000ULL,
                0,
                false,
                0,
                static_cast<uint64_t>(std::numeric_limits<int>::lowest()),
                0));
            bool cppThrew = false;
            Number cpp;
            try
            {
                cpp =
                    Number{false, 1'000'000'000'000'000'000ULL, 0, Number::Unchecked{}} / Number{};
            }
            catch (std::overflow_error const&)
            {
                cppThrew = true;
            }
            checkResult("div by canonical zero", lean, cpp, cppThrew);
        }
    }

    void
    testExtremeValues()
    {
        using namespace formal_verification;

        beginCase("LeanNumber.extreme_values");
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        SaveNumberRoundMode save{Number::setround(Number::RoundingMode::ToNearest)};

        auto const minMant = Number::minMantissa();
        auto const maxRep = Number::kMaxRep;

        compareMul("maxRep * maxRep", makePair(false, maxRep, 0), makePair(false, maxRep, 0));
        compareMul("minMant * minMant", makePair(false, minMant, 0), makePair(false, minMant, 0));
        compareDiv("maxRep / minMant", makePair(false, maxRep, 0), makePair(false, minMant, 0));
        compareDiv("minMant / maxRep", makePair(false, minMant, 0), makePair(false, maxRep, 0));
        compareAdd("maxRep + maxRep", makePair(false, maxRep, 0), makePair(false, maxRep, 0));

        // Cancellation to zero (mantissa-only compare).
        {
            auto lean =
                LeanNumberResult::fromLean(lean_number_add(false, maxRep, 0, true, maxRep, 0, 0));
            Number cpp = Number{false, maxRep, 0, Number::Unchecked{}} +
                Number{true, maxRep, 0, Number::Unchecked{}};
            checkZero("maxRep + (-maxRep)", lean, cpp);
        }

        compareAdd(
            "maxRep + (-(maxRep-1))", makePair(false, maxRep, 0), makePair(true, maxRep - 1, 0));

        compareMul(
            "minMant e80 * minMant e-96",
            makePair(false, minMant, 80),
            makePair(false, minMant, -96));
        compareDiv(
            "minMant e80 / minMant e-96",
            makePair(false, minMant, 80),
            makePair(false, minMant, -96));
        compareAdd(
            "minMant e80 + minMant e-96",
            makePair(false, minMant, 80),
            makePair(false, minMant, -96));
        compareAdd(
            "maxRep e80 + minMant e-96",
            makePair(false, maxRep, 80),
            makePair(false, minMant, -96));

        compareMul(
            "(-maxRep e80) * (-maxRep e80)",
            makePair(true, maxRep, 80),
            makePair(true, maxRep, 80));
        compareDiv("(-maxRep) / (-minMant)", makePair(true, maxRep, 0), makePair(true, minMant, 0));

        compareNormalize("normalize(1e0)", false, 1, 0);
        compareNormalize("normalize(maxRep e80)", false, maxRep, 80);
        compareNormalize("normalize(minMant e-96)", false, minMant, -96);

        compareMul(
            "minMant e32768 * minMant e0",
            makePair(false, minMant, 32768),
            makePair(false, minMant, 0));

        {
            auto lean = LeanNumberResult::fromLean(lean_number_div(
                false, minMant, static_cast<uint64_t>(-32768), false, maxRep, 0, 0));
            Number cpp = Number{false, minMant, -32768, Number::Unchecked{}} /
                Number{false, maxRep, 0, Number::Unchecked{}};
            checkZero("minMant e-32768 / maxRep — underflow to zero", lean, cpp);
        }

        compareDiv(
            "maxRep / maxRep — self division",
            makePair(false, maxRep, 42),
            makePair(false, maxRep, 42));

        {
            auto lean =
                LeanNumberResult::fromLean(lean_number_add(false, maxRep, 42, true, maxRep, 42, 0));
            Number cpp = Number{false, maxRep, 42, Number::Unchecked{}} +
                Number{true, maxRep, 42, Number::Unchecked{}};
            checkZero("maxRep e42 + (-maxRep e42) — self cancel", lean, cpp);
        }

        // Both must error: near-overflow multiplication (16384+16384+18 > 32768)
        compareMul(
            "minMant e16384 * minMant e16384",
            makePair(false, minMant, 16384),
            makePair(false, minMant, 16384));

        compareAdd(
            "maxRep e0 + (-minMant e0)", makePair(false, maxRep, 0), makePair(true, minMant, 0));
        compareAdd(
            "minMant e0 + (-maxRep e0)", makePair(false, minMant, 0), makePair(true, maxRep, 0));
    }

private:
    void
    runTests() override
    {
        testFuzzMul();
        testFuzzDiv();
        testFuzzAdd();
        testFuzzSub();
        testFuzzNeg();
        testFuzzSignum();
        testFuzzNormalize();
        testFuzzCompare();
        testKnownValues();
        testKnownComparison();
        testExtremeValues();
    }
};

BEAST_DEFINE_TESTSUITE(LeanNumber, formal_verification, xrpl);

}  // namespace xrpl::test
