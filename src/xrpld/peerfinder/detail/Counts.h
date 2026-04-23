#pragma once

#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/Slot.h>
#include <xrpld/peerfinder/detail/Tuning.h>

#include <xrpl/basics/random.h>

namespace xrpl::PeerFinder {

/** Manages the count of available connections for the various slots. */
class Counts
{
public:
    Counts()
        : attempts_(0)
        , active_(0)
        , in_max_(0)
        , in_active_(0)
        , out_max_(0)
        , out_active_(0)
        , fixed_(0)
        , fixed_active_(0)
        , reserved_(0)

        , acceptCount_(0)
        , closingCount_(0)
    {
    }

    //--------------------------------------------------------------------------

    /** Adds the slot state and properties to the slot counts. */
    void
    add(Slot const& s)
    {
        adjust(s, 1);
    }

    /** Removes the slot state and properties from the slot counts. */
    void
    remove(Slot const& s)
    {
        adjust(s, -1);
    }

    /** Returns `true` if the slot can become active. */
    bool
    can_activate(Slot const& s) const
    {
        // Must be handshaked and in the right state
        XRPL_ASSERT(
            s.state() == Slot::Connected || s.state() == Slot::Accept,
            "xrpl::PeerFinder::Counts::can_activate : valid input state");

        if (s.fixed() || s.reserved())
            return true;

        if (s.inbound())
            return in_active_ < in_max_;

        return out_active_ < out_max_;
    }

    /** Returns the number of attempts needed to bring us to the max. */
    std::size_t
    attempts_needed() const
    {
        if (attempts_ >= Tuning::MaxConnectAttempts)
            return 0;
        return Tuning::MaxConnectAttempts - attempts_;
    }

    /** Returns the number of outbound connection attempts. */
    std::size_t
    attempts() const
    {
        return attempts_;
    }

    /** Returns the total number of outbound slots. */
    int
    out_max() const
    {
        return out_max_;
    }

    /** Returns the number of outbound peers assigned an open slot.
        Fixed peers do not count towards outbound slots used.
    */
    int
    out_active() const
    {
        return out_active_;
    }

    /** Returns the number of fixed connections. */
    std::size_t
    fixed() const
    {
        return fixed_;
    }

    /** Returns the number of active fixed connections. */
    std::size_t
    fixed_active() const
    {
        return fixed_active_;
    }

    //--------------------------------------------------------------------------

    /** Called when the config is set or changed. */
    void
    onConfig(Config const& config)
    {
        out_max_ = config.outPeers;
        if (config.wantIncoming)
            in_max_ = config.inPeers;
    }

    /** Returns the number of accepted connections that haven't handshaked. */
    int
    acceptCount() const
    {
        return acceptCount_;
    }

    /** Returns the number of connection attempts currently active. */
    int
    connectCount() const
    {
        return attempts_;
    }

    /** Returns the number of connections that are gracefully closing. */
    int
    closingCount() const
    {
        return closingCount_;
    }

    /** Returns the total number of inbound slots. */
    int
    in_max() const
    {
        return in_max_;
    }

    /** Returns the number of inbound peers assigned an open slot. */
    int
    inboundActive() const
    {
        return in_active_;
    }

    /** Returns the total number of active peers excluding fixed peers. */
    int
    totalActive() const
    {
        return in_active_ + out_active_;
    }

    /** Returns the number of unused inbound slots.
        Fixed peers do not deduct from inbound slots or count towards totals.
    */
    int
    inboundSlotsFree() const
    {
        if (in_active_ < in_max_)
            return in_max_ - in_active_;
        return 0;
    }

    /** Returns the number of unused outbound slots.
        Fixed peers do not deduct from outbound slots or count towards totals.
    */
    int
    outboundSlotsFree() const
    {
        if (out_active_ < out_max_)
            return out_max_ - out_active_;
        return 0;
    }

    //--------------------------------------------------------------------------

    /** Returns true if the slot logic considers us "connected" to the network.
     */
    bool
    isConnectedToNetwork() const
    {
        // We will consider ourselves connected if we have reached
        // the number of outgoing connections desired, or if connect
        // automatically is false.
        //
        // Fixed peers do not count towards the active outgoing total.

        if (out_max_ > 0)
            return false;

        return true;
    }

    /** Output statistics. */
    void
    onWrite(beast::PropertyStream::Map& map) const
    {
        map["accept"] = acceptCount();
        map["connect"] = connectCount();
        map["close"] = closingCount();
        map["in"] << in_active_ << "/" << in_max_;
        map["out"] << out_active_ << "/" << out_max_;
        map["fixed"] = fixed_active_;
        map["reserved"] = reserved_;
        map["total"] = active_;
    }

    /** Records the state for diagnostics. */
    std::string
    state_string() const
    {
        std::stringstream ss;
        ss << out_active_ << "/" << out_max_ << " out, " << in_active_ << "/" << in_max_ << " in, "
           << connectCount() << " connecting, " << closingCount() << " closing";
        return ss.str();
    }

    //--------------------------------------------------------------------------
private:
    // Adjusts counts based on the specified slot, in the direction indicated.
    void
    adjust(Slot const& s, int const n)
    {
        if (s.fixed())
            fixed_ += n;

        if (s.reserved())
            reserved_ += n;

        switch (s.state())
        {
            case Slot::Accept:
                XRPL_ASSERT(s.inbound(), "xrpl::PeerFinder::Counts::adjust : input is inbound");
                acceptCount_ += n;
                break;

            case Slot::Connect:
            case Slot::Connected:
                XRPL_ASSERT(
                    !s.inbound(),
                    "xrpl::PeerFinder::Counts::adjust : input is not "
                    "inbound");
                attempts_ += n;
                break;

            case Slot::Active:
                if (s.fixed())
                    fixed_active_ += n;
                if (!s.fixed() && !s.reserved())
                {
                    if (s.inbound())
                        in_active_ += n;
                    else
                        out_active_ += n;
                }
                active_ += n;
                break;

            case Slot::Closing:
                closingCount_ += n;
                break;

            // LCOV_EXCL_START
            default:
                UNREACHABLE("xrpl::PeerFinder::Counts::adjust : invalid input state");
                break;
                // LCOV_EXCL_STOP
        };
    }

private:
    /** Outbound connection attempts. */
    int attempts_;

    /** Active connections, including fixed and reserved. */
    std::size_t active_;

    /** Total number of inbound slots. */
    std::size_t in_max_;

    /** Number of inbound slots assigned to active peers. */
    std::size_t in_active_;

    /** Maximum desired outbound slots. */
    std::size_t out_max_;

    /** Active outbound slots. */
    std::size_t out_active_;

    /** Fixed connections. */
    std::size_t fixed_;

    /** Active fixed connections. */
    std::size_t fixed_active_;

    /** Reserved connections. */
    std::size_t reserved_;

    // Number of inbound connections that are
    // not active or gracefully closing.
    int acceptCount_;

    // Number of connections that are gracefully closing.
    int closingCount_;
};

}  // namespace xrpl::PeerFinder
