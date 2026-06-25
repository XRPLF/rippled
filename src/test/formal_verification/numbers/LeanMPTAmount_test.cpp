#include <test/formal_verification/common/LeanSuite.h>
#include <test/formal_verification/ffi/protocol/NumberFFI.h>
#include <test/formal_verification/numbers/helpers/NumberGenerators.h>
#include <test/formal_verification/numbers/helpers/NumberHelpers.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/MPTAmount.h>
#include <xrpl/protocol/Protocol.h>

#include <exception>
#include <initializer_list>
#include <limits>
#include <random>
#include <sstream>
#include <tuple>

extern "C" {
int64_t
lean_mpt_of_int64(int64_t);
lean_object*
lean_mpt_of_number(uint8_t neg, uint64_t mant, int64_t exp, uint8_t mode);
lean_object*
lean_mpt_to_number(int64_t v, uint8_t mode);
uint8_t
lean_mpt_eq(int64_t, int64_t);
uint8_t
lean_mpt_ne(int64_t, int64_t);
uint8_t
lean_mpt_eq_int(int64_t, int64_t);
uint8_t
lean_mpt_ne_int(int64_t, int64_t);
uint8_t
lean_mpt_lt(int64_t, int64_t);
uint8_t
lean_mpt_le(int64_t, int64_t);
uint8_t
lean_mpt_gt(int64_t, int64_t);
uint8_t
lean_mpt_ge(int64_t, int64_t);
int64_t
lean_mpt_add(int64_t v1, int64_t v2);
int64_t
lean_mpt_sub(int64_t v1, int64_t v2);
int64_t
lean_mpt_neg(int64_t v);
int64_t
lean_mpt_add_int(int64_t, int64_t);
int64_t
lean_mpt_sub_int(int64_t, int64_t);
lean_object*
lean_mpt_mul_ratio(int64_t v, uint32_t num, uint32_t den, uint8_t roundUp);
}

namespace xrpl::test {

using namespace formal_verification;

class LeanMPTAmount_test : public LeanSuite
{
    static std::string
    fmtVal(int64_t v)
    {
        std::stringstream ss;
        ss << v;
        return ss.str();
    }

    // ofNumber: C++ MPTAmount(Number) throws on out-of-range via static_cast<int64_t>.
    // Lean returns status=1 in that case. Compare value when both succeed.
    bool
    checkOfNumber(NumberPair const& p, Number::RoundingMode mode)
    {
        auto lean = LeanMPTAmountResult::from_lean(lean_mpt_of_number(
            p.leanNum.negative,
            p.leanNum.mantissa,
            static_cast<int64_t>(p.leanNum.exponent),
            toLeanMode(mode)));
        bool cppThrew = false;
        int64_t cpp = 0;
        try
        {
            cpp = MPTAmount{p.cppNum}.value();
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
            fail(
                label() + ": error mismatch lean.ok=" + (lean.ok ? "1" : "0") +
                " cppThrew=" + (cppThrew ? "1" : "0"));
            return false;
        }
        if (!lean.ok)
        {
            pass();
            return true;
        }
        if (lean.value != cpp)
        {
            std::stringstream ss;
            ss << label() << ": value mismatch lean=" << lean.value << " cpp=" << cpp;
            fail(ss.str());
            return false;
        }
        pass();
        return true;
    }

    // toNumber: C++ side is the implicit `operator Number()` ctor — never throws.
    bool
    checkToNumber(MPTAmountPair const& p, Number::RoundingMode mode)
    {
        auto lean = LeanNumberResult::from_lean(lean_mpt_to_number(p.leanMpt, toLeanMode(mode)));
        Number cpp = static_cast<Number>(p.cppMpt);
        if (!lean.ok)
        {
            fail("toNumber(" + fmtVal(p.leanMpt) + "): Lean returned error");
            return false;
        }
        if (!fieldsEqual(lean, cpp))
        {
            std::stringstream ss;
            ss << "toNumber(" << p.leanMpt << "): mismatch lean=" << format(lean)
               << " cpp=" << format(cpp);
            fail(ss.str());
            return false;
        }
        pass();
        return true;
    }

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

