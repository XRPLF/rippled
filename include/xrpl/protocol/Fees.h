#pragma once

#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>

namespace xrpl {

// Deprecated constant for backwards compatibility with pre-XRPFees amendment.
// This was the reference fee units used in the old fee calculation.
inline constexpr std::uint32_t kFeeUnitsDeprecated = 10;

// Number of micro-drops in one drop.
constexpr std::uint32_t microDropsPerDrop{1'000'000};

/** Maximum Feature Extension fee settings. */
inline constexpr std::uint32_t kMaxGasLimit{2'000'000};
inline constexpr std::uint32_t kMaxBytecodeSizeLimit{200'000};

/**
 * Reflects the fee settings for a particular ledger.
 *
 * The fees are always the same for any transactions applied
 * to a ledger. Changes to fees occur in between ledgers.
 */
struct Fees
{
    /**
     * @brief Cost of a reference transaction in drops.
     */
    XRPAmount base{0};

    /**
     * @brief Minimum XRP an account must hold to exist on the ledger.
     */
    XRPAmount reserve{0};

    /**
     * @brief Additional XRP reserve required per owned ledger object.
     */
    XRPAmount increment{0};

    /** @brief Gas limit for Feature Extensions (instructions). */
    std::uint32_t gasLimit{0};

    /** @brief Bytecode size limit for Feature Extensions (bytes). */
    std::uint32_t bytecodeSizeLimit{0};

    /** @brief Price of WASM gas (micro-drops). */
    std::uint32_t gasPrice{0};

    explicit Fees() = default;
    Fees(Fees const&) = default;
    Fees&
    operator=(Fees const&) = default;

    Fees(XRPAmount base, XRPAmount reserve, XRPAmount increment)
        : base(base), reserve(reserve), increment(increment)
    {
    }

    /**
     * Returns the account reserve given the owner count, in drops.
     *
     * The reserve is calculated as the reserve base times the number of accounts plus the reserve
     * increment times the number of increments.
     */
    [[nodiscard]] XRPAmount
    accountReserve(std::uint32_t ownerCount, std::uint32_t accountCount) const
    {
        return (reserve * accountCount) + (increment * ownerCount);
    }
};

}  // namespace xrpl
