#include <test/formal_verification/common/LeanSuite.h>
#include <test/formal_verification/ffi/protocol/NumberFFI.h>
#include <test/formal_verification/numbers/helpers/NumberGenerators.h>
#include <test/formal_verification/numbers/helpers/NumberHelpers.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/IOUAmount.h>

#include <cstdint>
#include <exception>
#include <initializer_list>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>

// Lean IOUAmount ops (exported by xrpl-lean4/XRPL/IOUAmount/FFI.lean).
extern "C" {
lean_object*
lean_iou_of_mantissa_exp(int64_t, int64_t, uint8_t);
lean_object*
lean_iou_of_number(uint8_t, uint64_t, int64_t, uint8_t);
lean_object*
lean_iou_to_number(int64_t, int64_t, uint8_t);
uint8_t
lean_iou_eq(int64_t, int64_t, int64_t, int64_t);
uint8_t
lean_iou_ne(int64_t, int64_t, int64_t, int64_t);
lean_object*
lean_iou_lt(int64_t, int64_t, int64_t, int64_t, uint8_t);
lean_object*
lean_iou_le(int64_t, int64_t, int64_t, int64_t, uint8_t);
lean_object*
lean_iou_gt(int64_t, int64_t, int64_t, int64_t, uint8_t);
lean_object*
lean_iou_ge(int64_t, int64_t, int64_t, int64_t, uint8_t);
lean_object*
lean_iou_neg(int64_t, int64_t, uint8_t);
lean_object*
lean_iou_add(int64_t, int64_t, int64_t, int64_t, uint8_t);
lean_object*
lean_iou_sub(int64_t, int64_t, int64_t, int64_t, uint8_t);
lean_object*
lean_iou_mul_ratio(int64_t, int64_t, uint32_t, uint32_t, uint8_t, uint8_t);
}

namespace xrpl::test {

using namespace formal_verification;

class LeanIOUAmount_test : public LeanSuite
{
    static std::string
    fmtIOU(int64_t m, int64_t e)
    {
        std::stringstream ss;
        ss << m << "e" << e;
        return ss.str();
    }

    static std::string
    fmtIOU(IOUAmount const& a)
    {
        return fmtIOU(a.mantissa(), a.exponent());
    }

    static std::string
    fmtIOU(LeanIOUResult const& r)
    {
        if (!r.ok)
            return "<err>";
        return fmtIOU(r.mantissa, r.exponent);
    }

    // Compare a Lean IOUAmount result to a C++ outcome (which may throw).
    bool
    checkIOUResult(
        std::string const& label,
        LeanIOUResult const& lean,
        IOUAmount const& cpp,
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
        if (lean.mantissa != cpp.mantissa() || lean.exponent != cpp.exponent())
        {
            std::stringstream ss;
            ss << label << ": result mismatch lean=" << fmtIOU(lean) << " cpp=" << fmtIOU(cpp);
            fail(ss.str());
            return false;
        }
        pass();
        return true;
    }

    // ofMantissaExp ↔ IOUAmount{m, e}.
    bool
    checkOfMantissaExp(int64_t m, int64_t e, Number::RoundingMode mode)
    {
        NumberRoundModeGuard mg(mode);
        auto lean = LeanIOUResult::from_lean(lean_iou_of_mantissa_exp(m, e, toLeanMode(mode)));
        IOUAmount cpp;
        bool cppThrew = false;
        try
        {
            cpp = IOUAmount{m, static_cast<int>(e)};
        }
        catch (std::overflow_error const&)
        {
            cppThrew = true;
        }
        return checkIOUResult("ofMantissaExp(" + fmtIOU(m, e) + ")", lean, cpp, cppThrew);
    }

    // ofNumber ↔ IOUAmount{Number{...}}.
    bool
    checkOfNumber(NumberPair const& p, Number::RoundingMode mode)
    {
        NumberRoundModeGuard mg(mode);
        auto lean = LeanIOUResult::from_lean(lean_iou_of_number(
            p.leanNum.negative,
            p.leanNum.mantissa,
            static_cast<int64_t>(p.leanNum.exponent),
            toLeanMode(mode)));
        IOUAmount cpp;
        bool cppThrew = false;
        try
        {
            cpp = IOUAmount{p.cppNum};
        }
        catch (std::overflow_error const&)
        {
            cppThrew = true;
        }
        std::stringstream label;
        label << "ofNumber(" << (p.leanNum.negative ? "-" : "+") << p.leanNum.mantissa << "e"
              << p.cppNum.exponent() << ")";
        return checkIOUResult(label.str(), lean, cpp, cppThrew);
    }

