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

namespace ripple {
namespace PeerFinder {

Slots::Slots (DiscreteClock <DiscreteTime> clock, bool roundUpwards)
    : peerCount (0)
    , inboundCount (0)
    , outboundCount (0)
    , fixedCount (0)
    , outDesired (0)
    , inboundSlots (0)
    , inboundSlotsMaximum (0)
    , m_clock (clock)
    , m_startTime (0)
    , m_roundUpwards (roundUpwards)
{
}

void Slots::update (Config const& config)
{
    double outDesiredFraction = 1;

    if (config.wantIncoming)
        outDesiredFraction = config.maxPeerCount * (Config::outPercent * .01);

    if (m_roundUpwards)
        outDesired = int (std::ceil (outDesiredFraction));
    else
        outDesired = int (std::floor (outDesiredFraction));

    if (outDesired < Config::minOutCount)
        outDesired = Config::minOutCount;

    if (config.maxPeerCount >= outDesired)
        inboundSlotsMaximum = config.maxPeerCount - outDesired;
    else
        inboundSlotsMaximum = 0;

    inboundSlots = std::max (inboundSlotsMaximum - inboundCount, 0);
}

void Slots::addPeer (Config const& config, bool inbound)
{
    if (peerCount == 0)
        m_startTime = m_clock();

    ++peerCount;
    if (inbound)
        ++inboundCount;
    else
        ++outboundCount;

    update (config);
}

void Slots::dropPeer (Config const& config, bool inbound)
{
    bool const wasConnected (connected ());

    --peerCount;
    if (inbound)
        --inboundCount;
    else
        --outboundCount;

    if (wasConnected && ! connected())
        m_startTime = 0;

    update (config);
}

bool Slots::roundUpwards () const
{
    return m_roundUpwards;
}

bool Slots::connected () const
{
    return (peerCount-fixedCount) >= Config::minOutCount;
}

uint32 Slots::uptimeSeconds () const
{
    if (m_startTime != 0)
        return m_clock() - m_startTime;
    return 0;
}

void Slots::updateConnected ()
{
    bool const wasConnected (m_startTime != 0);
    bool const isConnected (connected());

    if (wasConnected && !isConnected)
    {
        m_startTime = 0;
    }
    else if (! wasConnected && isConnected)
    {
        m_startTime = m_clock();
    }
}

void Slots::onWrite (PropertyStream::Map& map)
{
    map ["peers"]        = peerCount;
    map ["in"]           = inboundCount;
    map ["out"]          = outboundCount;
    map ["fixed"]        = fixedCount;
    map ["out_desired"]  = outDesired;
    map ["in_avail"]     = inboundSlots;
    map ["in_max"]       = inboundSlotsMaximum;
    map ["round"]        = roundUpwards();
    map ["connected"]    = connected();
    map ["uptime"]       =
        RelativeTime (uptimeSeconds()).getDescription ().toStdString();
}

}
}
