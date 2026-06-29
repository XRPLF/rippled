#include <test/formal_verification/common/LeanSuite.h>
#include <test/formal_verification/ffi/protocol/NumberFFI.h>
#include <test/formal_verification/numbers/helpers/NumberGenerators.h>
#include <test/formal_verification/numbers/helpers/NumberHelpers.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test.h>

#include <boost/multiprecision/cpp_int.hpp>

#include <functional>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

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
int64_t
lean_number_signum(uint8_t, uint64_t, uint64_t);
lean_object*
lean_number_to_rep(uint8_t, uint64_t, uint64_t, uint8_t);
lean_object*
lean_number_normalize(uint8_t, uint64_t, uint64_t, uint8_t);
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

using namespace formal_verification;

class LeanNumber_test : public LeanSuite
{
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

    bool
    checkResult(
        std::string const& label,
        LeanNumberResult const& lean,
        Number const& cpp,
        bool cppThrew)
    {
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

        if (lean.mantissa == 0 && cpp.mantissa() == 0)
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
        NumberPair const& a,
        NumberPair const& b,
        Number::RoundingMode mode)
    {
        auto lean = LeanNumberResult::from_lean(leanOp(
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
    checkUnary(std::string const& label, LeanUnaryOp leanOp, CppUnaryOp cppOp, NumberPair const& x)
    {
        auto lean = LeanNumberResult::from_lean(
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
        uint8_t leanMode = toLeanMode(mode);
        auto lean = LeanNumberResult::from_lean(
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
            Number::kMaxRepUp,
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

    // Edge values in operands
    // every edge mantissa, both signs, paired against every other,
    // over the curated exponent relationships and all rounding modes.
    void
    runOperandEdges(LeanBinOp leanOp, char opChar, CppBinOp cppOp)
    {
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large330);

        auto operandsAt = [this](int e) {
            std::vector<NumberPair> ops;
            for (uint64_t m : edgeMantissas())
                for (bool neg : {false, true})
                    ops.push_back(makeNumberPair(neg, m, e));
            return ops;
        };

        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode save{Number::setround(mode)};
            for (auto const& [ea, eb] : edgeExpPairs())
                for (auto const& a : operandsAt(ea))
                    for (auto const& b : operandsAt(eb))
                        checkBinOp(
                            fmtNum(a) + ' ' + opChar + ' ' + fmtNum(b), leanOp, cppOp, a, b, mode);
        }
    }

    // Safety net for inputs the targeted sweeps never thought to try: random
    // operands, edge or interior, to catch divergences anywhere else.
    void
    runFuzzBinOp(LeanBinOp leanOp, char opChar, CppBinOp cppOp, int iterations = 10'000)
    {
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large330);
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode save{Number::setround(mode)};
            runFuzz(iterations, [&] {
                auto a = randomOperand();
                auto b = randomOperand();
                std::string label = fmtNum(a) + ' ' + opChar + ' ' + fmtNum(b);
                return checkBinOp(label, leanOp, cppOp, a, b, mode);
            });
        }
    }

    // A named group of (ea, eb) input-exponent pairs for runResultExpRegions.
    struct ResultRegion
    {
        char const* name;
        std::vector<std::pair<int, int>> exps;
    };

    // Walk the target result exponent over [center-radius, center+radius]; toExps
    // maps each target to the (ea, eb) that lands this op's result there.
    static std::vector<std::pair<int, int>>
    sweepResultExp(int center, int radius, std::function<std::pair<int, int>(int)> toExps)
    {
        std::vector<std::pair<int, int>> out;
        for (int r = center - radius; r <= center + radius; ++r)
            out.push_back(toExps(r));
        return out;
    }

    // Test edge result exponents
    // The mantissa is pinned to minMantissa so only the exponent moves; each
    // region's (ea,eb) pairs steer the result exponent (mul: ea+eb+18, div: ea-eb)
    // into normal / under- / over-flow, in both signs and all rounding modes.
    void
    runResultExpRegions(
        char opChar,
        LeanBinOp leanOp,
        CppBinOp cppOp,
        std::vector<ResultRegion> const& regions)
    {
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large330);
        uint64_t const m = Number::minMantissa();
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode save{Number::setround(mode)};
            for (auto const& region : regions)
                for (auto const& [ea, eb] : region.exps)
                    for (bool na : {false, true})
                        for (bool nb : {false, true})
                        {
                            auto a = makeNumberPair(na, m, ea);
                            auto b = makeNumberPair(nb, m, eb);
                            checkBinOp(
                                std::string(region.name) + ": " + fmtNum(a) + ' ' + opChar + ' ' +
                                    fmtNum(b),
                                leanOp,
                                cppOp,
                                a,
                                b,
                                mode);
                        }
        }
    }

    static boost::multiprecision::cpp_int
    pow10(int n)
    {
        boost::multiprecision::cpp_int r = 1;
        while (n-- > 0)
            r *= 10;
        return r;
    }

    // b such that a * b lands near (resultNeg ? -1 : 1) * resultMant * 10^resultExp.
    static NumberPair
    mulFactor(NumberPair const& a, uint64_t resultMant, int resultExp, bool resultNeg)
    {
        using boost::multiprecision::cpp_int;
        int constexpr scale = 40;
        cpp_int q = cpp_int(resultMant) * pow10(scale) / cpp_int(a.leanNum.mantissa);
        int exp = resultExp - a.cppNum.exponent() - scale;
        cpp_int const hi = pow10(19);
        while (q >= hi)
        {
            q /= 10;
            ++exp;
        }
        uint64_t mant = static_cast<uint64_t>(q);
        if (mant > Number::kMaxRep)
            mant -= mant % 10;
        bool const bNeg = (a.leanNum.negative != 0) != resultNeg;
        return makeNumberPair(bNeg, mant, exp);
    }

    static NumberPair
    divFactor(NumberPair const& a, uint64_t resultMant, int resultExp, bool resultNeg)
    {
        using boost::multiprecision::cpp_int;
        int constexpr scale = 40;
        cpp_int q = cpp_int(a.leanNum.mantissa) * pow10(scale) / cpp_int(resultMant);
        int exp = a.cppNum.exponent() - resultExp - scale;
        cpp_int const hi = pow10(19);
        while (q >= hi)
        {
            q /= 10;
            ++exp;
        }
        uint64_t mant = static_cast<uint64_t>(q);
        if (mant > Number::kMaxRep)
            mant -= mant % 10;
        bool const bNeg = (a.leanNum.negative != 0) != resultNeg;
        return makeNumberPair(bNeg, mant, exp);
    }

    // Sweep the result mantissa across [lo, hi] at a fixed result exponent and
    // both result signs; a's exponent is pinned near resultExp/2 so the derived
    // partner stays in range.
    void
    runResultMantissaSweep(
        char opChar,
        LeanBinOp leanOp,
        CppBinOp cppOp,
        std::function<NumberPair(NumberPair const&, uint64_t, int, bool)> partner,
        uint64_t lo,
        uint64_t hi,
        int resultExp)
    {
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large330);
        std::uniform_int_distribution<uint64_t> mantDist(Number::minMantissa(), Number::kMaxRep);
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode save{Number::setround(mode)};
            for (uint64_t t = lo; t <= hi; ++t)
                for (bool neg : {false, true})
                    for (int i = 0; i < 8; ++i)
                    {
                        auto a = makeNumberPair(false, mantDist(nextRng()), resultExp / 2);
                        auto b = partner(a, t, resultExp, neg);
                        checkBinOp(
                            fmtNum(a) + ' ' + opChar + ' ' + fmtNum(b), leanOp, cppOp, a, b, mode);
                    }
        }
    }

    void
    runSumCusp(char opChar, LeanBinOp leanOp, CppBinOp cppOp, bool flipB)
    {
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large330);
        uint64_t const minM = Number::minMantissa();
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode save{Number::setround(mode)};
            for (int e : {0, 40, -40})
                for (uint64_t t = Number::kMaxRep - 7; t <= Number::kMaxRepUp + 13; ++t)
                    for (bool aNeg : {false, true})
                    {
                        auto a = makeNumberPair(aNeg, minM, e);
                        auto b = makeNumberPair(aNeg != flipB, t - minM, e);
                        checkBinOp(
                            fmtNum(a) + ' ' + opChar + ' ' + fmtNum(b), leanOp, cppOp, a, b, mode);
                    }
        }
    }

    bool
    compareMul(char const* label, NumberPair const& a, NumberPair const& b)
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
    compareAdd(char const* label, NumberPair const& a, NumberPair const& b)
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
    compareSub(char const* label, NumberPair const& a, NumberPair const& b)
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
    checkSignum(std::string const& label, NumberPair const& x)
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

    using LeanCmpOp = uint8_t (*)(uint8_t, uint64_t, uint64_t, uint8_t, uint64_t, uint64_t);

    // Verify Lean and C++ agree on every relational operator for one pair.
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
    testMul()
    {
        beginCase("LeanNumber.mul", true);
        auto const mul = [](Number const& a, Number const& b) { return a * b; };

        runOperandEdges(lean_number_mul, '*', mul);

        // Input exponents whose mul result lands at exponent r (ea + eb + 18 = r)..
        auto const mulExps = [](int r) -> std::pair<int, int> {
            int const s = r - 18;  // result exp = ea + eb + 18
            return {s / 2, s - (s / 2)};
        };

        runResultExpRegions(
            '*',
            lean_number_mul,
            mul,
            {
                {"normal", {{0, 0}, {18, -18}, {-40, 40}}},
                {"near-underflow", sweepResultExp(Number::kMinExponent, 5, mulExps)},
                {"underflow", {{-20000, -20000}}},
                {"near-overflow", sweepResultExp(Number::kMaxExponent, 5, mulExps)},
                {"overflow", {{20000, 20000}}},
            });

        uint64_t const minM = Number::minMantissa();
        uint64_t const maxM = Number::maxMantissa();
        // Sweep the result mantissa across the cusp, at mid, top, and bottom exponents.
        for (int e : {0, Number::kMaxExponent, Number::kMinExponent})
            runResultMantissaSweep(
                '*',
                lean_number_mul,
                mul,
                mulFactor,
                Number::kMaxRep - 7,
                Number::kMaxRepUp + 13,
                e);

        // Land at maxMantissa (top of range) at kMax: there a round-up carries past
        // it, and only at kMax does that carry tip the exponent into overflow.
        runResultMantissaSweep(
            '*', lean_number_mul, mul, mulFactor, maxM - 20, maxM, Number::kMaxExponent);

        // Land at minMantissa (bottom of range) at kMin: there a value below it gets
        // shifted down, and only at kMin does that shift tip the exponent into underflow.
        runResultMantissaSweep(
            '*', lean_number_mul, mul, mulFactor, minM - 3, minM + 3, Number::kMinExponent);

        runFuzzBinOp(lean_number_mul, '*', mul);
    }

    void
    testDiv()
    {
        beginCase("LeanNumber.div", true);
        auto const div = [](Number const& a, Number const& b) { return a / b; };

        runOperandEdges(lean_number_div, '/', div);

        auto const divExps = [](int r) -> std::pair<int, int> {
            int const s = r + 18;
            return {s / 2, s / 2 - s};
        };

        runResultExpRegions(
            '/',
            lean_number_div,
            div,
            {
                {"normal", {{0, 0}, {18, -18}, {-40, 40}}},
                {"near-underflow", sweepResultExp(Number::kMinExponent, 5, divExps)},
                {"underflow", {{-20000, 20000}}},
                {"near-overflow", sweepResultExp(Number::kMaxExponent, 5, divExps)},
                {"overflow", {{20000, -20000}}},
            });

        uint64_t const minM = Number::minMantissa();
        uint64_t const maxM = Number::maxMantissa();
        for (int e : {0, Number::kMaxExponent, Number::kMinExponent})
            runResultMantissaSweep(
                '/',
                lean_number_div,
                div,
                divFactor,
                Number::kMaxRep - 7,
                Number::kMaxRepUp + 13,
                e);

        runResultMantissaSweep(
            '/', lean_number_div, div, divFactor, maxM - 20, maxM, Number::kMaxExponent);

        runResultMantissaSweep(
            '/', lean_number_div, div, divFactor, minM - 3, minM + 3, Number::kMinExponent);

        runFuzzBinOp(lean_number_div, '/', div);

        // Only the canonical zero (Number{}, exp=INT_MIN) is a zero divisor in
        // C++ operator/=; it is the one divisor the non-zero sweeps cannot reach.
        {
            auto lean = LeanNumberResult::from_lean(lean_number_div(
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

    // EXPECTED FAILURE: xrpld's operator+ has two rounding bugs (reported
    // upstream)
    void
    testAdd()
    {
        beginCase("LeanNumber.add", true);
        auto const add = [](Number const& a, Number const& b) { return a + b; };
        runOperandEdges(lean_number_add, '+', add);
        runSumCusp('+', lean_number_add, add, false);
        runFuzzBinOp(lean_number_add, '+', add);
    }

    // EXPECTED FAILURE: see testAdd
    void
    testSub()
    {
        beginCase("LeanNumber.sub", true);
        auto const sub = [](Number const& a, Number const& b) { return a - b; };
        runOperandEdges(lean_number_sub, '-', sub);
        runSumCusp('-', lean_number_sub, sub, true);
        runFuzzBinOp(lean_number_sub, '-', sub);
    }

    void
    testNeg()
    {
        beginCase("LeanNumber.neg", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large330);
        auto const neg = [](Number const& x) { return -x; };

        for (uint64_t m : edgeMantissas())
            for (bool n : {false, true})
                for (int e : edgeExps())
                {
                    auto a = makeNumberPair(n, m, e);
                    checkUnary("neg(" + fmtNum(a) + ")", lean_number_neg, neg, a);
                }
        checkUnary("neg(0)", lean_number_neg, neg, makeNumberPair(false, 0, 0));

        runFuzz(10'000, [&] {
            auto a = randomOperand();
            return checkUnary("neg(" + fmtNum(a) + ")", lean_number_neg, neg, a);
        });
    }

    void
    testSignum()
    {
        beginCase("LeanNumber.signum", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large330);

        for (uint64_t m : edgeMantissas())
            for (bool n : {false, true})
                for (int e : edgeExps())
                    checkSignum(fmtNum(makeNumberPair(n, m, e)), makeNumberPair(n, m, e));
        checkSignum("+zero", makeNumberPair(false, 0, 0));
        checkSignum("-zero", makeNumberPair(true, 0, 0));

        runFuzz(10'000, [&] {
            auto a = randomOperand();
            return checkSignum(fmtNum(a), a);
        });
    }

    void
    testNormalize()
    {
        beginCase("LeanNumber.normalize", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large330);
        uint64_t const minM = Number::minMantissa();

        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode save{Number::setround(mode)};

            for (uint64_t m = Number::kMaxRep - 7; m <= Number::kMaxRepUp + 13; ++m)
                for (bool neg : {false, true})
                    for (int e : {0, 1, -1, 40})
                        checkNormalize("normalize(" + fmtNum(neg, m, e) + ")", neg, m, e, mode);

            for (uint64_t m = minM - 3; m <= minM + 3; ++m)
                for (bool neg : {false, true})
                    for (int e : {0, 1, -1, 40})
                        checkNormalize("normalize(" + fmtNum(neg, m, e) + ")", neg, m, e, mode);

            // Non-normal inputs only normalize() may see.
            std::vector<uint64_t> normMants = edgeMantissas();
            for (uint64_t m :
                 {Number::kMaxRep + 1,
                  Number::kMaxRep + 2,
                  9'999'999'999'999'999'999ULL,
                  minM - 1,
                  uint64_t{1},
                  uint64_t{0}})
                normMants.push_back(m);
            for (uint64_t m : normMants)
                for (bool neg : {false, true})
                    for (int e : edgeExps())
                        checkNormalize("normalize(" + fmtNum(neg, m, e) + ")", neg, m, e, mode);

            runFuzz(10'000, [&] {
                auto p = randomNumberPair(1, 9'999'999'999'999'999'999ULL, -132800, 132800);
                bool neg = p.leanNum.negative != 0;
                int exp = p.cppNum.exponent();
                std::string label = "normalize(" + fmtNum(neg, p.leanNum.mantissa, exp) + ")";
                return checkNormalize(label, neg, p.leanNum.mantissa, exp, mode);
            });
        }
    }

    void
    testCompare()
    {
        beginCase("LeanNumber.compare", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large330);
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
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large330);

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

    // #7389: operations whose rounded result sits in the (kMaxRep, kMaxRepUp) cusp
    void
    testRangeBoundaryArithmetic()
    {
        beginCase("LeanNumber.range_boundary_arithmetic");
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large330);
        SaveNumberRoundMode save{Number::setround(Number::RoundingMode::ToNearest)};
        auto const maxRep = Number::kMaxRep;  // 9223372036854775807 = 2^63-1

        compareMul(
            "maxRep * (1 + 1e-18)",
            makeNumberPair(false, maxRep, 0),
            makeNumberPair(false, 1'000'000'000'000'000'001ULL, -18));
        compareAdd(
            "maxRep + 5e18 e-18",
            makeNumberPair(false, maxRep, 0),
            makeNumberPair(false, 5'000'000'000'000'000'000ULL, -18));
        compareSub(
            "9999...90 - 7777...7 (near top of range)",
            makeNumberPair(false, 9'999'999'999'999'999'990ULL, 0),
            makeNumberPair(false, 7'777'777'777'777'777'777ULL, 0));

        // #7369: diff-sign add/sub with a large exponent gap, so the guard
        // accumulates many dropped digits that are then recovered before
        // rounding down.
        compareSub(
            "large-gap subtraction",
            makeNumberPair(false, 1'000'000'000'000'000'001ULL, 5),
            makeNumberPair(false, 9'999'999'999'999'999'990ULL, -10));
        compareAdd(
            "large-gap diff-sign add",
            makeNumberPair(false, 1'234'567'890'123'456'789ULL, 3),
            makeNumberPair(true, 9'876'543'210'987'654'320ULL, -8));

        // Wide-gap diff-sign add under TowardsZero: 1e20 + -(1e18+1).
        {
            SaveNumberRoundMode save{Number::setround(Number::RoundingMode::TowardsZero)};
            checkBinOp(
                "1e20 + (-(1e18+1)) [TowardsZero]",
                lean_number_add,
                [](Number const& x, Number const& y) { return x + y; },
                makeNumberPair(false, 1'000'000'000'000'000'000ULL, 2),  // 1e20
                makeNumberPair(true, 1'000'000'000'000'000'001ULL, 0),   // -(1e18+1)
                Number::RoundingMode::TowardsZero);
        }
    }

    // EXPECTED FAILURE: the exact inputs from two operator+ rounding bugs
    void
    testAdditionRounding()
    {
        beginCase("LeanNumber.addition_rounding");
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large330);

        // Bug 1: directed-rounding sign swap.
        {
            auto a = makeNumberPair(true, 6'000'000'000'000'000'000ULL, 0);
            auto b = makeNumberPair(true, 6'000'000'000'000'000'003ULL, 0);
            for (auto mode :
                 {Number::RoundingMode::ToNearest,
                  Number::RoundingMode::TowardsZero,
                  Number::RoundingMode::Downward,
                  Number::RoundingMode::Upward})
            {
                SaveNumberRoundMode save{Number::setround(mode)};
                checkBinOp(
                    "(-6e18) + (-6.000000000000000003e18)",
                    lean_number_add,
                    [](Number const& x, Number const& y) { return x + y; },
                    a,
                    b,
                    mode);
            }
        }

        // Bug 2: ToNearest picks the farther neighbor in heavy cancellation.
        {
            SaveNumberRoundMode save{Number::setround(Number::RoundingMode::ToNearest)};
            compareAdd(
                "(-1074956551220905975e28) + (5175909259972499745e22)",
                makeNumberPair(true, 1'074'956'551'220'905'975ULL, 28),
                makeNumberPair(false, 5'175'909'259'972'499'745ULL, 22));
        }
    }

    // operator/= Upward at the kMaxRep cusp on the fix-enabled (Large330) scale.
    // We reported this as returning a value strictly below the exact quotient
    // (violating Upward)
    void
    testDivisionRounding()
    {
        beginCase("LeanNumber.division_rounding");
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large330);
        SaveNumberRoundMode save{Number::setround(Number::RoundingMode::Upward)};

        // 2 / (1e18 + 7), Upward.
        checkBinOp(
            "2 / (1e18+7) [Upward]",
            lean_number_div,
            [](Number const& x, Number const& y) { return x / y; },
            makeNumberPair(false, 2'000'000'000'000'000'000ULL, -18),  // 2
            makeNumberPair(false, 1'000'000'000'000'000'007ULL, 0),    // 1e18 + 7
            Number::RoundingMode::Upward);
    }

    // operator* Downward at the kMaxRep cusp on the fix-enabled (Large330) scale.
    // We reported (kMaxRep-7) * (minMant+1) as landing 7 ULPs short of the
    // tightest representable below truth, #7389 fixed it
    void
    testMultiplicationRounding()
    {
        beginCase("LeanNumber.multiplication_rounding");
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large330);
        SaveNumberRoundMode save{Number::setround(Number::RoundingMode::Downward)};

        checkBinOp(
            "(kMaxRep-7) * (minMant+1) [Downward]",
            lean_number_mul,
            [](Number const& a, Number const& b) { return a * b; },
            makeNumberPair(false, Number::kMaxRep - 7, 0),
            makeNumberPair(false, Number::minMantissa() + 1, 0),
            Number::RoundingMode::Downward);
    }

    void
    testCuspRounding()
    {
        beginCase("LeanNumber.cusp_rounding");
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large330);

        constexpr uint64_t kMaxRep = Number::kMaxRep;      // 2^63 - 1
        constexpr uint64_t kMaxRepUp = Number::kMaxRepUp;  // next multiple of 10

        auto modeName = [](Number::RoundingMode m) -> char const* {
            switch (m)
            {
                case Number::RoundingMode::ToNearest:
                    return "ToNearest";
                case Number::RoundingMode::TowardsZero:
                    return "TowardsZero";
                case Number::RoundingMode::Downward:
                    return "Downward";
                case Number::RoundingMode::Upward:
                    return "Upward";
            }
            return "?";
        };

        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            // C++ Normalized{} rounds with the ambient mode; match it to `mode`.
            SaveNumberRoundMode save{Number::setround(mode)};
            for (uint64_t m : {kMaxRep, kMaxRep + 1, kMaxRep + 2, kMaxRepUp})
            {
                for (bool neg : {false, true})
                {
                    std::string label =
                        "normalize(" + fmtNum(neg, m, 0) + ") [" + modeName(mode) + "]";
                    checkNormalize(label, neg, m, 0, mode);
                }
            }
        }
    }

    bool
    checkToRep(std::string const& label, NumberPair const& x, Number::RoundingMode mode)
    {
        auto const lean = LeanXRPResult::from_lean(lean_number_to_rep(
            x.leanNum.negative, x.leanNum.mantissa, x.leanNum.exponent, toLeanMode(mode)));
        bool cppThrew = false;
        std::int64_t cpp = 0;
        try
        {
            cpp = static_cast<std::int64_t>(x.cppNum);
        }
        catch (std::overflow_error const&)
        {
            cppThrew = true;
        }
        if (lean.ok == cppThrew)
        {
            fail(label + ": error mismatch");
            return false;
        }
        if (!lean.ok)
        {
            pass();
            return true;
        }
        if (lean.drops != cpp)
        {
            std::stringstream ss;
            ss << label << ": lean=" << lean.drops << " cpp=" << cpp;
            fail(ss.str());
            return false;
        }
        pass();
        return true;
    }

    void
    testToRep()
    {
        beginCase("LeanNumber.to_rep", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large330);

        std::vector<std::pair<bool, std::pair<uint64_t, int>>> const fractions = {
            {false, {15, -1}},
            {false, {25, -1}},
            {false, {14, -1}},
            {false, {16, -1}},
            {true, {15, -1}},
            {true, {25, -1}},
            {false, {5, -1}},
            {false, {1, 5}}};

        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode save{Number::setround(mode)};

            for (uint64_t m : edgeMantissas())
                for (bool n : {false, true})
                    for (int e : edgeExps())
                    {
                        auto a = makeNumberPair(n, m, e);
                        checkToRep("to_rep(" + fmtNum(a) + ")", a, mode);
                    }
            for (auto const& [n, me] : fractions)
            {
                auto a = makeNumberPair(n, me.first, me.second);
                checkToRep("to_rep(" + fmtNum(a) + ")", a, mode);
            }

            runFuzz(10'000, [&] {
                auto a = randomOperand();
                return checkToRep("to_rep(" + fmtNum(a) + ")", a, mode);
            });
        }
    }

private:
    void
    runTests() override
    {
        testMul();
        testDiv();
        testAdd();
        testSub();
        testNeg();
        testSignum();
        testNormalize();
        testCompare();
        testToRep();

        // Issues identified during formal verification.
        // Kept here to ensure there are no regression bugs.
        testNegativeComparison();
        testRangeBoundaryArithmetic();
        testAdditionRounding();
        testDivisionRounding();
        testMultiplicationRounding();
        testCuspRounding();
    }
};

BEAST_DEFINE_TESTSUITE(LeanNumber, formal_verification, xrpl);

}  // namespace xrpl::test
