#pragma once

#include <array>
#include <cstdint>

namespace xrpl {

// Tick boundaries matching Uniswap V3
constexpr std::int32_t CLAMM_MIN_TICK = -887272;
constexpr std::int32_t CLAMM_MAX_TICK = 887272;

// Maximum fee tier index
constexpr std::uint8_t CLAMM_MAX_FEE_TIER = 3;

// Fee tier configuration
struct CLAMMFeeTierConfig
{
    std::uint16_t tradingFee;  // in 1/1,000,000 (e.g. 100 = 0.01%)
    std::uint16_t tickSpacing;
};

// The four fee tiers supported by CLAMM
constexpr std::array<CLAMMFeeTierConfig, 4> clammFeeTiers = {{
    {100, 1},    // STABLE: 0.01%, spacing 1
    {500, 10},   // LOW:    0.05%, spacing 10
    {3000, 60},  // MEDIUM: 0.30%, spacing 60
    {10000, 200} // HIGH:   1.00%, spacing 200
}};

// NFToken taxon used for CLAMM position NFTs: "CLAM" in ASCII
constexpr std::uint32_t CLAMM_NFTOKEN_TAXON = 0x434C414D;

// Voting constants (reuse AMM patterns)
constexpr std::uint16_t CLAMM_VOTE_MAX_SLOTS = 8;
constexpr std::uint32_t CLAMM_VOTE_WEIGHT_SCALE_FACTOR = 100000;

// Minimum liquidity for a new deposit
constexpr std::uint64_t CLAMM_MIN_LIQUIDITY = 1000;

// Auction slot constants (reuse AMM patterns)
constexpr std::uint32_t CLAMM_TOTAL_TIME_SLOT_SECS = 24 * 3600;
constexpr std::uint16_t CLAMM_AUCTION_SLOT_TIME_INTERVALS = 20;
constexpr std::uint16_t CLAMM_AUCTION_SLOT_MAX_AUTH_ACCOUNTS = 4;

class Rules;  // forward declaration for clammEnabled

/** Return true if the CLAMM amendment is enabled */
bool
clammEnabled(Rules const&);

/** Validate fee tier index is in valid range */
bool
isValidCLAMMFeeTier(std::uint8_t feeTier);

/** Get tick spacing for a given fee tier */
std::uint16_t
clammTickSpacing(std::uint8_t feeTier);

/** Get trading fee for a given fee tier (in 1/1,000,000) */
std::uint16_t
clammTradingFee(std::uint8_t feeTier);

/** Check if a tick index is valid and aligned to the given tick spacing */
bool
isValidCLAMMTick(std::int32_t tick, std::uint16_t tickSpacing);

}  // namespace xrpl
