#ifndef XRPL_PROTOCOL_SYSTEMPARAMETERS_H_INCLUDED
#define XRPL_PROTOCOL_SYSTEMPARAMETERS_H_INCLUDED

#include <xrpl/basics/chrono.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <string>

namespace ripple {

// Various protocol and system specific constant globals.

/* The name of the system. */
static inline std::string const&
systemName()
{
    static std::string const name = "ripple";
    return name;
}

/** Configure the native currency. */

/** Number of drops in the genesis account. */
constexpr XRPAmount INITIAL_XRP{100'000'000'000 * DROPS_PER_XRP};

/** Returns true if the amount does not exceed the initial XRP in existence. */
inline bool
isLegalAmount(XRPAmount const& amount)
{
    return amount <= INITIAL_XRP;
}

/** Returns true if the absolute value of the amount does not exceed the initial
 * XRP in existence. */
inline bool
isLegalAmountSigned(XRPAmount const& amount)
{
    return amount >= -INITIAL_XRP && amount <= INITIAL_XRP;
}

/* The currency code for the native currency. */
static inline std::string const&
systemCurrencyCode()
{
    static std::string const code = "XRP";
    return code;
}

/** The XRP ledger network's earliest allowed sequence */
static constexpr std::uint32_t XRP_LEDGER_EARLIEST_SEQ{32570u};

/** The XRP Ledger mainnet's earliest ledger with a FeeSettings object. Only
 * used in asserts and tests. */
static constexpr std::uint32_t XRP_LEDGER_EARLIEST_FEES{562177u};

/** The minimum amount of support an amendment should have. */
constexpr std::ratio<80, 100> amendmentMajorityCalcThreshold;

/** The minimum amount of time an amendment must hold a majority */
constexpr std::chrono::seconds const defaultAmendmentMajorityTime = weeks{2};

}  // namespace ripple

/** Default peer port (IANA registered) */
inline std::uint16_t constexpr DEFAULT_PEER_PORT{2459};

#endif
