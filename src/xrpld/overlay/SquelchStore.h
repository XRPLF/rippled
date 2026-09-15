#pragma once

#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/beast/clock/abstract_clock.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/PublicKey.h>

#include <chrono>

namespace xrpl::reduce_relay {

/**
 * @brief Manages the temporary suppression ("squelching") of validators.
 *
 * @details This class provides a mechanism to temporarily ignore messages from
 * specific validators for a defined duration. It tracks which
 * validators are currently squelched and handles the
 * expiration of the squelch period. The use of an
 * abstract clock allows for deterministic testing of time-based
 * squelch logic.
 */
class SquelchStore
{
    using clock_type = beast::AbstractClock<std::chrono::steady_clock>;
    using time_point = clock_type::time_point;

public:
    explicit SquelchStore(beast::Journal journal, clock_type& clock)
        : journal_(journal), clock_(clock)
    {
    }
    virtual ~SquelchStore() = default;

    /**
     * @brief Manages the squelch status of a validator.
     *
     * @details This function acts as the primary public interface for
     * controlling a validator's squelch state. Based on the `squelch` flag, it
     * either adds a new squelch entry for the specified duration or removes an
     * existing one. This function also clears all expired squelches.
     *
     * @param validator The public key of the validator to manage.
     * @param squelch If `true`, the validator will be squelched. If `false`,
     * any existing squelch will be removed.
     * @param duration The duration in seconds for the squelch. This value is
     * only used when `squelch` is `true`.
     */
    void
    handleSquelch(PublicKey const& validator, bool squelch, std::chrono::seconds duration);

    /**
     * @brief Checks if a validator is currently squelched.
     *
     * @details This function checks if the validator's squelch has expired.
     *
     * @param validator The public key of the validator to check.
     * @return `true` if a non-expired squelch entry exists for the
     * validator, `false` otherwise.
     */
    bool
    isSquelched(PublicKey const& validator) const;

    // The following field is protected for unit tests.
protected:
    /**
     * @brief The core data structure mapping a validator's public key to the
     * time point when their squelch expires.
     */
    hash_map<PublicKey, time_point> squelched_;

private:
    /**
     * @brief Internal implementation to add or update a squelch entry.
     *
     * @details Calculates the expiration time point by adding the duration to
     * the current time and inserts or overwrites the entry for the validator in
     * the `squelched_` map.
     *
     * @param validator The public key of the validator to squelch.
     * @param squelchDuration The duration for which the validator should be
     * squelched.
     */
    void
    add(PublicKey const& validator, std::chrono::seconds const& squelchDuration);

    /**
     * @brief Internal implementation to remove a squelch entry.
     *
     * @details Erases the squelch entry for the given validator from the
     * `squelched_` map, effectively unsquelching it.
     *
     * @param validator The public key of the validator to unsquelch.
     */
    void
    remove(PublicKey const& validator);

    /**
     * @brief Internal implementation to remove all expired squelches.
     *
     * @details Erases all squelch entries whose expiration is in the past.
     */
    void
    removeExpired();

    /**
     * @brief The logging interface used by this store.
     */
    beast::Journal const journal_;

    /**
     * @brief A reference to the clock used for all time-based operations,
     * allowing for deterministic testing via dependency injection.
     */
    clock_type& clock_;
};

}  // namespace xrpl::reduce_relay
