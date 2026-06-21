#include <test/formal_verification/common/LeanSuite.h>
#include <test/formal_verification/ffi/protocol/NumberFFI.h>
#include <test/formal_verification/numbers/helpers/NumberGenerators.h>
#include <test/formal_verification/numbers/helpers/NumberHelpers.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/XRPAmount.h>

#include <exception>
#include <initializer_list>
#include <limits>
#include <random>
#include <sstream>
#include <tuple>

// Lean XRPAmount ops (exported by xrpl-lean4/XRPL/XRPAmount/FFI.lean).
extern "C" {
int64_t
lean_xrp_of_int64(int64_t);
lean_object*
lean_xrp_of_number(uint8_t, uint64_t, int64_t, uint8_t);
lean_object*
lean_xrp_to_number(int64_t, uint8_t);
uint8_t
lean_xrp_eq(int64_t, int64_t);
uint8_t
lean_xrp_ne(int64_t, int64_t);
uint8_t
lean_xrp_eq_int(int64_t, int64_t);
uint8_t
lean_xrp_ne_int(int64_t, int64_t);
uint8_t
lean_xrp_lt(int64_t, int64_t);
uint8_t
lean_xrp_le(int64_t, int64_t);
uint8_t
lean_xrp_gt(int64_t, int64_t);
uint8_t
lean_xrp_ge(int64_t, int64_t);
int64_t
lean_xrp_add(int64_t, int64_t);
int64_t
lean_xrp_sub(int64_t, int64_t);
int64_t
lean_xrp_neg(int64_t);
int64_t
lean_xrp_mul(int64_t, int64_t);
int64_t
lean_xrp_add_int(int64_t, int64_t);
int64_t
lean_xrp_sub_int(int64_t, int64_t);
lean_object*
lean_xrp_mul_ratio(int64_t, uint32_t, uint32_t, uint8_t);
}

namespace xrpl::test {

using namespace formal_verification;

class LeanXRPAmount_test : public LeanSuite
{
    bool
    checkOfNumber(NumberPair const& p, Number::RoundingMode mode)
    {
        auto lean = LeanXRPResult::from_lean(lean_xrp_of_number(
            p.leanNum.negative,
            p.leanNum.mantissa,
            static_cast<int64_t>(p.leanNum.exponent),
            toLeanMode(mode)));

        bool cppThrew = false;
        int64_t cpp = 0;
        try
        {
            cpp = XRPAmount{p.cppNum}.drops();
        }
        catch (std::exception const&)
        {
            cppThrew = true;
        }

        auto label = [&] {
            std::stringstream ss;
            ss << "ofNumber(" << (p.leanNum.negative ? "-" : "+") << p.leanNum.mantissa << "e"
               << p.cppNum.exponent() << ")";
            return ss.str();
        };

        if (lean.ok == cppThrew)
        {
            fail(label() + ": error mismatch");
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
            ss << label() << ": result mismatch lean=" << lean.drops << " cpp=" << cpp;
            fail(ss.str());
            return false;
        }
        pass();
        return true;
    }

    bool
    checkToNumber(XRPAmountPair const& p, Number::RoundingMode mode)
    {
        auto lean = LeanNumberResult::from_lean(lean_xrp_to_number(p.leanDrops, toLeanMode(mode)));
        Number cpp = static_cast<Number>(p.cppXrp);
        if (!lean.ok || !fieldsEqual(lean, cpp))
        {
            std::stringstream ss;
            ss << "toNumber(" << p.leanDrops << "): mismatch lean.ok=" << lean.ok
               << " lean=" << format(lean) << " cpp=" << format(cpp);
            fail(ss.str());
            return false;
        }
        pass();
        return true;
    }

    // Report a bool-valued op. lhs/rhs are shown in the failure message.
    bool
    expectBool(char const* op, int64_t lhs, int64_t rhs, uint8_t lean, bool cpp)
    {
        if ((lean != 0) != cpp)
        {
            std::stringstream ss;
            ss << op << "(" << lhs << "," << rhs << "): lean=" << (lean != 0) << " cpp=" << cpp;
            fail(ss.str());
            return false;
        }
        pass();
        return true;
    }

