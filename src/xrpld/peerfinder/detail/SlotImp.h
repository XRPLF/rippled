#pragma once

#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/Slot.h>

#include <xrpl/beast/container/aged_unordered_map.h>

#include <atomic>
#include <optional>

namespace xrpl::PeerFinder {

class SlotImp : public Slot
{
public:
    using ptr = std::shared_ptr<SlotImp>;

    // inbound
    SlotImp(
        beast::IP::Endpoint const& localEndpoint,
        beast::IP::Endpoint remoteEndpoint,
        bool fixed,
        clock_type& clock);

    // outbound
    SlotImp(beast::IP::Endpoint remoteEndpoint, bool fixed, clock_type& clock);

    bool
    inbound() const override
    {
        return inbound_;
    }

    bool
    fixed() const override
    {
        return fixed_;
    }

    bool
    reserved() const override
    {
        return reserved_;
    }

    State
    state() const override
    {
        return state_;
    }

    beast::IP::Endpoint const&
    remoteEndpoint() const override
    {
        return remote_endpoint_;
    }

    std::optional<beast::IP::Endpoint> const&
    localEndpoint() const override
    {
        return local_endpoint_;
    }

    std::optional<PublicKey> const&
    publicKey() const override
    {
        return public_key_;
    }

    std::string
    prefix() const
    {
        return "[" + getFingerprint(remoteEndpoint(), publicKey()) + "] ";
    }

    std::optional<std::uint16_t>
    listeningPort() const override
    {
        std::uint32_t const value = listening_port_;
        if (value == kUnknownPort)
            return std::nullopt;
        return value;
    }

    void
    setListeningPort(std::uint16_t port)
    {
        listening_port_ = port;
    }

    void
    localEndpoint(beast::IP::Endpoint const& endpoint)
    {
        local_endpoint_ = endpoint;
    }

    void
    remoteEndpoint(beast::IP::Endpoint const& endpoint)
    {
        remote_endpoint_ = endpoint;
    }

    void
    publicKey(PublicKey const& key)
    {
        public_key_ = key;
    }

    void
    reserved(bool reserved)
    {
        reserved_ = reserved;
    }

    //--------------------------------------------------------------------------

    void
    state(State state);

    void
    activate(clock_type::time_point const& now);

    // "Memberspace"
    //
    // The set of all recent addresses that we have seen from this peer.
    // We try to avoid sending a peer the same addresses they gave us.
    //
    class RecentT
    {
    public:
        explicit RecentT(clock_type& clock);

        /** Called for each valid endpoint received for a slot.
            We also insert messages that we send to the slot to prevent
            sending a slot the same address too frequently.
        */
        void
        insert(beast::IP::Endpoint const& ep, std::uint32_t hops);

        /** Returns `true` if we should not send endpoint to the slot. */
        bool
        filter(beast::IP::Endpoint const& ep, std::uint32_t hops);

    private:
        void
        expire();

        friend class SlotImp;
        beast::aged_unordered_map<beast::IP::Endpoint, std::uint32_t> cache_;
    } recent;

    void
    expire()
    {
        recent.expire();
    }

private:
    bool const inbound_;
    bool const fixed_;
    bool reserved_;
    State state_;
    beast::IP::Endpoint remote_endpoint_;
    std::optional<beast::IP::Endpoint> local_endpoint_;
    std::optional<PublicKey> public_key_;

    static constexpr std::int32_t kUnknownPort = -1;
    std::atomic<std::int32_t> listening_port_;

public:
    // DEPRECATED public data members

    // Tells us if we checked the connection. Outbound connections
    // are always considered checked since we successfully connected.
    bool checked;

    // Set to indicate if the connection can receive incoming at the
    // address advertised in mtENDPOINTS. Only valid if checked is true.
    bool canAccept;

    // Set to indicate that a connection check for this peer is in
    // progress. Valid always.
    bool connectivityCheckInProgress;

    // The time after which we will accept mtENDPOINTS from the peer
    // This is to prevent flooding or spamming. Receipt of mtENDPOINTS
    // sooner than the allotted time should impose a load charge.
    //
    clock_type::time_point whenAcceptEndpoints;
};

}  // namespace xrpl::PeerFinder
