#pragma once

#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/Slot.h>
#include <xrpld/peerfinder/detail/Tuning.h>

#include <xrpl/basics/random.h>

namespace xrpl::PeerFinder {

/** Direction of a slot count adjustment. */
enum class CountAdjustment : int { Decrement = -1, Increment = 1 };

/** Manages the count of available connections for the various slots. */
class Counts
{
public:
    /** Adds the slot state and properties to the slot counts. */
    void
    add(Slot const& s)
    {
        adjust(s, CountAdjustment::Increment);
    }

    /** Removes the slot state and properties from the slot counts. */
    void
    remove(Slot const& s)
    {
        adjust(s, CountAdjustment::Decrement);
    }

    /** Returns `true` if the slot can become active. */
    [[nodiscard]] bool
    can_activate(Slot const& s) const
    {
        // Must be handshaked and in the right state
        XRPL_ASSERT(
            s.state() == Slot::State::connected || s.state() == Slot::State::accept,
            "xrpl::PeerFinder::Counts::can_activate : valid input state");

        if (s.fixed() || s.reserved())
            return true;

        if (s.inbound())
            return m_in_active < m_in_max;

        return m_out_active < m_out_max;
    }

    /** Returns the number of attempts needed to bring us to the max. */
    [[nodiscard]] std::size_t
    attempts_needed() const
    {
        if (m_attempts >= Tuning::maxConnectAttempts)
            return 0;
        return Tuning::maxConnectAttempts - m_attempts;
    }

    /** Returns the number of outbound connection attempts. */
    [[nodiscard]] std::size_t
    attempts() const
    {
        return m_attempts;
    }

    /** Returns the total number of outbound slots. */
    [[nodiscard]] int
    out_max() const
    {
        return m_out_max;
    }

    /** Returns the number of outbound peers assigned an open slot.
        Fixed peers do not count towards outbound slots used.
    */
    [[nodiscard]] int
    out_active() const
    {
        return m_out_active;
    }

    /** Returns the number of fixed connections. */
    [[nodiscard]] std::size_t
    fixed() const
    {
        return m_fixed;
    }

    /** Returns the number of active fixed connections. */
    [[nodiscard]] std::size_t
    fixed_active() const
    {
        return m_fixed_active;
    }

    //--------------------------------------------------------------------------

    /** Called when the config is set or changed. */
    void
    onConfig(Config const& config)
    {
        m_out_max = config.outPeers;
        if (config.wantIncoming)
            m_in_max = config.inPeers;
    }

    /** Returns the number of accepted connections that haven't handshaked. */
    [[nodiscard]] int
    acceptCount() const
    {
        return m_acceptCount;
    }

    /** Returns the number of connection attempts currently active. */
    [[nodiscard]] int
    connectCount() const
    {
        return m_attempts;
    }

    /** Returns the number of connections that are gracefully closing. */
    [[nodiscard]] int
    closingCount() const
    {
        return m_closingCount;
    }

    /** Returns the total number of inbound slots. */
    [[nodiscard]] int
    in_max() const
    {
        return m_in_max;
    }

    /** Returns the number of inbound peers assigned an open slot. */
    [[nodiscard]] int
    inboundActive() const
    {
        return m_in_active;
    }

    /** Returns the total number of active peers excluding fixed peers. */
    [[nodiscard]] int
    totalActive() const
    {
        return m_in_active + m_out_active;
    }

    /** Returns the number of unused inbound slots.
        Fixed peers do not deduct from inbound slots or count towards totals.
    */
    [[nodiscard]] int
    inboundSlotsFree() const
    {
        if (m_in_active < m_in_max)
            return m_in_max - m_in_active;
        return 0;
    }

    /** Returns the number of unused outbound slots.
        Fixed peers do not deduct from outbound slots or count towards totals.
    */
    [[nodiscard]] int
    outboundSlotsFree() const
    {
        if (m_out_active < m_out_max)
            return m_out_max - m_out_active;
        return 0;
    }

