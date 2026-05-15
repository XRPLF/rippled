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
    static std::string const kName = "xrpld";
    return kName;
}

/** Configure the native currency. */

/** Number of drops in the genesis account. */
constexpr XRPAmount kInitialXrp{100'000'000'000 * kDropsPerXrp};
static_assert(kInitialXrp.drops() == 100'000'000'000'000'000);
static_assert(Number::kMaxRep >= kInitialXrp.drops());

/** Returns true if the amount does not exceed the initial XRP in existence. */
inline bool
isLegalAmount(XRPAmount const& amount)
{
    return amount <= kInitialXrp;
}

/** Returns true if the absolute value of the amount does not exceed the initial
 * XRP in existence. */
inline bool
isLegalAmountSigned(XRPAmount const& amount)
{
    return amount >= -kInitialXrp && amount <= kInitialXrp;
}

/* The currency code for the native currency. */
static inline std::string const&
systemCurrencyCode()
{
    static std::string const kCode = "XRP";
    return kCode;
}

/** The XRP ledger network's earliest allowed sequence */
static constexpr std::uint32_t kXrpLedgerEarliestSeq{32570u};

/** The XRP Ledger mainnet's earliest ledger with a FeeSettings object. Only
 * used in asserts and tests. */
static constexpr std::uint32_t kXrpLedgerEarliestFees{562177u};

/** The minimum amount of support an amendment should have. */
constexpr std::ratio<80, 100> kAmendmentMajorityCalcThreshold;

/** The minimum amount of time an amendment must hold a majority */
constexpr std::chrono::seconds const kDefaultAmendmentMajorityTime = weeks{2};

}  // namespace xrpl

/** Default peer port (IANA registered) */
inline constexpr std::uint16_t kDefaultPeerPort{2459};
