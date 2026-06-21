#include <test/formal_verification/common/LeanSuite.h>
#include <test/formal_verification/ffi/protocol/NumberFFI.h>
#include <test/formal_verification/numbers/helpers/NumberGenerators.h>
#include <test/formal_verification/numbers/helpers/NumberHelpers.h>
#include <test/formal_verification/numbers/helpers/NumberTypes.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTAmount.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <exception>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>

// Lean STAmount FFI exports (xrpl-lean4/XRPL/STAmount/FFI.lean).
extern "C" {
lean_object*
lean_stamount_xrp(uint8_t, uint64_t, int64_t, uint8_t);
lean_object*
lean_stamount_iou(uint8_t, uint64_t, int64_t, uint8_t, uint8_t);
lean_object*
lean_stamount_mpt(uint8_t, uint64_t, int64_t, uint8_t);
lean_object*
lean_stamount_to_number(uint8_t, uint64_t, int64_t, uint8_t, uint8_t);
lean_object*
lean_stamount_checked(uint8_t, uint64_t, int64_t, uint8_t, uint8_t);
lean_object*
lean_stamount_of_int64(uint8_t, int64_t, int64_t, uint8_t);
lean_object*
lean_stamount_of_iou_amount(int64_t, int64_t, uint8_t);
lean_object*
lean_stamount_of_xrp_amount(int64_t, uint8_t);
lean_object*
lean_stamount_of_mpt_amount(int64_t, uint8_t);
lean_object*
lean_stamount_of_number(uint8_t, uint8_t, uint64_t, int64_t, uint8_t);
lean_object*
lean_stamount_lt(uint8_t, uint64_t, int64_t, uint8_t, uint8_t, uint64_t, int64_t, uint8_t);
lean_object*
lean_stamount_add(
    uint8_t,
    uint64_t,
    int64_t,
    uint8_t,
    uint8_t,
    uint64_t,
    int64_t,
    uint8_t,
    uint8_t);
lean_object*
lean_stamount_sub(
    uint8_t,
    uint64_t,
    int64_t,
    uint8_t,
    uint8_t,
    uint64_t,
    int64_t,
    uint8_t,
    uint8_t);
lean_object*
lean_stamount_multiply(
    uint8_t,
    uint64_t,
    int64_t,
    uint8_t,
    uint8_t,
    uint64_t,
    int64_t,
    uint8_t,
    uint8_t,
    uint8_t);
lean_object*
lean_stamount_divide(
    uint8_t,
    uint64_t,
    int64_t,
    uint8_t,
    uint8_t,
    uint64_t,
    int64_t,
    uint8_t,
    uint8_t,
    uint8_t);
lean_object*
lean_stamount_mul_round(
    uint8_t,
    uint64_t,
    int64_t,
    uint8_t,
    uint8_t,
    uint64_t,
    int64_t,
    uint8_t,
    uint8_t,
    uint8_t,
    uint8_t);
lean_object*
lean_stamount_mul_round_strict(
    uint8_t,
    uint64_t,
    int64_t,
    uint8_t,
    uint8_t,
    uint64_t,
    int64_t,
    uint8_t,
    uint8_t,
    uint8_t,
    uint8_t);
lean_object*
lean_stamount_div_round(
    uint8_t,
    uint64_t,
    int64_t,
    uint8_t,
    uint8_t,
    uint64_t,
    int64_t,
    uint8_t,
    uint8_t,
    uint8_t,
    uint8_t);
lean_object*
lean_stamount_div_round_strict(
    uint8_t,
    uint64_t,
    int64_t,
    uint8_t,
    uint8_t,
    uint64_t,
    int64_t,
    uint8_t,
    uint8_t,
    uint8_t,
    uint8_t);
lean_object*
lean_stamount_can_add(
    uint8_t,
    uint64_t,
    int64_t,
    uint8_t,
    uint8_t,
    uint64_t,
    int64_t,
    uint8_t,
    uint8_t);
lean_object*
lean_stamount_can_subtract(
    uint8_t,
    uint64_t,
    int64_t,
    uint8_t,
    uint8_t,
    uint64_t,
    int64_t,
    uint8_t);
lean_object*
lean_stamount_round_to_scale(uint8_t, uint64_t, int64_t, uint8_t, int64_t, uint8_t);
uint64_t
lean_stamount_get_rate(
    uint8_t,
    uint64_t,
    int64_t,
    uint8_t,
    uint8_t,
    uint64_t,
    int64_t,
    uint8_t,
    uint8_t);
lean_object*
lean_stamount_neg(uint8_t, uint64_t, int64_t, uint8_t);
lean_object*
lean_stamount_of_native_int64(int64_t);
lean_object*
lean_stamount_unchecked_from_int64(uint8_t, int64_t, int64_t);
uint8_t
lean_stamount_eq(uint8_t, uint64_t, int64_t, uint8_t, uint8_t, uint64_t, int64_t, uint8_t);
uint8_t
lean_stamount_ne(uint8_t, uint64_t, int64_t, uint8_t, uint8_t, uint64_t, int64_t, uint8_t);
lean_object*
lean_stamount_le(uint8_t, uint64_t, int64_t, uint8_t, uint8_t, uint64_t, int64_t, uint8_t);
lean_object*
lean_stamount_gt(uint8_t, uint64_t, int64_t, uint8_t, uint8_t, uint64_t, int64_t, uint8_t);
lean_object*
lean_stamount_ge(uint8_t, uint64_t, int64_t, uint8_t, uint8_t, uint64_t, int64_t, uint8_t);
uint8_t
lean_stamount_are_comparable(
    uint8_t,
    uint64_t,
    int64_t,
    uint8_t,
    uint8_t,
    uint64_t,
    int64_t,
    uint8_t);
uint8_t
lean_stamount_is_legal_net(uint8_t, uint64_t, int64_t, uint8_t);
}

namespace xrpl::test {

using namespace formal_verification;

namespace {

// Mirrors FFI_Common.lean's encodeAsset.
uint8_t
kindFromAsset(Asset const& asset)
{
    return asset.visit(
        [](Issue const& iss) -> uint8_t { return iss.native() ? kKindXRP : kKindIOU; },
        [](MPTIssue const&) -> uint8_t { return kKindMPT; });
}

bool
stAmountFieldsEqual(LeanSTAmountResult const& lean, STAmount const& cpp)
{
    return lean.assetKind == kindFromAsset(cpp.asset()) && lean.mValue == cpp.mantissa() &&
        lean.mOffset == cpp.exponent() && (lean.isNegative != 0) == cpp.negative();
}

}  // namespace

class LeanSTAmount_test : public LeanSuite
{
    static std::string
    label(char const* op, uint8_t kind, uint64_t mValue, int64_t mOffset, uint8_t isNeg)
    {
        std::stringstream ss;
        ss << op << "(kind=" << static_cast<int>(kind) << "," << (isNeg ? "-" : "+") << mValue
           << "e" << mOffset << ")";
        return ss.str();
    }

    bool
    checkStAmountResult(
        std::string const& tag,
        LeanSTAmountResult const& lean,
        STAmount const& cpp,
        bool cppThrew)
    {
        if (lean.ok == cppThrew)
        {
            std::stringstream ss;
            ss << tag << ": error mismatch lean.ok=" << lean.ok << " cppThrew=" << cppThrew;
            fail(ss.str());
            return false;
        }
        if (!lean.ok)
        {
            pass();
            return true;
        }
        if (!stAmountFieldsEqual(lean, cpp))
        {
            std::stringstream ss;
            ss << tag << ": value mismatch lean=" << format(lean) << " cpp=" << format(cpp);
            fail(ss.str());
            return false;
        }
        pass();
        return true;
    }

    // Differential wrapper for STAmount-producing ops: decode the Lean result,
    // invoke cppFn in try/catch, hand both to checkStAmountResult.
    template <typename CppFn>
    bool
    runSTAmountOp(std::string const& tag, lean_object* leanRaw, CppFn&& cppFn)
    {
        auto lean = LeanSTAmountResult::from_lean(leanRaw);
        STAmount cpp;
        bool cppThrew = false;
        try
        {
            cpp = cppFn();
        }
        catch (std::exception const&)
        {
            cppThrew = true;
        }
        return checkStAmountResult(tag, lean, cpp, cppThrew);
    }