    // Report a drops-valued op.
    bool
    expectDrops(char const* op, int64_t lhs, int64_t rhs, int64_t lean, int64_t cpp)
    {
        if (lean != cpp)
        {
            std::stringstream ss;
            ss << op << "(" << lhs << "," << rhs << "): lean=" << lean << " cpp=" << cpp;
            fail(ss.str());
            return false;
        }
        pass();
        return true;
    }

    bool
    checkEq(XRPAmountPair const& a, XRPAmountPair const& b)
    {
        return expectBool(
            "eq",
            a.leanDrops,
            b.leanDrops,
            lean_xrp_eq(a.leanDrops, b.leanDrops),
            a.cppXrp == b.cppXrp);
    }

    bool
    checkNe(XRPAmountPair const& a, XRPAmountPair const& b)
    {
        return expectBool(
            "ne",
            a.leanDrops,
            b.leanDrops,
            lean_xrp_ne(a.leanDrops, b.leanDrops),
            a.cppXrp != b.cppXrp);
    }

    bool
    checkLt(XRPAmountPair const& a, XRPAmountPair const& b)
    {
        return expectBool(
            "lt",
            a.leanDrops,
            b.leanDrops,
            lean_xrp_lt(a.leanDrops, b.leanDrops),
            a.cppXrp < b.cppXrp);
    }

    bool
    checkLe(XRPAmountPair const& a, XRPAmountPair const& b)
    {
        return expectBool(
            "le",
            a.leanDrops,
            b.leanDrops,
            lean_xrp_le(a.leanDrops, b.leanDrops),
            a.cppXrp <= b.cppXrp);
    }

    bool
    checkGt(XRPAmountPair const& a, XRPAmountPair const& b)
    {
        return expectBool(
            "gt",
            a.leanDrops,
            b.leanDrops,
            lean_xrp_gt(a.leanDrops, b.leanDrops),
            a.cppXrp > b.cppXrp);
    }

    bool
    checkGe(XRPAmountPair const& a, XRPAmountPair const& b)
    {
        return expectBool(
            "ge",
            a.leanDrops,
            b.leanDrops,
            lean_xrp_ge(a.leanDrops, b.leanDrops),
            a.cppXrp >= b.cppXrp);
    }

    bool
    checkAllCompare(XRPAmountPair const& a, XRPAmountPair const& b)
    {
        bool ok = true;
        ok &= checkEq(a, b);
        ok &= checkNe(a, b);
        ok &= checkLt(a, b);
        ok &= checkLe(a, b);
        ok &= checkGt(a, b);
        ok &= checkGe(a, b);
        return ok;
    }

    bool
    checkEqInt(XRPAmountPair const& p, int64_t n)
    {
        return expectBool("eqInt", p.leanDrops, n, lean_xrp_eq_int(p.leanDrops, n), p.cppXrp == n);
    }

    bool
    checkNeInt(XRPAmountPair const& p, int64_t n)
    {
        return expectBool("neInt", p.leanDrops, n, lean_xrp_ne_int(p.leanDrops, n), p.cppXrp != n);
    }

    bool
    checkOfInt64(int64_t v)
    {
        return expectDrops("ofInt64", v, 0, lean_xrp_of_int64(v), XRPAmount{v}.drops());
    }

    bool
    checkAdd(XRPAmountPair const& a, XRPAmountPair const& b)
    {
        int64_t w;
        bool const ov = __builtin_add_overflow(a.leanDrops, b.leanDrops, &w);
        return expectDrops(
            "add",
            a.leanDrops,
            b.leanDrops,
            lean_xrp_add(a.leanDrops, b.leanDrops),
            ov ? w : (a.cppXrp + b.cppXrp).drops());
    }

