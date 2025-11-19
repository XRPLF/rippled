#ifndef XRPL_PEERFINDER_SLOTIMP_H_INCLUDED
#define XRPL_PEERFINDER_SLOTIMP_H_INCLUDED

#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/Slot.h>

#include <xrpl/beast/container/aged_unordered_map.h>

#include <atomic>
#include <optional>

namespace ripple {
namespace PeerFinder {

class SlotImp : public Slot
{
public:
    using ptr = std::shared_ptr<SlotImp>;

    // inbound
    SlotImp(
        beast::IP::Endpoint const& local_endpoint,
        beast::IP::Endpoint const& remote_endpoint,
        bool fixed,
        clock_type& clock);

    // outbound
    SlotImp(
        beast::IP::Endpoint const& remote_endpoint,
        bool fixed,
        clock_type& clock);

    bool
    inbound() const override
    {
        return m_inbound;
    }

    bool
    fixed() const override
    {
        return m_fixed;
    }

    bool
    reserved() const override
    {
        return m_reserved;
    }

    State
    state() const override
    {
        return m_state;
    }

    beast::IP::Endpoint const&
    remote_endpoint() const override
    {
        return m_remote_endpoint;
    }

    std::optional<beast::IP::Endpoint> const&
    local_endpoint() const override
    {
        return m_local_endpoint;
    }

    std::optional<PublicKey> const&
    public_key() const override
    {
        return m_public_key;
    }

    std::string
    prefix() const
    {
        return "[" + getFingerprint(remote_endpoint(), public_key()) + "] ";
    }

    std::optional<std::uint16_t>
    listening_port() const override
    {
        std::uint32_t const value = m_listening_port;
        if (value == unknownPort)
            return std::nullopt;
        return value;
    }

    void
    set_listening_port(std::uint16_t port)
    {
        m_listening_port = port;
    }

    void
    local_endpoint(beast::IP::Endpoint const& endpoint)
    {
        m_local_endpoint = endpoint;
    }

    void
    remote_endpoint(beast::IP::Endpoint const& endpoint)
    {
        m_remote_endpoint = endpoint;
    }

    void
    public_key(PublicKey const& key)
    {
        m_public_key = key;
    }

    void
    reserved(bool reserved_)
    {
        m_reserved = reserved_;
    }

    //--------------------------------------------------------------------------

    void
    state(State state_);

    void
    activate(clock_type::time_point const& now);

    // "Memberspace"
    //
    // The set of all recent addresses that we have seen from this peer.
    // We try to avoid sending a peer the same addresses they gave us.
    //
    class recent_t
    {
    public:
        explicit recent_t(clock_type& clock);

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
        beast::aged_unordered_map<beast::IP::Endpoint, std::uint32_t> cache;
    } recent;

    void
    expire()
    {
        recent.expire();
    }

private:
    bool const m_inbound;
    bool const m_fixed;
    bool m_reserved;
    State m_state;
    beast::IP::Endpoint m_remote_endpoint;
    std::optional<beast::IP::Endpoint> m_local_endpoint;
    std::optional<PublicKey> m_public_key;

    static std::int32_t constexpr unknownPort = -1;
    std::atomic<std::int32_t> m_listening_port;

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

}  // namespace PeerFinder
}  // namespace ripple

#endif
