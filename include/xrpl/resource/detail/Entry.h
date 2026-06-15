#pragma once

#include <xrpl/basics/DecayingSample.h>
#include <xrpl/beast/clock/abstract_clock.h>
#include <xrpl/beast/core/List.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/resource/detail/Key.h>
#include <xrpl/resource/detail/Tuning.h>

namespace xrpl::Resource {

using clock_type = beast::AbstractClock<std::chrono::steady_clock>;

// An entry in the table
// VFALCO DEPRECATED using boost::intrusive list
struct Entry : public beast::List<Entry>::Node
{
    Entry() = delete;

    /**
       @param now Construction time of Entry.
    */
    explicit Entry(clock_type::time_point const now)
        : refcount(0), localBalance(now), remoteBalance(0)
    {
    }

    [[nodiscard]] std::string
    toString() const
    {
        return getFingerprint(key->address, publicKey);
    }

    /**
     * Returns `true` if this connection should have no
     * resource limits applied--it is still possible for certain RPC commands
     * to be forbidden, but that depends on Role.
     */
    [[nodiscard]] bool
    isUnlimited() const
    {
        return key->kind == Kind::Unlimited;
    }

    // Balance including remote contributions
    int
    balance(clock_type::time_point const now)
    {
        return localBalance.value(now) + remoteBalance;
    }

    // Add a charge and return normalized balance
    // including contributions from imports.
    int
    add(int charge, clock_type::time_point const now)
    {
        return localBalance.add(charge, now) + remoteBalance;
    }

    // The public key of the peer
    std::optional<PublicKey> publicKey;

    // Back pointer to the map key (bit of a hack here)
    Key const* key{};

    // Number of Consumer references
    int refcount;

    // Exponentially decaying balance of resource consumption
    DecayingSample<kDecayWindowSeconds, clock_type> localBalance;

    // Normalized balance contribution from imports
    int remoteBalance;

    // Time of the last warning
    clock_type::time_point lastWarningTime;

    // For inactive entries, time after which this entry will be erased
    clock_type::time_point whenExpires;
};

inline std::ostream&
operator<<(std::ostream& os, Entry const& v)
{
    os << v.toString();
    return os;
}

}  // namespace xrpl::Resource
