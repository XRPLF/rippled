#pragma once

#include <xrpl/basics/chrono.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <string>

namespace xrpl {

// Various protocol and system specific constant globals.

/* The name of the system. */
static inline std::string const&
systemName()
{
    static std::string const kNAME = "xrpld";
    return kNAME;
}

/** Configure the native currency. */

/** Number of drops in the genesis account. */
constexpr XRPAmount kINITIAL_XRP{100'000'000'000 * kDROPS_PER_XRP};
static_assert(kINITIAL_XRP.drops() == 100'000'000'000'000'000);
static_assert(Number::kMAX_REP >= kINITIAL_XRP.drops());

/** Returns true if the amount does not exceed the initial XRP in existence. */
inline bool
isLegalAmount(XRPAmount const& amount)
{
    return amount <= kINITIAL_XRP;
}

/** Returns true if the absolute value of the amount does not exceed the initial
 * XRP in existence. */
inline bool
isLegalAmountSigned(XRPAmount const& amount)
{
    return amount >= -kINITIAL_XRP && amount <= kINITIAL_XRP;
}

/* The currency code for the native currency. */
static inline std::string const&
systemCurrencyCode()
{
    static std::string const kCODE = "XRP";
    return kCODE;
}

/** The XRP ledger network's earliest allowed sequence */
static constexpr std::uint32_t kXRP_LEDGER_EARLIEST_SEQ{32570u};

/** The XRP Ledger mainnet's earliest ledger with a FeeSettings object. Only
 * used in asserts and tests. */
static constexpr std::uint32_t kXRP_LEDGER_EARLIEST_FEES{562177u};

/** The minimum amount of support an amendment should have. */
constexpr std::ratio<80, 100> kAMENDMENT_MAJORITY_CALC_THRESHOLD;

/** The minimum amount of time an amendment must hold a majority */
constexpr std::chrono::seconds const kDEFAULT_AMENDMENT_MAJORITY_TIME = weeks{2};

}  // namespace xrpl

/** Default peer port (IANA registered) */
inline std::uint16_t constexpr kDEFAULT_PEER_PORT{2459};
