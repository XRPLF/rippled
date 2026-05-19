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
    canActivate(Slot const& s) const
    {
        // Must be handshaked and in the right state
        XRPL_ASSERT(
            s.state() == Slot::State::Connected || s.state() == Slot::State::Accept,
            "xrpl::PeerFinder::Counts::can_activate : valid input state");

        if (s.fixed() || s.reserved())
            return true;

        if (s.inbound())
            return in_active_ < in_max_;

        return out_active_ < out_max_;
    }

    /** Returns the number of attempts needed to bring us to the max. */
    [[nodiscard]] std::size_t
    attemptsNeeded() const
    {
        if (attempts_ >= Tuning::kMaxConnectAttempts)
            return 0;
        return Tuning::kMaxConnectAttempts - attempts_;
    }

    /** Returns the number of outbound connection attempts. */
    [[nodiscard]] std::size_t
    attempts() const
    {
        return attempts_;
    }

    /** Returns the total number of outbound slots. */
    [[nodiscard]] int
    outMax() const
    {
        return out_max_;
    }

    /** Returns the number of outbound peers assigned an open slot.
        Fixed peers do not count towards outbound slots used.
    */
    [[nodiscard]] int
    outActive() const
    {
        return out_active_;
    }

    /** Returns the number of fixed connections. */
    [[nodiscard]] std::size_t
    fixed() const
    {
        return fixed_;
    }

    /** Returns the number of active fixed connections. */
    [[nodiscard]] std::size_t
    fixedActive() const
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
    [[nodiscard]] int
    acceptCount() const
    {
        return acceptCount_;
    }

    /** Returns the number of connection attempts currently active. */
    [[nodiscard]] int
    connectCount() const
    {
        return attempts_;
    }

    /** Returns the number of connections that are gracefully closing. */
    [[nodiscard]] int
    closingCount() const
    {
        return closingCount_;
    }

    /** Returns the total number of inbound slots. */
    [[nodiscard]] int
    inMax() const
    {
        return in_max_;
    }

    /** Returns the number of inbound peers assigned an open slot. */
    [[nodiscard]] int
    inboundActive() const
    {
        return in_active_;
    }

    /** Returns the total number of active peers excluding fixed peers. */
    [[nodiscard]] int
    totalActive() const
    {
        return in_active_ + out_active_;
    }

    /** Returns the number of unused inbound slots.
        Fixed peers do not deduct from inbound slots or count towards totals.
    */
    [[nodiscard]] int
    inboundSlotsFree() const
    {
        if (in_active_ < in_max_)
            return in_max_ - in_active_;
        return 0;
    }

    /** Returns the number of unused outbound slots.
        Fixed peers do not deduct from outbound slots or count towards totals.
    */
    [[nodiscard]] int
    outboundSlotsFree() const
    {
        if (out_active_ < out_max_)
            return out_max_ - out_active_;
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

        return out_max_ <= 0;
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
    [[nodiscard]] std::string
    stateString() const
    {
        std::stringstream ss;
        ss << out_active_ << "/" << out_max_ << " out, " << in_active_ << "/" << in_max_ << " in, "
           << connectCount() << " connecting, " << closingCount() << " closing";
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
    //
    // IMPORTANT: All std::size_t counters MUST be adjusted via adjustCounter()
    // and NEVER via `+= n` where n = static_cast<int>(dir).  When dir is
    // Decrement, n == -1; adding -1 to a std::size_t implicitly converts -1 to
    // SIZE_MAX, which UBSan flags as unsigned-integer-overflow and masks real
    // underflow bugs (decrementing a counter already at zero).  Plain int
    // counters (acceptCount_, attempts_, closingCount_) are safe with += n.
    void
    adjust(Slot const& s, CountAdjustment const dir)
    {
        int const n = static_cast<int>(dir);
        if (s.fixed())
            adjustCounter(fixed_, dir);

        if (s.reserved())
            adjustCounter(reserved_, dir);

        switch (s.state())
        {
            case Slot::State::Accept:
                XRPL_ASSERT(s.inbound(), "xrpl::PeerFinder::Counts::adjust : input is inbound");
                acceptCount_ += n;
                break;

            case Slot::State::Connect:
            case Slot::State::Connected:
                XRPL_ASSERT(
                    !s.inbound(),
                    "xrpl::PeerFinder::Counts::adjust : input is not "
                    "inbound");
                attempts_ += n;
                break;

            case Slot::State::Active:
                if (s.fixed())
                    adjustCounter(fixed_active_, dir);
                if (!s.fixed() && !s.reserved())
                {
                    if (s.inbound())
                    {
                        adjustCounter(in_active_, dir);
                    }
                    else
                    {
                        adjustCounter(out_active_, dir);
                    }
                }
                adjustCounter(active_, dir);
                break;

            case Slot::State::Closing:
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
    int attempts_{0};

    /** Active connections, including fixed and reserved. */
    std::size_t active_{0};

    /** Total number of inbound slots. */
    std::size_t in_max_{0};

    /** Number of inbound slots assigned to active peers. */
    std::size_t in_active_{0};

    /** Maximum desired outbound slots. */
    std::size_t out_max_{0};

    /** Active outbound slots. */
    std::size_t out_active_{0};

    /** Fixed connections. */
    std::size_t fixed_{0};

    /** Active fixed connections. */
    std::size_t fixed_active_{0};

    /** Reserved connections. */
    std::size_t reserved_{0};

    // Number of inbound connections that are
    // not active or gracefully closing.
    int acceptCount_{0};

    // Number of connections that are gracefully closing.
    int closingCount_{0};
};

}  // namespace xrpl::PeerFinder