    bool
    checkSub(XRPAmountPair const& a, XRPAmountPair const& b)
    {
        int64_t w;
        bool const ov = __builtin_sub_overflow(a.leanDrops, b.leanDrops, &w);
        return expectDrops(
            "sub",
            a.leanDrops,
            b.leanDrops,
            lean_xrp_sub(a.leanDrops, b.leanDrops),
            ov ? w : (a.cppXrp - b.cppXrp).drops());
    }

    bool
    checkNeg(XRPAmountPair const& p)
    {
        // -INT64_MIN is signed-overflow UB; compute the wrapped reference via
        // __builtin_sub_overflow(0, x) and only invoke -p.cppXrp when safe.
        int64_t w;
        bool const ov = __builtin_sub_overflow(int64_t{0}, p.leanDrops, &w);
        return expectDrops(
            "neg", p.leanDrops, 0, lean_xrp_neg(p.leanDrops), ov ? w : (-p.cppXrp).drops());
    }

    bool
    checkMul(XRPAmountPair const& p, int64_t rhs)
    {
        int64_t w;
        bool const ov = __builtin_mul_overflow(p.leanDrops, rhs, &w);
        return expectDrops(
            "mul",
            p.leanDrops,
            rhs,
            lean_xrp_mul(p.leanDrops, rhs),
            ov ? w : (p.cppXrp * rhs).drops());
    }

    bool
    checkAddInt(XRPAmountPair const& p, int64_t n)
    {
        int64_t w;
        bool const ov = __builtin_add_overflow(p.leanDrops, n, &w);
        return expectDrops(
            "addInt",
            p.leanDrops,
            n,
            lean_xrp_add_int(p.leanDrops, n),
            ov ? w : (XRPAmount{p.cppXrp} += n).drops());
    }

    bool
    checkSubInt(XRPAmountPair const& p, int64_t n)
    {
        int64_t w;
        bool const ov = __builtin_sub_overflow(p.leanDrops, n, &w);
        return expectDrops(
            "subInt",
            p.leanDrops,
            n,
            lean_xrp_sub_int(p.leanDrops, n),
            ov ? w : (XRPAmount{p.cppXrp} -= n).drops());
    }

    bool
    checkMulRatio(XRPAmountPair const& p, uint32_t num, uint32_t den, bool roundUp)
    {
        int64_t const v = p.leanDrops;
        auto lean = LeanXRPResult::from_lean(lean_xrp_mul_ratio(v, num, den, roundUp ? 1 : 0));
        bool cppThrew = false;
        int64_t cpp = 0;
        try
        {
            cpp = mulRatio(XRPAmount{v}, num, den, roundUp).drops();
        }
        catch (std::exception const&)
        {
            cppThrew = true;
        }

        auto label = [&] {
            std::stringstream ss;
            ss << "mulRatio(" << v << "," << num << "," << den << ","
               << (roundUp ? "true" : "false") << ")";
            return ss.str();
        };
        if (lean.ok == cppThrew)
        {
            fail(label() + ": error mismatch");
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
            ss << label() << ": result mismatch lean=" << lean.drops << " cpp=" << cpp;
            fail(ss.str());
            return false;
        }
        pass();
        return true;
    }

public:
    void
    test_fuzz_of_number()
    {
        beginCase("LeanXRPAmount.fuzz_of_number", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);

        // exp ∈ [-18, 0] keeps the int64 result strictly representable.
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode save{Number::setround(mode)};
            runFuzz(50'000, [&] { return checkOfNumber(randomNumberPair(-18, 0), mode); });
        }
    }