    // Cross-kind accessor calls (e.g. xrp() on IOU) must error on both sides.
    bool
    checkAccessors(STAmountPair const& p, Number::RoundingMode mode)
    {
        uint8_t const kind = p.leanSt.assetKind;
        uint64_t const mValue = p.leanSt.mValue;
        int64_t const mOffset = p.leanSt.mOffset;
        uint8_t const isNeg = p.leanSt.isNegative;
        STAmount const& cpp = p.cppSt;
        std::string const tag = label("accessor", kind, mValue, mOffset, isNeg);
        uint8_t const leanMode = toLeanMode(mode);
        bool ok = true;

        {
            auto lean = LeanXRPResult::from_lean(lean_stamount_xrp(kind, mValue, mOffset, isNeg));
            bool cppThrew = false;
            int64_t cppDrops = 0;
            try
            {
                cppDrops = cpp.xrp().drops();
            }
            catch (std::exception const&)
            {
                cppThrew = true;
            }
            if (lean.ok == cppThrew)
            {
                fail(tag + ".xrp: error mismatch");
                ok = false;
            }
            else if (lean.ok && lean.drops != cppDrops)
            {
                fail(tag + ".xrp: drops mismatch");
                ok = false;
            }
            else
                pass();
        }
        {
            NumberRoundModeGuard mg(mode);
            auto lean =
                LeanIOUResult::from_lean(lean_stamount_iou(kind, mValue, mOffset, isNeg, leanMode));
            bool cppThrew = false;
            IOUAmount cppIou;
            try
            {
                cppIou = cpp.iou();
            }
            catch (std::exception const&)
            {
                cppThrew = true;
            }
            if (lean.ok == cppThrew)
            {
                fail(tag + ".iou: error mismatch");
                ok = false;
            }
            else if (
                lean.ok &&
                (lean.mantissa != cppIou.mantissa() || lean.exponent != cppIou.exponent()))
            {
                fail(tag + ".iou: value mismatch");
                ok = false;
            }
            else
                pass();
        }
        {
            auto lean =
                LeanMPTAmountResult::from_lean(lean_stamount_mpt(kind, mValue, mOffset, isNeg));
            bool cppThrew = false;
            int64_t cppVal = 0;
            try
            {
                cppVal = cpp.mpt().value();
            }
            catch (std::exception const&)
            {
                cppThrew = true;
            }
            if (lean.ok == cppThrew)
            {
                fail(tag + ".mpt: error mismatch");
                ok = false;
            }
            else if (lean.ok && lean.value != cppVal)
            {
                fail(tag + ".mpt: value mismatch");
                ok = false;
            }
            else
                pass();
        }
        {
            NumberRoundModeGuard mg(mode);
            auto lean = LeanNumberResult::from_lean(
                lean_stamount_to_number(kind, mValue, mOffset, isNeg, leanMode));
            bool cppThrew = false;
            Number cppN;
            try
            {
                cppN = static_cast<Number>(cpp);
            }
            catch (std::exception const&)
            {
                cppThrew = true;
            }
            if (lean.ok == cppThrew)
            {
                fail(tag + ".toNumber: error mismatch");
                ok = false;
            }
            else if (lean.ok && !fieldsEqual(lean, cppN))
            {
                std::stringstream ss;
                ss << tag << ".toNumber: mismatch lean=" << format(lean) << " cpp=" << format(cppN);
                fail(ss.str());
                ok = false;
            }
            else
                pass();
        }
        return ok;
    }

    bool
    checkChecked(STAmountPair const& p, Number::RoundingMode mode)
    {
        auto const& s = p.leanSt;
        NumberRoundModeGuard mg(mode);
        // Use the checked ctor to match Lean's error path.
        return runSTAmountOp(
            label("checked", s.assetKind, s.mValue, s.mOffset, s.isNegative),
            lean_stamount_checked(s.assetKind, s.mValue, s.mOffset, s.isNegative, toLeanMode(mode)),
            [&] {
                return STAmount{
                    assetForKind(s.assetKind),
                    s.mValue,
                    static_cast<int>(s.mOffset),
                    s.isNegative != 0};
            });
    }

    bool
    checkOfInt64(uint8_t kind, int64_t mantissa, int64_t exponent, Number::RoundingMode mode)
    {
        NumberRoundModeGuard mg(mode);
        std::stringstream tag;
        tag << "ofInt64(kind=" << static_cast<int>(kind) << "," << mantissa << "e" << exponent
            << ")";
        return runSTAmountOp(
            tag.str(), lean_stamount_of_int64(kind, mantissa, exponent, toLeanMode(mode)), [&] {
                return assetForKind(kind).visit(
                    [&](Issue const& iss) {
                        return STAmount{iss, mantissa, static_cast<int>(exponent)};
                    },
                    [&](MPTIssue const& mpt) {
                        return STAmount{mpt, mantissa, static_cast<int>(exponent)};
                    });
            });
    }

    bool
    checkAddSub(
        char const* op,
        bool isAdd,
        STAmountPair const& a,
        STAmountPair const& b,
        Number::RoundingMode mode)
    {
        NumberRoundModeGuard mg(mode);
        auto leanFn = isAdd ? lean_stamount_add : lean_stamount_sub;
        std::stringstream tag;
        tag << op << "(k1=" << static_cast<int>(a.leanSt.assetKind)
            << ",k2=" << static_cast<int>(b.leanSt.assetKind) << ")";
        return runSTAmountOp(
            tag.str(),
            leanFn(
                a.leanSt.assetKind,
                a.leanSt.mValue,
                a.leanSt.mOffset,
                a.leanSt.isNegative,
                b.leanSt.assetKind,
                b.leanSt.mValue,
                b.leanSt.mOffset,
                b.leanSt.isNegative,
                toLeanMode(mode)),
            [&] { return isAdd ? (a.cppSt + b.cppSt) : (a.cppSt - b.cppSt); });
    }

    bool
    checkMultiply(
        STAmountPair const& a,
        STAmountPair const& b,
        uint8_t assetKind,
        Number::RoundingMode mode)
    {
        NumberRoundModeGuard mg(mode);
        std::stringstream tag;
        tag << "multiply(k1=" << static_cast<int>(a.leanSt.assetKind)
            << ",k2=" << static_cast<int>(b.leanSt.assetKind)
            << ",ak=" << static_cast<int>(assetKind) << ")";
        return runSTAmountOp(
            tag.str(),
            lean_stamount_multiply(
                a.leanSt.assetKind,
                a.leanSt.mValue,
                a.leanSt.mOffset,
                a.leanSt.isNegative,
                b.leanSt.assetKind,
                b.leanSt.mValue,
                b.leanSt.mOffset,
                b.leanSt.isNegative,
                assetKind,
                toLeanMode(mode)),
            [&] { return multiply(a.cppSt, b.cppSt, assetForKind(assetKind)); });
    }

    bool
    checkDivide(
        STAmountPair const& num,
        STAmountPair const& den,
        uint8_t assetKind,
        Number::RoundingMode mode)
    {
        NumberRoundModeGuard mg(mode);
        std::stringstream tag;
        tag << "divide(kn=" << static_cast<int>(num.leanSt.assetKind)
            << ",kd=" << static_cast<int>(den.leanSt.assetKind)
            << ",ak=" << static_cast<int>(assetKind) << ")";
        return runSTAmountOp(
            tag.str(),
            lean_stamount_divide(
                num.leanSt.assetKind,
                num.leanSt.mValue,
                num.leanSt.mOffset,
                num.leanSt.isNegative,
                den.leanSt.assetKind,
                den.leanSt.mValue,
                den.leanSt.mOffset,
                den.leanSt.isNegative,
                assetKind,
                toLeanMode(mode)),
            [&] { return divide(num.cppSt, den.cppSt, assetForKind(assetKind)); });
    }

    template <typename LeanFn, typename CppFn>
    bool
    checkMulRoundLike(
        char const* op,
        LeanFn leanFn,
        CppFn cppFn,
        STAmountPair const& a,
        STAmountPair const& b,
        uint8_t assetKind,
        bool roundUp,
        Number::RoundingMode mode)
    {
        NumberRoundModeGuard mg(mode);
        std::stringstream tag;
        tag << op << "(k1=" << static_cast<int>(a.leanSt.assetKind)
            << ",k2=" << static_cast<int>(b.leanSt.assetKind)
            << ",ak=" << static_cast<int>(assetKind) << ",ru=" << roundUp << ")";
        return runSTAmountOp(
            tag.str(),
            leanFn(
                a.leanSt.assetKind,
                a.leanSt.mValue,
                a.leanSt.mOffset,
                a.leanSt.isNegative,
                b.leanSt.assetKind,
                b.leanSt.mValue,
                b.leanSt.mOffset,
                b.leanSt.isNegative,
                assetKind,
                roundUp ? 1u : 0u,
                toLeanMode(mode)),
            [&] { return cppFn(a.cppSt, b.cppSt, assetForKind(assetKind), roundUp); });
    }

    template <typename LeanFn, typename CppFn>
    bool
    checkDivRoundLike(
        char const* op,
        LeanFn leanFn,
        CppFn cppFn,
        STAmountPair const& num,
        STAmountPair const& den,
        uint8_t assetKind,
        bool roundUp,
        Number::RoundingMode mode)
    {
        NumberRoundModeGuard mg(mode);
        std::stringstream tag;
        tag << op << "(kn=" << static_cast<int>(num.leanSt.assetKind)
            << ",kd=" << static_cast<int>(den.leanSt.assetKind)
            << ",ak=" << static_cast<int>(assetKind) << ",ru=" << roundUp << ")";
        return runSTAmountOp(
            tag.str(),
            leanFn(
                num.leanSt.assetKind,
                num.leanSt.mValue,
                num.leanSt.mOffset,
                num.leanSt.isNegative,
                den.leanSt.assetKind,
                den.leanSt.mValue,
                den.leanSt.mOffset,
                den.leanSt.isNegative,
                assetKind,
                roundUp ? 1u : 0u,
                toLeanMode(mode)),
            [&] { return cppFn(num.cppSt, den.cppSt, assetForKind(assetKind), roundUp); });
    }

