#pragma once

#include <xrpl/beast/utility/PropertyStream.h>
#include <xrpl/peerfinder/detail/Tuning.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace xrpl::PeerFinder {

struct PeerLimitConfig
{
    std::optional<std::size_t> maxPeers;
    std::optional<std::size_t> inPeers;
    std::optional<std::size_t> outPeers;
};

/**
 * PeerFinder configuration settings.
 */
struct Config
{
    /**
     * The largest number of public peer slots to allow.
     * This includes both inbound and outbound, but does not include
     * fixed peers.
     */
    std::size_t maxPeers{Tuning::kDefaultMaxPeers};

    /**
     * The number of automatic outbound connections to maintain.
     * Outbound connections are only maintained if autoConnect
     * is `true`.
     */
    std::size_t outPeers = calcOutPeers();  // Note: relies on `maxPeers` being initialized

    /**
     * The number of automatic inbound connections to maintain.
     * Inbound connections are only maintained if wantIncoming
     * is `true`.
     */
    std::size_t inPeers{0};

    /**
     * `true` if we want our IP address kept private.
     */
    bool peerPrivate = true;

    /**
     * `true` if we want to accept incoming connections.
     */
    bool wantIncoming{true};

    /**
     * `true` if we want to establish connections automatically
     */
    bool autoConnect{true};

    /**
     * The listening port number.
     */
    std::uint16_t listeningPort{0};

    /**
     * The set of features we advertise.
     */
    std::string features;

    /**
     * Limit how many incoming connections we allow per IP
     */
    int ipLimit{0};

    /**
     * `true` if we want to verify endpoints in TMEndpoints messages
     */
    bool verifyEndpoints = true;

    //--------------------------------------------------------------------------

    /**
     * Returns a suitable value for outPeers according to the rules.
     */
    [[nodiscard]] std::size_t
    calcOutPeers() const;

    /**
     * Adjusts the values so they follow the business rules.
     */
    void
    applyTuning();

    /**
     * Write the configuration into a property stream
     */
    void
    onWrite(beast::PropertyStream::Map& map) const;

    /**
     * Make PeerFinder::Config from peer limit and server mode parameters.
     */
    static Config
    makeConfig(
        bool peerPrivate,
        bool standalone,
        PeerLimitConfig const& limits,
        std::uint16_t port,
        bool validationPublicKey,
        int ipLimit,
        bool verifyEndpoints);

    /**
     * Compares two configurations for equality field by field.
     */
    friend bool
    operator==(Config const& lhs, Config const& rhs) = default;
};

//------------------------------------------------------------------------------

/**
 * Possible results from activating a slot.
 */
enum class Result { InboundDisabled, DuplicatePeer, IpLimitExceeded, Full, Success };

/**
 * @brief Converts a `Result` enum value to its string representation.
 *
 * This function provides a human-readable string for a given `Result` enum,
 * which is useful for logging, debugging, or displaying status messages.
 *
 * @param result The `Result` enum value to convert.
 * @return A `std::string_view` representing the enum value. Returns "unknown"
 * if the enum value is not explicitly handled.
 *
 * @note This function returns a `std::string_view` for performance.
 * A `std::string` would need to allocate memory on the heap and copy the
 * string literal into it every time the function is called.
 */
inline std::string_view
to_string(Result result) noexcept
{
    switch (result)
    {
        case Result::InboundDisabled:
            return "inbound disabled";
        case Result::DuplicatePeer:
            return "peer already connected";
        case Result::IpLimitExceeded:
            return "ip limit exceeded";
        case Result::Full:
            return "slots full";
        case Result::Success:
            return "success";
    }

    return "unknown";
}

}  // namespace xrpl::PeerFinder