    // toNumber ↔ static_cast<Number>(IOUAmount).
    bool
    checkToNumber(IOUAmountPair const& p, Number::RoundingMode mode)
    {
        NumberRoundModeGuard mg(mode);
        auto lean = LeanNumberResult::from_lean(
            lean_iou_to_number(p.leanMant, p.leanExp, toLeanMode(mode)));
        Number cpp;
        bool cppThrew = false;
        try
        {
            cpp = static_cast<Number>(p.cppIou);
        }
        catch (std::overflow_error const&)
        {
            cppThrew = true;
        }
        std::stringstream label;
        label << "toNumber(" << fmtIOU(p.cppIou) << ")";
        if (lean.ok == cppThrew)
        {
            std::stringstream ss;
            ss << label.str() << ": error mismatch (lean." << (lean.ok ? "ok" : "error") << ", cpp."
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
            ss << label.str() << ": result mismatch lean=" << format(lean)
               << " cpp=" << format(cpp);
            fail(ss.str());
            return false;
        }
        pass();
        return true;
    }

    // Round-trip IOUAmount → Number → IOUAmount must be identity.
    bool
    checkRoundTripIOUNumberIOU(int64_t m, int64_t e, Number::RoundingMode mode)
    {
        NumberRoundModeGuard mg(mode);
        // First normalize via Lean ofMantissaExp so we test the canonical form.
        auto canonical = LeanIOUResult::from_lean(lean_iou_of_mantissa_exp(m, e, toLeanMode(mode)));
        bool ok1 = BEAST_EXPECT(canonical.ok);
        if (!ok1)
            return false;
        // toNumber
        auto asNum = LeanNumberResult::from_lean(
            lean_iou_to_number(canonical.mantissa, canonical.exponent, toLeanMode(mode)));
        bool ok2 = BEAST_EXPECT(asNum.ok);
        if (!ok2)
            return false;
        // back via ofNumber
        auto back = LeanIOUResult::from_lean(lean_iou_of_number(
            asNum.negative,
            asNum.mantissa,
            static_cast<int64_t>(asNum.exponent),
            toLeanMode(mode)));
        bool ok3 = BEAST_EXPECT(back.ok);
        if (!ok3)
            return false;
        bool eq = (back.mantissa == canonical.mantissa) && (back.exponent == canonical.exponent);
        if (!eq)
        {
            std::stringstream ss;
            ss << "round_trip(" << fmtIOU(m, e) << "): canonical=" << fmtIOU(canonical)
               << " back=" << fmtIOU(back);
            fail(ss.str());
            return false;
        }
        pass();
        return true;
    }

    bool
    checkOrdering(
        char const* op,
        lean_object* (*leanFn)(int64_t, int64_t, int64_t, int64_t, uint8_t),
        IOUAmountPair const& a,
        IOUAmountPair const& b,
        bool cppRet,
        Number::RoundingMode mode)
    {
        NumberRoundModeGuard mg(mode);
        auto lean = LeanBoolResult::from_lean(
            leanFn(a.leanMant, a.leanExp, b.leanMant, b.leanExp, toLeanMode(mode)));
        if (!lean.ok)
        {
            std::stringstream ss;
            ss << op << "(" << fmtIOU(a.cppIou) << "," << fmtIOU(b.cppIou)
               << "): lean errored on canonical IOU input";
            fail(ss.str());
            return false;
        }
        if ((lean.value != 0) != cppRet)
        {
            std::stringstream ss;
            ss << op << "(" << fmtIOU(a.cppIou) << "," << fmtIOU(b.cppIou)
               << "): lean=" << (lean.value != 0) << " cpp=" << cppRet;
            fail(ss.str());
            return false;
        }
        pass();
        return true;
    }

    bool
    checkLt(IOUAmountPair const& a, IOUAmountPair const& b, Number::RoundingMode mode)
    {
        return checkOrdering("lt", lean_iou_lt, a, b, a.cppIou < b.cppIou, mode);
    }

    bool
    checkLe(IOUAmountPair const& a, IOUAmountPair const& b, Number::RoundingMode mode)
    {
        return checkOrdering("le", lean_iou_le, a, b, a.cppIou <= b.cppIou, mode);
    }