    bool
    expectValue(char const* op, int64_t lhs, int64_t rhs, int64_t lean, int64_t cpp)
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
    checkEq(MPTAmountPair const& a, MPTAmountPair const& b)
    {
        return expectBool(
            "eq", a.leanMpt, b.leanMpt, lean_mpt_eq(a.leanMpt, b.leanMpt), a.cppMpt == b.cppMpt);
    }

    bool
    checkNe(MPTAmountPair const& a, MPTAmountPair const& b)
    {
        return expectBool(
            "ne", a.leanMpt, b.leanMpt, lean_mpt_ne(a.leanMpt, b.leanMpt), a.cppMpt != b.cppMpt);
    }

    bool
    checkLt(MPTAmountPair const& a, MPTAmountPair const& b)
    {
        return expectBool(
            "lt", a.leanMpt, b.leanMpt, lean_mpt_lt(a.leanMpt, b.leanMpt), a.cppMpt < b.cppMpt);
    }

    bool
    checkLe(MPTAmountPair const& a, MPTAmountPair const& b)
    {
        return expectBool(
            "le", a.leanMpt, b.leanMpt, lean_mpt_le(a.leanMpt, b.leanMpt), a.cppMpt <= b.cppMpt);
    }

    bool
    checkGt(MPTAmountPair const& a, MPTAmountPair const& b)
    {
        return expectBool(
            "gt", a.leanMpt, b.leanMpt, lean_mpt_gt(a.leanMpt, b.leanMpt), a.cppMpt > b.cppMpt);
    }

    bool
    checkGe(MPTAmountPair const& a, MPTAmountPair const& b)
    {
        return expectBool(
            "ge", a.leanMpt, b.leanMpt, lean_mpt_ge(a.leanMpt, b.leanMpt), a.cppMpt >= b.cppMpt);
    }

    bool
    checkEqInt(MPTAmountPair const& p, int64_t n)
    {
        return expectBool("eqInt", p.leanMpt, n, lean_mpt_eq_int(p.leanMpt, n), p.cppMpt == n);
    }

    bool
    checkNeInt(MPTAmountPair const& p, int64_t n)
    {
        return expectBool("neInt", p.leanMpt, n, lean_mpt_ne_int(p.leanMpt, n), !(p.cppMpt == n));
    }

    bool
    checkAllCompare(MPTAmountPair const& a, MPTAmountPair const& b)
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
    checkOfInt64(int64_t v)
    {
        return expectValue("ofInt64", v, 0, lean_mpt_of_int64(v), MPTAmount{v}.value());
    }

    // C++ MPTAmount arithmetic is signed-overflow UB; compute the wrapped int64
    // reference via __builtin_*_overflow and only call C++ when it won't wrap.
    bool
    checkAdd(MPTAmountPair const& a, MPTAmountPair const& b)
    {
        int64_t w;
        bool const ov = __builtin_add_overflow(a.leanMpt, b.leanMpt, &w);
        return expectValue(
            "add",
            a.leanMpt,
            b.leanMpt,
            lean_mpt_add(a.leanMpt, b.leanMpt),
            ov ? w : (a.cppMpt + b.cppMpt).value());
    }

    bool
    checkSub(MPTAmountPair const& a, MPTAmountPair const& b)
    {
        int64_t w;
        bool const ov = __builtin_sub_overflow(a.leanMpt, b.leanMpt, &w);
        return expectValue(
            "sub",
            a.leanMpt,
            b.leanMpt,
            lean_mpt_sub(a.leanMpt, b.leanMpt),
            ov ? w : (a.cppMpt - b.cppMpt).value());
    }

