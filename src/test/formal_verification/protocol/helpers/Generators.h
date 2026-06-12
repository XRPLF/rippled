#pragma once

#include <test/formal_verification/common/LeanSuite.h>
#include <test/formal_verification/protocol/helpers/Types.h>

#include <xrpl/basics/Number.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTAmount.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>

namespace xrpl {
namespace test {
namespace lean4 {

struct Pair
{
    Number cppNum;
    LeanNumber leanNum;
};

struct MPTAmountPair
{
    MPTAmount cppMpt;
    int64_t leanMpt;
};

struct XRPAmountPair
{
    XRPAmount cppXrp;
    int64_t leanDrops;
};

struct IOUAmountPair
{
    IOUAmount cppIou;
    int64_t leanMant;
    int64_t leanExp;
};

struct STAmountPair
{
    STAmount cppSt;
    LeanSTAmount leanSt;
};

inline int64_t
randomInt64(std::mt19937_64& rng)
{
    return std::uniform_int_distribution<int64_t>{
        std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max()}(rng);
}

inline uint8_t
randomKind(std::mt19937_64& rng)
{
    return static_cast<uint8_t>(std::uniform_int_distribution<int>{0, 2}(rng));
}

// Lean FFI assetKind tag: 0=XRP, 1=IOU (noIssue), 2=MPT (ffiMPTIssue).
constexpr uint8_t kKindXRP = 0;
constexpr uint8_t kKindIOU = 1;
constexpr uint8_t kKindMPT = 2;

// All-zero MPTID sentinel; matches Lean's ffiMPTIssue.
inline MPTIssue const&
ffiMPTIssue()
{
    static MPTIssue const k{MPTID{}};
    return k;
}

inline Asset
assetForKind(uint8_t kind)
{
    switch (kind)
    {
        case kKindXRP:
            return xrpIssue();
        case kKindIOU:
            return noIssue();
        case kKindMPT:
            return ffiMPTIssue();
        default:
            throw std::logic_error("assetForKind: unknown kind " + std::to_string(kind));
    }
}

inline Pair
makePair(bool negative, uint64_t mantissa, int exponent)
{
    return {
        Number{negative, mantissa, exponent, Number::Unchecked{}},
        LeanNumber{negative, mantissa, static_cast<uint64_t>(exponent)}};
}

inline Pair
randomPair(uint64_t mantMin, uint64_t mantMax, int expMin, int expMax)
{
    auto& rng = nextRng();
    std::uniform_int_distribution<uint64_t> mantDist(mantMin, mantMax);
    std::uniform_int_distribution<int> expDist(expMin, expMax);
    std::bernoulli_distribution signDist(0.5);
    return makePair(signDist(rng), mantDist(rng), expDist(rng));
}

// Normalized mantissa range, caller-chosen exponent range.
inline Pair
randomPair(int expMin, int expMax)
{
    return randomPair(Number::minMantissa(), Number::kMaxRep, expMin, expMax);
}

inline MPTAmountPair
makeMPTAmountPair(int64_t value)
{
    return {MPTAmount{value}, value};
}

inline MPTAmountPair
randomMPTAmountPair(int64_t min, int64_t max)
{
    std::uniform_int_distribution<int64_t> dist(min, max);
    return makeMPTAmountPair(dist(nextRng()));
}

inline MPTAmountPair
randomMPTAmountPair()
{
    return makeMPTAmountPair(randomInt64(nextRng()));
}

inline XRPAmountPair
makeXRPAmountPair(int64_t drops)
{
    return {XRPAmount{drops}, drops};
}

inline XRPAmountPair
randomXRPAmountPair(std::mt19937_64& rng)
{
    return makeXRPAmountPair(randomInt64(rng));
}

inline XRPAmountPair
randomXRPAmountPair()
{
    return randomXRPAmountPair(nextRng());
}

// Throws on out-of-canonical-range (m, e); use raw fields for edge fuzz.
inline IOUAmountPair
makeIOUAmountPair(int64_t mantissa, int64_t exponent)
{
    return {IOUAmount{mantissa, static_cast<int>(exponent)}, mantissa, exponent};
}

// Canonical-range IOUAmountPair — never throws at construction.
inline IOUAmountPair
randomIOUAmountPair(std::mt19937_64& rng)
{
    std::uniform_int_distribution<uint64_t> mantDist(
        STAmount::kMinValue, STAmount::kMaxValue);
    std::uniform_int_distribution<int> expDist(
        STAmount::kMinOffset, STAmount::kMaxOffset);
    std::bernoulli_distribution signDist(0.5);
    uint64_t const mag = mantDist(rng);
    int64_t const mantissa =
        signDist(rng) ? -static_cast<int64_t>(mag) : static_cast<int64_t>(mag);
    return makeIOUAmountPair(mantissa, static_cast<int64_t>(expDist(rng)));
}

// STAmount::Unchecked ctor - mirrors Lean's decodeSTAmount (no canonicalize).
inline STAmount
stAmountUnchecked(uint8_t kind, uint64_t mValue, int64_t mOffset, uint8_t mIsNegative)
{
    Asset const asset = assetForKind(kind);
    return asset.visit(
        [&](Issue const& iss) {
            return STAmount{
                iss,
                static_cast<STAmount::mantissa_type>(mValue),
                static_cast<STAmount::exponent_type>(mOffset),
                mIsNegative != 0,
                STAmount::Unchecked{}};
        },
        [&](MPTIssue const& mpt) {
            return STAmount{
                mpt,
                static_cast<STAmount::mantissa_type>(mValue),
                static_cast<STAmount::exponent_type>(mOffset),
                mIsNegative != 0,
                STAmount::Unchecked{}};
        });
}

inline STAmountPair
makeSTAmountPair(uint8_t kind, uint64_t mValue, int64_t mOffset, uint8_t isNegative)
{
    return {
        stAmountUnchecked(kind, mValue, mOffset, isNegative),
        LeanSTAmount{kind, mValue, mOffset, isNegative}};
}

// When uint64_t to int64 and then negate, we need to prevent -INT64_MIN (-2^63)
inline bool
canSign(uint64_t mv) noexcept
{
    return mv != 0 && mv != (uint64_t{1} << 63);
}

// Broad per-kind STAmountPair: XRP/MPT span the full uint64 mantissa range
// (offset 0); IOU is 10% canonical zero plus full-uint64 mantissa over
// [kMinOffset-100, kMaxOffset+100].
inline STAmountPair
randomSTAmountPair(std::mt19937_64& rng, uint8_t kind)
{
    std::bernoulli_distribution sign(0.5);
    std::uniform_int_distribution<uint64_t> mant(
        0, std::numeric_limits<uint64_t>::max());
    switch (kind)
    {
        case kKindIOU:
        {
            if (std::uniform_int_distribution<int>{0, 9}(rng) == 0)
                return makeSTAmountPair(kKindIOU, 0, -100, 0);
            std::uniform_int_distribution<int> exp(
                STAmount::kMinOffset - 100, STAmount::kMaxOffset + 100);
            uint64_t const mv = mant(rng);
            return makeSTAmountPair(
                kKindIOU,
                mv,
                static_cast<int64_t>(exp(rng)),
                static_cast<uint8_t>(canSign(mv) && sign(rng)));
        }
        case kKindXRP:
        case kKindMPT:
        default:
        {
            uint64_t const mv = mant(rng);
            return makeSTAmountPair(
                kind, mv, 0, static_cast<uint8_t>(canSign(mv) && sign(rng)));
        }
    }
}

inline STAmountPair
randomSTAmountPair(std::mt19937_64& rng)
{
    return randomSTAmountPair(rng, randomKind(rng));
}

}  // namespace lean4
}  // namespace test
}  // namespace xrpl