    bool
    checkCanAdd(STAmountPair const& a, STAmountPair const& b, Number::RoundingMode mode)
    {
        uint8_t const leanMode = toLeanMode(mode);
        NumberRoundModeGuard mg(mode);
        auto lean = LeanBoolResult::from_lean(lean_stamount_can_add(
            a.leanSt.assetKind,
            a.leanSt.mValue,
            a.leanSt.mOffset,
            a.leanSt.isNegative,
            b.leanSt.assetKind,
            b.leanSt.mValue,
            b.leanSt.mOffset,
            b.leanSt.isNegative,
            leanMode));
        bool cppThrew = false;
        bool cppRet = false;
        try
        {
            cppRet = canAdd(a.cppSt, b.cppSt);
        }
        catch (std::exception const&)
        {
            cppThrew = true;
        }
        std::stringstream tag;
        tag << "canAdd(k1=" << static_cast<int>(a.leanSt.assetKind)
            << ",k2=" << static_cast<int>(b.leanSt.assetKind) << ")";
        if (lean.ok == cppThrew)
        {
            fail(tag.str() + ": error mismatch");
            return false;
        }
        if (lean.ok && lean.value != cppRet)
        {
            std::stringstream ss;
            ss << tag.str() << ": value mismatch lean=" << lean.value << " cpp=" << cppRet;
            fail(ss.str());
            return false;
        }
        pass();
        return true;
    }

    bool
    checkCanSub(STAmountPair const& a, STAmountPair const& b)
    {
        auto lean = LeanBoolResult::from_lean(lean_stamount_can_subtract(
            a.leanSt.assetKind,
            a.leanSt.mValue,
            a.leanSt.mOffset,
            a.leanSt.isNegative,
            b.leanSt.assetKind,
            b.leanSt.mValue,
            b.leanSt.mOffset,
            b.leanSt.isNegative));
        bool cppThrew = false;
        bool cppRet = false;
        try
        {
            cppRet = canSubtract(a.cppSt, b.cppSt);
        }
        catch (std::exception const&)
        {
            cppThrew = true;
        }
        std::stringstream tag;
        tag << "canSub(k1=" << static_cast<int>(a.leanSt.assetKind)
            << ",k2=" << static_cast<int>(b.leanSt.assetKind) << ")";
        if (lean.ok == cppThrew)
        {
            fail(tag.str() + ": error mismatch");
            return false;
        }
        if (lean.ok && lean.value != cppRet)
        {
            fail(tag.str() + ": value mismatch");
            return false;
        }
        pass();
        return true;
    }

    bool
    checkRoundToScale(STAmountPair const& p, int32_t scale, Number::RoundingMode mode)
    {
        NumberRoundModeGuard mg(mode);
        std::stringstream tag;
        tag << "roundToScale(kind=" << static_cast<int>(p.leanSt.assetKind) << ",scale=" << scale
            << ")";
        return runSTAmountOp(
            tag.str(),
            lean_stamount_round_to_scale(
                p.leanSt.assetKind,
                p.leanSt.mValue,
                p.leanSt.mOffset,
                p.leanSt.isNegative,
                scale,
                toLeanMode(mode)),
            [&] { return roundToScale(p.cppSt, scale, mode); });
    }

    bool
    checkGetRate(
        STAmountPair const& offerOut,
        STAmountPair const& offerIn,
        Number::RoundingMode mode)
    {
        uint8_t const leanMode = toLeanMode(mode);
        NumberRoundModeGuard mg(mode);
        uint64_t const lean = lean_stamount_get_rate(
            offerOut.leanSt.assetKind,
            offerOut.leanSt.mValue,
            offerOut.leanSt.mOffset,
            offerOut.leanSt.isNegative,
            offerIn.leanSt.assetKind,
            offerIn.leanSt.mValue,
            offerIn.leanSt.mOffset,
            offerIn.leanSt.isNegative,
            leanMode);
        uint64_t const cpp = getRate(offerOut.cppSt, offerIn.cppSt);
        if (lean != cpp)
        {
            std::stringstream ss;
            ss << "getRate(kOut=" << static_cast<int>(offerOut.leanSt.assetKind)
               << ",kIn=" << static_cast<int>(offerIn.leanSt.assetKind) << "): lean=" << lean
               << " cpp=" << cpp;
            fail(ss.str());
            return false;
        }
        pass();
        return true;
    }

    bool
    expectBool(char const* op, uint8_t lean, bool cpp)
    {
        if ((lean != 0) != cpp)
        {
            std::stringstream ss;
            ss << op << ": lean=" << (lean != 0) << " cpp=" << cpp;
            fail(ss.str());
            return false;
        }
        pass();
        return true;
    }

    bool
    checkEq(STAmountPair const& a, STAmountPair const& b)
    {
        return expectBool(
            "eq",
            lean_stamount_eq(
                a.leanSt.assetKind,
                a.leanSt.mValue,
                a.leanSt.mOffset,
                a.leanSt.isNegative,
                b.leanSt.assetKind,
                b.leanSt.mValue,
                b.leanSt.mOffset,
                b.leanSt.isNegative),
            a.cppSt == b.cppSt);
    }

    bool
    checkNe(STAmountPair const& a, STAmountPair const& b)
    {
        return expectBool(
            "ne",
            lean_stamount_ne(
                a.leanSt.assetKind,
                a.leanSt.mValue,
                a.leanSt.mOffset,
                a.leanSt.isNegative,
                b.leanSt.assetKind,
                b.leanSt.mValue,
                b.leanSt.mOffset,
                b.leanSt.isNegative),
            a.cppSt != b.cppSt);
    }

    bool
    checkOrdering(
        char const* op,
        lean_object* (
            *leanFn)(uint8_t, uint64_t, int64_t, uint8_t, uint8_t, uint64_t, int64_t, uint8_t),
        bool cppRet,
        bool cppThrew,
        STAmountPair const& a,
        STAmountPair const& b)
    {
        auto lean = LeanBoolResult::from_lean(leanFn(
            a.leanSt.assetKind,
            a.leanSt.mValue,
            a.leanSt.mOffset,
            a.leanSt.isNegative,
            b.leanSt.assetKind,
            b.leanSt.mValue,
            b.leanSt.mOffset,
            b.leanSt.isNegative));
        std::stringstream tag;
        tag << op << "(k1=" << static_cast<int>(a.leanSt.assetKind)
            << ",k2=" << static_cast<int>(b.leanSt.assetKind) << ")";
        if (lean.ok == cppThrew)
        {
            fail(tag.str() + ": error mismatch");
            return false;
        }
        if (lean.ok && (lean.value != 0) != cppRet)
        {
            fail(tag.str() + ": value mismatch");
            return false;
        }
        pass();
        return true;
    }

    bool
    checkLt(STAmountPair const& a, STAmountPair const& b)
    {
        bool cpp = false, cppThrew = false;
        try
        {
            cpp = a.cppSt < b.cppSt;
        }
        catch (std::exception const&)
        {
            cppThrew = true;
        }
        return checkOrdering("lt", lean_stamount_lt, cpp, cppThrew, a, b);
    }

    bool
    checkLe(STAmountPair const& a, STAmountPair const& b)
    {
        bool cpp = false, cppThrew = false;
        try
        {
            cpp = a.cppSt <= b.cppSt;
        }
        catch (std::exception const&)
        {
            cppThrew = true;
        }
        return checkOrdering("le", lean_stamount_le, cpp, cppThrew, a, b);
    }

    bool
    checkGt(STAmountPair const& a, STAmountPair const& b)
    {
        bool cpp = false, cppThrew = false;
        try
        {
            cpp = a.cppSt > b.cppSt;
        }
        catch (std::exception const&)
        {
            cppThrew = true;
        }
        return checkOrdering("gt", lean_stamount_gt, cpp, cppThrew, a, b);
    }

    bool
    checkGe(STAmountPair const& a, STAmountPair const& b)
    {
        bool cpp = false, cppThrew = false;
        try
        {
            cpp = a.cppSt >= b.cppSt;
        }
        catch (std::exception const&)
        {
            cppThrew = true;
        }
        return checkOrdering("ge", lean_stamount_ge, cpp, cppThrew, a, b);
    }

    bool
    checkAreComparable(STAmountPair const& a, STAmountPair const& b)
    {
        // C++ areComparable is file-static; for sentinel assets kind ⇔ asset.
        return expectBool(
            "areComparable",
            lean_stamount_are_comparable(
                a.leanSt.assetKind,
                a.leanSt.mValue,
                a.leanSt.mOffset,
                a.leanSt.isNegative,
                b.leanSt.assetKind,
                b.leanSt.mValue,
                b.leanSt.mOffset,
                b.leanSt.isNegative),
            a.leanSt.assetKind == b.leanSt.assetKind);
    }

    bool
    checkIsLegalNet(STAmountPair const& p)
    {
        return expectBool(
            "isLegalNet",
            lean_stamount_is_legal_net(
                p.leanSt.assetKind, p.leanSt.mValue, p.leanSt.mOffset, p.leanSt.isNegative),
            isLegalNet(p.cppSt));
    }

    bool
    checkNeg(STAmountPair const& p)
    {
        auto const& s = p.leanSt;
        return runSTAmountOp(
            label("neg", s.assetKind, s.mValue, s.mOffset, s.isNegative),
            lean_stamount_neg(s.assetKind, s.mValue, s.mOffset, s.isNegative),
            [&] { return -p.cppSt; });
    }