    bool
    checkGt(IOUAmountPair const& a, IOUAmountPair const& b, Number::RoundingMode mode)
    {
        return checkOrdering("gt", lean_iou_gt, a, b, a.cppIou > b.cppIou, mode);
    }

    bool
    checkGe(IOUAmountPair const& a, IOUAmountPair const& b, Number::RoundingMode mode)
    {
        return checkOrdering("ge", lean_iou_ge, a, b, a.cppIou >= b.cppIou, mode);
    }

    // Field-level equality — no mode, no error channel.
    bool
    checkEq(IOUAmountPair const& a, IOUAmountPair const& b)
    {
        bool const lean = lean_iou_eq(a.leanMant, a.leanExp, b.leanMant, b.leanExp) != 0;
        bool const cpp = a.cppIou == b.cppIou;
        if (lean != cpp)
        {
            std::stringstream ss;
            ss << "eq(" << fmtIOU(a.cppIou) << "," << fmtIOU(b.cppIou) << "): lean=" << lean
               << " cpp=" << cpp;
            fail(ss.str());
            return false;
        }
        pass();
        return true;
    }

    bool
    checkNe(IOUAmountPair const& a, IOUAmountPair const& b)
    {
        bool const lean = lean_iou_ne(a.leanMant, a.leanExp, b.leanMant, b.leanExp) != 0;
        bool const cpp = a.cppIou != b.cppIou;
        if (lean != cpp)
        {
            std::stringstream ss;
            ss << "ne(" << fmtIOU(a.cppIou) << "," << fmtIOU(b.cppIou) << "): lean=" << lean
               << " cpp=" << cpp;
            fail(ss.str());
            return false;
        }
        pass();
        return true;
    }

    bool
    checkAllCompare(IOUAmountPair const& a, IOUAmountPair const& b, Number::RoundingMode mode)
    {
        bool ok = true;
        ok &= checkEq(a, b);
        ok &= checkNe(a, b);
        ok &= checkLt(a, b, mode);
        ok &= checkLe(a, b, mode);
        ok &= checkGt(a, b, mode);
        ok &= checkGe(a, b, mode);
        return ok;
    }

    bool
    checkNeg(IOUAmountPair const& p, Number::RoundingMode mode)
    {
        NumberRoundModeGuard mg(mode);
        auto lean = LeanIOUResult::from_lean(lean_iou_neg(p.leanMant, p.leanExp, toLeanMode(mode)));
        IOUAmount cpp;
        bool cppThrew = false;
        try
        {
            cpp = -p.cppIou;
        }
        catch (std::overflow_error const&)
        {
            cppThrew = true;
        }
        return checkIOUResult("neg(" + fmtIOU(p.cppIou) + ")", lean, cpp, cppThrew);
    }

    bool
    checkAdd(IOUAmountPair const& a, IOUAmountPair const& b, Number::RoundingMode mode)
    {
        NumberRoundModeGuard mg(mode);
        auto lean = LeanIOUResult::from_lean(
            lean_iou_add(a.leanMant, a.leanExp, b.leanMant, b.leanExp, toLeanMode(mode)));
        IOUAmount cpp;
        bool cppThrew = false;
        try
        {
            cpp = a.cppIou + b.cppIou;
        }
        catch (std::overflow_error const&)
        {
            cppThrew = true;
        }
        return checkIOUResult(
            "add(" + fmtIOU(a.cppIou) + "," + fmtIOU(b.cppIou) + ")", lean, cpp, cppThrew);
    }

    bool
    checkSub(IOUAmountPair const& a, IOUAmountPair const& b, Number::RoundingMode mode)
    {
        NumberRoundModeGuard mg(mode);
        auto lean = LeanIOUResult::from_lean(
            lean_iou_sub(a.leanMant, a.leanExp, b.leanMant, b.leanExp, toLeanMode(mode)));
        IOUAmount cpp;
        bool cppThrew = false;
        try
        {
            cpp = a.cppIou - b.cppIou;
        }
        catch (std::overflow_error const&)
        {
            cppThrew = true;
        }
        return checkIOUResult(
            "sub(" + fmtIOU(a.cppIou) + "," + fmtIOU(b.cppIou) + ")", lean, cpp, cppThrew);
    }