    bool
    checkNeg(MPTAmountPair const& a)
    {
        // -INT64_MIN is signed-overflow UB; compute the wrapped reference via
        // __builtin_sub_overflow(0, x) and only invoke -a.cppMpt when safe.
        int64_t w;
        bool const ov = __builtin_sub_overflow(int64_t{0}, a.leanMpt, &w);
        return expectValue(
            "neg", a.leanMpt, 0, lean_mpt_neg(a.leanMpt), ov ? w : (-a.cppMpt).value());
    }

    bool
    checkAddInt(MPTAmountPair const& p, int64_t n)
    {
        int64_t w;
        bool const ov = __builtin_add_overflow(p.leanMpt, n, &w);
        return expectValue(
            "addInt",
            p.leanMpt,
            n,
            lean_mpt_add_int(p.leanMpt, n),
            ov ? w : (p.cppMpt + MPTAmount{n}).value());
    }

    bool
    checkSubInt(MPTAmountPair const& p, int64_t n)
    {
        int64_t w;
        bool const ov = __builtin_sub_overflow(p.leanMpt, n, &w);
        return expectValue(
            "subInt",
            p.leanMpt,
            n,
            lean_mpt_sub_int(p.leanMpt, n),
            ov ? w : (p.cppMpt - MPTAmount{n}).value());
    }

