#pragma once

#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>

namespace xrpl {

// Deprecated constant for backwards compatibility with pre-XRPFees amendment.
// This was the reference fee units used in the old fee calculation.
inline constexpr std::uint32_t kFeeUnitsDeprecated = 10;

// Number of micro-drops in one drop.
constexpr std::uint32_t microDropsPerDrop{1'000'000};

/**
 * Hard protocol ceilings and a floor on the Feature Extension fee settings.
 * A voted value can never exceed these, so `preflight`, which has no view,
 * may bound against them.
 */
inline constexpr std::uint32_t kMaxGasLimit{2'000'000};
inline constexpr std::uint32_t kMaxBytecodeSizeLimit{200'000};
inline constexpr std::uint32_t kMinGasPrice{1'000};

// The following default values of fee settings will seed into FeeSettings
// and write to the ledger on featureSmartEscrow activation.
inline constexpr std::uint32_t kDefaultGasLimit{400'000};
inline constexpr std::uint32_t kDefaultBytecodeSizeLimit{50'000};
inline constexpr std::uint32_t kDefaultGasPrice{1'000'000};

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

    /**
     * @brief Gas limit for Feature Extensions (instructions).
     *
     * This and the one below default to the protocol values rather than 0,
     * because an explicit 0 is the voted kill switch.
     */
    std::uint32_t gasLimit{kDefaultGasLimit};

    /**
     * @brief Bytecode size limit for Feature Extensions (bytes).
     */
    std::uint32_t bytecodeSizeLimit{kDefaultBytecodeSizeLimit};

    /**
     * @brief Price of WASM gas (micro-drops).
     */
    std::uint32_t gasPrice{kDefaultGasPrice};

    explicit Fees() = default;
    Fees(Fees const&) = default;
    Fees&
    operator=(Fees const&) = default;

    Fees(
        XRPAmount base,
        XRPAmount reserve,
        XRPAmount increment,
        std::uint32_t gasLimit,
        std::uint32_t bytecodeSizeLimit,
        std::uint32_t gasPrice)
        : base(base)
        , reserve(reserve)
        , increment(increment)
        , gasLimit(gasLimit)
        , bytecodeSizeLimit(bytecodeSizeLimit)
        , gasPrice(gasPrice)
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