    bool
    checkMulRatio(
        IOUAmountPair const& p,
        uint32_t num,
        uint32_t den,
        bool roundUp,
        Number::RoundingMode mode)
    {
        NumberRoundModeGuard mg(mode);
        auto lean = LeanIOUResult::from_lean(
            lean_iou_mul_ratio(p.leanMant, p.leanExp, num, den, roundUp ? 1 : 0, toLeanMode(mode)));
        IOUAmount cpp;
        bool cppThrew = false;
        try
        {
            cpp = mulRatio(p.cppIou, num, den, roundUp);
        }
        catch (std::exception const&)
        {
            cppThrew = true;
        }
        std::stringstream label;
        label << "mulRatio(" << fmtIOU(p.cppIou) << "," << num << "," << den << ","
              << (roundUp ? "true" : "false") << ")";
        return checkIOUResult(label.str(), lean, cpp, cppThrew);
    }

    static int64_t
    randomMantissa(std::mt19937_64& rng, uint64_t magnitudeMin, uint64_t magnitudeMax)
    {
        std::uniform_int_distribution<uint64_t> dist(magnitudeMin, magnitudeMax);
        std::bernoulli_distribution sign(0.5);
        uint64_t mag = dist(rng);
        return sign(rng) ? -static_cast<int64_t>(mag) : static_cast<int64_t>(mag);
    }

    static int64_t
    randomExponent(std::mt19937_64& rng, int min, int max)
    {
        std::uniform_int_distribution<int> dist(min, max);
        return static_cast<int64_t>(dist(rng));
    }

public:
    void
    test_known_construction()
    {
        beginCase("LeanIOUAmount.known_construction");
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);

        constexpr int64_t kMinValue = static_cast<int64_t>(STAmount::kMinValue);  // 10^15
        constexpr int64_t kMaxValue = static_cast<int64_t>(STAmount::kMaxValue);  // 10^16 - 1
        constexpr int eMin = STAmount::kMinOffset;
        constexpr int eMax = STAmount::kMaxOffset;

        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            // Zero handling (canonical IOU zero: m=0, e=-100).
            checkOfMantissaExp(0, 0, mode);
            checkOfMantissaExp(0, 5, mode);
            checkOfMantissaExp(0, -100, mode);

            // Already-normalized inputs at boundary.
            checkOfMantissaExp(kMinValue, 0, mode);
            checkOfMantissaExp(kMaxValue, 0, mode);
            checkOfMantissaExp(-kMinValue, 0, mode);
            checkOfMantissaExp(-kMaxValue, 0, mode);

            // Below kMinValue → multiplied up.
            checkOfMantissaExp(1, 0, mode);
            checkOfMantissaExp(-1, 0, mode);
            checkOfMantissaExp(kMinValue - 1, 0, mode);

            // Above kMaxValue → divided down.
            checkOfMantissaExp(kMaxValue + 1, 0, mode);
            checkOfMantissaExp(kMaxValue * 9, 0, mode);

            // Exponent boundaries — canonical mantissa at the offset corners.
            checkOfMantissaExp(kMinValue, eMin, mode);
            checkOfMantissaExp(kMinValue, eMax, mode);
            checkOfMantissaExp(kMaxValue, eMin, mode);
            checkOfMantissaExp(kMaxValue, eMax, mode);

            // ofNumber happy path: in-range Number maps to a normalized IOU.
            checkOfNumber(makeNumberPair(false, 1'000'000'000'000'000'000ULL, 0), mode);
            checkOfNumber(makeNumberPair(true, 1'000'000'000'000'000'000ULL, 0), mode);
            checkOfNumber(makeNumberPair(false, 5'000'000'000'000'000'000ULL, -3), mode);
            checkOfNumber(makeNumberPair(false, Number::minMantissa(), eMin), mode);
            checkOfNumber(makeNumberPair(false, Number::kMaxRep, eMax), mode);

            // toNumber on canonical IOUAmount values.
            checkToNumber(makeIOUAmountPair(kMinValue, 0), mode);
            checkToNumber(makeIOUAmountPair(kMaxValue, 0), mode);
            checkToNumber(makeIOUAmountPair(-kMinValue, 0), mode);
            checkToNumber(makeIOUAmountPair(-kMaxValue, 0), mode);
            checkToNumber(makeIOUAmountPair(kMinValue, eMin), mode);
            checkToNumber(makeIOUAmountPair(kMaxValue, eMax), mode);
            // toNumber on IOU zero (m=0, e=-100): mantissa-only equality.
            checkToNumber(makeIOUAmountPair(0, -100), mode);