    bool
    checkMulRatio(MPTAmountPair const& v, uint32_t num, uint32_t den, bool roundUp)
    {
        auto lean = LeanMPTAmountResult::from_lean(
            lean_mpt_mul_ratio(v.leanMpt, num, den, roundUp ? 1u : 0u));
        bool cppThrew = false;
        int64_t cpp = 0;
        try
        {
            cpp = mulRatio(v.cppMpt, num, den, roundUp).value();
        }
        catch (std::exception const&)
        {
            cppThrew = true;
        }

        auto label = [&] {
            std::stringstream ss;
            ss << "mulRatio(" << v.leanMpt << "," << num << "," << den << ","
               << (roundUp ? "true" : "false") << ")";
            return ss.str();
        };
        if (lean.ok == cppThrew)
        {
            fail(
                label() + ": error mismatch lean.ok=" + (lean.ok ? "1" : "0") +
                " cppThrew=" + (cppThrew ? "1" : "0"));
            return false;
        }
        if (!lean.ok)
        {
            pass();
            return true;
        }
        if (lean.value != cpp)
        {
            std::stringstream ss;
            ss << label() << ": value mismatch lean=" << lean.value << " cpp=" << cpp;
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
        beginCase("LeanMPTAmount.fuzz_of_number", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large330);
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode save{Number::setround(mode)};
            runFuzz(50'000, [&] { return checkOfNumber(randomNumberPair(-100, 100), mode); });
        }
    }

    void
    test_fuzz_to_number()
    {
        beginCase("LeanMPTAmount.fuzz_to_number", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large330);
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode save{Number::setround(mode)};
            runFuzz(50'000, [&] { return checkToNumber(randomMPTAmountPair(), mode); });
        }
    }

    void
    test_fuzz_add()
    {
        beginCase("LeanMPTAmount.fuzz_add", true);
        runFuzz(100'000, [&] { return checkAdd(randomMPTAmountPair(), randomMPTAmountPair()); });
    }

    void
    test_fuzz_sub()
    {
        beginCase("LeanMPTAmount.fuzz_sub", true);
        runFuzz(100'000, [&] { return checkSub(randomMPTAmountPair(), randomMPTAmountPair()); });
    }

    void
    test_fuzz_neg()
    {
        beginCase("LeanMPTAmount.fuzz_neg", true);
        runFuzz(100'000, [&] { return checkNeg(randomMPTAmountPair()); });
    }

    void
    test_fuzz_mul_ratio()
    {
        beginCase("LeanMPTAmount.fuzz_mul_ratio", true);
        auto& rng = nextRng();
        std::uniform_int_distribution<uint32_t> numDist(0, std::numeric_limits<uint32_t>::max());
        std::uniform_int_distribution<uint32_t> denDist(1, std::numeric_limits<uint32_t>::max());
        std::bernoulli_distribution roundDist(0.5);
        runFuzz(100'000, [&] {
            auto p = randomMPTAmountPair();
            uint32_t const num = numDist(rng);
            uint32_t const den = denDist(rng);
            bool const ru = roundDist(rng);
            return checkMulRatio(p, num, den, ru);
        });
    }

    void
    test_fuzz_compare()
    {
        beginCase("LeanMPTAmount.fuzz_compare", true);
        runFuzz(
            100'000, [&] { return checkAllCompare(randomMPTAmountPair(), randomMPTAmountPair()); });
    }

    void
    test_fuzz_compare_int()
    {
        beginCase("LeanMPTAmount.fuzz_compare_int", true);
        auto& rng = nextRng();
        runFuzz(100'000, [&] {
            auto p = randomMPTAmountPair();
            int64_t const n = randomInt64(rng);
            return checkEqInt(p, n) && checkNeInt(p, n);
        });
    }

    void
    test_fuzz_of_int64()
    {
        beginCase("LeanMPTAmount.fuzz_of_int64", true);
        runFuzz(100'000, [&] { return checkOfInt64(randomMPTAmountPair().leanMpt); });
    }

    void
    test_fuzz_add_sub_int()
    {
        beginCase("LeanMPTAmount.fuzz_add_sub_int", true);
        auto& rng = nextRng();
        runFuzz(100'000, [&] {
            auto p = randomMPTAmountPair();
            int64_t const n = randomInt64(rng);
            return checkAddInt(p, n) && checkSubInt(p, n);
        });
    }

    void
    test_known_values()
    {
        beginCase("LeanMPTAmount.known_values");
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large330);
        SaveNumberRoundMode save{Number::setround(Number::RoundingMode::ToNearest)};

        constexpr int64_t int64Max = std::numeric_limits<int64_t>::max();
        constexpr int64_t halfMax = int64Max / 2;
        constexpr int64_t e18 = 1'000'000'000'000'000'000LL;
        static_assert(
            static_cast<int64_t>(kMaxMpTokenAmount) == int64Max,
            "kMaxMpTokenAmount must equal INT64_MAX");

        checkAdd(makeMPTAmountPair(0), makeMPTAmountPair(0));
        checkAdd(makeMPTAmountPair(1), makeMPTAmountPair(-1));
        checkAdd(makeMPTAmountPair(halfMax), makeMPTAmountPair(halfMax));
        checkAdd(makeMPTAmountPair(e18), makeMPTAmountPair(7 * e18));

        checkSub(makeMPTAmountPair(0), makeMPTAmountPair(0));

        checkNeg(makeMPTAmountPair(0));
        checkNeg(makeMPTAmountPair(1));
        checkNeg(makeMPTAmountPair(-1));

        checkMulRatio(makeMPTAmountPair(e18), 3, 2, true);
        checkMulRatio(makeMPTAmountPair(e18), 3, 2, false);
        checkMulRatio(makeMPTAmountPair(-halfMax), 3, 2, false);
        checkMulRatio(makeMPTAmountPair(-halfMax), 3, 2, true);
        // 1/3 roundUp/roundDown across signs.
        checkMulRatio(makeMPTAmountPair(1), 1, 3, true);
        checkMulRatio(makeMPTAmountPair(1), 1, 3, false);
        checkMulRatio(makeMPTAmountPair(-1), 1, 3, true);
        checkMulRatio(makeMPTAmountPair(-1), 1, 3, false);
        // num = 0 short-circuits; den = 0 errors on both sides.
        checkMulRatio(makeMPTAmountPair(int64Max), 0, 1, true);
        checkMulRatio(makeMPTAmountPair(int64Max), 0, 1, false);
        checkMulRatio(makeMPTAmountPair(e18), 7, 0, true);
        checkMulRatio(makeMPTAmountPair(0), 1, 0, false);

        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode m{Number::setround(mode)};
            checkToNumber(makeMPTAmountPair(0), mode);
            checkToNumber(makeMPTAmountPair(1), mode);
            checkToNumber(makeMPTAmountPair(-1), mode);
            checkToNumber(makeMPTAmountPair(1'000'000'000LL), mode);
            checkToNumber(makeMPTAmountPair(-1'000'000'000LL), mode);

            checkOfNumber(makeNumberPair(false, 0, 0), mode);
            checkOfNumber(makeNumberPair(false, e18, 0), mode);
            checkOfNumber(makeNumberPair(true, e18, 0), mode);
            // Fractional input: each mode rounds differently.
            checkOfNumber(makeNumberPair(false, 1'234'567'890'123'456'789ULL, -2), mode);
            checkOfNumber(makeNumberPair(true, 1'234'567'890'123'456'789ULL, -2), mode);
        }

        // Small-value grid: compare/op-int over ±0..2.
        constexpr int64_t vals[] = {-2, -1, 0, 1, 2};
        for (int64_t a : vals)
        {
            auto const pa = makeMPTAmountPair(a);
            checkOfInt64(a);
            checkNeg(pa);
            for (int64_t b : vals)
            {
                auto const pb = makeMPTAmountPair(b);
                checkAllCompare(pa, pb);
                checkEqInt(pa, b);
                checkNeInt(pa, b);
                checkAddInt(pa, b);
                checkSubInt(pa, b);
            }
        }
    }

    void
    test_extreme_values()
    {
        beginCase("LeanMPTAmount.extreme_values");
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large330);
        SaveNumberRoundMode save{Number::setround(Number::RoundingMode::ToNearest)};

        constexpr int64_t int64Max = std::numeric_limits<int64_t>::max();
        constexpr int64_t int64Min = std::numeric_limits<int64_t>::min();
        constexpr int64_t kMax = static_cast<int64_t>(kMaxMpTokenAmount);

        // toNumber at the int64 boundaries. INT64_MIN is included
        for (int64_t v : {int64Min, int64Min + 1, -kMax, int64_t{-1}, int64_t{0}, int64_t{1}, kMax})
            checkToNumber(makeMPTAmountPair(v), Number::RoundingMode::ToNearest);

        // add/sub at the int64 boundaries — INT64_MAX+1, INT64_MIN-1 are C++ UB;
        checkAdd(makeMPTAmountPair(int64Max - 1), makeMPTAmountPair(1));
        checkAdd(makeMPTAmountPair(int64Max), makeMPTAmountPair(1));
        checkAdd(makeMPTAmountPair(int64Min), makeMPTAmountPair(-1));
        checkAdd(makeMPTAmountPair(int64Max), makeMPTAmountPair(-int64Max));
        checkAdd(makeMPTAmountPair(0), makeMPTAmountPair(int64Max));
        checkAdd(makeMPTAmountPair(0), makeMPTAmountPair(int64Min));
        checkSub(makeMPTAmountPair(0), makeMPTAmountPair(int64Min + 1));
        checkSub(makeMPTAmountPair(0), makeMPTAmountPair(int64Min));
        checkSub(makeMPTAmountPair(0), makeMPTAmountPair(int64Max));
        checkSub(makeMPTAmountPair(int64Max), makeMPTAmountPair(int64Max - 1));
        checkSub(makeMPTAmountPair(int64Max), makeMPTAmountPair(int64Max));
        checkSub(makeMPTAmountPair(int64Min), makeMPTAmountPair(int64Min));

        // neg at boundaries. -INT64_MIN is C++ UB
        checkNeg(makeMPTAmountPair(int64Max));
        checkNeg(makeMPTAmountPair(-int64Max));
        checkNeg(makeMPTAmountPair(int64Min + 1));
        {
            int64_t lean = lean_mpt_neg(int64Min);
            int64_t cpp;
            __builtin_sub_overflow(int64_t{0}, int64Min, &cpp);
            BEAST_EXPECT(lean == int64Min);
            BEAST_EXPECT(cpp == int64Min);
            BEAST_EXPECT(lean == cpp);
        }

        // ofNumber at boundary mantissas + overflow via large exponent.
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode m{Number::setround(mode)};
            checkOfNumber(makeNumberPair(false, Number::minMantissa(), 0), mode);
            checkOfNumber(makeNumberPair(false, Number::kMaxRep, 0), mode);
            checkOfNumber(makeNumberPair(true, Number::kMaxRep, 0), mode);
            checkOfNumber(makeNumberPair(false, Number::kMaxRep, -18), mode);
            checkOfNumber(makeNumberPair(true, Number::kMaxRep, -18), mode);
            checkOfNumber(makeNumberPair(false, Number::kMaxRep, 5), mode);
            checkOfNumber(makeNumberPair(true, Number::kMaxRep, 5), mode);
        }

        // mulRatio at the boundaries.
        checkMulRatio(makeMPTAmountPair(int64Max), 1, 1, false);
        checkMulRatio(makeMPTAmountPair(int64Max), 1, 1, true);
        checkMulRatio(makeMPTAmountPair(-int64Max), 1, 1, false);
        checkMulRatio(makeMPTAmountPair(-int64Max), 1, 1, true);
        // INT64_MAX * 1000 > INT64_MAX → both error.
        checkMulRatio(makeMPTAmountPair(int64Max), 1000, 1, true);
        checkMulRatio(makeMPTAmountPair(int64Max), 1000, 1, false);

        // -2^62 * 2 / 1 == INT64_MIN exactly - boundary without saturation.
        {
            int64_t v = -(int64_t{1} << 62);
            auto lean = LeanMPTAmountResult::from_lean(lean_mpt_mul_ratio(v, 2, 1, 0));
            int64_t cpp = mulRatio(MPTAmount{v}, 2, 1, false).value();
            BEAST_EXPECT(lean.ok);
            BEAST_EXPECT(lean.value == int64Min);
            BEAST_EXPECT(lean.value == cpp);
        }

        // Full UInt32 num/den - exercises the int128 path.
        constexpr uint32_t u32Max = std::numeric_limits<uint32_t>::max();
        for (auto&& [v, num, den, roundUp] :
             std::initializer_list<std::tuple<int64_t, uint32_t, uint32_t, bool>>{
                 {int64_t{2}, u32Max, 1u, false},
                 {int64_t{-2}, u32Max, 1u, true},
                 {int64_t{u32Max - 1}, 1u, u32Max, true},
                 {int64_t{u32Max - 1}, 1u, u32Max, false},
                 {int64_t{12345}, u32Max, u32Max, false},
                 {int64_t{-12345}, u32Max, u32Max, true},
                 {int64_t{1} << 31, 1u << 31, 1u, false},
                 {-(int64_t{1} << 31), 1u << 31, 1u, false}})
        {
            checkMulRatio(makeMPTAmountPair(v), num, den, roundUp);
        }

        // mulRatio underflow: r < INT64_MIN. Lean saturates; C++ goes through impl-defined
        // int128→int64
        checkMulRatio(makeMPTAmountPair(-(int64_t{1} << 50)), 1'000'000u, 1u, false);
        checkMulRatio(makeMPTAmountPair(int64Min + 1), 2u, 1u, false);

        // Compare/op-int boundary grid: INT64_MIN/MAX corners the small-value
        // grid in known_values can't cover.
        constexpr int64_t vals[] = {int64Min, int64Min + 1, int64Max - 1, int64Max};
        for (int64_t a : vals)
        {
            auto const pa = makeMPTAmountPair(a);
            checkOfInt64(a);
            for (int64_t b : vals)
            {
                auto const pb = makeMPTAmountPair(b);
                checkAllCompare(pa, pb);
                checkEqInt(pa, b);
                checkNeInt(pa, b);
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
        test_fuzz_add_sub_int();
        test_known_values();
        // test_extreme_values();
    }
};

BEAST_DEFINE_TESTSUITE(LeanMPTAmount, formal_verification, xrpl);

}  // namespace xrpl::test