    // Lean's ofNativeInt64 / uncheckedFromInt64 route through unchecked construction
    bool
    checkOfNativeInt64(int64_t drops)
    {
        bool const neg = drops < 0;
        std::stringstream tag;
        tag << "ofNativeInt64(" << drops << ")";
        return runSTAmountOp(tag.str(), lean_stamount_of_native_int64(drops), [&] {
            return stAmountUnchecked(kKindXRP, magnitude(drops), 0, neg ? 1 : 0);
        });
    }

    bool
    checkUncheckedFromInt64(uint8_t kind, int64_t v, int64_t offset)
    {
        bool const neg = v < 0;
        std::stringstream tag;
        tag << "uncheckedFromInt64(kind=" << static_cast<int>(kind) << "," << v << "e" << offset
            << ")";
        return runSTAmountOp(tag.str(), lean_stamount_unchecked_from_int64(kind, v, offset), [&] {
            return stAmountUnchecked(kind, magnitude(v), offset, neg ? 1 : 0);
        });
    }

public:
    void
    test_known_construction()
    {
        beginCase("LeanSTAmount.known_construction");
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        Number::RoundingMode const m = Number::RoundingMode::ToNearest;

        constexpr uint64_t kMin = STAmount::kMinValue;
        constexpr uint64_t kMax = STAmount::kMaxValue;
        constexpr int eMin = STAmount::kMinOffset;
        constexpr int eMax = STAmount::kMaxOffset;
        constexpr uint64_t xrpMax = STAmount::kMaxNativeN;

        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            checkAccessors(makeSTAmountPair(kKindXRP, 0, 0, 0), mode);
            checkAccessors(makeSTAmountPair(kKindXRP, 1, 0, 0), mode);
            checkAccessors(makeSTAmountPair(kKindXRP, 1, 0, 1), mode);
            checkAccessors(makeSTAmountPair(kKindXRP, xrpMax, 0, 0), mode);

            checkAccessors(makeSTAmountPair(kKindIOU, 0, -100, 0), mode);
            checkAccessors(makeSTAmountPair(kKindIOU, kMin, 0, 0), mode);
            checkAccessors(makeSTAmountPair(kKindIOU, kMax, eMax, 0), mode);
            checkAccessors(makeSTAmountPair(kKindIOU, kMin, eMin, 1), mode);

            checkAccessors(makeSTAmountPair(kKindMPT, 0, 0, 0), mode);
            checkAccessors(makeSTAmountPair(kKindMPT, 1'000, 0, 0), mode);
            checkAccessors(makeSTAmountPair(kKindMPT, kMaxMpTokenAmount, 0, 0), mode);
        }

        // checked ctor: in-range happy paths per kind
        checkChecked(makeSTAmountPair(kKindXRP, 1'000, 0, 0), m);
        checkChecked(makeSTAmountPair(kKindXRP, 0, 0, 0), m);
        checkChecked(makeSTAmountPair(kKindXRP, xrpMax, 0, 0), m);
        checkChecked(makeSTAmountPair(kKindIOU, 5'000'000'000'000'000ULL, 0, 0), m);
        checkChecked(makeSTAmountPair(kKindIOU, 0, 0, 0), m);
        checkChecked(makeSTAmountPair(kKindIOU, 1, 0, 0), m);
        checkChecked(makeSTAmountPair(kKindIOU, kMax, eMax, 0), m);
        checkChecked(makeSTAmountPair(kKindIOU, kMin, eMin, 0), m);
        checkChecked(makeSTAmountPair(kKindMPT, 1'000'000'000ULL, 0, 0), m);
        checkChecked(makeSTAmountPair(kKindMPT, kMaxMpTokenAmount, 0, 0), m);

        // Unchecked-from-int64 entry: STAmount can store raw fields without
        // canonicalization. Cover each kind with zero, ±1, and the kind's
        // semantic max. Out-of-range field extremes live in test_extreme_values.
        for (uint8_t kind : {kKindXRP, kKindIOU, kKindMPT})
        {
            checkUncheckedFromInt64(kind, 0, 0);
            checkUncheckedFromInt64(kind, 1, 0);
            checkUncheckedFromInt64(kind, -1, 0);
        }
        checkUncheckedFromInt64(kKindXRP, static_cast<int64_t>(xrpMax), 0);
        checkUncheckedFromInt64(kKindXRP, -static_cast<int64_t>(xrpMax), 0);
        checkUncheckedFromInt64(kKindMPT, static_cast<int64_t>(kMaxMpTokenAmount), 0);
        checkUncheckedFromInt64(kKindIOU, static_cast<int64_t>(kMin), eMin);
        checkUncheckedFromInt64(kKindIOU, static_cast<int64_t>(kMax), eMax);
        checkUncheckedFromInt64(kKindIOU, -static_cast<int64_t>(kMax), eMax);

        // ofNativeInt64: routes through unchecked, no throw — sanity at ±1, max.
        checkOfNativeInt64(0);
        checkOfNativeInt64(1);
        checkOfNativeInt64(-1);
        checkOfNativeInt64(static_cast<int64_t>(xrpMax));
        checkOfNativeInt64(-static_cast<int64_t>(xrpMax));

        checkOfInt64(kKindXRP, 1'000'000, 0, m);
        checkOfInt64(kKindXRP, -1'000'000, 0, m);
        checkOfInt64(kKindIOU, 1'234'567'890LL, 0, m);
        checkOfInt64(kKindIOU, -1'234'567'890LL, 0, m);
        checkOfInt64(kKindMPT, 999'999'999LL, 0, m);
        checkOfInt64(kKindMPT, -999'999'999LL, 0, m);

        // ofIOUAmount always uses noIssue() on the C++ side.
        for (auto [m_, e_] : std::initializer_list<std::pair<int64_t, int64_t>>{
                 {0, 0}, {1'234'567'890'123'456LL, -10}, {-1'234'567'890'123'456LL, -10}})
        {
            NumberRoundModeGuard mg(m);
            uint8_t const leanMode = toLeanMode(m);
            auto lean =
                LeanSTAmountResult::from_lean(lean_stamount_of_iou_amount(m_, e_, leanMode));
            STAmount cpp;
            bool cppThrew = false;
            try
            {
                cpp = STAmount{IOUAmount{m_, static_cast<int>(e_)}, noIssue()};
            }
            catch (std::exception const&)
            {
                cppThrew = true;
            }
            std::stringstream tag;
            tag << "ofIOUAmount(" << m_ << "e" << e_ << ")";
            checkStAmountResult(tag.str(), lean, cpp, cppThrew);
        }

        for (int64_t v : {int64_t{0}, int64_t{1}, int64_t{-1}, int64_t{1'000'000'000LL}})
        {
            NumberRoundModeGuard mg(m);
            uint8_t const leanMode = toLeanMode(m);
            auto lean = LeanSTAmountResult::from_lean(lean_stamount_of_xrp_amount(v, leanMode));
            STAmount cpp;
            bool cppThrew = false;
            try
            {
                cpp = STAmount{XRPAmount{v}};
            }
            catch (std::exception const&)
            {
                cppThrew = true;
            }
            std::stringstream tag;
            tag << "ofXRPAmount(" << v << ")";
            checkStAmountResult(tag.str(), lean, cpp, cppThrew);
        }

        for (int64_t v : {int64_t{0}, int64_t{1}, int64_t{-1}, int64_t{1'000'000'000'000LL}})
        {
            NumberRoundModeGuard mg(m);
            uint8_t const leanMode = toLeanMode(m);
            auto lean = LeanSTAmountResult::from_lean(lean_stamount_of_mpt_amount(v, leanMode));
            STAmount cpp;
            bool cppThrew = false;
            try
            {
                cpp = STAmount{MPTAmount{v}, ffiMPTIssue()};
            }
            catch (std::exception const&)
            {
                cppThrew = true;
            }
            std::stringstream tag;
            tag << "ofMPTAmount(" << v << ")";
            checkStAmountResult(tag.str(), lean, cpp, cppThrew);
        }

        for (auto kind : {kKindXRP, kKindIOU, kKindMPT})
        {
            for (auto [neg, mant, exp_] :
                 std::initializer_list<std::tuple<uint8_t, uint64_t, int64_t>>{
                     {0, 0, 0},
                     {0, 1'000'000'000'000'000'000ULL, 0},
                     {1, 1'000'000'000'000'000'000ULL, 0},
                     {0, 1'234'567'890'123'456'789ULL, -2}})
            {
                NumberRoundModeGuard mg(m);
                uint8_t const leanMode = toLeanMode(m);
                auto lean = LeanSTAmountResult::from_lean(
                    lean_stamount_of_number(kind, neg, mant, exp_, leanMode));
                STAmount cpp;
                bool cppThrew = false;
                try
                {
                    Asset const a = assetForKind(kind);
                    Number const n{neg != 0, mant, static_cast<int>(exp_), Number::Unchecked{}};
                    cpp = a.visit(
                        [&](Issue const& iss) { return STAmount{iss, n}; },
                        [&](MPTIssue const& mpt) { return STAmount{mpt, n}; });
                }
                catch (std::exception const&)
                {
                    cppThrew = true;
                }
                std::stringstream tag;
                tag << "ofNumber(kind=" << static_cast<int>(kind) << "," << (neg ? "-" : "+")
                    << mant << "e" << exp_ << ")";
                checkStAmountResult(tag.str(), lean, cpp, cppThrew);
            }
        }
    }

    void
    test_known_comparison()
    {
        beginCase("LeanSTAmount.known_comparison");
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        checkLt(makeSTAmountPair(kKindXRP, 1, 0, 0), makeSTAmountPair(kKindXRP, 2, 0, 0));
        checkLt(makeSTAmountPair(kKindXRP, 2, 0, 0), makeSTAmountPair(kKindXRP, 1, 0, 0));
        checkLt(makeSTAmountPair(kKindXRP, 1, 0, 1), makeSTAmountPair(kKindXRP, 1, 0, 0));
        checkLt(
            makeSTAmountPair(kKindIOU, 5'000'000'000'000'000ULL, 0, 0),
            makeSTAmountPair(kKindIOU, 5'000'000'000'000'000ULL, 1, 0));
        checkLt(
            makeSTAmountPair(kKindIOU, 5'000'000'000'000'000ULL, 0, 0),
            makeSTAmountPair(kKindIOU, 5'000'000'000'000'001ULL, 0, 0));
        checkLt(makeSTAmountPair(kKindMPT, 100, 0, 0), makeSTAmountPair(kKindMPT, 200, 0, 0));
        checkLt(makeSTAmountPair(kKindXRP, 100, 0, 0), makeSTAmountPair(kKindXRP, 100, 0, 0));
        checkLt(
            makeSTAmountPair(kKindIOU, 0, -100, 0),
            makeSTAmountPair(kKindIOU, STAmount::kMinValue, 0, 0));
        // Cross-asset is incomparable: C++ throws, Lean returns error.
        checkLt(
            makeSTAmountPair(kKindXRP, 100, 0, 0),
            makeSTAmountPair(kKindIOU, 5'000'000'000'000'000ULL, 0, 0));
        checkLt(
            makeSTAmountPair(kKindIOU, 5'000'000'000'000'000ULL, 0, 0),
            makeSTAmountPair(kKindMPT, 100, 0, 0));
        checkLt(makeSTAmountPair(kKindXRP, 100, 0, 0), makeSTAmountPair(kKindMPT, 100, 0, 0));
    }

    void
    test_known_arithmetic()
    {
        beginCase("LeanSTAmount.known_arithmetic");
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        Number::RoundingMode const m = Number::RoundingMode::ToNearest;

        checkAddSub(
            "add",
            true,
            makeSTAmountPair(kKindXRP, 1'000, 0, 0),
            makeSTAmountPair(kKindXRP, 2'000, 0, 0),
            m);
        checkAddSub(
            "sub",
            false,
            makeSTAmountPair(kKindXRP, 5'000, 0, 0),
            makeSTAmountPair(kKindXRP, 2'000, 0, 0),
            m);
        checkAddSub(
            "add",
            true,
            makeSTAmountPair(kKindXRP, 0, 0, 0),
            makeSTAmountPair(kKindXRP, 2'000, 0, 0),
            m);
        checkAddSub(
            "add",
            true,
            makeSTAmountPair(kKindXRP, 5'000, 0, 0),
            makeSTAmountPair(kKindXRP, 0, 0, 0),
            m);
        checkAddSub(
            "add",
            true,
            makeSTAmountPair(kKindIOU, 5'000'000'000'000'000ULL, 0, 0),
            makeSTAmountPair(kKindIOU, 3'000'000'000'000'000ULL, 0, 0),
            m);
        checkAddSub(
            "sub",
            false,
            makeSTAmountPair(kKindIOU, 5'000'000'000'000'000ULL, 0, 0),
            makeSTAmountPair(kKindIOU, 3'000'000'000'000'000ULL, 0, 0),
            m);
        checkAddSub(
            "add",
            true,
            makeSTAmountPair(kKindMPT, 1'000'000, 0, 0),
            makeSTAmountPair(kKindMPT, 2'000'000, 0, 0),
            m);
        checkAddSub(
            "sub",
            false,
            makeSTAmountPair(kKindMPT, 5'000'000, 0, 0),
            makeSTAmountPair(kKindMPT, 2'000'000, 0, 0),
            m);
        // Cross-asset add/sub must error on both sides.
        checkAddSub(
            "add",
            true,
            makeSTAmountPair(kKindXRP, 1'000, 0, 0),
            makeSTAmountPair(kKindIOU, 5'000'000'000'000'000ULL, 0, 0),
            m);
        checkAddSub(
            "sub",
            false,
            makeSTAmountPair(kKindIOU, 5'000'000'000'000'000ULL, 0, 0),
            makeSTAmountPair(kKindMPT, 1'000'000, 0, 0),
            m);
        checkAddSub(
            "add",
            true,
            makeSTAmountPair(kKindXRP, 1'000, 0, 0),
            makeSTAmountPair(kKindMPT, 1'000, 0, 0),
            m);

        // Mode sweep on a non-trivial IOU sum.
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            checkAddSub(
                "add",
                true,
                makeSTAmountPair(kKindIOU, 1'234'567'890'123'456ULL, 0, 0),
                makeSTAmountPair(kKindIOU, 9'876'543'210'987'654ULL, -3, 1),
                mode);
        }

        checkMultiply(
            makeSTAmountPair(kKindXRP, 1'000, 0, 0),
            makeSTAmountPair(kKindXRP, 1'000, 0, 0),
            kKindXRP,
            m);
        checkMultiply(
            makeSTAmountPair(kKindMPT, 1'000ULL, 0, 0),
            makeSTAmountPair(kKindMPT, 1'000ULL, 0, 0),
            kKindMPT,
            m);
        checkMultiply(
            makeSTAmountPair(kKindIOU, 2'000'000'000'000'000ULL, 0, 0),
            makeSTAmountPair(kKindIOU, 3'000'000'000'000'000ULL, 0, 0),
            kKindIOU,
            m);
        // IOU × IOU result rounded into XRP — exercises the ofNumber XRP path.
        checkMultiply(
            makeSTAmountPair(kKindIOU, 2'000'000'000'000'000ULL, 0, 0),
            makeSTAmountPair(kKindIOU, 3'000'000'000'000'000ULL, 0, 0),
            kKindXRP,
            m);
        checkMultiply(
            makeSTAmountPair(kKindXRP, 0, 0, 0),
            makeSTAmountPair(kKindXRP, 1'000, 0, 0),
            kKindXRP,
            m);
        checkMultiply(
            makeSTAmountPair(kKindIOU, 0, -100, 0),
            makeSTAmountPair(kKindIOU, 5'000'000'000'000'000ULL, 0, 0),
            kKindIOU,
            m);

        checkDivide(
            makeSTAmountPair(kKindIOU, 1'000'000'000'000'000ULL, 0, 0),
            makeSTAmountPair(kKindIOU, 3'000'000'000'000'000ULL, 0, 0),
            kKindIOU,
            m);
        // den = 0 errors on both sides; num = 0 short-circuits to zero.
        checkDivide(
            makeSTAmountPair(kKindIOU, 1'000'000'000'000'000ULL, 0, 0),
            makeSTAmountPair(kKindIOU, 0, -100, 0),
            kKindIOU,
            m);
        checkDivide(
            makeSTAmountPair(kKindIOU, 0, -100, 0),
            makeSTAmountPair(kKindIOU, 3'000'000'000'000'000ULL, 0, 0),
            kKindIOU,
            m);
        // 1 drop / 1 drop into noIssue() — the kURateOne recipe.
        checkDivide(
            makeSTAmountPair(kKindXRP, 1, 0, 0), makeSTAmountPair(kKindXRP, 1, 0, 0), kKindIOU, m);
        checkDivide(
            makeSTAmountPair(kKindMPT, 100, 0, 0),
            makeSTAmountPair(kKindMPT, 200, 0, 0),
            kKindIOU,
            m);

        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            checkDivide(
                makeSTAmountPair(kKindIOU, 1'000'000'000'000'000ULL, 0, 0),
                makeSTAmountPair(kKindIOU, 7'000'000'000'000'000ULL, 0, 0),
                kKindIOU,
                mode);
        }
    }

    void
    test_known_rounding()
    {
        beginCase("LeanSTAmount.known_rounding");
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        Number::RoundingMode const m = Number::RoundingMode::ToNearest;

        auto const mulRoundFn =
            static_cast<STAmount (*)(STAmount const&, STAmount const&, Asset const&, bool)>(
                &mulRound);
        auto const mulRoundStrictFn =
            static_cast<STAmount (*)(STAmount const&, STAmount const&, Asset const&, bool)>(
                &mulRoundStrict);

        // mulRound and mulRoundStrict, both roundUp values, across kinds.
        for (bool ru : {false, true})
        {
            checkMulRoundLike(
                "mulRound",
                lean_stamount_mul_round,
                mulRoundFn,
                makeSTAmountPair(kKindIOU, 1'000'000'000'000'000ULL, 0, 0),
                makeSTAmountPair(kKindIOU, 3'000'000'000'000'000ULL, 0, 0),
                kKindIOU,
                ru,
                m);
            checkMulRoundLike(
                "mulRoundStrict",
                lean_stamount_mul_round_strict,
                mulRoundStrictFn,
                makeSTAmountPair(kKindIOU, 1'000'000'000'000'000ULL, 0, 0),
                makeSTAmountPair(kKindIOU, 3'000'000'000'000'000ULL, 0, 0),
                kKindIOU,
                ru,
                m);
            // Tiny non-negative input: roundUp=true rescues to smallest-above-zero.
            checkMulRoundLike(
                "mulRound",
                lean_stamount_mul_round,
                mulRoundFn,
                makeSTAmountPair(kKindIOU, STAmount::kMinValue, STAmount::kMinOffset, 0),
                makeSTAmountPair(kKindIOU, STAmount::kMinValue, STAmount::kMinOffset, 0),
                kKindIOU,
                ru,
                m);
            // XRP × XRP → XRP integral fast path.
            checkMulRoundLike(
                "mulRound",
                lean_stamount_mul_round,
                mulRoundFn,
                makeSTAmountPair(kKindXRP, 1'000, 0, 0),
                makeSTAmountPair(kKindXRP, 1'000, 0, 0),
                kKindXRP,
                ru,
                m);
            checkMulRoundLike(
                "mulRoundStrict",
                lean_stamount_mul_round_strict,
                mulRoundStrictFn,
                makeSTAmountPair(kKindXRP, 1'000, 0, 0),
                makeSTAmountPair(kKindXRP, 1'000, 0, 0),
                kKindXRP,
                ru,
                m);
            // Zero short-circuit.
            checkMulRoundLike(
                "mulRound",
                lean_stamount_mul_round,
                mulRoundFn,
                makeSTAmountPair(kKindIOU, 0, -100, 0),
                makeSTAmountPair(kKindIOU, 5'000'000'000'000'000ULL, 0, 0),
                kKindIOU,
                ru,
                m);
        }

        // canonicalizeRound's `resultNegative != roundUp` branch.
        checkMulRoundLike(
            "mulRound",
            lean_stamount_mul_round,
            mulRoundFn,
            makeSTAmountPair(kKindIOU, 1'234'567'890'123'456ULL, 0, 0),
            makeSTAmountPair(kKindIOU, 9'876'543'210'987'654ULL, 0, 1),
            kKindIOU,
            true,
            m);

        auto const divRoundFn =
            static_cast<STAmount (*)(STAmount const&, STAmount const&, Asset const&, bool)>(
                &divRound);
        auto const divRoundStrictFn =
            static_cast<STAmount (*)(STAmount const&, STAmount const&, Asset const&, bool)>(
                &divRoundStrict);

        for (bool ru : {false, true})
        {
            checkDivRoundLike(
                "divRound",
                lean_stamount_div_round,
                divRoundFn,
                makeSTAmountPair(kKindIOU, 1'000'000'000'000'000ULL, 0, 0),
                makeSTAmountPair(kKindIOU, 3'000'000'000'000'000ULL, 0, 0),
                kKindIOU,
                ru,
                m);
            checkDivRoundLike(
                "divRoundStrict",
                lean_stamount_div_round_strict,
                divRoundStrictFn,
                makeSTAmountPair(kKindIOU, 1'000'000'000'000'000ULL, 0, 0),
                makeSTAmountPair(kKindIOU, 3'000'000'000'000'000ULL, 0, 0),
                kKindIOU,
                ru,
                m);
            // Tiny positive numerator with large denominator: roundUp=true rescues.
            checkDivRoundLike(
                "divRound",
                lean_stamount_div_round,
                divRoundFn,
                makeSTAmountPair(kKindIOU, STAmount::kMinValue, STAmount::kMinOffset, 0),
                makeSTAmountPair(kKindIOU, STAmount::kMaxValue, STAmount::kMaxOffset, 0),
                kKindIOU,
                ru,
                m);
            // den = 0 errors on both sides; num = 0 short-circuits.
            checkDivRoundLike(
                "divRound",
                lean_stamount_div_round,
                divRoundFn,
                makeSTAmountPair(kKindIOU, 1'000'000'000'000'000ULL, 0, 0),
                makeSTAmountPair(kKindIOU, 0, -100, 0),
                kKindIOU,
                ru,
                m);
            checkDivRoundLike(
                "divRound",
                lean_stamount_div_round,
                divRoundFn,
                makeSTAmountPair(kKindIOU, 0, -100, 0),
                makeSTAmountPair(kKindIOU, 3'000'000'000'000'000ULL, 0, 0),
                kKindIOU,
                ru,
                m);
        }
    }

    void
    test_known_predicates()
    {
        beginCase("LeanSTAmount.known_predicates");
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        Number::RoundingMode const m = Number::RoundingMode::ToNearest;

        // canAdd at the native max ± 1 (overflow on both signs), zero
        // short-circuit, and an in-range happy path.
        checkCanAdd(
            makeSTAmountPair(kKindXRP, STAmount::kMaxNativeN, 0, 0),
            makeSTAmountPair(kKindXRP, 1, 0, 0),
            m);
        checkCanAdd(
            makeSTAmountPair(kKindXRP, STAmount::kMaxNativeN, 0, 1),
            makeSTAmountPair(kKindXRP, 1, 0, 1),
            m);
        checkCanAdd(makeSTAmountPair(kKindXRP, 0, 0, 0), makeSTAmountPair(kKindXRP, 1, 0, 0), m);
        checkCanAdd(makeSTAmountPair(kKindXRP, 1, 0, 0), makeSTAmountPair(kKindXRP, 0, 0, 0), m);
        checkCanAdd(
            makeSTAmountPair(kKindXRP, 1'000, 0, 0), makeSTAmountPair(kKindXRP, 2'000, 0, 0), m);

        // MPT: INT64_MAX + 1 overflow + happy path.
        checkCanAdd(
            makeSTAmountPair(kKindMPT, kMaxMpTokenAmount, 0, 0),
            makeSTAmountPair(kKindMPT, 1, 0, 0),
            m);
        checkCanAdd(
            makeSTAmountPair(kKindMPT, 1'000, 0, 0), makeSTAmountPair(kKindMPT, 2'000, 0, 0), m);

        // IOU precision-loss path: equal magnitudes vs vastly different magnitudes.
        checkCanAdd(
            makeSTAmountPair(kKindIOU, 5'000'000'000'000'000ULL, 0, 0),
            makeSTAmountPair(kKindIOU, 5'000'000'000'000'000ULL, 0, 0),
            m);
        checkCanAdd(
            makeSTAmountPair(kKindIOU, STAmount::kMaxValue, STAmount::kMaxOffset, 0),
            makeSTAmountPair(kKindIOU, STAmount::kMinValue, STAmount::kMinOffset, 0),
            m);

        // Cross-asset canAdd → false on both sides.
        checkCanAdd(
            makeSTAmountPair(kKindXRP, 100, 0, 0),
            makeSTAmountPair(kKindIOU, STAmount::kMinValue, 0, 0),
            m);
        checkCanAdd(
            makeSTAmountPair(kKindIOU, STAmount::kMinValue, 0, 0),
            makeSTAmountPair(kKindMPT, 100, 0, 0),
            m);

        // canSubtract: XRP/MPT underflow, in-range, IOU always true, zero shortcut.
        checkCanSub(makeSTAmountPair(kKindXRP, 100, 0, 0), makeSTAmountPair(kKindXRP, 200, 0, 0));
        checkCanSub(makeSTAmountPair(kKindXRP, 1'000, 0, 0), makeSTAmountPair(kKindXRP, 100, 0, 0));
        checkCanSub(makeSTAmountPair(kKindMPT, 100, 0, 0), makeSTAmountPair(kKindMPT, 200, 0, 0));
        checkCanSub(
            makeSTAmountPair(kKindIOU, STAmount::kMinValue, 0, 0),
            makeSTAmountPair(kKindIOU, STAmount::kMaxValue, STAmount::kMaxOffset, 0));
        checkCanSub(makeSTAmountPair(kKindMPT, 0, 0, 0), makeSTAmountPair(kKindMPT, 1'000, 0, 0));
        checkCanSub(
            makeSTAmountPair(kKindXRP, 100, 0, 0),
            makeSTAmountPair(kKindIOU, STAmount::kMinValue, 0, 0));
    }

    void
    test_known_round_to_scale()
    {
        beginCase("LeanSTAmount.known_round_to_scale");
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);

        // XRP / MPT / IOU-zero: no-op short-circuits.
        checkRoundToScale(
            makeSTAmountPair(kKindXRP, 1'000, 0, 0), 0, Number::RoundingMode::ToNearest);
        checkRoundToScale(
            makeSTAmountPair(kKindMPT, 1'000, 0, 0), 0, Number::RoundingMode::ToNearest);
        checkRoundToScale(
            makeSTAmountPair(kKindIOU, 0, -100, 0), 0, Number::RoundingMode::ToNearest);
        // IOU exponent >= scale: no-op.
        checkRoundToScale(
            makeSTAmountPair(kKindIOU, 5'000'000'000'000'000ULL, 0, 0),
            -1,
            Number::RoundingMode::ToNearest);
        // Scale equal to exponent: no-op (boundary).
        checkRoundToScale(
            makeSTAmountPair(kKindIOU, 5'000'000'000'000'000ULL, -5, 0),
            -5,
            Number::RoundingMode::ToNearest);
        // IOU exponent < scale: rounds via the add-then-sub trick.
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            checkRoundToScale(
                makeSTAmountPair(kKindIOU, 1'234'567'890'123'456ULL, -10, 0), -5, mode);
            checkRoundToScale(
                makeSTAmountPair(kKindIOU, 1'234'567'890'123'456ULL, -10, 1), -5, mode);
        }
    }

    void
    test_known_get_rate()
    {
        beginCase("LeanSTAmount.known_get_rate");
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        Number::RoundingMode const m = Number::RoundingMode::ToNearest;

        // offerOut == 0 → 0.
        checkGetRate(makeSTAmountPair(kKindXRP, 0, 0, 0), makeSTAmountPair(kKindXRP, 100, 0, 0), m);
        // 1 drop / 1 drop — the kURateOne recipe.
        checkGetRate(makeSTAmountPair(kKindXRP, 1, 0, 0), makeSTAmountPair(kKindXRP, 1, 0, 0), m);
        checkGetRate(makeSTAmountPair(kKindXRP, 1, 0, 0), makeSTAmountPair(kKindXRP, 10, 0, 0), m);
        checkGetRate(makeSTAmountPair(kKindXRP, 10, 0, 0), makeSTAmountPair(kKindXRP, 1, 0, 0), m);
        checkGetRate(
            makeSTAmountPair(kKindIOU, STAmount::kMinValue, 0, 0),
            makeSTAmountPair(kKindIOU, STAmount::kMinValue, 1, 0),
            m);
        // Mixed-kind: both sides go through divide(in, out, noIssue).
        checkGetRate(
            makeSTAmountPair(kKindXRP, 1, 0, 0),
            makeSTAmountPair(kKindIOU, STAmount::kMinValue, 0, 0),
            m);
        checkGetRate(
            makeSTAmountPair(kKindIOU, STAmount::kMinValue, 0, 0),
            makeSTAmountPair(kKindXRP, 1, 0, 0),
            m);

        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            checkGetRate(
                makeSTAmountPair(kKindIOU, STAmount::kMinValue, 0, 0),
                makeSTAmountPair(kKindIOU, 7'000'000'000'000'000ULL, 0, 0),
                mode);
        }
    }

    void
    test_fuzz_accessors()
    {
        beginCase("LeanSTAmount.fuzz_accessors", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        auto& rng = nextRng();
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode save{Number::setround(mode)};
            runFuzz(5'000, [&] { return checkAccessors(randomSTAmountPair(rng), mode); });
        }
    }

    void
    test_fuzz_constructors()
    {
        beginCase("LeanSTAmount.fuzz_constructors", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        auto& rng = nextRng();
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode save{Number::setround(mode)};
            // checked: hits canonicalization + boundary errors per kind.
            runFuzz(10'000, [&] { return checkChecked(randomSTAmountPair(rng), mode); });
            // ofInt64: mantissa drawn as signed int64, kind picked uniformly.
            std::uniform_int_distribution<int64_t> mDist(
                std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max());
            std::uniform_int_distribution<int> eDist(-10, 10);
            runFuzz(10'000, [&] {
                uint8_t const kind = randomKind(rng);
                int64_t const mant = mDist(rng);
                int64_t const exp_ = eDist(rng);
                return checkOfInt64(kind, mant, exp_, mode);
            });
        }
    }

    void
    test_fuzz_add_sub()
    {
        beginCase("LeanSTAmount.fuzz_add_sub", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        auto& rng = nextRng();
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode save{Number::setround(mode)};
            std::bernoulli_distribution opDist(0.5);
            runFuzz(10'000, [&] {
                bool const isAdd = opDist(rng);
                auto a = randomSTAmountPair(rng);
                auto b = randomSTAmountPair(rng);
                return checkAddSub(isAdd ? "add" : "sub", isAdd, a, b, mode);
            });
        }
    }

    void
    test_fuzz_multiply_divide()
    {
        beginCase("LeanSTAmount.fuzz_multiply_divide", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        auto& rng = nextRng();
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode save{Number::setround(mode)};
            runFuzz(10'000, [&] {
                auto a = randomSTAmountPair(rng);
                auto b = randomSTAmountPair(rng);
                uint8_t const k = randomKind(rng);
                return checkMultiply(a, b, k, mode);
            });
            runFuzz(10'000, [&] {
                auto a = randomSTAmountPair(rng);
                auto b = randomSTAmountPair(rng);
                uint8_t const k = randomKind(rng);
                return checkDivide(a, b, k, mode);
            });
        }
    }

    void
    test_fuzz_mul_round()
    {
        beginCase("LeanSTAmount.fuzz_mul_round", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        auto& rng = nextRng();
        std::bernoulli_distribution ruDist(0.5);
        std::bernoulli_distribution strictDist(0.5);
        auto const mulRoundFn =
            static_cast<STAmount (*)(STAmount const&, STAmount const&, Asset const&, bool)>(
                &mulRound);
        auto const mulRoundStrictFn =
            static_cast<STAmount (*)(STAmount const&, STAmount const&, Asset const&, bool)>(
                &mulRoundStrict);
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode save{Number::setround(mode)};
            runFuzz(10'000, [&] {
                auto a = randomSTAmountPair(rng);
                auto b = randomSTAmountPair(rng);
                bool const strict = strictDist(rng);
                uint8_t const k = randomKind(rng);
                bool const ru = ruDist(rng);
                if (strict)
                    return checkMulRoundLike(
                        "mulRoundStrict",
                        lean_stamount_mul_round_strict,
                        mulRoundStrictFn,
                        a,
                        b,
                        k,
                        ru,
                        mode);
                return checkMulRoundLike(
                    "mulRound", lean_stamount_mul_round, mulRoundFn, a, b, k, ru, mode);
            });
        }
    }

    void
    test_fuzz_div_round()
    {
        beginCase("LeanSTAmount.fuzz_div_round", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        auto& rng = nextRng();
        std::bernoulli_distribution ruDist(0.5);
        std::bernoulli_distribution strictDist(0.5);
        auto const divRoundFn =
            static_cast<STAmount (*)(STAmount const&, STAmount const&, Asset const&, bool)>(
                &divRound);
        auto const divRoundStrictFn =
            static_cast<STAmount (*)(STAmount const&, STAmount const&, Asset const&, bool)>(
                &divRoundStrict);
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode save{Number::setround(mode)};
            runFuzz(10'000, [&] {
                auto num = randomSTAmountPair(rng);
                auto den = randomSTAmountPair(rng);
                bool const strict = strictDist(rng);
                uint8_t const k = randomKind(rng);
                bool const ru = ruDist(rng);
                if (strict)
                    return checkDivRoundLike(
                        "divRoundStrict",
                        lean_stamount_div_round_strict,
                        divRoundStrictFn,
                        num,
                        den,
                        k,
                        ru,
                        mode);
                return checkDivRoundLike(
                    "divRound", lean_stamount_div_round, divRoundFn, num, den, k, ru, mode);
            });
        }
    }

    void
    test_fuzz_predicates()
    {
        beginCase("LeanSTAmount.fuzz_predicates", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        auto& rng = nextRng();
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode save{Number::setround(mode)};
            runFuzz(10'000, [&] {
                auto a = randomSTAmountPair(rng);
                auto b = randomSTAmountPair(rng);
                return checkCanAdd(a, b, mode);
            });
        }
        runFuzz(30'000, [&] {
            auto a = randomSTAmountPair(rng);
            auto b = randomSTAmountPair(rng);
            return checkCanSub(a, b);
        });
    }

    void
    test_fuzz_round_to_scale()
    {
        beginCase("LeanSTAmount.fuzz_round_to_scale", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        auto& rng = nextRng();
        std::uniform_int_distribution<int32_t> scaleDist(-110, 10);
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode save{Number::setround(mode)};
            runFuzz(10'000, [&] {
                auto p = randomSTAmountPair(rng);
                int32_t const scale = scaleDist(rng);
                return checkRoundToScale(p, scale, mode);
            });
        }
    }

    void
    test_fuzz_get_rate()
    {
        beginCase("LeanSTAmount.fuzz_get_rate", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        auto& rng = nextRng();
        for (auto mode :
             {Number::RoundingMode::ToNearest,
              Number::RoundingMode::TowardsZero,
              Number::RoundingMode::Downward,
              Number::RoundingMode::Upward})
        {
            SaveNumberRoundMode save{Number::setround(mode)};
            runFuzz(10'000, [&] {
                auto a = randomSTAmountPair(rng);
                auto b = randomSTAmountPair(rng);
                return checkGetRate(a, b, mode);
            });
        }
    }

    void
    test_fuzz_compare()
    {
        beginCase("LeanSTAmount.fuzz_compare", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        auto& rng = nextRng();
        runFuzz(20'000, [&] {
            auto a = randomSTAmountPair(rng);
            auto b = randomSTAmountPair(rng);
            bool ok = true;
            ok &= checkEq(a, b);
            ok &= checkNe(a, b);
            ok &= checkLt(a, b);
            ok &= checkLe(a, b);
            ok &= checkGt(a, b);
            ok &= checkGe(a, b);
            ok &= checkAreComparable(a, b);
            return ok;
        });
    }

    void
    test_fuzz_is_legal_net()
    {
        beginCase("LeanSTAmount.fuzz_is_legal_net", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        auto& rng = nextRng();
        runFuzz(20'000, [&] { return checkIsLegalNet(randomSTAmountPair(rng)); });
    }

    void
    test_fuzz_neg()
    {
        beginCase("LeanSTAmount.fuzz_neg", true);
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        auto& rng = nextRng();
        runFuzz(20'000, [&] { return checkNeg(randomSTAmountPair(rng)); });
    }

    void
    test_fuzz_of_native_int64()
    {
        beginCase("LeanSTAmount.fuzz_of_native_int64", true);
        auto& rng = nextRng();
        std::uniform_int_distribution<int64_t> dist(
            std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max());
        runFuzz(20'000, [&] { return checkOfNativeInt64(dist(rng)); });
    }

    void
    test_fuzz_unchecked_from_int64()
    {
        beginCase("LeanSTAmount.fuzz_unchecked_from_int64", true);
        auto& rng = nextRng();
        std::uniform_int_distribution<int64_t> vDist(
            std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max());
        std::uniform_int_distribution<int64_t> offDist(-100, 100);
        runFuzz(20'000, [&] {
            uint8_t const k = randomKind(rng);
            int64_t const v = vDist(rng);
            int64_t const off = offDist(rng);
            return checkUncheckedFromInt64(k, v, off);
        });
    }

    void
    test_extreme_values()
    {
        beginCase("LeanSTAmount.extreme_values");
        NumberMantissaScaleGuard sg(MantissaRange::MantissaScale::Large);
        Number::RoundingMode const m = Number::RoundingMode::ToNearest;

        constexpr uint64_t xrpMax = STAmount::kMaxNativeN;
        constexpr uint64_t mptMax = kMaxMpTokenAmount;
        constexpr uint64_t iouMax = static_cast<uint64_t>(STAmount::kMaxValue);
        constexpr uint64_t iouMin = static_cast<uint64_t>(STAmount::kMinValue);
        constexpr int64_t eMax = STAmount::kMaxOffset;
        constexpr int64_t eMin = STAmount::kMinOffset;

        // checked ctor at the out-of-range corners — both sides must error.
        checkChecked(makeSTAmountPair(kKindXRP, xrpMax + 1, 0, 0), m);
        checkChecked(makeSTAmountPair(kKindIOU, iouMax, eMax + 1, 0), m);
        checkChecked(makeSTAmountPair(kKindIOU, iouMin, eMin - 1, 0), m);
        checkChecked(makeSTAmountPair(kKindMPT, 1, 19, 0), m);

        // XRP: max + max = 2·kMaxNativeN → over native cap → both error.
        checkAddSub(
            "add",
            true,
            makeSTAmountPair(kKindXRP, xrpMax, 0, 0),
            makeSTAmountPair(kKindXRP, xrpMax, 0, 0),
            m);
        checkAddSub(
            "sub",
            false,
            makeSTAmountPair(kKindXRP, xrpMax, 0, 1),
            makeSTAmountPair(kKindXRP, xrpMax, 0, 0),
            m);
        checkAddSub(
            "add",
            true,
            makeSTAmountPair(kKindXRP, xrpMax, 0, 0),
            makeSTAmountPair(kKindXRP, xrpMax, 0, 1),
            m);

        // MPT: INT64_MAX + 1 → overflow; both signs.
        checkAddSub(
            "add",
            true,
            makeSTAmountPair(kKindMPT, mptMax, 0, 0),
            makeSTAmountPair(kKindMPT, 1, 0, 0),
            m);
        checkAddSub(
            "sub",
            false,
            makeSTAmountPair(kKindMPT, mptMax, 0, 1),
            makeSTAmountPair(kKindMPT, 1, 0, 0),
            m);
        checkAddSub(
            "add",
            true,
            makeSTAmountPair(kKindMPT, mptMax, 0, 0),
            makeSTAmountPair(kKindMPT, mptMax, 0, 1),
            m);

        // IOU exponent overflow at kMaxOffset; smallest ± smallest at kMinOffset.
        checkAddSub(
            "add",
            true,
            makeSTAmountPair(kKindIOU, iouMax, eMax, 0),
            makeSTAmountPair(kKindIOU, iouMax, eMax, 0),
            m);
        checkAddSub(
            "sub",
            false,
            makeSTAmountPair(kKindIOU, iouMax, eMax, 0),
            makeSTAmountPair(kKindIOU, iouMax, eMax, 1),
            m);
        checkAddSub(
            "add",
            true,
            makeSTAmountPair(kKindIOU, iouMin, eMin, 0),
            makeSTAmountPair(kKindIOU, iouMin, eMin, 1),
            m);

        // multiply at the maxima.
        checkMultiply(
            makeSTAmountPair(kKindXRP, xrpMax, 0, 0),
            makeSTAmountPair(kKindXRP, xrpMax, 0, 0),
            kKindXRP,
            m);
        checkMultiply(
            makeSTAmountPair(kKindMPT, mptMax, 0, 0),
            makeSTAmountPair(kKindMPT, mptMax, 0, 0),
            kKindMPT,
            m);
        checkMultiply(
            makeSTAmountPair(kKindIOU, iouMax, eMax, 0),
            makeSTAmountPair(kKindIOU, iouMax, eMax, 0),
            kKindIOU,
            m);
        // XRP/MPT multiplication precheck overflows.
        checkMultiply(
            makeSTAmountPair(kKindXRP, 3'000'000'001ULL, 0, 0),
            makeSTAmountPair(kKindXRP, 3'000'000'001ULL, 0, 0),
            kKindXRP,
            m);
        checkMultiply(
            makeSTAmountPair(kKindXRP, 9'000'000'000'000'000ULL, 0, 0),
            makeSTAmountPair(kKindXRP, 1'000'000ULL, 0, 0),
            kKindXRP,
            m);
        checkMultiply(
            makeSTAmountPair(kKindMPT, 3'037'000'500ULL, 0, 0),
            makeSTAmountPair(kKindMPT, 3'037'000'500ULL, 0, 0),
            kKindMPT,
            m);

        // divide at the IOU extremes: huge / tiny quotients.
        checkDivide(
            makeSTAmountPair(kKindIOU, iouMax, eMax, 0),
            makeSTAmountPair(kKindIOU, iouMin, eMin, 0),
            kKindIOU,
            m);
        checkDivide(
            makeSTAmountPair(kKindIOU, iouMin, eMin, 0),
            makeSTAmountPair(kKindIOU, iouMax, eMax, 0),
            kKindIOU,
            m);

        // neg is sign-magnitude → never overflows.
        checkNeg(makeSTAmountPair(kKindXRP, xrpMax, 0, 0));
        checkNeg(makeSTAmountPair(kKindMPT, mptMax, 0, 1));
        checkNeg(makeSTAmountPair(kKindIOU, iouMax, eMax, 0));

        // Cross-kind compare → error; same-kind ordering -max < +max.
        checkLt(
            makeSTAmountPair(kKindXRP, xrpMax, 0, 0), makeSTAmountPair(kKindIOU, iouMax, eMax, 0));
        checkLt(
            makeSTAmountPair(kKindIOU, iouMax, eMax, 1),
            makeSTAmountPair(kKindIOU, iouMax, eMax, 0));

        // Raw field extremes via the Unchecked path: both sides decode these
        // verbatim, so we see how each operation copes at the field limits.
        constexpr uint64_t u64Max = std::numeric_limits<uint64_t>::max();
        constexpr int64_t intMax = std::numeric_limits<int>::max();
        constexpr int64_t intMin = std::numeric_limits<int>::min();
        for (uint8_t neg : {uint8_t{0}, uint8_t{1}})
        {
            for (uint8_t kind : {kKindXRP, kKindIOU, kKindMPT})
            {
                checkNeg(makeSTAmountPair(kind, u64Max, 0, neg));
                checkNeg(makeSTAmountPair(kind, 0, 0, neg));
                checkAddSub(
                    "add",
                    true,
                    makeSTAmountPair(kind, u64Max, 0, neg),
                    makeSTAmountPair(kind, u64Max, 0, neg),
                    m);
                checkLt(makeSTAmountPair(kind, u64Max, 0, neg), makeSTAmountPair(kind, 0, 0, neg));
            }
            // IOU additionally spans the int-exponent extremes (Number-mediated
            // out-of-range offsets surface as errors on both sides).
            checkNeg(makeSTAmountPair(kKindIOU, u64Max, intMax, neg));
            checkNeg(makeSTAmountPair(kKindIOU, u64Max, intMin, neg));
            checkAddSub(
                "add",
                true,
                makeSTAmountPair(kKindIOU, u64Max, intMax, neg),
                makeSTAmountPair(kKindIOU, 1, intMin, neg),
                m);
            checkLt(
                makeSTAmountPair(kKindIOU, u64Max, intMax, neg),
                makeSTAmountPair(kKindIOU, u64Max, intMin, neg));
        }
    }

private:
    void
    runTests() override
    {
        test_fuzz_accessors();
        test_fuzz_constructors();
        // test_fuzz_add_sub();
        test_fuzz_multiply_divide();
        test_fuzz_mul_round();
        test_fuzz_div_round();
        // test_fuzz_predicates();
        // test_fuzz_round_to_scale();
        test_fuzz_get_rate();
        test_fuzz_compare();
        test_fuzz_is_legal_net();
        test_fuzz_neg();
        test_fuzz_of_native_int64();
        test_fuzz_unchecked_from_int64();
        test_known_construction();
        test_known_comparison();
        test_known_arithmetic();
        test_known_rounding();
        test_known_predicates();
        test_known_round_to_scale();
        test_known_get_rate();
        test_extreme_values();
    }
};

BEAST_DEFINE_TESTSUITE(LeanSTAmount, formal_verification, xrpl);

}  // namespace xrpl::test