    //--------------------------------------------------------------------------

    /** Returns true if the slot logic considers us "connected" to the network.
     */
    [[nodiscard]] bool
    isConnectedToNetwork() const
    {
        // We will consider ourselves connected if we have reached
        // the number of outgoing connections desired, or if connect
        // automatically is false.
        //
        // Fixed peers do not count towards the active outgoing total.

        return m_out_max <= 0;
    }

    /** Output statistics. */
    void
    onWrite(beast::PropertyStream::Map& map) const
    {
        map["accept"] = acceptCount();
        map["connect"] = connectCount();
        map["close"] = closingCount();
        map["in"] << m_in_active << "/" << m_in_max;
        map["out"] << m_out_active << "/" << m_out_max;
        map["fixed"] = m_fixed_active;
        map["reserved"] = m_reserved;
        map["total"] = m_active;
    }

    /** Records the state for diagnostics. */
    [[nodiscard]] std::string
    state_string() const
    {
        std::stringstream ss;
        ss << m_out_active << "/" << m_out_max << " out, " << m_in_active << "/" << m_in_max
           << " in, " << connectCount() << " connecting, " << closingCount() << " closing";
        return ss.str();
    }

    //--------------------------------------------------------------------------
private:
    /** Increments or decrements a counter based on the adjustment direction. */
    template <typename T>
    static void
    adjustCounter(T& counter, CountAdjustment dir)
    {
        switch (dir)
        {
            case CountAdjustment::Increment:
                ++counter;
                break;
            case CountAdjustment::Decrement:
                --counter;
                break;
        }
    }

    // Adjusts counts based on the specified slot, in the direction indicated.
    // Using ++/-- instead of += on std::size_t counters avoids UBSan
    // unsigned-integer-overflow from implicit conversion of -1 to SIZE_MAX.
    // A decrement on a zero counter is a real bug that UBSan should catch.
    void
    adjust(Slot const& s, CountAdjustment const dir)
    {
        if (s.fixed())
            adjustCounter(m_fixed, dir);

        if (s.reserved())
            adjustCounter(m_reserved, dir);

        switch (s.state())
        {
            case Slot::State::accept:
                XRPL_ASSERT(s.inbound(), "xrpl::PeerFinder::Counts::adjust : input is inbound");
                adjustCounter(m_acceptCount, dir);
                break;

            case Slot::State::connect:
            case Slot::State::connected:
                XRPL_ASSERT(
                    !s.inbound(),
                    "xrpl::PeerFinder::Counts::adjust : input is not "
                    "inbound");
                adjustCounter(m_attempts, dir);
                break;

            case Slot::State::active:
                if (s.fixed())
                    adjustCounter(m_fixed_active, dir);
                if (!s.fixed() && !s.reserved())
                {
                    if (s.inbound())
                    {
                        adjustCounter(m_in_active, dir);
                    }
                    else
                    {
                        adjustCounter(m_out_active, dir);
                    }
                }
                adjustCounter(m_active, dir);
                break;

            case Slot::State::closing:
                adjustCounter(m_closingCount, dir);
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
    int m_attempts{0};

    /** Active connections, including fixed and reserved. */
    std::size_t m_active{0};

    /** Total number of inbound slots. */
    std::size_t m_in_max{0};

    /** Number of inbound slots assigned to active peers. */
    std::size_t m_in_active{0};

    /** Maximum desired outbound slots. */
    std::size_t m_out_max{0};

    /** Active outbound slots. */
    std::size_t m_out_active{0};

    /** Fixed connections. */
    std::size_t m_fixed{0};

    /** Active fixed connections. */
    std::size_t m_fixed_active{0};

    /** Reserved connections. */
    std::size_t m_reserved{0};

    // Number of inbound connections that are
    // not active or gracefully closing.
    int m_acceptCount{0};

    // Number of connections that are gracefully closing.
    int m_closingCount{0};
};

}  // namespace xrpl::PeerFinder