            // Round-trip IOU -> Number -> IOU on canonical values.
            checkRoundTripIOUNumberIOU(kMinValue, 0, mode);
            checkRoundTripIOUNumberIOU(kMaxValue, 0, mode);
            checkRoundTripIOUNumberIOU(-kMaxValue, 5, mode);
            checkRoundTripIOUNumberIOU(kMinValue, -50, mode);
            checkRoundTripIOUNumberIOU(kMaxValue, eMax, mode);
            checkRoundTripIOUNumberIOU(-kMinValue, eMin, mode);
        }
    }

    void
    test_fuzz_construction()
    {
        beginCase("LeanIOUAmount.fuzz_construction", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        auto& rng = nextRng();
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode save{Number::setround(mode)};
            runFuzz(20'000, [&] {
                int64_t m = randomMantissa(rng, 0, std::numeric_limits<int64_t>::max());
                int64_t e = randomExponent(rng, -120, 100);
                return checkOfMantissaExp(m, e, mode);
            });
        }
    }

    void
    test_fuzz_conversion()
    {
        beginCase("LeanIOUAmount.fuzz_conversion", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        auto& rng = nextRng();
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode save{Number::setround(mode)};
            runFuzz(20'000, [&] { return checkToNumber(randomIOUAmountPair(rng), mode); });
        }
    }

    void
    test_known_comparison()
    {
        beginCase("LeanIOUAmount.known_comparison");
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);

        constexpr int64_t kMinValue = static_cast<int64_t>(STAmount::kMinValue);
        constexpr int64_t kMaxValue = static_cast<int64_t>(STAmount::kMaxValue);
        constexpr int eMin = STAmount::kMinOffset;

        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            // Reflexive (must be false).
            checkAllCompare(makeIOUAmountPair(kMinValue, 0), makeIOUAmountPair(kMinValue, 0), mode);
            checkAllCompare(makeIOUAmountPair(0, -100), makeIOUAmountPair(0, -100), mode);
            checkAllCompare(
                makeIOUAmountPair(-kMaxValue, 5), makeIOUAmountPair(-kMaxValue, 5), mode);

            // Differ only in exponent.
            checkAllCompare(makeIOUAmountPair(kMinValue, 0), makeIOUAmountPair(kMinValue, 1), mode);
            checkAllCompare(makeIOUAmountPair(kMinValue, 1), makeIOUAmountPair(kMinValue, 0), mode);
            checkAllCompare(
                makeIOUAmountPair(-kMinValue, 0), makeIOUAmountPair(-kMinValue, 1), mode);

            // Zero vs non-zero at kMinOffset (smallest representable nonzero).
            checkAllCompare(makeIOUAmountPair(0, -100), makeIOUAmountPair(kMinValue, eMin), mode);
            checkAllCompare(makeIOUAmountPair(0, -100), makeIOUAmountPair(-kMinValue, eMin), mode);
            checkAllCompare(makeIOUAmountPair(kMinValue, eMin), makeIOUAmountPair(0, -100), mode);
            checkAllCompare(makeIOUAmountPair(-kMinValue, eMin), makeIOUAmountPair(0, -100), mode);

            // Same exponent, mantissa differs.
            checkAllCompare(makeIOUAmountPair(kMinValue, 0), makeIOUAmountPair(kMaxValue, 0), mode);
            checkAllCompare(makeIOUAmountPair(kMaxValue, 0), makeIOUAmountPair(kMinValue, 0), mode);
        }
    }

    void
    test_fuzz_comparison()
    {
        beginCase("LeanIOUAmount.fuzz_comparison", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        auto& rng = nextRng();
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode save{Number::setround(mode)};
            runFuzz(20'000, [&] {
                auto a = randomIOUAmountPair(rng);
                auto b = randomIOUAmountPair(rng);
                return checkAllCompare(a, b, mode);
            });
        }
    }

    void
    test_known_arithmetic()
    {
        beginCase("LeanIOUAmount.known_arithmetic");
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);

        constexpr int64_t kMinValue = static_cast<int64_t>(STAmount::kMinValue);
        constexpr int64_t kMaxValue = static_cast<int64_t>(STAmount::kMaxValue);
        constexpr int eMin = STAmount::kMinOffset;
        constexpr uint32_t u32Max = std::numeric_limits<uint32_t>::max();

        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            // neg on representative non-corner values (corner-only cases live
            // in test_extreme_values).
            checkNeg(makeIOUAmountPair(0, -100), mode);  // neg(0) stays zero.
            checkNeg(makeIOUAmountPair(0, 0), mode);     // neg of any zero stays zero.
            checkNeg(makeIOUAmountPair(kMinValue, 0), mode);
            checkNeg(makeIOUAmountPair(-kMinValue, 0), mode);

            // add: identity with zero.
            checkAdd(makeIOUAmountPair(kMinValue, 0), makeIOUAmountPair(0, -100), mode);
            checkAdd(makeIOUAmountPair(0, -100), makeIOUAmountPair(kMinValue, 0), mode);
            checkAdd(makeIOUAmountPair(0, -100), makeIOUAmountPair(0, -100), mode);

            // add: exact cancellation (a + (-a) = 0).
            checkAdd(makeIOUAmountPair(kMinValue, 0), makeIOUAmountPair(-kMinValue, 0), mode);
            checkAdd(makeIOUAmountPair(kMaxValue, 50), makeIOUAmountPair(-kMaxValue, 50), mode);

            // add: same-sign accumulation.
            checkAdd(makeIOUAmountPair(kMinValue, 0), makeIOUAmountPair(kMinValue, 0), mode);
            checkAdd(
                makeIOUAmountPair(5'000'000'000'000'000LL, 0),
                makeIOUAmountPair(5'000'000'000'000'000LL, 0),
                mode);

            // add: mixed-sign, partial cancellation.
            checkAdd(makeIOUAmountPair(kMaxValue, 0), makeIOUAmountPair(-kMinValue, 0), mode);
            checkAdd(makeIOUAmountPair(-kMaxValue, 0), makeIOUAmountPair(kMinValue, 0), mode);

            // add: differing exponents (shift normalization, in-range).
            checkAdd(makeIOUAmountPair(kMinValue, 0), makeIOUAmountPair(kMinValue, 5), mode);

            // sub: self-cancellation (a - a = 0) at non-corner exponents.
            checkSub(makeIOUAmountPair(kMinValue, 0), makeIOUAmountPair(kMinValue, 0), mode);
            checkSub(makeIOUAmountPair(kMaxValue, 50), makeIOUAmountPair(kMaxValue, 50), mode);
            checkSub(makeIOUAmountPair(-kMaxValue, -10), makeIOUAmountPair(-kMaxValue, -10), mode);

            // sub: zero - x = -x, x - zero = x.
            checkSub(makeIOUAmountPair(0, -100), makeIOUAmountPair(kMinValue, 0), mode);
            checkSub(makeIOUAmountPair(kMinValue, 0), makeIOUAmountPair(0, -100), mode);

            // mulRatio: exact ratio (no remainder) — roundUp/roundDown equal.
            checkMulRatio(makeIOUAmountPair(kMinValue, 0), 2, 1, false, mode);
            checkMulRatio(makeIOUAmountPair(kMinValue, 0), 2, 1, true, mode);
            checkMulRatio(makeIOUAmountPair(kMaxValue, 0), 1, 1, false, mode);
            checkMulRatio(makeIOUAmountPair(kMaxValue, 0), 1, 1, true, mode);
            // num = 0 → result zero.
            checkMulRatio(makeIOUAmountPair(kMinValue, 0), 0, 1, false, mode);
            checkMulRatio(makeIOUAmountPair(kMinValue, 0), 0, 1, true, mode);
            checkMulRatio(makeIOUAmountPair(-kMinValue, 0), 0, 100, true, mode);
            // den = 0 → both error.
            checkMulRatio(makeIOUAmountPair(kMinValue, 0), 1, 0, false, mode);
            checkMulRatio(makeIOUAmountPair(kMinValue, 0), 7, 0, true, mode);
            checkMulRatio(makeIOUAmountPair(0, -100), 1, 0, false, mode);
            // Inexact ratio — roundUp/roundDown differ for positive.
            checkMulRatio(makeIOUAmountPair(kMinValue, 0), 1, 3, false, mode);
            checkMulRatio(makeIOUAmountPair(kMinValue, 0), 1, 3, true, mode);
            // Inexact ratio, negative mantissa — the !roundUp && neg branch.
            checkMulRatio(makeIOUAmountPair(-kMinValue, 0), 1, 3, false, mode);
            checkMulRatio(makeIOUAmountPair(-kMinValue, 0), 1, 3, true, mode);
            // Small ratio — kFL64 / roomToGrow rescale path.
            checkMulRatio(makeIOUAmountPair(kMinValue, 0), 1, u32Max, false, mode);
            checkMulRatio(makeIOUAmountPair(kMinValue, 0), 1, u32Max, true, mode);
            checkMulRatio(makeIOUAmountPair(-kMinValue, 0), 1, u32Max, false, mode);
            checkMulRatio(makeIOUAmountPair(-kMinValue, 0), 1, u32Max, true, mode);
            // Large num — mustShrink / shrink path.
            checkMulRatio(makeIOUAmountPair(kMaxValue, 0), u32Max, 1, false, mode);
            checkMulRatio(makeIOUAmountPair(kMaxValue, 0), u32Max, 1, true, mode);
            checkMulRatio(makeIOUAmountPair(-kMaxValue, 0), u32Max, 1, false, mode);
            checkMulRatio(makeIOUAmountPair(-kMaxValue, 0), u32Max, 1, true, mode);
            // Zero-result rescue: positive+roundUp → minPositiveAmount.
            checkMulRatio(makeIOUAmountPair(kMinValue, eMin), 1, u32Max, true, mode);
            // Zero-result rescue: negative+roundDown → (-kMinValue, kMinOffset).
            checkMulRatio(makeIOUAmountPair(-kMinValue, eMin), 1, u32Max, false, mode);
            // Cross-product roundUp/roundDown × pos/neg matrix.
            checkMulRatio(makeIOUAmountPair(kMinValue, 0), 7, 13, true, mode);
            checkMulRatio(makeIOUAmountPair(kMinValue, 0), 7, 13, false, mode);
            checkMulRatio(makeIOUAmountPair(-kMinValue, 0), 7, 13, true, mode);
            checkMulRatio(makeIOUAmountPair(-kMinValue, 0), 7, 13, false, mode);
            // Zero amount.
            checkMulRatio(makeIOUAmountPair(0, -100), 7, 11, true, mode);
            checkMulRatio(makeIOUAmountPair(0, -100), 7, 11, false, mode);
            // Full UInt32 num and den combos.
            checkMulRatio(makeIOUAmountPair(kMaxValue, 0), u32Max, u32Max, false, mode);
            checkMulRatio(makeIOUAmountPair(-kMaxValue, 0), u32Max, u32Max, true, mode);
        }
    }

    void
    test_fuzz_arithmetic()
    {
        beginCase("LeanIOUAmount.fuzz_arithmetic", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        auto& rng = nextRng();
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode save{Number::setround(mode)};
            runFuzz(20'000, [&] {
                auto a = randomIOUAmountPair(rng);
                auto b = randomIOUAmountPair(rng);
                bool ok = true;
                ok &= checkNeg(a, mode);
                ok &= checkAdd(a, b, mode);
                ok &= checkSub(a, b, mode);
                return ok;
            });
        }
    }

    void
    test_fuzz_mul_ratio()
    {
        beginCase("LeanIOUAmount.fuzz_mul_ratio", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        auto& rng = nextRng();
        constexpr uint32_t u32Max = std::numeric_limits<uint32_t>::max();
        std::uniform_int_distribution<uint32_t> numDist(0u, u32Max);
        std::uniform_int_distribution<uint32_t> denDist(1u, u32Max);
        std::bernoulli_distribution roundDist(0.5);
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode save{Number::setround(mode)};
            runFuzz(20'000, [&] {
                auto p = randomIOUAmountPair(rng);
                uint32_t const num = numDist(rng);
                uint32_t const den = denDist(rng);
                bool const ru = roundDist(rng);
                return checkMulRatio(p, num, den, ru, mode);
            });
        }
    }

    void
    test_extreme_values()
    {
        beginCase("LeanIOUAmount.extreme_values");
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);

        constexpr int64_t kMin = static_cast<int64_t>(STAmount::kMinValue);
        constexpr int64_t kMax = static_cast<int64_t>(STAmount::kMaxValue);
        constexpr int eMax = STAmount::kMaxOffset;
        constexpr int eMin = STAmount::kMinOffset;
        constexpr uint32_t u32Max = std::numeric_limits<uint32_t>::max();

        // Largest and smallest nonzero representable magnitudes, both signs.
        auto const posMax = makeIOUAmountPair(kMax, eMax);
        auto const negMax = makeIOUAmountPair(-kMax, eMax);
        auto const posMin = makeIOUAmountPair(kMin, eMin);
        auto const negMin = makeIOUAmountPair(-kMin, eMin);
        auto const zero = makeIOUAmountPair(0, -100);

        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode save{Number::setround(mode)};

            // ofMantissaExp underflow path — exp shifts below kMinOffset → zero.
            checkOfMantissaExp(1, -110, mode);
            checkOfMantissaExp(10, -110, mode);
            // ofMantissaExp overflow path — exp shifts above kMaxOffset → error.
            checkOfMantissaExp(kMax, eMax + 1, mode);
            checkOfMantissaExp(kMin, eMax + 1, mode);
            // ofNumber overflow / underflow.
            checkOfNumber(makeNumberPair(false, Number::kMaxRep, eMax + 1), mode);
            checkOfNumber(makeNumberPair(false, Number::minMantissa(), -200), mode);

            // Compare at opposite-sign extreme corners.
            checkAllCompare(negMax, posMin, mode);
            checkAllCompare(posMin, negMax, mode);

            // Arithmetic at the max - same-sign max push the exponent past
            // kMaxOffset → overflow.
            checkAdd(posMax, posMax, mode);
            checkAdd(negMax, negMax, mode);
            // max - (-max) doubles the magnitude → overflow.
            checkSub(posMax, negMax, mode);
            checkSub(negMax, posMax, mode);
            // Exact cancellation at the extreme → zero, no overflow.
            checkAdd(posMax, negMax, mode);
            checkSub(posMax, posMax, mode);

            // Extreme ± smallest: the tiny operand is below precision and dropped.
            checkAdd(posMax, posMin, mode);
            checkSub(posMax, posMin, mode);
            checkAdd(negMax, negMin, mode);

            // Smallest nonzero accumulation / cancellation.
            checkAdd(posMin, posMin, mode);
            checkSub(posMin, posMin, mode);
            checkAdd(posMin, negMin, mode);

            // Maximal exponent spread: kMinValue@kMinOffset + kMaxValue@kMaxOffset
            // — the tiny operand is below precision and is dropped.
            checkAdd(makeIOUAmountPair(kMin, eMin), makeIOUAmountPair(kMax, eMax), mode);

            // neg at every corner (sign-magnitude range is symmetric → never
            // overflows).
            checkNeg(posMax, mode);
            checkNeg(negMax, mode);
            checkNeg(posMin, mode);
            checkNeg(negMin, mode);
            checkNeg(makeIOUAmountPair(-kMax, eMin), mode);
            checkNeg(zero, mode);

            // Scale the maximum up → exponent past kMaxOffset → overflow.
            checkMulRatio(posMax, u32Max, 1, false, mode);
            checkMulRatio(posMax, u32Max, 1, true, mode);
            checkMulRatio(negMax, u32Max, 1, true, mode);
            // Scale the minimum down → underflow toward zero / rescue path.
            checkMulRatio(posMin, 1, u32Max, false, mode);
            checkMulRatio(posMin, 1, u32Max, true, mode);
            checkMulRatio(negMin, 1, u32Max, false, mode);
            // Full-range num and den together at the extremes.
            checkMulRatio(posMax, u32Max, u32Max, false, mode);
            checkMulRatio(negMax, u32Max, u32Max, true, mode);
            // num = den = 1 (no-op ratio) at every corner — exercises the
            // identity path at the boundary.
            checkMulRatio(posMin, 1, 1, false, mode);
            checkMulRatio(posMax, 1, 1, false, mode);
            checkMulRatio(makeIOUAmountPair(-kMax, eMin), 1, 1, true, mode);

            // Comparison at the extremes.
            checkAllCompare(posMax, negMax, mode);
            checkAllCompare(posMax, posMin, mode);
            checkAllCompare(negMax, negMin, mode);
            checkAllCompare(posMax, posMax, mode);
        }
        // Note: IOUAmount has no Unchecked ctor, so operation inputs must be
        // canonical (above). Raw int64 mantissa-field extremes are exercised in
        // test_known_construction / test_fuzz_construction, where both sides run
        // the same normalize(raw mantissa, exp).
    }

private:
    void
    runTests() override
    {
        // Lean models only the post-switchover (Number-mediated)
        // TODO: add back this line when fixes merged
        // NumberSO const so{true};
        test_fuzz_construction();
        test_fuzz_conversion();
        // test_fuzz_comparison();
        // test_fuzz_arithmetic();
        test_fuzz_mul_ratio();
        test_known_construction();
        test_known_comparison();
        test_known_arithmetic();
        // test_extreme_values();
    }
};

BEAST_DEFINE_TESTSUITE(LeanIOUAmount, formal_verification, xrpl);

}  // namespace xrpl::test