    void
    test_fuzz_to_number()
    {
        beginCase("LeanXRPAmount.fuzz_to_number", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode save{Number::setround(mode)};
            runFuzz(50'000, [&] { return checkToNumber(randomXRPAmountPair(), mode); });
        }
    }

    void
    test_fuzz_add()
    {
        beginCase("LeanXRPAmount.fuzz_add", true);
        runFuzz(100'000, [&] { return checkAdd(randomXRPAmountPair(), randomXRPAmountPair()); });
    }

    void
    test_fuzz_sub()
    {
        beginCase("LeanXRPAmount.fuzz_sub", true);
        runFuzz(100'000, [&] { return checkSub(randomXRPAmountPair(), randomXRPAmountPair()); });
    }

    void
    test_fuzz_neg()
    {
        beginCase("LeanXRPAmount.fuzz_neg", true);
        runFuzz(100'000, [&] { return checkNeg(randomXRPAmountPair()); });
    }

    void
    test_fuzz_mul_ratio()
    {
        beginCase("LeanXRPAmount.fuzz_mul_ratio", true);
        auto& rng = nextRng();
        std::uniform_int_distribution<uint32_t> numDist(0, std::numeric_limits<uint32_t>::max());
        std::uniform_int_distribution<uint32_t> denDist(1, std::numeric_limits<uint32_t>::max());
        std::bernoulli_distribution roundDist(0.5);
        runFuzz(100'000, [&] {
            auto p = randomXRPAmountPair(rng);
            uint32_t const num = numDist(rng);
            uint32_t const den = denDist(rng);
            bool const ru = roundDist(rng);
            return checkMulRatio(p, num, den, ru);
        });
    }

    void
    test_fuzz_compare()
    {
        beginCase("LeanXRPAmount.fuzz_compare", true);
        runFuzz(
            100'000, [&] { return checkAllCompare(randomXRPAmountPair(), randomXRPAmountPair()); });
    }

    void
    test_fuzz_compare_int()
    {
        beginCase("LeanXRPAmount.fuzz_compare_int", true);
        auto& rng = nextRng();
        runFuzz(100'000, [&] {
            auto p = randomXRPAmountPair(rng);
            int64_t const n = randomInt64(rng);
            return checkEqInt(p, n) && checkNeInt(p, n);
        });
    }

    void
    test_fuzz_of_int64()
    {
        beginCase("LeanXRPAmount.fuzz_of_int64", true);
        runFuzz(100'000, [&] { return checkOfInt64(randomXRPAmountPair().leanDrops); });
    }

    void
    test_fuzz_mul()
    {
        beginCase("LeanXRPAmount.fuzz_mul", true);
        auto& rng = nextRng();
        runFuzz(100'000, [&] {
            auto p = randomXRPAmountPair(rng);
            int64_t const n = randomInt64(rng);
            return checkMul(p, n);
        });
    }

    void
    test_fuzz_add_sub_int()
    {
        beginCase("LeanXRPAmount.fuzz_add_sub_int", true);
        auto& rng = nextRng();
        runFuzz(100'000, [&] {
            auto p = randomXRPAmountPair(rng);
            int64_t const n = randomInt64(rng);
            return checkAddInt(p, n) && checkSubInt(p, n);
        });
    }

    void
    test_known_values()
    {
        beginCase("LeanXRPAmount.known_values");
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        SaveNumberRoundMode save{Number::setround(Number::RoundingMode::ToNearest)};

        constexpr int64_t int64Max = std::numeric_limits<int64_t>::max();
        constexpr int64_t int64Min = std::numeric_limits<int64_t>::min();
        constexpr int64_t halfMax = int64Max / 2;

        checkAdd(makeXRPAmountPair(1), makeXRPAmountPair(2));
        checkAdd(makeXRPAmountPair(-3), makeXRPAmountPair(7));
        checkAdd(makeXRPAmountPair(halfMax), makeXRPAmountPair(halfMax));
        checkAdd(makeXRPAmountPair(0), makeXRPAmountPair(0));
        checkAdd(makeXRPAmountPair(0), makeXRPAmountPair(int64Max));
        checkAdd(makeXRPAmountPair(int64Min), makeXRPAmountPair(0));

        checkSub(makeXRPAmountPair(5), makeXRPAmountPair(3));
        checkSub(makeXRPAmountPair(0), makeXRPAmountPair(1));
        checkSub(makeXRPAmountPair(1), makeXRPAmountPair(2));
        checkSub(makeXRPAmountPair(-1), makeXRPAmountPair(1));
        checkSub(makeXRPAmountPair(int64Max), makeXRPAmountPair(int64Max - 1));

        checkNeg(makeXRPAmountPair(0));
        checkNeg(makeXRPAmountPair(1));
        checkNeg(makeXRPAmountPair(-1));

        // mulRatio: 1/3 — roundUp diverges from roundDown across signs.
        checkMulRatio(makeXRPAmountPair(1'000'000), 3, 2, true);
        checkMulRatio(makeXRPAmountPair(1'000'000), 3, 2, false);
        checkMulRatio(makeXRPAmountPair(1), 1, 3, true);
        checkMulRatio(makeXRPAmountPair(1), 1, 3, false);
        checkMulRatio(makeXRPAmountPair(-1), 1, 3, true);
        checkMulRatio(makeXRPAmountPair(-1), 1, 3, false);
        // num = 0 short-circuits to 0; den = 0 errors on both sides.
        checkMulRatio(makeXRPAmountPair(int64Max), 0, 1, true);
        checkMulRatio(makeXRPAmountPair(int64Max), 0, 1, false);
        checkMulRatio(makeXRPAmountPair(1'000'000), 7, 0, true);
        checkMulRatio(makeXRPAmountPair(0), 1, 0, false);

        checkToNumber(makeXRPAmountPair(0), Number::RoundingMode::ToNearest);
        checkToNumber(makeXRPAmountPair(1), Number::RoundingMode::ToNearest);
        checkToNumber(makeXRPAmountPair(-1), Number::RoundingMode::ToNearest);
        checkToNumber(makeXRPAmountPair(1'000'000'000LL), Number::RoundingMode::ToNearest);
        checkToNumber(makeXRPAmountPair(-1'000'000'000LL), Number::RoundingMode::ToNearest);

        checkOfNumber(makeNumberPair(false, 1'000'000ULL, 0), Number::RoundingMode::ToNearest);
        checkOfNumber(makeNumberPair(true, 1'000'000ULL, 0), Number::RoundingMode::ToNearest);
        checkOfNumber(makeNumberPair(false, 1ULL, 0), Number::RoundingMode::ToNearest);
        checkOfNumber(makeNumberPair(true, 1ULL, 0), Number::RoundingMode::ToNearest);
        // Sub-drop magnitudes round to zero under to-nearest.
        checkOfNumber(makeNumberPair(false, 1ULL, -1), Number::RoundingMode::ToNearest);
        checkOfNumber(makeNumberPair(true, 1ULL, -1), Number::RoundingMode::ToNearest);
        checkOfNumber(makeNumberPair(false, 5ULL, -1), Number::RoundingMode::ToNearest);

        // Small-value grid
        constexpr int64_t vals[] = {-2, -1, 0, 1, 2};
        for (int64_t a : vals)
        {
            auto const pa = makeXRPAmountPair(a);
            checkOfInt64(a);
            checkNeg(pa);
            for (int64_t b : vals)
            {
                auto const pb = makeXRPAmountPair(b);
                checkAllCompare(pa, pb);
                checkEqInt(pa, b);
                checkNeInt(pa, b);
                checkMul(pa, b);
                checkAddInt(pa, b);
                checkSubInt(pa, b);
            }
        }
    }

    void
    test_extreme_values()
    {
        beginCase("LeanXRPAmount.extreme_values");
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        SaveNumberRoundMode save{Number::setround(Number::RoundingMode::ToNearest)};

        constexpr int64_t int64Max = std::numeric_limits<int64_t>::max();
        constexpr int64_t int64Min = std::numeric_limits<int64_t>::min();
        constexpr uint32_t u32Max = std::numeric_limits<uint32_t>::max();

        // add/sub at the int64 boundaries — INT64_MAX+1, INT64_MIN-1 are C++ UB;
        checkAdd(makeXRPAmountPair(int64Max - 1), makeXRPAmountPair(1));
        checkAdd(makeXRPAmountPair(int64Max), makeXRPAmountPair(1));
        checkAdd(makeXRPAmountPair(int64Min), makeXRPAmountPair(-1));
        checkAdd(makeXRPAmountPair(int64Max), makeXRPAmountPair(-int64Max));
        checkSub(makeXRPAmountPair(0), makeXRPAmountPair(int64Min));
        checkSub(makeXRPAmountPair(0), makeXRPAmountPair(int64Max));
        checkSub(makeXRPAmountPair(int64Min), makeXRPAmountPair(1));
        checkSub(makeXRPAmountPair(int64Max), makeXRPAmountPair(-1));
        checkSub(makeXRPAmountPair(int64Max), makeXRPAmountPair(int64Max));
        checkSub(makeXRPAmountPair(int64Min), makeXRPAmountPair(int64Min));

        // neg at boundaries. -INT64_MIN is C++ UB
        checkNeg(makeXRPAmountPair(int64Max));
        checkNeg(makeXRPAmountPair(-int64Max));
        {
            int64_t lean = lean_xrp_neg(int64Min);
            int64_t cpp;
            __builtin_sub_overflow(int64_t{0}, int64Min, &cpp);
            BEAST_EXPECT(lean == int64Min);
            BEAST_EXPECT(cpp == int64Min);
            BEAST_EXPECT(lean == cpp);
        }

        // toNumber at int64 boundaries — int64→Number is exact for |v| ≤ INT64_MAX
        for (int64_t v : {int64Min, int64Min + 1, int64_t{-1}, int64_t{0}, int64_t{1}, int64Max})
            checkToNumber(makeXRPAmountPair(v), Number::RoundingMode::ToNearest);
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode m{Number::setround(mode)};
            checkToNumber(makeXRPAmountPair(0), mode);
            checkToNumber(makeXRPAmountPair(1), mode);
            checkToNumber(makeXRPAmountPair(-1), mode);
            checkToNumber(makeXRPAmountPair(int64Max), mode);
            checkToNumber(makeXRPAmountPair(int64Min + 1), mode);
        }

        // ofNumber: at-boundary mantissas + fractional values whose rounding
        // direction differs by mode.
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode m{Number::setround(mode)};
            checkOfNumber(makeNumberPair(false, Number::kMaxRep, 0), mode);
            checkOfNumber(makeNumberPair(true, Number::kMaxRep, 0), mode);
            checkOfNumber(makeNumberPair(false, 5ULL, -1), mode);
            checkOfNumber(makeNumberPair(true, 5ULL, -1), mode);
            checkOfNumber(makeNumberPair(false, 15ULL, -1), mode);
            checkOfNumber(makeNumberPair(true, 15ULL, -1), mode);
            checkOfNumber(makeNumberPair(false, 25ULL, -1), mode);
            checkOfNumber(makeNumberPair(true, 25ULL, -1), mode);
            checkOfNumber(makeNumberPair(false, 1ULL, -2), mode);
            checkOfNumber(makeNumberPair(true, 1ULL, -2), mode);
        }
        // ofNumber overflow — magnitude exceeds INT64_MAX. Swept across modes
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode m{Number::setround(mode)};
            checkOfNumber(makeNumberPair(false, Number::kMaxRep, 1), mode);
            checkOfNumber(makeNumberPair(true, Number::kMaxRep, 1), mode);
            checkOfNumber(makeNumberPair(false, 1ULL, 19), mode);
        }

        // mulRatio at the boundaries
        checkMulRatio(makeXRPAmountPair(int64Max), 1, 1, false);
        checkMulRatio(makeXRPAmountPair(int64Max), 1, 1, true);
        checkMulRatio(makeXRPAmountPair(-int64Max), 1, 1, false);
        checkMulRatio(makeXRPAmountPair(-int64Max), 1, 1, true);
        checkMulRatio(makeXRPAmountPair(int64Min), 1, 1, false);
        checkMulRatio(makeXRPAmountPair(int64Min), 1, 1, true);
        // Overflow on positive side; underflow saturation on negative side.
        checkMulRatio(makeXRPAmountPair(int64Max), 1000, 1, true);
        checkMulRatio(makeXRPAmountPair(int64Max), 2, 1, false);
        checkMulRatio(makeXRPAmountPair(-(int64_t{1} << 50)), 1'000'000, 1, true);
        checkMulRatio(makeXRPAmountPair(int64Min + 1), 2, 1, false);
        checkMulRatio(makeXRPAmountPair(-int64Max), 1000, 1, false);

        // Rounding direction at the remainder boundary, both signs.
        // 5/2 → 3 up, 2 down; -5/2 → -2 up (ceil toward +∞), -3 down.
        checkMulRatio(makeXRPAmountPair(5), 1, 2, true);
        checkMulRatio(makeXRPAmountPair(5), 1, 2, false);
        checkMulRatio(makeXRPAmountPair(-5), 1, 2, true);
        checkMulRatio(makeXRPAmountPair(-5), 1, 2, false);

        // Full UInt32 num/den
        for (auto&& [v, num, den, roundUp] :
             std::initializer_list<std::tuple<int64_t, uint32_t, uint32_t, bool>>{
                 {int64_t{2}, u32Max, 1u, false},
                 {int64_t{-2}, u32Max, 1u, true},
                 {int64_t{u32Max - 1}, 1u, u32Max, true},
                 {int64_t{u32Max - 1}, 1u, u32Max, false},
                 {int64_t{12345}, u32Max, u32Max, false},
                 {int64_t{-12345}, u32Max, u32Max, true},
                 {int64_t{1} << 31, 1u << 31, 1u, false},
                 {-(int64_t{1} << 31), 1u << 31, 1u, false},
                 // Overflows int64 but not int128 → both error.
                 {int64Max / 2, 3u, 1u, false},
                 {int64Max / 2, 3u, 1u, true}})
        {
            checkMulRatio(makeXRPAmountPair(v), num, den, roundUp);
        }

        // -2^62 * 2 / 1 == INT64_MIN exactly
        {
            int64_t v = -(int64_t{1} << 62);
            auto lean = LeanXRPResult::from_lean(lean_xrp_mul_ratio(v, 2, 1, 0));
            int64_t cpp = mulRatio(XRPAmount{v}, 2, 1, false).drops();
            BEAST_EXPECT(lean.ok);
            BEAST_EXPECT(lean.drops == int64Min);
            BEAST_EXPECT(lean.drops == cpp);
        }

        constexpr int64_t vals[] = {int64Min, int64Min + 1, int64Max - 1, int64Max};
        for (int64_t a : vals)
        {
            auto const pa = makeXRPAmountPair(a);
            checkOfInt64(a);
            for (int64_t b : vals)
            {
                auto const pb = makeXRPAmountPair(b);
                checkAllCompare(pa, pb);
                checkEqInt(pa, b);
                checkNeInt(pa, b);
                checkMul(pa, b);
                checkAddInt(pa, b);
                checkSubInt(pa, b);
            }
        }
    }

private:
    void
    runTests() override
    {
        test_fuzz_of_number();
        test_fuzz_to_number();
        test_fuzz_add();
        test_fuzz_sub();
        test_fuzz_neg();
        test_fuzz_mul_ratio();
        test_fuzz_compare();
        test_fuzz_compare_int();
        test_fuzz_of_int64();
        test_fuzz_mul();
        test_fuzz_add_sub_int();
        test_known_values();
        // test_extreme_values();
    }
};

BEAST_DEFINE_TESTSUITE(LeanXRPAmount, formal_verification, xrpl);

}  // namespace xrpl::test
