//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_PEERFINDER_COUNTS_H_INCLUDED
#define RIPPLE_PEERFINDER_COUNTS_H_INCLUDED

#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/peerfinder/Slot.h>
#include <xrpld/peerfinder/detail/Tuning.h>
#include <xrpl/basics/random.h>

#include <cmath>

namespace ripple {
namespace PeerFinder {

/** Manages the count of available connections for the various slots. */
class Counts
{
public:
    Counts()
        : m_attempts(0)
        , m_active(0)
        , m_in_max(0)
        , m_in_active(0)
        , m_out_max(0)
        , m_out_active(0)
        , m_fixed(0)
        , m_fixed_active(0)
        , m_reserved(0)

        , m_acceptCount(0)
        , m_closingCount(0)
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
        ASSERT(
            s.state() == Slot::connected || s.state() == Slot::accept,
            "ripple::PeerFinder::Counts::can_activate : valid input state");

        if (s.fixed() || s.reserved())
            return true;

        if (s.inbound())
            return m_in_active < m_in_max;

        return m_out_active < m_out_max;
    }

    /** Returns the number of attempts needed to bring us to the max. */
    std::size_t
    attempts_needed() const
    {
        if (m_attempts >= Tuning::maxConnectAttempts)
            return 0;
        return Tuning::maxConnectAttempts - m_attempts;
    }

    /** Returns the number of outbound connection attempts. */
    std::size_t
    attempts() const
    {
        return m_attempts;
    }

    /** Returns the total number of outbound slots. */
    int
    out_max() const
    {
        return m_out_max;
    }

    /** Returns the number of outbound peers assigned an open slot.
        Fixed peers do not count towards outbound slots used.
    */
    int
    out_active() const
    {
        return m_out_active;
    }

    /** Returns the number of fixed connections. */
    std::size_t
    fixed() const
    {
        return m_fixed;
    }

    /** Returns the number of active fixed connections. */
    std::size_t
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
    int
    acceptCount() const
    {
        return m_acceptCount;
    }

    /** Returns the number of connection attempts currently active. */
    int
    connectCount() const
    {
        return m_attempts;
    }

    /** Returns the number of connections that are gracefully closing. */
    int
    closingCount() const
    {
        return m_closingCount;
    }

    /** Returns the total number of inbound slots. */
    int
    inboundSlots() const
    {
        return m_in_max;
    }

    /** Returns the number of inbound peers assigned an open slot. */
    int
    inboundActive() const
    {
        return m_in_active;
    }

    /** Returns the total number of active peers excluding fixed peers. */
    int
    totalActive() const
    {
        return m_in_active + m_out_active;
    }

    /** Returns the number of unused inbound slots.
        Fixed peers do not deduct from inbound slots or count towards totals.
    */
    int
    inboundSlotsFree() const
    {
        if (m_in_active < m_in_max)
            return m_in_max - m_in_active;
        return 0;
    }

    /** Returns the number of unused outbound slots.
        Fixed peers do not deduct from outbound slots or count towards totals.
    */
    int
    outboundSlotsFree() const
    {
        if (m_out_active < m_out_max)
            return m_out_max - m_out_active;
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

        if (m_out_max > 0)
            return false;

        return true;
    }

    /** Output statistics. */
    void
    onWrite(beast::PropertyStream::Map& map)
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
    std::string
    state_string() const
    {
        std::stringstream ss;
        ss << m_out_active << "/" << m_out_max << " out, " << m_in_active << "/"
           << m_in_max << " in, " << connectCount() << " connecting, "
           << closingCount() << " closing";
        return ss.str();
    }

    //--------------------------------------------------------------------------
private:
    // Adjusts counts based on the specified slot, in the direction indicated.
    void
    adjust(Slot const& s, int const n)
    {
        if (s.fixed())
            m_fixed += n;

        if (s.reserved())
            m_reserved += n;

        switch (s.state())
        {
            case Slot::accept:
                ASSERT(
                    s.inbound(),
                    "ripple::PeerFinder::Counts::adjust : input is inbound");
                m_acceptCount += n;
                break;

            case Slot::connect:
            case Slot::connected:
                ASSERT(
                    !s.inbound(),
                    "ripple::PeerFinder::Counts::adjust : input is not "
                    "inbound");
                m_attempts += n;
                break;

            case Slot::active:
                if (s.fixed())
                    m_fixed_active += n;
                if (!s.fixed() && !s.reserved())
                {
                    if (s.inbound())
                        m_in_active += n;
                    else
                        m_out_active += n;
                }
                m_active += n;
                break;

            case Slot::closing:
                m_closingCount += n;
                break;

            default:
                UNREACHABLE(
                    "ripple::PeerFinder::Counts::adjust : invalid input state");
                break;
        };
    }

private:
    /** Outbound connection attempts. */
    int m_attempts;

    /** Active connections, including fixed and reserved. */
    std::size_t m_active;

    /** Total number of inbound slots. */
    std::size_t m_in_max;

    /** Number of inbound slots assigned to active peers. */
    std::size_t m_in_active;

    /** Maximum desired outbound slots. */
    std::size_t m_out_max;

    /** Active outbound slots. */
    std::size_t m_out_active;

    /** Fixed connections. */
    std::size_t m_fixed;

    /** Active fixed connections. */
    std::size_t m_fixed_active;

    /** Reserved connections. */
    std::size_t m_reserved;

    // Number of inbound connections that are
    // not active or gracefully closing.
    int m_acceptCount;

    // Number of connections that are gracefully closing.
    int m_closingCount;
};

}  // namespace PeerFinder
}  // namespace ripple

#endif
